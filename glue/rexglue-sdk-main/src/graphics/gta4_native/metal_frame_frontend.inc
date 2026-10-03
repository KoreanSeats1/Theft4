// Live CPU frame producer, included in graphics_system.cpp. This is deliberately
// fail-closed while game draw/helper lowering is being connected: an unsupported
// command rejects the WHOLE batch before Metal submits any of it.
struct Gta4NativeGraphicsSystem::NativeMetalFrameState {
  struct SurfaceRecord {SurfaceDescriptor descriptor;std::shared_ptr<const theft4::render::Surface> owner;};
  struct TextureRecord {std::shared_ptr<const NativeTextureResource> resource;
                        std::shared_ptr<const theft4::render::Surface> owner;};
  std::map<std::pair<uint32_t,bool>,SurfaceRecord> surfaces;
  std::map<uint64_t,TextureRecord> textures;
  theft4::render::SurfaceContents contents;
};

void Gta4NativeGraphicsSystem::ResetNativeMetalFrontend() {
  // Allocation generations must not restart across a title device reset.
  if(native_metal_frame_) {
    native_metal_frame_->surfaces.clear();native_metal_frame_->textures.clear();
    native_metal_frame_->contents.clear();
  }
}
void Gta4NativeGraphicsSystem::ReleaseNativeMetalResource(uint32_t handle) {
  if(!native_metal_frame_)return;
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
  auto next=*native_metal_frame_;
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
    plan->commands.push_back(std::move(command));
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
    attachment.load=full?r::Load::Clear:next.contents.contains(view)?r::Load::Load:r::Load::Discard;
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
    }
    if(full)next.contents.insert(view);
    return validate_append(std::move(pass));
  };
  for(const NativeCommand& command:current_frame_) {
    if(command.type==CommandType::kReleaseResource) {
      ReleaseResourceCommand release{};std::memcpy(&release,command.bytes.data(),sizeof(release));
      std::erase_if(next.surfaces,[&](const auto& entry){return entry.first.first==release.resource;});
      std::erase_if(next.textures,[&](const auto& entry){return entry.second.resource->handle==release.resource;});
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
  *native_metal_frame_=std::move(next);error.clear();return true;
}
