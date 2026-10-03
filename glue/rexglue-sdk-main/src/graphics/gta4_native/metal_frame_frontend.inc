// Live CPU frame producer, included in graphics_system.cpp. This is deliberately
// fail-closed while game draw/helper lowering is being connected: an unsupported
// command rejects the WHOLE batch before Metal submits any of it.
struct Gta4NativeGraphicsSystem::NativeMetalFrameState {
  struct SurfaceRecord {SurfaceDescriptor descriptor;std::shared_ptr<const theft4::render::Surface> owner;};
  struct TextureRecord {std::shared_ptr<const NativeTextureResource> resource;
                        std::shared_ptr<const theft4::render::Surface> owner;
                        std::shared_ptr<NativeTextureImage> description;};
  std::map<std::pair<uint32_t,bool>,SurfaceRecord> surfaces;
  std::map<uint64_t,TextureRecord> textures;
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
    native_metal_frame_->surfaces.clear();native_metal_frame_->textures.clear();
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
    if(entry.first.first!=handle)return false;forget(entry.second.owner->key);return true;
  });
  std::erase_if(native_metal_frame_->textures,[&](const auto& entry) {
    if(entry.second.resource->handle!=handle)return false;forget(entry.second.owner->key);return true;
  });
}

bool Gta4NativeGraphicsSystem::PublishNativeMetalFrame(
    const PresentCommand& present,const std::shared_ptr<const NativeTextureResource>& source,
    std::string& error) {
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
  next.surfaces=native_metal_frame_->surfaces;next.textures=native_metal_frame_->textures;
  next.contents=native_metal_frame_->contents;
  auto plan=std::make_shared<r::FramePlan>();plan->sequence=++next_native_metal_sequence_;
  const auto caps=frame_backend_->Capabilities();
  const auto reject=[&](const char* reason){error=reason;return false;};
  const auto retain=[&](const std::shared_ptr<const r::Surface>& surface) {
    if(std::none_of(plan->surfaces.begin(),plan->surfaces.end(),[&](const auto& s){return s->key==surface->key;}))
      plan->surfaces.push_back(surface);
  };
  const auto surface_for=[&](const SurfaceDescriptor& descriptor,bool depth)->std::shared_ptr<const r::Surface> {
    const auto format=r::source::PixelFormat(ConvertSurfaceFormat(descriptor.format,depth));
    const uint32_t samples=uint32_t(ConvertSurfaceSamples(descriptor.sample_type));
    if(!descriptor.handle || !descriptor.width || !descriptor.height ||
       descriptor.width>caps.max_image_dimension_2d || descriptor.height>caps.max_image_dimension_2d ||
       format==r::Format::Invalid || samples>8 || !(caps.sample_counts&(1u<<samples))) {
      error="Metal surface descriptor unsupported";return {};
    }
    if(FindNativeReflectionSurface(reflection_resources_,descriptor,depth)) {
      error="Metal reflection allocation policy requires full draw lowering";return {};
    }
    {
      std::lock_guard lock(texture_resource_mutex_);
      const auto* virtual_resource=virtual_resource_registry_.Find(descriptor.handle);
      if(virtual_resource && (virtual_resource->physical_width!=descriptor.width ||
                              virtual_resource->physical_height!=descriptor.height)) {
        error="Metal scaled placement requires full draw lowering";return {};
      }
    }
    const auto key=std::pair{descriptor.handle,depth};
    auto it=next.surfaces.find(key);
    if(it!=next.surfaces.end() && SurfaceDescriptorsEqual(it->second.descriptor,descriptor)) {
      retain(it->second.owner);return it->second.owner;
    }
    // Shared EDRAM placement cannot be treated as independent texture contents.
    // The complete producer must materialize aliases explicitly before reading.
    for(const auto& [other_key,record]:next.surfaces) {
      if(other_key!=key && record.descriptor.base==descriptor.base &&
          (record.descriptor.address&0x7ffu)==(descriptor.address&0x7ffu)) {
        error="Metal surface placement alias requires explicit materialization";return {};
      }
    }
    auto owner=std::make_shared<r::Surface>();
    owner->key={++next_native_metal_allocation_,1};owner->format=format;
    owner->width=descriptor.width;owner->height=descriptor.height;owner->samples=samples;
    next.surfaces[key]={descriptor,owner};retain(owner);return owner;
  };
  const auto validate_append=[&](r::FrameCommand command) {
    if(auto* pass=std::get_if<r::Pass>(&command))r::AppendPass(*plan,std::move(*pass));
    else plan->commands.push_back(std::move(command));
    return true;
  };
  const auto clear_surface=[&](const std::shared_ptr<const r::Surface>& surface,
                              r::Aspect aspect,const ClearCommand& clear,uint32_t slot) {
    const auto left=std::clamp(clear.left,0,int32_t(surface->width));
    const auto top=std::clamp(clear.top,0,int32_t(surface->height));
    const auto right=std::clamp(clear.right,left,int32_t(surface->width));
    const auto bottom=std::clamp(clear.bottom,top,int32_t(surface->height));
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
    return validate_append(std::move(pass));
  };
  const auto produced_texture=[&](const std::shared_ptr<const NativeTextureResource>& resource) {
    auto existing=next.textures.find(resource->generation);
    if(existing!=next.textures.end()) {retain(existing->second.owner);return true;}
    if(!resource->gpu_produced||resource->packed_depth_source)
      return reject("Metal packed-depth image requires its ordered conversion pass");
    const VkFormat format=ConvertTextureFormat(resource->info.format);
    const auto lowered=r::source::PixelFormat(format);
    if(lowered==r::Format::Invalid||lowered>r::Format::Depth32FloatStencil8)
      return reject("Metal produced texture requires a renderable native format");
    PreparedNativeTextureDescription description;
    NativeTextureCapabilities tcaps{caps.max_image_dimension_2d,true,true,
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT};
    if(!PrepareNativeTextureDescription(NativeResourceView<NativeTextureResource>(resource),format,nullptr,tcaps,description,error))return false;
    const auto& metadata=*description.image;
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
    next.textures[resource->generation]={resource,surface,std::move(cpu)};return true;
  };
  size_t command_index=0;
  diagnostic_submitted_frame_=uint32_t(plan->sequence);
  for(const NativeCommand& command:current_frame_) {
    diagnostic_command_index_=command_index++;
    if(command.type==CommandType::kReleaseResource) {
      ReleaseResourceCommand release{};std::memcpy(&release,command.bytes.data(),sizeof(release));
      std::erase_if(next.surfaces,[&](const auto& entry){return entry.first.first==release.resource;});
      std::erase_if(next.textures,[&](const auto& entry){return entry.second.resource->handle==release.resource;});
      continue;
    }
    if(command.type==CommandType::kDrawPrimitive || command.type==CommandType::kDrawPrimitiveUp ||
       command.type==CommandType::kDrawIndexedPrimitive) {
      if(!command.pipeline_state||!command.pipeline_state->vertex_shader_resource)
        return reject("Metal draw snapshot unavailable");
      const auto usage=GetRenderingTargetUsage(command);
      NativeRenderingTarget target;r::Pass pass;
      const auto attachment_for=[&](const std::shared_ptr<const r::Surface>& surface,r::Aspect aspect) {
        r::Attachment attachment;attachment.view={surface->key,0,0,aspect};
        attachment.load=next.contents.contains(attachment.view)?r::Load::Load:r::Load::Clear;
        attachment.clear_color={0,0,0,1};attachment.clear_depth=1;
        attachment.store=r::Store::Store;return attachment;
      };
      const auto merge_extent=[&](const std::shared_ptr<const r::Surface>& surface) {
        if(target.width && (target.width!=surface->width||target.height!=surface->height||uint32_t(target.samples)!=surface->samples))
          return reject("Metal draw attachments have incompatible dimensions or samples");
        target.width=target.logical_width=surface->width;target.height=target.logical_height=surface->height;
        target.samples=target.guest_samples=VkSampleCountFlagBits(surface->samples);return true;
      };
      for(uint32_t i=0;i<kRenderTargetCount;++i)if(usage.color_attachment_mask&(1u<<i)) {
        const auto& descriptor=command.pipeline_state->render_targets[i];if(!descriptor.handle)continue;
        auto surface=surface_for(descriptor,false);if(!surface||!merge_extent(surface))return false;
        pass.colors[i]=attachment_for(surface,r::Aspect::Color);
        target.color_formats[i]=ConvertSurfaceFormat(descriptor.format,false);
        target.color_attachment_mask|=1u<<i;
        target.color_write_mask|=NativeColorWriteMaskForTarget(usage.color_write_mask,i)<<(i*4);
      }
      if(usage.depth_stencil_aspects && command.pipeline_state->depth_stencil.handle) {
        const auto& descriptor=command.pipeline_state->depth_stencil;
        auto surface=surface_for(descriptor,true);if(!surface||!merge_extent(surface))return false;
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
        if(!produced_texture(NativeResourceView<NativeTextureResource>(command.textures[slot]).Share()))return false;
      }
      r::FrameDraw draw;if(!PrepareNativeMetalDraw(command,target,next,draw,error))return false;
      if(draw.capture) {
        for(const auto& attachment:pass.colors)if(attachment&&attachment->load==r::Load::Clear)
          next.contents.insert(attachment->view);
        if(pass.depth&&pass.depth->load==r::Load::Clear)next.contents.insert(pass.depth->view);
        if(pass.stencil&&pass.stencil->load==r::Load::Clear)next.contents.insert(pass.stencil->view);
        pass.commands.push_back(std::move(draw));r::AppendPass(*plan,std::move(pass));
      }
      continue;
    }
    if(command.type==CommandType::kResolve) {
      if(command.bytes.size()!=sizeof(ResolveCommand)||!command.resolve_destination)
        return reject("Metal resolve command or destination unavailable");
      ResolveCommand resolve;std::memcpy(&resolve,command.bytes.data(),sizeof(resolve));
      resolve.flags=NormalizeResolveSampleFlags(resolve.flags,resolve.source.sample_type);
      const bool depth=(resolve.flags&7u)==4;
      if(depth)return reject("Metal depth resolve requires depth/stencil conversion lowering");
      if(resolve.parameters_valid&&(resolve.flags&0x300u))
        return reject("Metal resolve-clear packing overrides are unrepresentable");
      auto source_surface=surface_for(resolve.source,false);if(!source_surface)return false;
      const r::SurfaceView source_view{source_surface->key,0,0,r::Aspect::Color};
      if(!next.contents.contains(source_view))
        return reject("Metal resolve source placement has not been written");
      const auto resource=NativeResourceView<NativeTextureResource>(command.resolve_destination).Share();
      if(!produced_texture(resource))return false;
      const auto& destination=next.textures.at(resource->generation);
      const auto& storage=*destination.owner;const auto& metadata=*destination.description;
      if(!r::SupportsAspect(storage.format,r::Aspect::Color)||resolve.destination_level>=storage.levels||
         resolve.destination_slice_or_face>=r::SurfaceSlices(storage))
        return reject("Metal color resolve destination subresource unavailable");
      GuestSurfaceView guest_view;
      if(!DecodeGuestSurfaceView(resolve.source,false,guest_view))return reject("Metal resolve guest sample view invalid");
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
      auto select=SanitizeGuestCopySampleSelect(DecodeResolveSampleSelect(resolve.flags),guest_view.msaa_samples,false);
      const auto host_samples=ConvertHostSamplesToGuestSamples(VkSampleCountFlagBits(source_surface->samples));
      const auto mapping=NormalizeColorResolveSampleMapping(guest_view.msaa_samples,guest_view.msaa_samples,host_samples,select);
      constants.source_guest_sample_type=uint32_t(mapping.content_samples);
      constants.requested_guest_sample_type=uint32_t(mapping.requested_samples);
      constants.physical_source_sample_type=uint32_t(host_samples);constants.sample_select=uint32_t(mapping.sample_select);
      const int32_t exponent=NativeResolveExponent(resolve.flags);
      constants.flags=(uint32_t(exponent)&63u)<<8;
      if(storage.format==r::Format::RGBA16Float)constants.flags|=kResolveConversionFlagXenosFloat16Pack;
      if(RequiresNativeResolveScaleConversion(constants.source_width,constants.source_height,
                                             constants.destination_width,constants.destination_height))
        constants.flags|=kResolveConversionFlagScaleConversion;
      const r::SurfaceView destination_view{storage.key,resolve.destination_level,resolve.destination_slice_or_face,r::Aspect::Color};
      const bool exact=source_surface->samples==1&&source_surface->format==storage.format&&
          !(constants.flags&~kResolveConversionFlagXenosFloat16Pack)&&guest_view.msaa_samples==xenos::MsaaSamples::k1X;
      if(exact) {
        r::ImageCopy copy;copy.source=source_view;copy.destination=destination_view;
        copy.source_origin={uint32_t(constants.source_x),uint32_t(constants.source_y)};
        copy.destination_origin={uint32_t(constants.destination_x),uint32_t(constants.destination_y)};
        copy.extent={constants.destination_width,constants.destination_height};
        plan->commands.push_back(copy);
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
      if(resolve.flags&0x300u) {
        ClearCommand clear;
        std::copy(std::begin(resolve.clear_color_bits),std::end(resolve.clear_color_bits),clear.color_bits);
        clear.depth_bits=resolve.clear_depth_bits;clear.stencil=resolve.clear_stencil;
        const auto rectangle_for=[&](const SurfaceDescriptor& descriptor) {
          clear.left=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.left:0,0,int32_t(descriptor.width));
          clear.top=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.top:0,0,int32_t(descriptor.height));
          clear.right=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.right:int32_t(descriptor.width),clear.left,int32_t(descriptor.width));
          clear.bottom=std::clamp(resolve.source_rectangle_valid?resolve.source_rectangle.bottom:int32_t(descriptor.height),clear.top,int32_t(descriptor.height));
        };
        if(resolve.flags&(1u<<8)) {
          const uint32_t slot=resolve.flags&7u;
          const auto& descriptor=command.pipeline_state&&slot<kRenderTargetCount?
              command.pipeline_state->render_targets[slot]:resolve.source;
          rectangle_for(descriptor);
          auto surface=surface_for(descriptor,false);if(!surface||!clear_surface(surface,r::Aspect::Color,clear,0))return false;
        }
        if(resolve.flags&(1u<<9)) {
          if(!command.pipeline_state||!command.pipeline_state->depth_stencil.handle)
            return reject("Metal resolve source depth clear unavailable");
          const auto& descriptor=command.pipeline_state->depth_stencil;rectangle_for(descriptor);
          auto surface=surface_for(descriptor,true);
          if(!surface||!clear_surface(surface,r::Aspect::Depth,clear,0)||!clear_surface(surface,r::Aspect::Stencil,clear,0))return false;
        }
      }
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
    const auto& state=*command.pipeline_state;
    for(uint32_t i=0;i<kRenderTargetCount;++i)if(clear.flags&(1u<<i)) {
      // Guest null render targets have no clear destination.
      if(!state.render_targets[i].handle)continue;
      auto surface=surface_for(state.render_targets[i],false);if(!surface)return false;
      if(!clear_surface(surface,r::Aspect::Color,clear,i))return false;
    }
    if((clear.flags&0x30u) && state.depth_stencil.handle) {
      auto surface=surface_for(state.depth_stencil,true);if(!surface)return false;
      if((clear.flags&0x10u) && !clear_surface(surface,r::Aspect::Depth,clear,0))return false;
      if((clear.flags&0x20u) && !clear_surface(surface,r::Aspect::Stencil,clear,0))return false;
    }
  }
  if(present.device) {
    // Publication is wired to this producer, but no half-rendered game frame
    // is presented while draw, resolve and post-processing integration remains.
    (void)source;return reject("Metal game presentation awaits complete draw/resolve/helper lowering");
  }
  if(!plan->commands.empty()) {
    r::SurfaceContents final;
    if(!r::ValidateFrame(*plan,native_metal_frame_->contents,final,error))return false;
    if(!frame_backend_->Submit(plan,false,error))return false;
    std::erase_if(next.contents,[&](const auto& view){return r::FindSurface(*plan,view.surface)!=nullptr;});
    next.contents.insert(final.begin(),final.end());
  }
  std::erase_if(next.contents,[&](const auto& view) {
    return std::none_of(next.surfaces.begin(),next.surfaces.end(),[&](const auto& entry) {
      return entry.second.owner->key==view.surface;
    }) && std::none_of(next.textures.begin(),next.textures.end(),[&](const auto& entry) {
      return entry.second.owner->key==view.surface;
    });
  });
  next.buffers=std::move(native_metal_frame_->buffers);
  next.images=std::move(native_metal_frame_->images);
  *native_metal_frame_=std::move(next);error.clear();return true;
}
