bool Gta4NativeGraphicsSystem::PrepareNativeMetalFetch(
    NativeResourceView<NativeTextureResource> resource_view,const xenos::xe_gpu_texture_fetch_t& fetch,
    const NativeMetalFrameState& frame_state,theft4::render::HostFetch& output,std::string& error) {
  namespace r=theft4::render;
  const auto reject=[&](const char* reason){error=reason;return false;};
  if(!native_metal_frame_||!frame_backend_)return reject("Metal fetch CPU state unavailable");
  const auto caps=frame_backend_->Capabilities();output={};
  struct FetchPacket {r::Fetch fetch;std::optional<r::SampledSurfaceView> produced;} packet;
  const auto make_bytes=[&](std::span<const uint8_t> data,std::array<uint64_t,4> conversion)->std::shared_ptr<const r::Bytes> {
    if(data.empty()||data.size()>64*1024*1024)return {};
    auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
    bytes->conversion=conversion;bytes->value.assign(data.begin(),data.end());return bytes;
  };
    const auto resource=resource_view.Share();std::shared_ptr<NativeTextureImage> metadata;
    if(resource) {
      if(resource->gpu_produced) {
        const auto found=frame_state.textures.find(resource->generation);
        if(found==frame_state.textures.end())return reject("Metal GPU-produced fetch allocation missing");
        metadata=found->second.description;const auto& surface=*found->second.owner;
        r::SampledSurfaceView view;view.surface=surface.key;view.kind=surface.kind;
        view.levels=surface.levels;view.slices=r::SurfaceSlices(surface);
        view.aspect=r::SupportsAspect(surface.format,r::Aspect::Depth)?r::Aspect::Depth:r::Aspect::Color;
        const auto& channels=metadata->view_components;
        const std::array components{channels.r,channels.g,channels.b,channels.a};
        for(size_t c=0;c<4;++c) {
          if(components[c]<VK_COMPONENT_SWIZZLE_IDENTITY||components[c]>VK_COMPONENT_SWIZZLE_A)
            return reject("Metal produced fetch channel mapping invalid");
          view.swizzle[c]=components[c]==VK_COMPONENT_SWIZZLE_IDENTITY?r::Swizzle(uint32_t(r::Swizzle::Red)+c):
              r::Swizzle(uint32_t(components[c])-1);
        }
        packet.produced=view;
      } else {
      auto found=native_metal_frame_->images.find(resource->generation);
      if(found!=native_metal_frame_->images.end()&&!found->second.resource.expired()) {
        packet.fetch.image=found->second.owner;metadata=found->second.description;found->second.used=next_native_metal_sequence_;
      } else {
        VkFormat format=ConvertTextureFormat(resource->info.format);
        if(format==VK_FORMAT_UNDEFINED||format==VK_FORMAT_D24_UNORM_S8_UINT||format==VK_FORMAT_D32_SFLOAT_S8_UINT)
          return reject("Metal static texture format unavailable");
        std::span<const uint8_t> payload=resource->payload;
        std::vector<r::source::TextureUploadMip> mips;
        for(const auto& m:resource->mip_levels)mips.push_back({m.level,m.width,m.height,m.depth,m.base_array_layer,
            m.layer_count,m.buffer_row_length,m.buffer_image_height,m.payload_offset,m.payload_size});
#ifdef THEFT4_BC_TEXTURE_COMPATIBILITY
        theft4::astc::BackendPayload prepared;
        const bool bc=format==VK_FORMAT_BC1_RGBA_UNORM_BLOCK||format==VK_FORMAT_BC2_UNORM_BLOCK||format==VK_FORMAT_BC3_UNORM_BLOCK;
        const char* setting=std::getenv("THEFT4_ASTC_ENABLED");const bool astc=setting&&std::strcmp(setting,"1")==0;
        if(bc&&(!caps.bc_textures||astc)) {
          std::vector<theft4::astc::Mip> input_mips;
          for(const auto& m:mips)input_mips.push_back({m.level,m.width,m.height,m.depth,m.base_array_layer,m.layer_count,
              m.buffer_row_length,m.buffer_image_height,size_t(m.payload_offset),size_t(m.payload_size)});
          const auto bc_format=format==VK_FORMAT_BC1_RGBA_UNORM_BLOCK?theft4::astc::BcFormat::kBc1:
              format==VK_FORMAT_BC2_UNORM_BLOCK?theft4::astc::BcFormat::kBc2:theft4::astc::BcFormat::kBc3;
          theft4::astc::Input input{bc_format,resource->content_hash,resource->info.width+1,resource->info.height+1,payload,input_mips};
          const char* root=std::getenv("THEFT4_ASTC_PREPARATION_ROOT");
          theft4::astc::BackendPolicy policy{caps.bc_textures,astc,caps.astc_textures,true,root?std::filesystem::path(root):std::filesystem::path()};
          if(!theft4::astc::PrepareForBackend(input,policy,prepared,&error))return false;
          if(prepared.storage!=theft4::astc::PayloadStorage::OriginalBc) {
            format=prepared.storage==theft4::astc::PayloadStorage::Astc4x4?VK_FORMAT_ASTC_4x4_UNORM_BLOCK:VK_FORMAT_R8G8B8A8_UNORM;
            payload=prepared.converted.payload;mips.clear();
            for(const auto& m:prepared.converted.mips)mips.push_back({m.level,m.width,m.height,m.depth,m.base_array_layer,
                m.layer_count,m.buffer_row_length,m.buffer_image_height,m.payload_offset,m.payload_size});
          }
        }
#endif
        if(!caps.bc_textures&&(format==VK_FORMAT_BC1_RGBA_UNORM_BLOCK||format==VK_FORMAT_BC2_UNORM_BLOCK||format==VK_FORMAT_BC3_UNORM_BLOCK||
            format==VK_FORMAT_BC4_UNORM_BLOCK||format==VK_FORMAT_BC4_SNORM_BLOCK||format==VK_FORMAT_BC5_UNORM_BLOCK||format==VK_FORMAT_BC5_SNORM_BLOCK))
          return reject("Metal sampled BC format unsupported by device");
        PreparedNativeTextureDescription description;
        NativeTextureCapabilities tcaps{caps.max_image_dimension_2d,true,true,
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
        if(!PrepareNativeTextureDescription(NativeResourceView<NativeTextureResource>(resource),format,nullptr,tcaps,description,error,&frame_state.reflections))return false;
        auto image=std::make_shared<r::Image>();const auto& m=*description.image;
        r::source::SampledImageDescription sampled{m.format,m.view_type,m.view_components,m.width,m.height,
            m.view_type==VK_IMAGE_VIEW_TYPE_3D?resource->info.depth+1:1,
            m.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY?resource->info.depth+1:1,m.mip_levels};
        if(!r::source::DecodeImageUpload(sampled,mips,payload.size(),*image,error))return false;
        image->source=make_bytes(payload,{uint64_t(format),m.width,m.height,m.mip_levels});
        if(!image->source)return reject("Metal static image packet allocation failed");
        metadata=std::shared_ptr<NativeTextureImage>(std::move(description.image));metadata->source.reset();
        native_metal_frame_->images[resource->generation]={NativeResourceView<NativeTextureResource>(resource).Share(),image,metadata,next_native_metal_sequence_};
        packet.fetch.image=std::move(image);
      }
      }
    }
    // Borrow the original resource only during CPU font/material sampler policy;
    // the image cache itself does not keep retired guest snapshots alive.
    if(metadata)metadata->source=NativeResourceView<NativeTextureResource>(resource).Share();
    const auto unborrow=MakeScopeExit([&]{if(metadata)metadata->source.reset();});
    const auto format=metadata?metadata->format:VK_FORMAT_R8G8B8A8_UNORM;
    const bool linear=caps.float32_filtering||(format!=VK_FORMAT_R32_SFLOAT&&format!=VK_FORMAT_R32G32_SFLOAT&&format!=VK_FORMAT_R32G32B32A32_SFLOAT);
    NativeSamplerKey sampler_key;VkSamplerCreateInfo sampler_info;auto sampler=std::make_shared<r::Sampler>();
    if(!PrepareNativeSampler(fetch,metadata.get(),
        {caps.mirror_clamp_to_edge,true,float(caps.maximum_anisotropy)},linear,sampler_key,sampler_info)||
        !r::source::DecodeSampler(sampler_info,*sampler,error))return reject("Metal effective sampler preparation failed");
    packet.fetch.sampler=std::move(sampler);

  output.produced=packet.produced;output.image=packet.fetch.image;output.sampler=packet.fetch.sampler;
  error.clear();return true;
}

// Actual title draw lowering. This runs on the native render worker and uses
// immutable CPU snapshots; it never creates or reads Vulkan driver objects.
bool Gta4NativeGraphicsSystem::PrepareNativeMetalDraw(
    const NativeCommand& command,const NativeRenderingTarget& target,
    const NativeMetalFrameState& frame_state,theft4::render::FrameDraw& output,std::string& error) {
  namespace r=theft4::render;
  const auto reject=[&](const char* reason){error=reason;return false;};
  if(!frame_backend_ || !native_metal_frame_ || !command.pipeline_state || !command.shader_state)
    return reject("Metal draw CPU state unavailable");
  const auto& state=*command.pipeline_state;const auto& fixed=command.fixed_function_state;
  const auto caps=frame_backend_->Capabilities();
  uint32_t primitive=0,first=0,count=0,up_stride=0,start_index=0,marker=0;
  int32_t base_vertex=0;bool indexed=false,restart=false;
  if(command.type==CommandType::kDrawPrimitive) {
    if(command.bytes.size()!=sizeof(DrawPrimitiveCommand))return reject("Invalid Metal primitive command");
    DrawPrimitiveCommand draw;std::memcpy(&draw,command.bytes.data(),sizeof(draw));
    primitive=draw.primitive_type;first=draw.start_vertex;count=draw.vertex_count;
  } else if(command.type==CommandType::kDrawPrimitiveUp) {
    if(command.bytes.size()!=sizeof(DrawPrimitiveUpCommand))return reject("Invalid Metal immediate command");
    DrawPrimitiveUpCommand draw;std::memcpy(&draw,command.bytes.data(),sizeof(draw));
    primitive=draw.primitive_type;up_stride=draw.stride;count=draw.vertex_count;
    if(!up_stride||uint64_t(count)*up_stride>command.payload.size())return reject("Metal immediate vertex bounds");
  } else if(command.type==CommandType::kDrawIndexedPrimitive) {
    if(command.bytes.size()!=sizeof(DrawIndexedPrimitiveCommand)||!command.index_buffer)
      return reject("Invalid Metal indexed command");
    DrawIndexedPrimitiveCommand draw;std::memcpy(&draw,command.bytes.data(),sizeof(draw));
    primitive=draw.primitive_type;start_index=draw.start_index;count=draw.index_count;
    base_vertex=draw.base_vertex;indexed=true;
    const bool index32=(command.index_buffer->flags&kIndex32Flag)!=0;
    const auto topology=ConvertPrimitiveTopology(primitive);
    restart=draw.primitive_restart_enabled&&(topology==VK_PRIMITIVE_TOPOLOGY_LINE_STRIP||
        topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP||topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)&&
        (index32||draw.primitive_restart_index<=UINT16_MAX);
    marker=index32?draw.primitive_restart_index&xenos::kVertexIndexMask:draw.primitive_restart_index;
  } else return reject("Unsupported Metal draw command");
  if(!count){output={};error.clear();return true;}
  if(!state.vertex_shader_resource || !state.vertex_declaration_resource)
    return reject("Metal vertex shader or declaration unavailable");
  if(primitive==uint32_t(xenos::PrimitiveType::kRectangleList)&&!up_stride)
    return reject("Indexed/buffered guest rectangle expansion unavailable");

  // Invisible draws need no upload, allocation or texture preparation.
  struct ViewportLimits {uint32_t maxViewportDimensions[2];float viewportBoundsRange[2];};
  const ViewportLimits limits{{caps.max_image_dimension_2d,caps.max_image_dimension_2d},
                             {-32768.0f,32767.0f}};
  std::array<float,6> requested;
  for(size_t i=0;i<6;++i)requested[i]=std::bit_cast<float>(fixed.viewport_bits[i]);
  const float sx=float(target.width)/target.logical_width,sy=float(target.height)/target.logical_height;
  const auto viewport=SelectNativeViewport(requested,sx,sy,target.width,target.height,limits);
  const int32_t logical_left=std::clamp(fixed.scissor[0],0,int32_t(target.logical_width));
  const int32_t logical_top=std::clamp(fixed.scissor[1],0,int32_t(target.logical_height));
  const int32_t logical_right=std::clamp(fixed.scissor[2],logical_left,int32_t(target.logical_width));
  const int32_t logical_bottom=std::clamp(fixed.scissor[3],logical_top,int32_t(target.logical_height));
  const uint32_t left=ScaleCoordinateFloor(logical_left,target.logical_width,target.width);
  const uint32_t top=ScaleCoordinateFloor(logical_top,target.logical_height,target.height);
  const uint32_t right=ScaleCoordinateCeil(logical_right,target.logical_width,target.width);
  const uint32_t bottom=ScaleCoordinateCeil(logical_bottom,target.logical_height,target.height);
  if(viewport.empty||right==left||bottom==top){output={};error.clear();return true;}

  const auto selection=ResolvePipelineShaderOverrides(state.vertex_shader_resource,state.pixel_shader_resource,target.samples);
  if(selection.vertex_override||selection.pixel_override)return reject("Metal override shader library lowering unavailable");
  NativePipelineCapabilities capabilities;
  capabilities.maxVertexInputAttributes=31;capabilities.maxVertexInputBindings=kVertexStreamCount;
  capabilities.maxVertexInputBindingStride=2048;capabilities.maxVertexInputAttributeOffset=2047;
  capabilities.constant_alpha_color_blend=capabilities.shader_clip_distance=capabilities.depth_clamp=true;
  capabilities.negative_depth_clip=capabilities.separate_stencil_mask_ref=true;
  capabilities.fill_mode_non_solid=capabilities.independent_blend=true;
  capabilities.attachmentless_samples=1;
  const bool expand=primitive==5||primitive==13||primitive==8||restart;
  const uint32_t pipeline_primitive=primitive==3&&restart?2:expand?4:primitive;
  NativePipelineKey key;
  if(!PrepareNativePipelineKey(state,fixed,pipeline_primitive,target,up_stride,false,capabilities,selection,key,error))return false;
  const auto select_shader=[](const NativeShader* shader) {
    NativeShaderSelection chosen;chosen.shader=shader;if(!shader)return chosen;
    chosen.has_early=shader->module_code_hashes[0]!=0;chosen.has_late=shader->module_code_hashes[1]!=0;
    chosen.specialization_constants_mask=shader->specialization_constants_mask;
    chosen.used_texture_mask=shader->used_texture_mask;chosen.specialization_constants_mask=shader->specialization_constants_mask;
    chosen.filename=shader->filename.c_str();return chosen;
  };
  auto description=PrepareNativePipelineDescription(state,fixed,key,capabilities,
      select_shader(state.vertex_shader_resource),select_shader(state.pixel_shader_resource),error);
  if(!description)return false;
  if(!description->snapshot)return reject("Metal effective pipeline snapshot unavailable");
  auto capture=std::make_shared<r::Capture>();capture->frame=diagnostic_submitted_frame_;
  capture->command=uint32_t(std::min(diagnostic_command_index_,size_t(UINT32_MAX)));
  capture->width=target.width;capture->height=target.height;
  auto& draw=capture->draw;if(!r::source::Pipeline(*description->snapshot,draw,error))return false;
  draw.first_vertex=first;draw.vertex_count=indexed?0:count;draw.base_vertex=base_vertex;
  const auto& v=viewport.viewport;
  draw.viewport={v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth};draw.scissor={left,top,right-left,bottom-top};
  const auto blend=SelectNativeBlendConstants(fixed.blend_controls,
      NativeBlendWriteMask(target.color_write_mask,target.color_formats),fixed.blend_constants,true);
  if(!blend.representable)return reject("Metal blend constants unrepresentable");
  draw.blend_color=blend.constants;draw.stencil_front_reference=fixed.stencil_reference;
  draw.stencil_back_reference=fixed.two_sided_stencil?fixed.back_stencil_reference:fixed.stencil_reference;
  draw.pipeline.front.read_mask=fixed.stencil_mask;draw.pipeline.front.write_mask=fixed.stencil_write_mask;
  draw.pipeline.back.read_mask=fixed.two_sided_stencil?fixed.back_stencil_mask:fixed.stencil_mask;
  draw.pipeline.back.write_mask=fixed.two_sided_stencil?fixed.back_stencil_write_mask:fixed.stencil_write_mask;
  if(fixed.depth_bias_enable) {
    draw.depth_bias=std::bit_cast<float>(fixed.depth_bias_bits)*draw_util::kD3D10PolygonOffsetFactorFloat24;
    draw.slope_bias=std::bit_cast<float>(fixed.slope_scaled_depth_bias_bits)*
        xenos::kPolygonOffsetScaleSubpixelUnit*NativeResolutionDepthBiasScale(sx,sy);
  }
  const auto make_bytes=[&](std::span<const uint8_t> data,std::array<uint64_t,4> conversion)->std::shared_ptr<const r::Bytes> {
    if(data.empty()||data.size()>64*1024*1024)return {};
    auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
    bytes->conversion=conversion;bytes->value.assign(data.begin(),data.end());return bytes;
  };
  const auto cached_bytes=[&](std::array<uint64_t,8> cache_key,std::shared_ptr<const void> resource,
      std::span<const uint8_t> data,std::array<uint64_t,4> conversion,bool compare=false)->std::shared_ptr<const r::Bytes> {
    auto it=native_metal_frame_->buffers.find(cache_key);
    if(it!=native_metal_frame_->buffers.end()) {
      auto owner=it->second.resource.lock();
      if(owner&&!owner.owner_before(resource)&&!resource.owner_before(owner)&&
         (!compare||(it->second.owner->value.size()==data.size()&&
             !std::memcmp(it->second.owner->value.data(),data.data(),data.size())))) {
        it->second.used=next_native_metal_sequence_;return it->second.owner;
      }
    }
    auto bytes=make_bytes(data,conversion);
    if(bytes)native_metal_frame_->buffers[cache_key]={resource,bytes,next_native_metal_sequence_};return bytes;
  };
  const std::array<std::shared_ptr<const ConstantStateVersion>,2> versions{
      command.shader_state->vertex_constants,command.shader_state->pixel_constants};
  for(size_t bank=0;bank<2;++bank) {
    const auto& version=versions[bank];const size_t required=bank?kPixelConstantsSize:kVertexConstantsSize;
    if(!version||version->byte_size!=required)return reject("Metal authoritative constant bank unavailable");
    const auto cache_key=std::array<uint64_t,8>{3,uint64_t(reinterpret_cast<uintptr_t>(version.get())),bank};
    auto it=native_metal_frame_->buffers.find(cache_key);std::shared_ptr<const r::Bytes> bytes;
    if(it!=native_metal_frame_->buffers.end()&&!it->second.resource.expired()) {
      bytes=it->second.owner;it->second.used=next_native_metal_sequence_;
    } else {
      const auto* guest=AuthoritativeConstantState::MaterializeView(version);
      if(!guest||guest->size()!=required)return reject("Metal constant materialization failed");
      std::vector<uint8_t> host(required);CopyGuestWordsToHost(host.data(),guest->data(),required);
      bytes=cached_bytes(cache_key,version,host,{3,bank,version->token.revision,required});
    }
    if(!bytes)return reject("Metal constant packet allocation failed");draw.constants[bank]={bytes,0,required};
  }
  NativeSharedConstants shared;
  // Descriptor arrays remain zero: offline Metal lowering binds each reflected
  // fetch directly, without guest descriptor indices or GPU addresses.
  shared.booleans=PackNativeShaderBooleans(command.shader_state->vertex_booleans,command.shader_state->pixel_booleans);
  shared.alpha_threshold=fixed.alpha_reference;shared.alpha_to_mask=fixed.alpha_to_mask;
  shared.alpha_to_mask_sample_count=uint32_t(target.samples);
  shared.half_pixel_offset_x=1.0f/target.logical_width;shared.half_pixel_offset_y=-1.0f/target.logical_height;
  const auto scale=CalculateFragmentCoordinateScale(target.logical_width,target.logical_height,target.width,target.height);
  shared.fragment_coordinate_scale_x=scale.x;shared.fragment_coordinate_scale_y=scale.y;
  for(size_t i=0;i<4;++i)shared.clip_plane[i]=std::bit_cast<float>(fixed.clip_plane_bits[i]);
  shared.clip_plane_enabled=(fixed.user_clip_plane_enable_mask&1u)!=0;
  for(size_t i=0;i<kNativeColorOutputTargetCount;++i)
    shared.color_output[i]=NativeColorOutput(state.render_targets[i].address,(target.color_attachment_mask&(1u<<i))!=0);
  for(size_t i=0;i<kShaderTextureCount;++i)
    shared.sampler_lod_bias[i]=NativeSamplerLodBias(command.texture_fetches[i].lod_bias,caps.maximum_sampler_lod_bias);
  shared.motion_blur_time_scale=MotionBlurTimeScale(command.environmental_data.get());
  if(command.environmental_data) {
    const auto& e=*command.environmental_data;shared.environmental_valid_fields=e.valid_fields;
    shared.fog_parameters[0]=e.fog_density;shared.fog_parameters[1]=e.fog_height_falloff;
    shared.fog_parameters[2]=e.fog_altitude_tweak;shared.fog_parameters[3]=e.fog_power;
    std::copy(e.camera_position.begin(),e.camera_position.end(),shared.camera_position);
    std::copy(e.view_inverse_matrix.begin(),e.view_inverse_matrix.end(),shared.view_inverse_matrix);
    shared.projection_scale[0]=e.projection_matrix[0];shared.projection_scale[1]=e.projection_matrix[5];
  }
  const auto shared_data=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&shared),sizeof(shared));
  const uint64_t shared_hash=XXH3_64bits(shared_data.data(),shared_data.size());
  auto shared_bytes=cached_bytes({4,shared_hash},versions[0],shared_data,{4,shared_hash},true);
  if(!shared_bytes)return reject("Metal shared constant allocation failed");
  draw.constants[2]={shared_bytes,0,sizeof(shared)};

  if(up_stride) {
    auto bytes=std::make_shared<r::Bytes>();bytes->generation=++next_native_metal_allocation_;
    bytes->conversion={state.vertex_declaration_resource->content_hash,state.vertex_shader_resource->hash,0,up_stride};
    bytes->value.resize(size_t(count)*up_stride);
    ConvertGuestVertexPayload(bytes->value.data(),command.payload.data(),bytes->value.size(),
        *state.vertex_declaration_resource,*state.vertex_shader_resource,0,0,up_stride);
    if(primitive==8) {
      std::vector<r::FloatVertexField> fields;uint32_t position=UINT32_MAX;
      for(const auto& element:state.vertex_declaration_resource->elements)if(element.stream==0) {
        uint32_t components=GetFloat32VertexElementComponentCount(element.type);
        if(components&&uint64_t(element.offset)+components*4<=up_stride)fields.push_back({element.offset,components});
        if(position==UINT32_MAX&&ConvertVertexUsageToLocation(element.usage,element.usage_index)==0&&
           components>=2&&uint64_t(element.offset)+8<=up_stride)position=element.offset;
      }
      std::vector<uint8_t> expanded;
      if(!r::ExpandGuestRectangles(bytes->value,up_stride,fields,position,expanded,error))return false;
      bytes->value=std::move(expanded);count=uint32_t(bytes->value.size()/up_stride);
    }
    draw.vertices[0]={bytes,0,bytes->value.size()};
  } else {
    std::array<bool,kVertexStreamCount> streams{};
    if(!GetRequiredVertexStreams(state,streams))return reject("Metal required vertex streams unavailable");
    for(uint32_t stream=0;stream<kVertexStreamCount;++stream)if(streams[stream]) {
      const auto& resource=command.vertex_buffers[stream];const auto& binding=state.vertex_streams[stream];
      if(!resource)return reject("Metal converted vertex source missing");
      const auto cache_key=std::array<uint64_t,8>{1,resource->generation,state.vertex_declaration_resource->content_hash,
          state.vertex_shader_resource->hash,stream,binding.offset,binding.stride};
      auto it=native_metal_frame_->buffers.find(cache_key);std::shared_ptr<const r::Bytes> bytes;
      if(it!=native_metal_frame_->buffers.end()&&!it->second.resource.expired()) {
        bytes=it->second.owner;it->second.used=next_native_metal_sequence_;
      } else {
        const auto* converted=PrepareConvertedVertexPayload(resource.get(),state,stream);
        if(!converted)return reject("Metal vertex conversion failed");
        bytes=cached_bytes(cache_key,NativeResourceView<NativeBufferResource>(resource).Share(),converted->payload,
            {converted->declaration_hash,converted->shader_hash,stream,(uint64_t(binding.offset)<<32)|binding.stride});
      }
      if(!bytes||binding.offset>=bytes->value.size())return reject("Metal converted vertex packet bounds");
      draw.vertices[stream]={bytes,binding.offset,bytes->value.size()-binding.offset};
    }
  }
  if(std::any_of(draw.pipeline.attributes.begin(),draw.pipeline.attributes.end(),
      [](const auto& attribute){return attribute.stream==kDefaultVertexBinding;})) {
    static const auto defaults=[] {
      auto bytes=std::make_shared<r::Bytes>();bytes->generation=1;bytes->conversion={0x44454641554c54ull};
      bytes->value.resize(kNativeDefaultVertexRecordSize);return std::shared_ptr<const r::Bytes>(bytes);
    }();draw.vertices[kDefaultVertexBinding]={defaults,0,defaults->value.size()};
  }
  if(indexed) {
    const auto& resource=command.index_buffer;const bool index32=(resource->flags&kIndex32Flag)!=0;
    const size_t element=index32?4:2;
    if((uint64_t(start_index)+count)*element>resource->payload.size())return reject("Metal guest index bounds");
    auto& host=index32?resource->host_index32_payload:resource->host_index16_payload;
    if(host.empty()){host.resize(resource->payload.size());CopyGuestIndicesToHost(host.data(),resource->payload.data(),host.size(),index32);}
    auto selected=std::span<const uint8_t>(host).subspan(size_t(start_index)*element,size_t(count)*element);
    bool sentinel=false;
    if(!restart)for(uint32_t i=0;i<count;++i) {
      uint32_t value=0;if(index32)std::memcpy(&value,selected.data()+i*4,4);
      else {uint16_t v;std::memcpy(&v,selected.data()+i*2,2);value=v;}
      sentinel|=value==(index32?UINT32_MAX:UINT16_MAX);
    }
    if(expand||sentinel) {
      std::vector<uint32_t> input(count),expanded;
      for(uint32_t i=0;i<count;++i)if(index32)std::memcpy(&input[i],selected.data()+i*4,4);
        else {uint16_t v;std::memcpy(&v,selected.data()+i*2,2);input[i]=v;}
      r::Primitive topology;
      if(!r::ExpandGuestIndices(primitive,input,restart,marker,topology,expanded,error))return false;
      if(expanded.empty()){output={};error.clear();return true;}
      const auto data=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(expanded.data()),expanded.size()*4);
      auto bytes=cached_bytes({2,resource->generation,primitive,start_index,count,restart,marker},
          NativeResourceView<NativeBufferResource>(resource).Share(),data,{primitive,start_index,count,marker});
      if(!bytes)return reject("Metal expanded index packet allocation failed");
      draw.indices={bytes,0,data.size()};draw.index_bytes=4;draw.index_count=uint32_t(expanded.size());draw.primitive=topology;
    } else {
      auto bytes=cached_bytes({2,resource->generation,index32},NativeResourceView<NativeBufferResource>(resource).Share(),host,{2,index32});
      if(!bytes)return reject("Metal index packet allocation failed");
      draw.indices={bytes,size_t(start_index)*element,size_t(count)*element};draw.index_bytes=uint32_t(element);draw.index_count=count;
    }
    draw.vertex_count=0;draw.first_vertex=0;draw.primitive_restart=false;
  } else if(expand) {
    std::vector<uint32_t> input(count),expanded;for(uint32_t i=0;i<count;++i)input[i]=first+i;
    r::Primitive topology;
    if(primitive==8) {
      if(count%4)return reject("Metal rectangle expansion count");
      for(uint32_t i=0;i<count;i+=4)for(uint32_t corner:{0u,1u,2u,2u,1u,3u})expanded.push_back(i+corner);
      topology=r::Primitive::Triangle;
    } else if(!r::ExpandGuestIndices(primitive,input,false,0,topology,expanded,error))return false;
    if(expanded.empty()){output={};error.clear();return true;}
    auto bytes=make_bytes({reinterpret_cast<const uint8_t*>(expanded.data()),expanded.size()*4},{primitive,count});
    if(!bytes)return reject("Metal immediate index allocation failed");
    draw.indices={bytes,0,bytes->value.size()};draw.index_bytes=4;draw.index_count=uint32_t(expanded.size());
    draw.first_vertex=draw.vertex_count=0;draw.primitive=topology;
  }

  r::FrameDraw result;
  const uint32_t used=state.vertex_shader_resource->used_texture_mask|
      (state.pixel_shader_resource?state.pixel_shader_resource->used_texture_mask:0);
  for(uint32_t slot=0;slot<kTextureStageCount;++slot)if(used&(1u<<slot)) {
    r::HostFetch fetch;if(!PrepareNativeMetalFetch(NativeResourceView<NativeTextureResource>(command.textures[slot]),
        command.texture_fetches[slot],frame_state,fetch,error))return false;
    draw.fetches[slot].image=fetch.image;draw.fetches[slot].sampler=fetch.sampler;
    result.produced[slot]=fetch.produced;
  }
  if(!r::Validate(*capture,error))return false;
  result.capture=std::move(capture);output=std::move(result);error.clear();return true;
}
