// Live CPU frame producer, included in graphics_system.cpp. This is deliberately
// fail-closed while game draw/helper lowering is being connected: an unsupported
// command rejects the WHOLE batch before Metal submits any of it.
struct Gta4NativeGraphicsSystem::NativeMetalFrameState {
  struct SurfaceRecord {
    SurfaceDescriptor descriptor;
    std::shared_ptr<const theft4::render::Surface> owner;
    uint32_t logical_width=0,logical_height=0;
    bool is_reflection=false;
    NativeReflectionTarget reflection;
    NativeReflectionCaptureState capture;
    uint64_t materialized_serial=0;
  };
  struct PlacementOwner {uint64_t surface=0,serial=0;GuestSurfaceView view;};
  struct TextureRecord {std::shared_ptr<const NativeTextureResource> resource;
                        std::shared_ptr<const theft4::render::Surface> owner;
                        std::shared_ptr<NativeTextureImage> description;
                        bool depth_resolved=false;
                        std::set<std::pair<uint32_t,uint32_t>> resolved;
                        uint64_t content_serial=0,packed_source_serial=0;
                        uint64_t virtual_lifetime=0,packed_allocation=0;};
  // A handle can have several physical extent/sample variants. Placement
  // ownership identifies the current writer, independently of those allocations.
  std::map<uint64_t,SurfaceRecord> surfaces;
  std::map<std::pair<uint32_t,bool>,std::vector<uint64_t>> surface_buckets;
  std::unordered_map<GuestPlacementKey,PlacementOwner,GuestPlacementKeyHash> placements;
  std::map<uint64_t,TextureRecord> textures;
  std::map<std::string,std::shared_ptr<const theft4::render::Surface>> utilities;
  theft4::render::SmaaLookups smaa_lookups;
  NativeReflectionRegistry reflections; // Snapshot for this speculative batch.
  struct BytesRecord {
    std::weak_ptr<const void> resource;
    std::shared_ptr<const theft4::render::Bytes> owner;
    uint64_t used=0;
  };
  struct ImageRecord {
    std::weak_ptr<const NativeTextureResource> resource;
    std::shared_ptr<const theft4::render::Image> owner;
    std::shared_ptr<NativeTextureImage> description;
    uint64_t used=0;
  };
  std::map<std::array<uint64_t,8>,BytesRecord> buffers;
  std::map<uint64_t,ImageRecord> images;
  theft4::render::SurfaceContents contents;
};

void Gta4NativeGraphicsSystem::ResetNativeMetalFrontend() {
  // Allocation generations must not restart across a title device reset.
  if(native_metal_frame_) {
    native_metal_frame_->surfaces.clear();native_metal_frame_->surface_buckets.clear();
    native_metal_frame_->placements.clear();native_metal_frame_->textures.clear();
    native_metal_frame_->utilities.clear();native_metal_frame_->smaa_lookups={};
    native_metal_frame_->contents.clear();
    native_metal_frame_->buffers.clear();native_metal_frame_->images.clear();
  }
}
void Gta4NativeGraphicsSystem::ReleaseNativeMetalResource(uint32_t handle) {
  if(!native_metal_frame_)return;
  std::erase_if(native_metal_frame_->images,[&](const auto& entry) {
    const auto owner=entry.second.resource.lock();return !owner||owner->handle==handle;
  });
  const auto forget=[&](theft4::render::SurfaceKey key) {
    std::erase_if(native_metal_frame_->contents,[&](const auto& view){return view.surface==key;});
  };
  std::erase_if(native_metal_frame_->surfaces,[&](const auto& entry) {
    if(entry.second.descriptor.handle!=handle)return false;forget(entry.second.owner->key);return true;
  });
  std::erase_if(native_metal_frame_->surface_buckets,[&](const auto& entry){return entry.first.first==handle;});
  std::erase_if(native_metal_frame_->placements,[&](const auto& entry){return !native_metal_frame_->surfaces.contains(entry.second.surface);});
  std::erase_if(native_metal_frame_->textures,[&](const auto& entry) {
    if(entry.second.resource->handle!=handle)return false;forget(entry.second.owner->key);return true;
  });
}

bool Gta4NativeGraphicsSystem::ReadNativeMetalTextureToGuest(
    const std::shared_ptr<const NativeTextureResource>& resource,
    const TextureLockCommand& command,TextureLockResult& result) {
  namespace r=theft4::render;
  std::string error;
  const auto reject=[&](const char* reason) {
    REXLOG_ERROR("gta4-metal: texture lock {:08X} level {} rejected: {}",command.texture,command.level,reason);return false;
  };
  if(!resource||!native_metal_frame_||command.dimension!=TextureLockDimension::k2D||command.array_index)
    return reject("Native lock requires an available 2D texture generation");
  const auto found=native_metal_frame_->textures.find(resource->generation);
  if(found==native_metal_frame_->textures.end()||command.level>=found->second.owner->levels)
    return reject("Native texture generation or mip was not produced");
  const auto& record=found->second;const auto* format=resource->info.format_info();
  if(!format||format->block_width!=1||format->block_height!=1||record.owner->kind!=r::ImageKind::Texture2D||
     record.owner->format!=r::source::PixelFormat(ConvertTextureFormat(resource->info.format))||
     !r::SupportsAspect(record.owner->format,r::Aspect::Color))
    return reject("Native color storage does not match the guest texel format");
  r::FramePlan declarations;declarations.surfaces={record.owner};
  r::ColorReadback readback;
  if(!frame_backend_->ReadColor(declarations,{record.owner->key,command.level,0,r::Aspect::Color},readback,error))
    return reject(error.c_str());
  const uint32_t pixel_bytes=format->bytes_per_block();
  uint32_t width=0,height=0,packed_x=0,packed_y=0;
  resource->info.GetMipSize(command.level,&width,&height);
  const uint32_t address=resource->info.GetMipLocation(command.level,&packed_x,&packed_y,true);
  if(!address)return reject("Guest texture mip has no physical backing address");
  const auto extent=resource->info.GetMipExtent(command.level,true);
  if(!pixel_bytes||!rex::is_pow2(pixel_bytes)||readback.row_bytes!=uint64_t(readback.width)*pixel_bytes)
    return reject("Native readback and guest texel sizes differ");
  uint8_t* destination=memory_->TranslatePhysical<uint8_t*>(address);
  const uint64_t capacity=std::min(uint64_t(extent.block_pitch_h)*extent.block_pitch_v*extent.depth*pixel_bytes,
                                 uint64_t(0x20000000u-(address&0x1fffffffu)));
  const uint32_t log2=rex::log2_floor(pixel_bytes);
  const auto offset=[&](uint32_t x,uint32_t y)->int64_t {
    if(uint64_t(packed_x)+x>INT32_MAX||uint64_t(packed_y)+y>INT32_MAX)return -1;
    if(resource->info.is_tiled)return texture_util::GetTiledOffset2D(int32_t(packed_x+x),int32_t(packed_y+y),extent.block_pitch_h,log2);
    return (uint64_t(packed_y+y)*extent.block_pitch_h+packed_x+x)*pixel_bytes;
  };
  const auto copy=[&](uint64_t at,const uint8_t* source,uint32_t bytes) {
    texture_conversion::CopySwapBlock(resource->info.endianness,destination+at,source,bytes);
  };
  if(!CopyNativeColorToGuest(readback.bytes,readback.width,readback.height,readback.row_bytes,
                            width,height,pixel_bytes,capacity,offset,copy,error))return reject(error.c_str());
  result.copied_to_guest=1;return true;
}

bool Gta4NativeGraphicsSystem::PublishNativeMetalFrame(
    const PresentCommand& present,const std::shared_ptr<const NativeTextureResource>& source,
    const std::shared_ptr<const EnvironmentalDataV1>& environmental_data,std::string& error) {
  namespace r=theft4::render;
  if(!frame_backend_){error="Metal backend missing";return false;}
  if(current_frame_.empty() && !present.device)return true;
  if(!native_metal_frame_)native_metal_frame_=std::make_unique<NativeMetalFrameState>();
  // CPU ownership/content journals are speculative until the entire batch is
  // accepted. Earlier clears never become visible when a later draw rejects.
  // Immutable packets are independently safe to cache even if frame admission
  // later rejects. They do not claim any GPU-produced content or placement.
  std::erase_if(native_metal_frame_->buffers,[](const auto& entry){return entry.second.resource.expired();});
  std::erase_if(native_metal_frame_->images,[](const auto& entry){return entry.second.resource.expired();});
  size_t resident=0;
  for(const auto& entry:native_metal_frame_->buffers)resident+=entry.second.owner->value.size();
  for(const auto& entry:native_metal_frame_->images)resident+=entry.second.owner->source->value.size();
  constexpr size_t budget=256*1024*1024;
  while(resident>budget) {
    auto b=std::min_element(native_metal_frame_->buffers.begin(),native_metal_frame_->buffers.end(),
        [](const auto& a,const auto& b){return a.second.used<b.second.used;});
    auto i=std::min_element(native_metal_frame_->images.begin(),native_metal_frame_->images.end(),
        [](const auto& a,const auto& b){return a.second.used<b.second.used;});
    if(b!=native_metal_frame_->buffers.end()&&(i==native_metal_frame_->images.end()||b->second.used<=i->second.used)) {
      resident-=b->second.owner->value.size();native_metal_frame_->buffers.erase(b);
    } else if(i!=native_metal_frame_->images.end()) {
      resident-=i->second.owner->source->value.size();native_metal_frame_->images.erase(i);
    } else break;
  }
  NativeMetalFrameState next;
  next.surfaces=native_metal_frame_->surfaces;next.surface_buckets=native_metal_frame_->surface_buckets;
  next.placements=native_metal_frame_->placements;next.textures=native_metal_frame_->textures;
  next.contents=native_metal_frame_->contents;next.utilities=native_metal_frame_->utilities;
  next.smaa_lookups=native_metal_frame_->smaa_lookups;
  auto plan=std::make_shared<r::FramePlan>();plan->sequence=++next_native_metal_sequence_;
  const auto caps=frame_backend_->Capabilities();
  // Releases in this batch retire reflection aliases in command order. Failed
  // admission commits neither that registry nor placement/content ownership.
  next.reflections=reflection_resources_;auto& reflections=next.reflections;
  const auto reject=[&](const char* reason){error=reason;return false;};
  const auto retain=[&](const std::shared_ptr<const r::Surface>& surface) {
    if(std::none_of(plan->surfaces.begin(),plan->surfaces.end(),[&](const auto& s){return s->key==surface->key;}))
      plan->surfaces.push_back(surface);
  };
  const uint32_t supported_samples=((caps.sample_counts&(1u<<1))?1u:0u)|
      ((caps.sample_counts&(1u<<2))?2u:0u)|((caps.sample_counts&(1u<<4))?4u:0u);
  const auto forget_contents=[&](r::SurfaceKey key) {
    std::erase_if(next.contents,[&](const auto& view){return view.surface==key;});
  };
  const r::UtilityAllocator utility_for=[&](std::string_view role,uint32_t width,uint32_t height,r::Format format)
      ->std::shared_ptr<const r::Surface> {
    if(!width||!height||width>caps.max_image_dimension_2d||height>caps.max_image_dimension_2d)return {};
    auto& cached=next.utilities[std::string(role)];
    if(cached&&cached->width==width&&cached->height==height&&cached->format==format) {retain(cached);return cached;}
    if(cached)forget_contents(cached->key);
    auto surface=std::make_shared<r::Surface>();surface->key={++next_native_metal_allocation_,1};
    surface->width=width;surface->height=height;surface->format=format;cached=surface;retain(surface);return surface;
  };
  const auto surface_for=[&](const SurfaceDescriptor& descriptor,bool depth,
                            uint32_t scene_sample_override=0)->std::shared_ptr<const r::Surface> {
    const auto format=r::source::PixelFormat(ConvertSurfaceFormat(descriptor.format,depth));
    uint32_t samples=uint32_t(ConvertSurfaceSamples(descriptor.sample_type));
    uint32_t width=descriptor.width,height=descriptor.height;
    if(!descriptor.handle||!width||!height||format==r::Format::Invalid||samples>8) {
      error="Metal surface descriptor unsupported";return {};
    }
    {
      std::lock_guard lock(texture_resource_mutex_);
      const auto* registered=virtual_resource_registry_.Find(descriptor.handle);
      if(registered&&registered->kind==VirtualResourceKind::kSurface&&
         registered->logical_width==descriptor.width&&registered->logical_height==descriptor.height) {
        width=registered->physical_width;height=registered->physical_height;
      }
    }
    NativeReflectionTarget reflection;
    const auto* entry=FindNativeReflectionSurface(reflections,descriptor,depth);
    const bool is_reflection=entry!=nullptr;
    if(entry) {
      reflection=*entry;width=reflection.physical_width;height=reflection.physical_height;
      if(reflection.sample_count_override)
        samples=SelectSupportedNativeSceneSampleCount(reflection.sample_count_override,supported_samples);
      if(width>caps.max_image_dimension_2d||height>caps.max_image_dimension_2d) {
        width=descriptor.width;height=descriptor.height;
      }
    } else if(scene_sample_override) {
      samples=SelectSupportedNativeSceneSampleCount(scene_sample_override,supported_samples);
    }
    if(!width||!height||width>caps.max_image_dimension_2d||height>caps.max_image_dimension_2d||
       !samples||samples>8||!(caps.sample_counts&(1u<<samples))) {
      error="Metal surface physical extent or coherent samples unsupported";return {};
    }
    const auto bucket=std::pair{descriptor.handle,depth};
    for(uint64_t id:next.surface_buckets[bucket]) {
      auto& record=next.surfaces.at(id);const auto& owner=record.owner;
      if(SurfaceDescriptorsEqual(record.descriptor,descriptor)&&owner->width==width&&
         owner->height==height&&owner->samples==samples&&record.is_reflection==is_reflection&&
         (!is_reflection||SameNativeReflectionRegistration(record.reflection,reflection))) {
        // Persistent resolved reflection textures are separate. An old capture
        // workspace must receive a real first-use clear before this frame uses it.
        if(is_reflection&&!HasCurrentNativeReflectionCaptureContent(record.capture,present.submitted_frame))
          forget_contents(owner->key);
        retain(owner);return owner;
      }
    }
    auto owner=std::make_shared<r::Surface>();
    owner->key={++next_native_metal_allocation_,1};owner->format=format;
    owner->width=width;owner->height=height;owner->samples=samples;
    auto& record=next.surfaces[owner->key.id];record.descriptor=descriptor;record.owner=owner;
    record.logical_width=descriptor.width;record.logical_height=descriptor.height;
    record.is_reflection=is_reflection;record.reflection=reflection;
    next.surface_buckets[bucket].push_back(owner->key.id);retain(owner);return owner;
  };
  const auto placement_owner=[&](const SurfaceDescriptor& descriptor,bool depth)
      ->const NativeMetalFrameState::PlacementOwner* {
    if(FindNativeReflectionSurface(reflections,descriptor,depth))return nullptr;
    GuestSurfaceView view;if(!DecodeGuestSurfaceView(descriptor,depth,view))return nullptr;
    const auto it=next.placements.find(GetGuestPlacementKey(view));
    if(it==next.placements.end())return nullptr;
    const auto surface=next.surfaces.find(it->second.surface);
    return surface!=next.surfaces.end()&&surface->second.materialized_serial==it->second.serial?
        &it->second:nullptr;
  };
  const auto claim=[&](const std::shared_ptr<const r::Surface>& surface) {
    auto& record=next.surfaces.at(surface->key.id);
    const uint64_t serial=++next_native_metal_content_serial_;
    record.materialized_serial=serial;
    if(record.is_reflection) {
      ClaimNativeReflectionCaptureContent(record.capture,serial,serial,present.submitted_frame,
                                           diagnostic_command_index_,0,0);
    } else {
      GuestSurfaceView view;
      if(DecodeGuestSurfaceView(record.descriptor,r::SupportsAspect(surface->format,r::Aspect::Depth),view))
        next.placements[GetGuestPlacementKey(view)]={surface->key.id,serial,view};
    }
  };
  const auto prepare_surface=[&](const std::shared_ptr<const r::Surface>& surface) {
    auto& record=next.surfaces.at(surface->key.id);
    if(record.is_reflection)return true;
    const bool depth=r::SupportsAspect(surface->format,r::Aspect::Depth);
    GuestSurfaceView view;
    if(!DecodeGuestSurfaceView(record.descriptor,depth,view))return reject("Metal attachment guest placement invalid");
    const auto* current=placement_owner(record.descriptor,depth);
    if(!current) {forget_contents(surface->key);return true;}
    if(current->surface==surface->key.id)return true;
    // Save before retargeting the map. Both allocations remain retained by this
    // plan, including when materialization spans differently sampled views.
    const auto writer=*current;const auto input=next.surfaces.at(writer.surface).owner;retain(input);
    const r::SurfaceView input_view{input->key,0,0,depth?r::Aspect::Depth:r::Aspect::Color};
    const r::SurfaceView output_view{surface->key,0,0,input_view.aspect};
    if(!next.contents.contains(input_view))return reject("Metal placement writer contents undefined");
    if(depth) {
      if(input->format!=r::Format::Depth32FloatStencil8||surface->format!=input->format||
         input->samples!=1||surface->samples!=1||input->width!=surface->width||input->height!=surface->height||
         writer.view!=view||!next.contents.contains({input->key,0,0,r::Aspect::Stencil}))
        return reject("Metal depth placement materialization requires identical combined storage");
      r::ImageCopy copy;copy.source=input_view;copy.destination=output_view;
      copy.extent={surface->width,surface->height};copy.combined_depth_stencil=true;
      plan->commands.push_back(copy);next.contents.insert({surface->key,0,0,r::Aspect::Stencil});
    } else {
      NativeResolveConversionConstants constants;
      constants.source_guest_sample_type=uint32_t(writer.view.msaa_samples);
      constants.requested_guest_sample_type=uint32_t(view.msaa_samples);
      constants.destination_guest_sample_type=uint32_t(view.msaa_samples);
      constants.physical_source_sample_type=uint32_t(ConvertHostSamplesToGuestSamples(VkSampleCountFlagBits(input->samples)));
      constants.physical_destination_sample_type=uint32_t(ConvertHostSamplesToGuestSamples(VkSampleCountFlagBits(surface->samples)));
      constants.source_width=input->width;constants.source_height=input->height;
      constants.destination_width=surface->width;constants.destination_height=surface->height;constants.mode=1;
      if(RequiresNativeResolveScaleConversion(input->width,input->height,surface->width,surface->height))
        constants.flags|=kResolveConversionFlagScaleConversion;
      else if(CanDirectlyMaterializePhysicalSamples(writer.view==view,input->samples,surface->samples))
        constants.flags|=kResolveConversionFlagDirectPhysicalMaterialization;
      auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
      bytes->conversion={uint64_t(r::HostProgram::Resolve),writer.serial,input->key.id,surface->key.id};
      const auto* data=reinterpret_cast<const uint8_t*>(&constants);bytes->value.assign(data,data+sizeof(constants));
      r::HostDraw draw;draw.program=input->samples==1?r::HostProgram::Resolve:r::HostProgram::ResolveMSAA;
      draw.pipeline.colors[0]=surface->format;draw.pipeline.samples=surface->samples;
      draw.constants={bytes,0,sizeof(constants)};draw.scissor={0,0,surface->width,surface->height};
      draw.fetches[0].produced=input_view;draw.fetches[0].sampler=std::make_shared<r::Sampler>();
      r::Attachment attachment;attachment.view=output_view;attachment.load=r::Load::Clear;attachment.store=r::Store::Store;
      r::Pass pass;pass.colors[0]=attachment;pass.commands.push_back(std::move(draw));r::AppendPass(*plan,std::move(pass));
    }
    next.contents.insert(output_view);record.materialized_serial=writer.serial;
    next.placements[GetGuestPlacementKey(view)]={surface->key.id,writer.serial,view};return true;
  };
  const auto sample_override_for=[&](const NativeCommand& command) {
    const auto usage=GetRenderingTargetUsage(command);const auto& state=*command.pipeline_state;
    bool reflection=false,has_bound=false,all_multisampled=true;
    const auto inspect=[&](const SurfaceDescriptor& descriptor,bool depth) {
      if(!descriptor.handle)return;
      has_bound=true;all_multisampled&=descriptor.sample_type!=uint32_t(xenos::MsaaSamples::k1X);
      reflection|=FindNativeReflectionSurface(reflections,descriptor,depth)!=nullptr;
    };
    for(uint32_t i=0;i<kRenderTargetCount;++i)if(usage.color_attachment_mask&(1u<<i))inspect(state.render_targets[i],false);
    if(usage.depth_stencil_aspects)inspect(state.depth_stencil,true);
    return ShouldApplyNativeSceneSampleOverride(reflection,has_bound,all_multisampled)?
        uint32_t(GetNativeSceneSampleOverride()):0u;
  };
  const auto validate_append=[&](r::FrameCommand command) {
    if(auto* pass=std::get_if<r::Pass>(&command))r::AppendPass(*plan,std::move(*pass));
    else plan->commands.push_back(std::move(command));
    return true;
  };
  const auto clear_surface=[&](const std::shared_ptr<const r::Surface>& surface,
                              r::Aspect aspect,const ClearCommand& clear,uint32_t slot) {
    if(!prepare_surface(surface))return false;
    const auto& record=next.surfaces.at(surface->key.id);
    const auto logical_left=std::clamp(clear.left,0,int32_t(record.logical_width));
    const auto logical_top=std::clamp(clear.top,0,int32_t(record.logical_height));
    const auto logical_right=std::clamp(clear.right,logical_left,int32_t(record.logical_width));
    const auto logical_bottom=std::clamp(clear.bottom,logical_top,int32_t(record.logical_height));
    const auto left=ScaleCoordinateFloor(logical_left,record.logical_width,surface->width);
    const auto top=ScaleCoordinateFloor(logical_top,record.logical_height,surface->height);
    const auto right=ScaleCoordinateCeil(logical_right,record.logical_width,surface->width);
    const auto bottom=ScaleCoordinateCeil(logical_bottom,record.logical_height,surface->height);
    if(left==right || top==bottom)return true;
    r::SurfaceView view{surface->key,0,0,aspect};r::Attachment attachment;
    attachment.view=view;attachment.store=r::Store::Store;
    const bool full=left==0 && top==0 && right==int32_t(surface->width) && bottom==int32_t(surface->height);
    attachment.load=full?r::Load::Clear:next.contents.contains(view)?r::Load::Load:r::Load::Clear;
    for(uint32_t c=0;c<4;++c)attachment.clear_color[c]=std::bit_cast<float>(clear.color_bits[c]);
    attachment.clear_depth=std::bit_cast<double>(clear.depth_bits);attachment.clear_stencil=clear.stencil;
    r::Pass pass;
    if(aspect==r::Aspect::Color)pass.colors[slot]=attachment;
    else if(aspect==r::Aspect::Depth)pass.depth=attachment;
    else pass.stencil=attachment;
    if(!full) {
      r::RectClear rectangle;
      rectangle.rectangle={uint32_t(left),uint32_t(top),uint32_t(right-left),uint32_t(bottom-top)};
      rectangle.colors=aspect==r::Aspect::Color?1u<<slot:0;
      rectangle.depth=aspect==r::Aspect::Depth;rectangle.stencil=aspect==r::Aspect::Stencil;
      for(uint32_t c=0;c<4;++c)rectangle.color[c]=float(attachment.clear_color[c]);
      rectangle.depth_value=float(attachment.clear_depth);rectangle.stencil_value=clear.stencil;
      pass.commands.push_back(rectangle);
      // The existing renderer defines a previously unwritten attachment before
      // partial operations. This is an actual Metal load-clear, not a claim
      // that partial coverage initialized the remaining pixels.
      if(attachment.load==r::Load::Clear) {
        auto& initial=aspect==r::Aspect::Color?*pass.colors[slot]:
                      aspect==r::Aspect::Depth?*pass.depth:*pass.stencil;
        initial.clear_color={0,0,0,1};initial.clear_depth=1;initial.clear_stencil=0;
      }
    }
    if(attachment.load==r::Load::Clear)next.contents.insert(view);
    claim(surface);return validate_append(std::move(pass));
  };
  const auto produced_texture=[&](const std::shared_ptr<const NativeTextureResource>& resource) {
    auto existing=next.textures.find(resource->generation);
    if(!resource->gpu_produced)return reject("Metal produced texture has no GPU resource contract");
    VkFormat format=ConvertTextureFormat(resource->info.format);
    // Metal uses float32 depth storage. Packed-depth reconstruction retains the
    // original encoding and explicitly quantizes to UNORM24 or float24, rather
    // than treating the host storage bytes as guest depth/stencil bytes.
    if(format==VK_FORMAT_D24_UNORM_S8_UINT)format=VK_FORMAT_D32_SFLOAT_S8_UINT;
    const auto lowered=r::source::PixelFormat(format);
    if(lowered==r::Format::Invalid||lowered>r::Format::Depth32FloatStencil8)
      return reject("Metal produced texture requires a renderable native format");
    uint64_t virtual_lifetime=0,packed_allocation=0;
    {
      std::lock_guard lock(texture_resource_mutex_);
      const auto* registered=virtual_resource_registry_.Find(resource->handle);
      if(registered&&registered->kind==VirtualResourceKind::kTexture&&
         registered->logical_width==resource->info.width+1&&registered->logical_height==resource->info.height+1)
        virtual_lifetime=registered->lifetime;
    }
    const auto* reflection=FindNativeReflectionTexture(reflections,resource->handle,
                                                       resource->info.width+1,resource->info.height+1);
    if(resource->packed_depth_source) {
      const auto parent=next.textures.find(resource->packed_depth_source->generation);
      if(parent==next.textures.end())return reject("Metal packed alias requires prepared source metadata");
      packed_allocation=parent->second.owner->key.id;
    }
    if(existing!=next.textures.end()&&existing->second.virtual_lifetime==virtual_lifetime&&
       existing->second.packed_allocation==packed_allocation&&existing->second.description->is_reflection==bool(reflection)&&
       (!reflection||SameNativeReflectionRegistration(existing->second.description->reflection,*reflection))) {
      // Immutable resource identity plus registration identity avoids rebuilding
      // CPU descriptions on every draw that samples the same produced texture.
      retain(existing->second.owner);return true;
    }
    PreparedNativeTextureDescription description;
    NativeTextureCapabilities tcaps{caps.max_image_dimension_2d,true,true,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT};
    const NativeTextureImage* packed_source=nullptr;
    if(resource->packed_depth_source) {
      const auto parent=next.textures.find(resource->packed_depth_source->generation);
      if(parent==next.textures.end())return reject("Metal packed alias requires prepared source metadata");
      packed_source=parent->second.description.get();
    }
    if(!PrepareNativeTextureDescription(NativeResourceView<NativeTextureResource>(resource),format,packed_source,tcaps,description,error,&reflections))return false;
    const auto& metadata=*description.image;
    if(existing!=next.textures.end()) {
      const auto& old=*existing->second.description;
      if(old.width==metadata.width&&old.height==metadata.height&&old.mip_levels==metadata.mip_levels&&
         old.view_type==metadata.view_type&&old.format==metadata.format&&old.is_reflection==metadata.is_reflection&&
         (!old.is_reflection||SameNativeReflectionRegistration(old.reflection,metadata.reflection))) {
        existing->second.virtual_lifetime=virtual_lifetime;existing->second.packed_allocation=packed_allocation;
        existing->second.description=std::shared_ptr<NativeTextureImage>(std::move(description.image));
        existing->second.description->source.reset();retain(existing->second.owner);return true;
      }
      // A virtual/reflection registration can change physical storage without
      // changing the guest resource generation. Retire its old content journal.
      forget_contents(existing->second.owner->key);
    }
    auto surface=std::make_shared<r::Surface>();surface->key={++next_native_metal_allocation_,1};
    surface->format=lowered;surface->width=metadata.width;surface->height=metadata.height;
    surface->levels=metadata.mip_levels;
    switch(metadata.view_type) {
      case VK_IMAGE_VIEW_TYPE_2D:surface->kind=r::ImageKind::Texture2D;break;
      case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
        surface->kind=r::ImageKind::Texture2DArray;surface->layers=description.allocation.arrayLayers;break;
      case VK_IMAGE_VIEW_TYPE_CUBE:surface->kind=r::ImageKind::TextureCube;break;
      default:return reject("Metal produced volume storage requires explicit slice lowering");
    }
    if(!surface->width||!surface->height||surface->width>caps.max_image_dimension_2d||
       surface->height>caps.max_image_dimension_2d||surface->levels>std::bit_width(std::max(surface->width,surface->height))||
       r::SurfaceSlices(*surface)>2048)
      return reject("Metal produced texture storage extent unavailable");
    retain(surface);
    // Match the existing renderer's deterministic first-use fallback, including
    // every mip and cube/array layer. Later resolves replace these bytes in
    // command order; the CPU never uploads or reads GPU-produced pixels.
    for(uint32_t level=0;level<surface->levels;++level)
      for(uint32_t slice=0;slice<r::SurfaceSlices(*surface);++slice) {
        r::Pass pass;
        for(r::Aspect aspect:{r::Aspect::Color,r::Aspect::Depth,r::Aspect::Stencil})
          if(r::SupportsAspect(surface->format,aspect)) {
            r::Attachment attachment;attachment.view={surface->key,level,slice,aspect};
            attachment.load=r::Load::Clear;attachment.store=r::Store::Store;attachment.clear_depth=1;
            if(aspect==r::Aspect::Color)pass.colors[0]=attachment;
            else if(aspect==r::Aspect::Depth)pass.depth=attachment;
            else pass.stencil=attachment;
            next.contents.insert(attachment.view);
          }
        r::AppendPass(*plan,std::move(pass));
      }
    auto cpu=std::shared_ptr<NativeTextureImage>(std::move(description.image));cpu->source.reset();
    auto& record=next.textures[resource->generation];record={resource,surface,std::move(cpu)};
    record.content_serial=++next_native_metal_content_serial_;
    record.virtual_lifetime=virtual_lifetime;record.packed_allocation=packed_allocation;return true;
  };
  const auto produced_fetch=[&](const std::shared_ptr<const NativeTextureResource>& resource) {
    if(!resource->packed_depth_source)return produced_texture(resource);
    const auto& parent=resource->packed_depth_source;
    if(parent->packed_depth_source||!produced_texture(parent))
      return reject("Metal packed alias source must be original resolved depth/stencil storage");
    if(!produced_texture(resource))return false;
    auto& input=next.textures.at(parent->generation);auto& destination=next.textures.at(resource->generation);
    const auto& source_storage=*input.owner;const auto& storage=*destination.owner;
    if(source_storage.format!=r::Format::Depth32FloatStencil8||source_storage.kind!=r::ImageKind::Texture2D||
       source_storage.levels!=1||source_storage.samples!=1||storage.levels!=1||storage.kind!=r::ImageKind::Texture2D||
       storage.format!=r::Format::RGBA8Unorm||storage.width!=source_storage.width||storage.height!=source_storage.height||
       (parent->info.format!=xenos::TextureFormat::k_24_8&&parent->info.format!=xenos::TextureFormat::k_24_8_FLOAT))
      return reject("Metal packed alias encoding or storage shape unsupported");
    if(destination.packed_source_serial==input.content_serial)return true;
    NativeResolveConversionConstants constants;constants.mode=parent->info.format==xenos::TextureFormat::k_24_8_FLOAT;
    constants.flags=resource->fetch.swizzle;
    auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
    bytes->conversion={uint64_t(r::HostProgram::PackedDepthAlias),parent->generation,input.content_serial,constants.flags};
    const auto* data=reinterpret_cast<const uint8_t*>(&constants);bytes->value.assign(data,data+sizeof(constants));
    r::HostDraw draw;draw.program=r::HostProgram::PackedDepthAlias;draw.pipeline.colors[0]=storage.format;
    draw.constants={bytes,0,sizeof(constants)};draw.scissor={0,0,storage.width,storage.height};
    draw.fetches[0].produced=r::SurfaceView{source_storage.key,0,0,r::Aspect::Depth};
    draw.fetches[1].produced=r::SurfaceView{source_storage.key,0,0,r::Aspect::Stencil};
    draw.fetches[0].sampler=draw.fetches[1].sampler=std::make_shared<r::Sampler>();
    r::Attachment attachment;attachment.view={storage.key,0,0,r::Aspect::Color};
    attachment.load=r::Load::Load;attachment.store=r::Store::Store;
    r::Pass pass;pass.colors[0]=attachment;pass.commands.push_back(std::move(draw));r::AppendPass(*plan,std::move(pass));
    destination.packed_source_serial=input.content_serial;destination.content_serial=++next_native_metal_content_serial_;
    return true;
  };
  const auto resolve_clears=[&](const NativeCommand& command,const ResolveCommand& resolve) {
    if(!(resolve.flags&0x300u))return true;
    ClearCommand clear;
    std::copy(std::begin(resolve.clear_color_bits),std::end(resolve.clear_color_bits),clear.color_bits);
    clear.depth_bits=resolve.clear_depth_bits;clear.stencil=resolve.clear_stencil;
    const auto clear_target=[&](const SurfaceDescriptor& descriptor,bool depth) {
      const auto* writer=placement_owner(descriptor,depth);
      auto surface=writer?next.surfaces.at(writer->surface).owner:surface_for(descriptor,depth);
      if(!surface)return false;retain(surface);
      // Resolve clears act on the actual writer. The existing renderer clamps
      // their rectangle in that writer's logical extent, before physical scaling.
      const auto& record=next.surfaces.at(surface->key.id);
      clear.left=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.left:0,0,int32_t(record.logical_width));
      clear.top=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.top:0,0,int32_t(record.logical_height));
      clear.right=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.right:int32_t(record.logical_width),clear.left,int32_t(record.logical_width));
      clear.bottom=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.bottom:int32_t(record.logical_height),clear.top,int32_t(record.logical_height));
      if(!clear_surface(surface,depth?r::Aspect::Depth:r::Aspect::Color,clear,0))return false;
      return !depth||clear_surface(surface,r::Aspect::Stencil,clear,0);
    };
    if(resolve.flags&(1u<<8)) {
      const uint32_t slot=resolve.flags&7u;
      const auto& descriptor=command.pipeline_state&&slot<kRenderTargetCount?
          command.pipeline_state->render_targets[slot]:resolve.source;
      if(!clear_target(descriptor,false))return false;
    }
    if(resolve.flags&(1u<<9)) {
      if(!command.pipeline_state||!command.pipeline_state->depth_stencil.handle)
        return reject("Metal resolve source depth clear unavailable");
      if(!clear_target(command.pipeline_state->depth_stencil,true))return false;
    }
    return true;
  };
  PostFxScheduler postfx;postfx.BeginFrame();
  const auto host_fetch=[&](const NativeCommand& command,uint32_t slot,r::HostFetch& fetch) {
    const auto& resource=command.textures[slot];
    if(resource&&resource->gpu_produced&&!produced_fetch(NativeResourceView<NativeTextureResource>(resource).Share()))return false;
    if(!PrepareNativeMetalFetch(NativeResourceView<NativeTextureResource>(resource),command.texture_fetches[slot],next,fetch,error))return false;
    if(fetch.produced) {
      if(fetch.produced->kind!=r::ImageKind::Texture2D)return reject("Metal host effect requires a 2D input");
      fetch.produced->level=fetch.produced->slice=0;fetch.produced->levels=fetch.produced->slices=1;
    } else if(!fetch.image||fetch.image->kind!=r::ImageKind::Texture2D)return reject("Metal host effect texture unavailable");
    return true;
  };
  const auto capture_scene=[&](const NativeCommand& command) {
    bool captured=false;
    if(command.textures[2]&&command.textures[2]->gpu_produced&&command.textures[0]&&command.textures[1]) {
      r::HostFetch scene,depth,mask;
      if(!host_fetch(command,2,scene)||!host_fetch(command,1,depth)||!host_fetch(command,0,mask))return false;
      auto& texture=next.textures.at(command.textures[2]->generation);
      if(!r::SupportsAspect(texture.owner->format,r::Aspect::Color)||texture.owner->kind!=r::ImageKind::Texture2D)
        return reject("Metal composite scene requires color 2D storage");
      const auto bytes=command.shader_state?AuthoritativeConstantState::Materialize(command.shader_state->pixel_constants):nullptr;
      const auto vector=[&](size_t offset) {
        std::array<float,4> value{};const size_t relative=offset-kPixelConstantsOffset;
        if(bytes&&relative<=bytes->size()&&sizeof(value)<=bytes->size()-relative)
          for(size_t c=0;c<4;++c)value[c]=std::bit_cast<float>(LoadGuestWord(*bytes,relative+c*4));
        return value;
      };
      r::SplitConstants constants;constants.projection=vector(kDofProjectionSnapshotOffset);
      constants.distance=vector(kDofDistanceSnapshotOffset);constants.blur=vector(kDofBlurSnapshotOffset);
      if(!r::AppendSplitPostFx(*plan,next.contents,texture.owner,depth,mask,constants,utility_for,next_native_metal_allocation_,error))return false;
      texture.content_serial=++next_native_metal_content_serial_;captured=true;
      if(REXCVAR_GET(gta4_native_host_sun_shafts)) {
        const auto parameters=BuildSunShaftParameters(environmental_data.get());
        if(parameters.valid) {
          r::SunConstants sun;sun.density=parameters.density;sun.decay=parameters.decay;
          sun.screen={parameters.screen_position[0],parameters.screen_position[1],parameters.intensity,parameters.horizon_fade};
          std::copy(parameters.sun_color.begin(),parameters.sun_color.end(),sun.color_and_sky_start.begin());
          sun.color_and_sky_start[3]=0.9f;sun.sky_end[0]=1;
          if(!r::AppendSunShafts(*plan,next.contents,texture.owner,depth,sun,utility_for,next_native_metal_allocation_,error))return false;
          texture.content_serial=++next_native_metal_content_serial_;
        }
      }
    }
    postfx.FinishSceneCapture(captured);return true;
  };
  size_t command_index=0;
  diagnostic_submitted_frame_=present.submitted_frame;
  for(const NativeCommand& command:current_frame_) {
    diagnostic_command_index_=command_index++;
    if(command.type==CommandType::kRenderPhaseMarker) {
      // The worker already applies phase nesting and each draw captures its
      // effective phase. This marker performs no graphics-memory operation.
      if(command.bytes.size()!=sizeof(RenderPhaseMarkerCommand))return reject("Metal render phase marker malformed");
      RenderPhaseMarkerCommand marker;std::memcpy(&marker,command.bytes.data(),sizeof(marker));
      postfx.ObserveMarker(marker.phase,marker.event);continue;
    }
    if(command.type==CommandType::kReleaseResource) {
      ReleaseResourceCommand release{};std::memcpy(&release,command.bytes.data(),sizeof(release));
      EraseNativeReflectionRegistration(reflections,release.resource);
      std::erase_if(next.surfaces,[&](const auto& entry){return entry.second.descriptor.handle==release.resource;});
      std::erase_if(next.surface_buckets,[&](const auto& entry){return entry.first.first==release.resource;});
      std::erase_if(next.placements,[&](const auto& entry){return !next.surfaces.contains(entry.second.surface);});
      std::erase_if(next.textures,[&](const auto& entry){return entry.second.resource->handle==release.resource;});
      continue;
    }
    if(command.type==CommandType::kDrawPrimitive || command.type==CommandType::kDrawPrimitiveUp ||
       command.type==CommandType::kDrawIndexedPrimitive) {
      if(!command.pipeline_state||!command.pipeline_state->vertex_shader_resource)
        return reject("Metal draw snapshot unavailable");
      if(postfx.scene_capture_pending()&&!capture_scene(command))return false;
      const auto usage=GetRenderingTargetUsage(command);
      NativeRenderingTarget target;r::Pass pass;const auto sample_override=sample_override_for(command);
      const auto attachment_for=[&](const std::shared_ptr<const r::Surface>& surface,r::Aspect aspect) {
        r::Attachment attachment;attachment.view={surface->key,0,0,aspect};
        attachment.load=next.contents.contains(attachment.view)?r::Load::Load:r::Load::Clear;
        attachment.clear_color={0,0,0,1};attachment.clear_depth=1;
        attachment.store=r::Store::Store;return attachment;
      };
      const auto merge_extent=[&](const std::shared_ptr<const r::Surface>& surface) {
        const auto& record=next.surfaces.at(surface->key.id);
        const auto guest_samples=ConvertSurfaceSamples(record.descriptor.sample_type);
        if(target.width&&(target.width!=surface->width||target.height!=surface->height||
           uint32_t(target.samples)!=surface->samples||target.guest_samples!=guest_samples||
           target.logical_width!=record.logical_width||target.logical_height!=record.logical_height||
           target.is_reflection!=record.is_reflection||
           (target.is_reflection&&target.reflection.family!=record.reflection.family)))
          return reject("Metal draw attachments have incompatible physical/logical extent, sample topology or family");
        target.width=surface->width;target.height=surface->height;
        target.logical_width=record.logical_width;target.logical_height=record.logical_height;
        target.samples=VkSampleCountFlagBits(surface->samples);target.guest_samples=guest_samples;
        target.is_reflection=record.is_reflection;target.reflection=record.reflection;return true;
      };
      for(uint32_t i=0;i<kRenderTargetCount;++i)if(usage.color_attachment_mask&(1u<<i)) {
        const auto& descriptor=command.pipeline_state->render_targets[i];if(!descriptor.handle)continue;
        auto surface=surface_for(descriptor,false,sample_override);if(!surface||!merge_extent(surface)||!prepare_surface(surface))return false;
        pass.colors[i]=attachment_for(surface,r::Aspect::Color);
        target.color_formats[i]=ConvertSurfaceFormat(descriptor.format,false);
        target.color_attachment_mask|=1u<<i;
        target.color_write_mask|=NativeColorWriteMaskForTarget(usage.color_write_mask,i)<<(i*4);
      }
      if(usage.depth_stencil_aspects && command.pipeline_state->depth_stencil.handle) {
        const auto& descriptor=command.pipeline_state->depth_stencil;
        auto surface=surface_for(descriptor,true,sample_override);if(!surface||!merge_extent(surface)||!prepare_surface(surface))return false;
        // A combined allocation exposes both declared pipeline aspects. Preserve
        // the unmodified aspect instead of discarding it when only depth writes.
        pass.depth=attachment_for(surface,r::Aspect::Depth);
        pass.stencil=attachment_for(surface,r::Aspect::Stencil);
        target.depth_stencil_attachment_active=true;
        target.depth_format=ConvertSurfaceFormat(descriptor.format,true);
      }
      if(!target.width)return reject("Metal draw requires explicit attachmentless/presenter lowering");
      const uint32_t fetch_mask=command.pipeline_state->vertex_shader_resource->used_texture_mask|
          (command.pipeline_state->pixel_shader_resource?command.pipeline_state->pixel_shader_resource->used_texture_mask:0);
      for(uint32_t slot=0;slot<kTextureStageCount;++slot)if((fetch_mask&(1u<<slot))&&
          command.textures[slot]&&command.textures[slot]->gpu_produced) {
        if(!produced_fetch(NativeResourceView<NativeTextureResource>(command.textures[slot]).Share()))return false;
      }
      r::FrameDraw draw;if(!PrepareNativeMetalDraw(command,target,next,draw,error))return false;
      if(draw.capture) {
        for(uint32_t i=0;i<kRenderTargetCount;++i)if(pass.colors[i]) {
          const auto& attachment=*pass.colors[i];
          const auto surface=next.surfaces.at(attachment.view.surface.id).owner;
          if(attachment.load==r::Load::Clear)next.contents.insert(attachment.view);
          if(attachment.load==r::Load::Clear||NativeColorWriteMaskForTarget(target.color_write_mask,i))claim(surface);
        }
        const auto& fixed=command.fixed_function_state;
        const bool writes_depth=fixed.depth_enable&&fixed.depth_write_enable;
        const bool writes_stencil=fixed.stencil_enable&&
            ((fixed.stencil_write_mask&&(fixed.stencil_fail||fixed.stencil_depth_fail||fixed.stencil_pass))||
             (fixed.two_sided_stencil&&fixed.back_stencil_write_mask&&
              (fixed.ccw_stencil_fail||fixed.ccw_stencil_depth_fail||fixed.ccw_stencil_pass)));
        for(const auto& attachment:{pass.depth,pass.stencil})if(attachment&&attachment->load==r::Load::Clear)
          next.contents.insert(attachment->view);
        if(pass.depth&&(writes_depth||writes_stencil||pass.depth->load==r::Load::Clear||
                        (pass.stencil&&pass.stencil->load==r::Load::Clear)))
          claim(next.surfaces.at(pass.depth->view.surface.id).owner);
        pass.commands.push_back(std::move(draw));r::AppendPass(*plan,std::move(pass));
      }
      continue;
    }
    if(command.type==CommandType::kDepthSurfaceHandoff) {
      if(command.bytes.size()!=sizeof(DepthSurfaceHandoffCommand)||!command.depth_handoff_source)
        return reject("Metal depth handoff command or resolved source missing");
      DepthSurfaceHandoffCommand handoff;std::memcpy(&handoff,command.bytes.data(),sizeof(handoff));
      const auto resource=NativeResourceView<NativeTextureResource>(command.depth_handoff_source).Share();
      if(!produced_texture(resource))return false;
      const auto& input=next.textures.at(resource->generation);
      if(!input.depth_resolved)return reject("Metal depth handoff source has no successful depth resolve");
      auto destination=surface_for(handoff.destination,true);if(!destination)return false;
      if(next.surfaces.at(destination->key.id).is_reflection||input.owner->format!=r::Format::Depth32FloatStencil8||destination->format!=input.owner->format||
         input.owner->width!=destination->width||input.owner->height!=destination->height||
         input.description->logical_width!=handoff.destination.width||
         input.description->logical_height!=handoff.destination.height||input.owner->samples!=1||
         input.description->is_reflection)
        return reject("Metal depth handoff extent, encoding or sample contract mismatch");
      const bool rebuild=handoff.stencil_policy==ForwardStencilHandoffPolicy::kRebuildSceneCoverage;
      if(handoff.stencil_policy!=ForwardStencilHandoffPolicy::kPreserve&&!rebuild)
        return reject("Metal depth handoff stencil policy unavailable");
      if(rebuild&&resource->info.format!=xenos::TextureFormat::k_24_8&&
         resource->info.format!=xenos::TextureFormat::k_24_8_FLOAT)
        return reject("Metal scene coverage requires the original guest depth encoding");
      r::Attachment depth;depth.view={destination->key,0,0,r::Aspect::Depth};
      depth.store=r::Store::Store;depth.load=rebuild?r::Load::Clear:
          next.contents.contains(depth.view)?r::Load::Load:r::Load::Clear;
      depth.clear_depth=rebuild?0:1;
      auto stencil=depth;stencil.view.aspect=r::Aspect::Stencil;
      stencil.load=rebuild?r::Load::Clear:next.contents.contains(stencil.view)?r::Load::Load:r::Load::Clear;
      stencil.clear_stencil=rebuild?kForwardEmptySceneStencil:0;
      r::HostDraw draw;draw.program=rebuild?r::HostProgram::SceneDepthHandoff:r::HostProgram::DepthHandoff;
      draw.pipeline.depth=draw.pipeline.stencil=destination->format;draw.pipeline.samples=destination->samples;
      draw.pipeline.depth_test=draw.pipeline.depth_write=true;draw.pipeline.depth_compare=r::Compare::Always;
      draw.fetches[0].produced=r::SurfaceView{input.owner->key,0,0,r::Aspect::Depth};
      draw.fetches[0].sampler=std::make_shared<r::Sampler>();draw.scissor={0,0,destination->width,destination->height};
      if(rebuild) {
        const uint32_t float24=resource->info.format==xenos::TextureFormat::k_24_8_FLOAT;
        auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
        const auto* data=reinterpret_cast<const uint8_t*>(&float24);bytes->value.assign(data,data+sizeof(float24));
        draw.constants={bytes,0,sizeof(float24)};draw.pipeline.stencil_test=true;
        draw.pipeline.front.compare=draw.pipeline.back.compare=r::Compare::Always;
        draw.pipeline.front.pass=draw.pipeline.back.pass=r::StencilOp::Replace;
        draw.stencil_front_reference=draw.stencil_back_reference=kForwardCoveredSceneStencil;
      }
      r::Pass pass;pass.depth=depth;pass.stencil=stencil;pass.commands.push_back(std::move(draw));
      r::AppendPass(*plan,std::move(pass));next.contents.insert(depth.view);next.contents.insert(stencil.view);claim(destination);
      continue;
    }
    if(command.type==CommandType::kResolve) {
      if(command.bytes.size()!=sizeof(ResolveCommand)||!command.resolve_destination)
        return reject("Metal resolve command or destination unavailable");
      ResolveCommand resolve;std::memcpy(&resolve,command.bytes.data(),sizeof(resolve));
      resolve.flags=NormalizeResolveSampleFlags(resolve.flags,resolve.source.sample_type);
      const bool depth=(resolve.flags&7u)==4;
      if(resolve.parameters_valid&&(resolve.flags&0x300u))
        return reject("Metal resolve-clear packing overrides are unrepresentable");
      GuestSurfaceView guest_view;
      if(!DecodeGuestSurfaceView(resolve.source,depth,guest_view))return reject("Metal resolve guest sample view invalid");
      const auto* writer=placement_owner(resolve.source,depth);
      const GuestSurfaceView content_view=writer?writer->view:guest_view;
      auto source_surface=writer?next.surfaces.at(writer->surface).owner:surface_for(resolve.source,depth);
      if(!source_surface)return false;retain(source_surface);
      const auto& source_record=next.surfaces.at(source_surface->key.id);
      const r::SurfaceView source_view{source_surface->key,0,0,depth?r::Aspect::Depth:r::Aspect::Color};
      const auto resource=NativeResourceView<NativeTextureResource>(command.resolve_destination).Share();
      if(resource->packed_depth_source)return reject("Metal resolve cannot overwrite a packed-depth view");
      if(!produced_texture(resource))return false;
      auto& destination=next.textures.at(resource->generation);
      const auto& storage=*destination.owner;const auto& metadata=*destination.description;
      const bool current_source=source_record.is_reflection?
          HasCurrentNativeReflectionCaptureContent(source_record.capture,present.submitted_frame):writer!=nullptr;
      if(!current_source||!next.contents.contains(source_view)) {
        // First-use fallback initialization is not a successful resolve. Only a
        // completed reflection texture may survive a frame without a new capture.
        if(destination.resolved.contains({resolve.destination_level,resolve.destination_slice_or_face})&&
           (source_record.is_reflection||metadata.is_reflection)) {
          if(!resolve_clears(command,resolve))return false;continue;
        }
        return reject("Metal resolve source has no current written placement");
      }
      if(!r::SupportsAspect(storage.format,source_view.aspect)||resolve.destination_level>=storage.levels||
         resolve.destination_slice_or_face>=r::SurfaceSlices(storage))
        return reject("Metal resolve destination subresource unavailable");
      const int32_t logical_width=int32_t(resolve.source.width),logical_height=int32_t(resolve.source.height);
      const int32_t left=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.left:0,0,logical_width);
      const int32_t top=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.top:0,0,logical_height);
      const int32_t right=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.right:logical_width,left,logical_width);
      const int32_t bottom=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.bottom:logical_height,top,logical_height);
      const uint32_t logical_mip_width=CalculateNativeMipExtent(metadata.logical_width,resolve.destination_level);
      const uint32_t logical_mip_height=CalculateNativeMipExtent(metadata.logical_height,resolve.destination_level);
      const uint32_t mip_width=CalculateNativeMipExtent(storage.width,resolve.destination_level);
      const uint32_t mip_height=CalculateNativeMipExtent(storage.height,resolve.destination_level);
      const int32_t destination_x=resolve.destination_point_valid?resolve.destination_point.x:0;
      const int32_t destination_y=resolve.destination_point_valid?resolve.destination_point.y:0;
      if(destination_x<0||destination_y<0||uint32_t(destination_x)>=logical_mip_width||uint32_t(destination_y)>=logical_mip_height)
        return reject("Metal color resolve destination point invalid");
      const uint32_t copy_width=std::min(uint32_t(right-left),logical_mip_width-uint32_t(destination_x));
      const uint32_t copy_height=std::min(uint32_t(bottom-top),logical_mip_height-uint32_t(destination_y));
      if(!copy_width||!copy_height)return reject("Metal color resolve copy extent empty");
      NativeResolveConversionConstants constants;
      constants.source_x=ScaleCoordinateFloor(left,logical_width,source_surface->width);
      constants.source_y=ScaleCoordinateFloor(top,logical_height,source_surface->height);
      constants.source_width=ScaleCoordinateCeil(left+copy_width,logical_width,source_surface->width)-constants.source_x;
      constants.source_height=ScaleCoordinateCeil(top+copy_height,logical_height,source_surface->height)-constants.source_y;
      constants.destination_x=ScaleCoordinateFloor(destination_x,logical_mip_width,mip_width);
      constants.destination_y=ScaleCoordinateFloor(destination_y,logical_mip_height,mip_height);
      constants.destination_width=ScaleCoordinateCeil(destination_x+copy_width,logical_mip_width,mip_width)-constants.destination_x;
      constants.destination_height=ScaleCoordinateCeil(destination_y+copy_height,logical_mip_height,mip_height)-constants.destination_y;
      auto select=SanitizeGuestCopySampleSelect(DecodeResolveSampleSelect(resolve.flags),guest_view.msaa_samples,depth);
      const auto host_samples=ConvertHostSamplesToGuestSamples(VkSampleCountFlagBits(source_surface->samples));
      const auto mapping=NormalizeColorResolveSampleMapping(content_view.msaa_samples,guest_view.msaa_samples,host_samples,select);
      constants.source_guest_sample_type=uint32_t(mapping.content_samples);
      constants.requested_guest_sample_type=uint32_t(mapping.requested_samples);
      constants.physical_source_sample_type=uint32_t(host_samples);constants.sample_select=uint32_t(mapping.sample_select);
      const int32_t exponent=depth?0:NativeResolveExponent(resolve.flags);
      constants.flags=(uint32_t(exponent)&63u)<<8;
      if(storage.format==r::Format::RGBA16Float)constants.flags|=kResolveConversionFlagXenosFloat16Pack;
      if(RequiresNativeResolveScaleConversion(constants.source_width,constants.source_height,
                                             constants.destination_width,constants.destination_height))
        constants.flags|=kResolveConversionFlagScaleConversion;
      const r::SurfaceView destination_view{storage.key,resolve.destination_level,resolve.destination_slice_or_face,source_view.aspect};
      const bool combined_depth=depth&&storage.format==r::Format::Depth32FloatStencil8&&source_surface->format==storage.format;
      if(depth&&(!combined_depth||!next.contents.contains({source_surface->key,0,0,r::Aspect::Stencil})||
         (constants.flags&kResolveConversionFlagScaleConversion)))
        return reject("Metal depth resolve requires matching defined depth/stencil and unscaled storage");
      const bool exact=source_surface->samples==1&&source_surface->format==storage.format&&
          !(constants.flags&~kResolveConversionFlagXenosFloat16Pack)&&
          (depth||(content_view.msaa_samples==xenos::MsaaSamples::k1X&&guest_view.msaa_samples==xenos::MsaaSamples::k1X));
      if(exact) {
        r::ImageCopy copy;copy.source=source_view;copy.destination=destination_view;
        copy.source_origin={uint32_t(constants.source_x),uint32_t(constants.source_y)};
        copy.destination_origin={uint32_t(constants.destination_x),uint32_t(constants.destination_y)};
        copy.extent={constants.destination_width,constants.destination_height};
        copy.combined_depth_stencil=combined_depth;
        plan->commands.push_back(copy);
      } else if(depth) {
        if(select!=xenos::CopySampleSelect::k0||constants.source_x||constants.source_y||
           constants.destination_x||constants.destination_y||source_surface->width!=mip_width||
           source_surface->height!=mip_height||constants.destination_width!=mip_width||constants.destination_height!=mip_height)
          return reject("Metal combined MSAA depth/stencil resolve requires full sample-zero storage");
        r::Attachment attachment;attachment.view=source_view;attachment.load=r::Load::Load;
        attachment.store=r::Store::StoreAndResolve;attachment.resolve=destination_view;attachment.filter=r::ResolveFilter::Sample0;
        r::Pass pass;pass.depth=attachment;attachment.view.aspect=r::Aspect::Stencil;
        attachment.resolve->aspect=r::Aspect::Stencil;pass.stencil=attachment;
        r::AppendPass(*plan,std::move(pass));
      } else {
        auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
        bytes->conversion={uint64_t(r::HostProgram::Resolve),source_surface->samples,resource->generation};
        const auto* data=reinterpret_cast<const uint8_t*>(&constants);bytes->value.assign(data,data+sizeof(constants));
        r::HostDraw draw;draw.program=source_surface->samples==1?r::HostProgram::Resolve:r::HostProgram::ResolveMSAA;
        draw.pipeline.colors[0]=storage.format;draw.constants={bytes,0,sizeof(constants)};
        draw.fetches[0].produced=source_view;draw.fetches[0].sampler=std::make_shared<r::Sampler>();
        draw.scissor={uint32_t(constants.destination_x),uint32_t(constants.destination_y),
                      constants.destination_width,constants.destination_height};
        r::Attachment attachment;attachment.view=destination_view;attachment.load=r::Load::Load;attachment.store=r::Store::Store;
        r::Pass pass;pass.colors[0]=attachment;pass.commands.push_back(std::move(draw));r::AppendPass(*plan,std::move(pass));
      }
      // Destination storage was explicitly initialized before this partial or
      // full resolve, so pixels outside the copied rectangle remain defined.
      next.contents.insert(destination_view);
      destination.content_serial=++next_native_metal_content_serial_;
      destination.resolved.insert({resolve.destination_level,resolve.destination_slice_or_face});
      if(depth) {
        next.contents.insert({storage.key,resolve.destination_level,resolve.destination_slice_or_face,r::Aspect::Stencil});
        if(!resolve.destination_level&&!resolve.destination_slice_or_face)destination.depth_resolved=true;
      }
      if(!resolve_clears(command,resolve))return false;
      continue;
    }
    if(command.type!=CommandType::kClear) {
      error=fmt::format("Metal live command {} still needs CPU frame lowering",uint32_t(command.type));
      return false;
    }
    if(!command.pipeline_state || command.bytes.size()!=sizeof(ClearCommand))
      return reject("Metal clear snapshot unavailable");
    ClearCommand clear{};std::memcpy(&clear,command.bytes.data(),sizeof(clear));
    if(clear.flags&~0x3fu)return reject("Metal clear flags unsupported");
    const auto& state=*command.pipeline_state;const auto sample_override=sample_override_for(command);
    for(uint32_t i=0;i<kRenderTargetCount;++i)if(clear.flags&(1u<<i)) {
      // Guest null render targets have no clear destination.
      if(!state.render_targets[i].handle)continue;
      auto surface=surface_for(state.render_targets[i],false,sample_override);if(!surface)return false;
      if(!clear_surface(surface,r::Aspect::Color,clear,i))return false;
    }
    if((clear.flags&0x30u) && state.depth_stencil.handle) {
      auto surface=surface_for(state.depth_stencil,true,sample_override);if(!surface)return false;
      if((clear.flags&0x10u) && !clear_surface(surface,r::Aspect::Depth,clear,0))return false;
      if((clear.flags&0x20u) && !clear_surface(surface,r::Aspect::Stencil,clear,0))return false;
    }
  }
  if(present.device) {
    if(!source)return reject("Metal presentation source resource missing");
    r::PresentationTarget target;if(!frame_backend_->Target(target,error))return false;
    // The existing utility shader emits encoded SDR or extended-linear HDR.
    // Enabling an sRGB drawable here would encode its SDR output a second time.
    // The HDR layer/EDR contract must be connected before using float drawables.
    if(target.format!=r::Format::BGRA8Unorm)return reject("Metal presentation requires the SDR drawable color contract");
    if(source->gpu_produced&&!produced_fetch(source))return false;
    r::HostFetch input;
    if(!PrepareNativeMetalFetch(NativeResourceView<NativeTextureResource>(source),source->fetch,next,input,error))return false;
    uint32_t width=0,height=0,logical_width=0,logical_height=0;
    if(input.produced) {
      const auto& texture=next.textures.at(source->generation);const auto& storage=*texture.owner;
      if(storage.kind!=r::ImageKind::Texture2D||!r::SupportsAspect(storage.format,r::Aspect::Color))
        return reject("Metal presentation requires single-slice color input");
      width=storage.width;height=storage.height;
      logical_width=texture.description->logical_width;logical_height=texture.description->logical_height;
      input.produced->levels=input.produced->slices=1;input.produced->level=input.produced->slice=0;
    } else {
      if(!input.image||input.image->kind!=r::ImageKind::Texture2D)return reject("Metal presentation texture unavailable");
      width=input.image->width;height=input.image->height;logical_width=source->info.width+1;logical_height=source->info.height+1;
    }
    r::PresentConstants constants;constants.source_width=width;constants.source_height=height;
    constants.destination_width=target.width;constants.destination_height=target.height;
    const bool ssaa=IsNativeSsaaEnabled()&&(width!=logical_width||height!=logical_height);
    constants.output_mode=GetNativePresentationAABits()|(ssaa?32u:0u)|4u|(REXCVAR_GET(gta4_native_output_dither)?8u:0u);
    const int quality=IsNativeSmaaEnabled()?int(ParseSmaaQuality(rex::cvar::Query<std::string>("gta4_native_smaa_quality"))):-1;
    if(quality>=0&&!next.smaa_lookups.area)next.smaa_lookups=r::MakeSmaaLookups(next_native_metal_allocation_);
    auto output=utility_for("present-output",target.width,target.height,target.format);
    if(!output||!r::AppendPresentation(*plan,next.contents,input,output,constants,quality,next.smaa_lookups,
                                     utility_for,next_native_metal_allocation_,error))return false;
  }
  if(!plan->commands.empty()) {
    r::SurfaceContents final;
    if(!r::ValidateFrame(*plan,native_metal_frame_->contents,final,error))return false;
    if(!frame_backend_->Submit(plan,present.device!=0,error))return false;
    std::erase_if(next.contents,[&](const auto& view){return r::FindSurface(*plan,view.surface)!=nullptr;});
    next.contents.insert(final.begin(),final.end());
  }
  std::erase_if(next.contents,[&](const auto& view) {
    return std::none_of(next.surfaces.begin(),next.surfaces.end(),[&](const auto& entry) {
      return entry.second.owner->key==view.surface;
    }) && std::none_of(next.textures.begin(),next.textures.end(),[&](const auto& entry) {
      return entry.second.owner->key==view.surface;
    }) && std::none_of(next.utilities.begin(),next.utilities.end(),[&](const auto& entry){return entry.second->key==view.surface;});
  });
  next.buffers=std::move(native_metal_frame_->buffers);
  next.images=std::move(native_metal_frame_->images);
  reflection_resources_=std::move(next.reflections);*native_metal_frame_=std::move(next);error.clear();return true;
}
