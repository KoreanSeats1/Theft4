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
        native_metal_frame_->images.Touch(found);
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
        if(!PrepareNativeTextureDescription(NativeResourceView<NativeTextureResource>(resource),format,nullptr,tcaps,description,error,&frame_state.reflections,&frame_state.virtuals))return false;
        auto image=std::make_shared<r::Image>();const auto& m=*description.image;
        r::source::SampledImageDescription sampled{m.format,m.view_type,m.view_components,m.width,m.height,
            m.view_type==VK_IMAGE_VIEW_TYPE_3D?resource->info.depth+1:1,
            m.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY?resource->info.depth+1:1,m.mip_levels};
        if(!r::source::DecodeImageUpload(sampled,mips,payload.size(),*image,error))return false;
        image->source=make_bytes(payload,{uint64_t(format),m.width,m.height,m.mip_levels});
        if(!image->source)return reject("Metal static image packet allocation failed");
        metadata=std::shared_ptr<NativeTextureImage>(std::move(description.image));metadata->source.reset();
        native_metal_frame_->images.Store(resource->generation,{NativeResourceView<NativeTextureResource>(resource).Share(),image,metadata,next_native_metal_sequence_});
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
    NativeSamplerKey sampler_key;VkSamplerCreateInfo sampler_info;r::Sampler sampler;
    if(!PrepareNativeSampler(fetch,metadata.get(),
        {caps.mirror_clamp_to_edge,true,float(caps.maximum_anisotropy)},linear,sampler_key,sampler_info)||
        !r::source::DecodeSampler(sampler_info,sampler,error))return reject("Metal effective sampler preparation failed");
    if(auto i=native_metal_frame_->samplers.find(sampler);i!=native_metal_frame_->samplers.end())packet.fetch.sampler=i->second;
    else {
      if(native_metal_frame_->samplers.size()>=2048)native_metal_frame_->samplers.clear();
      packet.fetch.sampler=std::make_shared<const r::Sampler>(sampler);
      native_metal_frame_->samplers.emplace(sampler,packet.fetch.sampler);
    }

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

  // Fine timings sample one frame in 60, with no clock calls on other draws.
  const bool sample=native_metal_frame_->sample_draw_timing;
  uint64_t phase_tick=sample?rex::chrono::Clock::QueryHostTickCount():0;
  const auto phase=[&](uint64_t& accumulated) {
    if(sample){const auto now=rex::chrono::Clock::QueryHostTickCount();accumulated+=now-phase_tick;phase_tick=now;}
  };
  auto& timing=native_metal_frame_->draw_timing;

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
    chosen.used_texture_mask=shader->used_texture_mask;
    chosen.filename=shader->filename.c_str();return chosen;
  };
  auto capture=native_metal_frame_->draw_arena->Acquire();capture->allocation_generation=++next_native_metal_allocation_;capture->frame=diagnostic_submitted_frame_;
  capture->command=uint32_t(std::min(diagnostic_command_index_,size_t(UINT32_MAX)));
  capture->width=target.width;capture->height=target.height;
  // Preserve every field consumed by CPU snapshot construction, including
  // disabled state that the Vulkan PSO key intentionally canonicalizes away.
  // Dynamic masks, references, viewport and bias are applied below each draw.
  auto cpu_key=key;
  cpu_key.depth_function=fixed.depth_function;cpu_key.depth_write_enable=fixed.depth_write_enable;
  cpu_key.alpha_function=fixed.alpha_function;cpu_key.alpha_to_mask_enable=fixed.alpha_to_mask;
  cpu_key.two_sided_stencil=fixed.two_sided_stencil;
  cpu_key.stencil_fail=fixed.stencil_fail;cpu_key.stencil_depth_fail=fixed.stencil_depth_fail;
  cpu_key.stencil_pass=fixed.stencil_pass;cpu_key.stencil_function=fixed.stencil_function;
  cpu_key.ccw_stencil_fail=fixed.ccw_stencil_fail;cpu_key.ccw_stencil_depth_fail=fixed.ccw_stencil_depth_fail;
  cpu_key.ccw_stencil_pass=fixed.ccw_stencil_pass;cpu_key.ccw_stencil_function=fixed.ccw_stencil_function;
  auto& draw=capture->draw;
  const NativeMetalFrameState::PipelineTemplate* pipeline_template=nullptr;
  if(auto found=native_metal_frame_->pipelines.find(cpu_key);found!=native_metal_frame_->pipelines.end()) {
    const auto& p=found->second;draw.pipeline=p.pipeline;draw.primitive=p.primitive;
    draw.cull=p.cull;draw.clockwise=p.clockwise;draw.lines=p.lines;draw.depth_clamp=p.depth_clamp;
    draw.primitive_restart=p.primitive_restart;if(!theft4_retail_mode())++native_metal_frame_->pipeline_hits;
    pipeline_template=&p;
  } else {
    auto description=PrepareNativePipelineDescription(state,fixed,key,capabilities,
        select_shader(state.vertex_shader_resource),select_shader(state.pixel_shader_resource),error);
    if(!description)return false;
    if(!description->snapshot)return reject("Metal effective pipeline snapshot unavailable");
    if(!r::source::Pipeline(*description->snapshot,draw,error))return false;
    if(native_metal_frame_->pipelines.size()>=4096) {
      native_metal_frame_->pipelines.clear();
    }
    std::array<bool,kVertexStreamCount> required{};
    std::array<std::shared_ptr<const NativeMetalVertexConversion>,kVertexStreamCount> vertex_conversions{};
    if(!GetRequiredVertexStreams(state,required))return reject("Metal required vertex streams unavailable");
    for(uint32_t stream=0;stream<kVertexStreamCount;++stream)if(required[stream]) {
      auto plan=BuildNativeMetalVertexConversionPlan(*state.vertex_declaration_resource,
          *state.vertex_shader_resource,stream,up_stride?up_stride:state.vertex_streams[stream].stride,
          GetVertexElement16BitComponentCount,ConvertVertexUsageToLocation);
      if(!plan)continue; // Unusual declarations retain the exact original cache path.
      auto& recipes=native_metal_frame_->vertex_conversions;
      auto found=recipes.find(*plan);
      if(found!=recipes.end())vertex_conversions[stream]=found->second;
      else if(recipes.size()<4096) {
        auto recipe=std::make_shared<NativeMetalVertexConversion>();
        recipe->plan=*plan;recipe->identity=++next_native_metal_allocation_;
        vertex_conversions[stream]=recipe;recipes.emplace(*plan,std::move(recipe));
      }
    }
    auto entry=native_metal_frame_->pipelines.emplace(std::move(cpu_key),NativeMetalFrameState::PipelineTemplate{
        draw.pipeline,draw.primitive,draw.cull,draw.clockwise,draw.lines,draw.depth_clamp,draw.primitive_restart,std::move(vertex_conversions)});
    pipeline_template=&entry.first->second;
    if(!theft4_retail_mode())++native_metal_frame_->pipeline_misses;
  }
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
  phase(timing.pipeline);
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
        it->second.used=next_native_metal_sequence_;native_metal_frame_->buffers.Touch(it);return it->second.owner;
      }
    }
    auto bytes=make_bytes(data,conversion);
    if(bytes)native_metal_frame_->buffers.Store(cache_key,{resource,bytes,next_native_metal_sequence_});return bytes;
  };
  const std::array<std::shared_ptr<const ConstantStateVersion>,2> versions{
      command.shader_state->vertex_constants,command.shader_state->pixel_constants};
  auto constant_usage=state.vertex_shader_resource->constant_usage;
  if(state.pixel_shader_resource)constant_usage.Merge(state.pixel_shader_resource->constant_usage);
  if(!NativeConstantProjectionEnabled()||REXCVAR_GET(gta4_validate_native_hot_caches))constant_usage.known=false;
  if(!native_metal_frame_->projected_constants)
    native_metal_frame_->projected_constants=std::make_unique<NativeMetalConstantProjection<r::Bytes>>(!theft4_retail_mode());
  auto& projected_constants=*native_metal_frame_->projected_constants;
  for(size_t bank=0;bank<2;++bank) {
    const auto& version=versions[bank];const size_t required=bank?kPixelConstantsSize:kVertexConstantsSize;
    const size_t extent=constant_usage.known?NativeMaskedConstantExtent(constant_usage.banks[bank]):required;
    draw.constant_bytes[bank]=uint32_t(extent);
    if(!version||version->byte_size!=required)return reject("Metal authoritative constant bank unavailable");
    std::shared_ptr<const r::Bytes> bytes;
    if(constant_usage.known&&std::all_of(constant_usage.banks[bank].begin(),constant_usage.banks[bank].end(),
        [](uint64_t mask){return !mask;})) {
      bytes=native_metal_frame_->unused_constants[bank];
      if(!bytes) {
        const std::vector<uint8_t> zero(required);
        bytes=make_bytes(zero,{6,bank,required});
        native_metal_frame_->unused_constants[bank]=bytes;
      }
      if(!theft4_retail_mode())++native_metal_frame_->unused_constant_banks;if(!theft4_retail_mode())native_metal_frame_->constant_bytes_avoided+=required;
    } else {
      bytes=projected_constants.Find(key.vertex_shader_hash,key.pixel_shader_hash,bank,version,constant_usage);
      if(bytes)if(!theft4_retail_mode())native_metal_frame_->constant_bytes_avoided+=required;
    }
    if(bytes){draw.constants[bank]={bytes,0,bytes->value.size()};continue;}
    const auto cache_key=std::array<uint64_t,8>{3,uint64_t(reinterpret_cast<uintptr_t>(version.get())),bank};
    bytes=native_metal_frame_->constant_packets.Find(cache_key);
    if(!bytes&&constant_usage.known&&NativeMaskedConstantsEnabled()) {
      const auto& mask=constant_usage.banks[bank];
      const std::array<uint64_t,8> covered_key{7,uint64_t(reinterpret_cast<uintptr_t>(version.get())),bank,
          mask[0],mask[1],mask[2],mask[3],required};
      bytes=native_metal_frame_->constant_packets.Find(covered_key);
      if(!bytes) {
        auto covered=native_metal_frame_->AcquireConstant();covered->value.resize(extent);
        const uint64_t written=projected_constants.WriteMasked(key.vertex_shader_hash,key.pixel_shader_hash,
            bank,version,constant_usage,covered->value,
            [](const r::Bytes& payload){return std::span<const uint8_t>(payload.value);},CopyGuestWordsToHost);
        if(written) {
          covered->generation=++next_native_metal_allocation_;
          covered->conversion={7,bank,version->token.revision,XXH3_64bits(mask.data(),sizeof(mask))};
          bytes=std::move(covered);
          if(!native_metal_frame_->constant_packets.Store(covered_key,bytes))return reject("Metal covered constant cache allocation failed");
          if(!theft4_retail_mode())++native_metal_frame_->masked_constant_materializations;
          if(!theft4_retail_mode())native_metal_frame_->masked_constant_bytes_written+=written;
          if(!theft4_retail_mode())native_metal_frame_->masked_constant_full_bank_bytes+=required;
        } else if(!theft4_retail_mode())++native_metal_frame_->masked_constant_fallbacks;
      }
    }
    if(!bytes) {
      const auto* guest=AuthoritativeConstantState::MaterializeView(version);
      if(!guest||guest->size()!=required)return reject("Metal constant materialization failed");
      auto full=native_metal_frame_->AcquireConstant();full->generation=++next_native_metal_allocation_;
      full->conversion={3,bank,version->token.revision,required};full->value.resize(required);
      CopyGuestWordsToHost(full->value.data(),guest->data(),required);
      bytes=std::move(full);
      if(!native_metal_frame_->constant_packets.Store(cache_key,bytes))return reject("Metal full constant cache allocation failed");
    }
    if(!bytes)return reject("Metal constant packet allocation failed");draw.constants[bank]={bytes,0,bytes->value.size()};
    projected_constants.Remember(key.vertex_shader_hash,key.pixel_shader_hash,bank,version,constant_usage,bytes);
  }
  NativeMetalSharedConstantKey shared_semantics;
  shared_semantics.width=target.width;shared_semantics.height=target.height;
  shared_semantics.logical_width=target.logical_width;shared_semantics.logical_height=target.logical_height;
  shared_semantics.samples=uint32_t(target.samples);
  shared_semantics.booleans=PackNativeShaderBooleans(command.shader_state->vertex_booleans,command.shader_state->pixel_booleans);
  shared_semantics.alpha_reference_bits=std::bit_cast<uint32_t>(fixed.alpha_reference);
  shared_semantics.alpha_to_mask=fixed.alpha_to_mask;
  shared_semantics.clip_plane_bits=fixed.clip_plane_bits;
  shared_semantics.clip_enabled=fixed.user_clip_plane_enable_mask&1u;
  shared_semantics.color_mask=target.color_attachment_mask;
  shared_semantics.lod_limit_bits=std::bit_cast<uint32_t>(caps.maximum_sampler_lod_bias);
  for(size_t i=0;i<kNativeColorOutputTargetCount;++i)
    shared_semantics.color_info[i]=state.render_targets[i].address;
  for(size_t i=0;i<kShaderTextureCount;++i)
    shared_semantics.lod_bias[i]=command.texture_fetches[i].lod_bias;
  auto shared_bytes=native_metal_frame_->shared_constants.Find(shared_semantics,command.environmental_data);
  if(!shared_bytes) {
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
    // Shared shader policy is content-addressed, not owned by a per-object
    // matrix bank. Compare the entire payload on hits to handle hash collisions.
    // Its values include camera/environment data that changes every frame. Keep
    // them with this batch rather than accumulating in the geometry LRU.
    const std::array<uint64_t,8> shared_key{4,shared_hash};
    shared_bytes=native_metal_frame_->constant_packets.Find(shared_key);
    if(!shared_bytes||shared_bytes->value.size()!=shared_data.size()||
        std::memcmp(shared_bytes->value.data(),shared_data.data(),shared_data.size())) {
      auto prepared=native_metal_frame_->AcquireConstant();prepared->generation=++next_native_metal_allocation_;
      prepared->conversion={4,shared_hash};prepared->value.assign(shared_data.begin(),shared_data.end());
      shared_bytes=std::move(prepared);
      if(!native_metal_frame_->constant_packets.Store(shared_key,shared_bytes))return reject("Metal shared constant cache allocation failed");
    }
    if(!shared_bytes)return reject("Metal shared constant allocation failed");
    native_metal_frame_->shared_constants.Remember(shared_semantics,command.environmental_data,shared_bytes);
  }
  draw.constants[2]={shared_bytes,0,sizeof(NativeSharedConstants)};
  phase(timing.constants);

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
      const auto& conversion=pipeline_template->vertex_conversions[stream];
      const auto cache_key=conversion?std::array<uint64_t,8>{6,resource->generation,conversion->identity,
          conversion->plan.Offset(binding.offset)}:std::array<uint64_t,8>{1,resource->generation,state.vertex_declaration_resource->content_hash,
          state.vertex_shader_resource->hash,stream,binding.offset,binding.stride};
      auto it=native_metal_frame_->buffers.find(cache_key);std::shared_ptr<const r::Bytes> bytes;
      if(it!=native_metal_frame_->buffers.end()&&!it->second.resource.expired()) {
        bytes=it->second.owner;it->second.used=next_native_metal_sequence_;native_metal_frame_->buffers.Touch(it);
      } else {
        const auto* converted=PrepareConvertedVertexPayload(resource.get(),state,stream,conversion.get());
        if(!converted)return reject("Metal vertex conversion failed");
        bytes=cached_bytes(cache_key,NativeResourceView<NativeBufferResource>(resource).Share(),converted->payload,
            conversion?std::array<uint64_t,4>{conversion->identity,conversion->plan.Offset(binding.offset)}:
              std::array<uint64_t,4>{converted->declaration_hash,converted->shader_hash,stream,(uint64_t(binding.offset)<<32)|binding.stride});
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
  if(primitive==uint32_t(xenos::PrimitiveType::kRectangleList)&&!up_stride) {
    if(count%3||count>16*1024*1024)return reject("Metal buffered rectangle count");
    std::vector<uint32_t> selected(count);
    if(indexed) {
      const auto& resource=command.index_buffer;
      const bool index32=(resource->flags&kIndex32Flag)!=0;const size_t element=index32?4:2;
      if((uint64_t(start_index)+count)*element>resource->payload.size())return reject("Metal rectangle index bounds");
      auto& host=index32?resource->host_index32_payload:resource->host_index16_payload;
      if(host.empty()){host.resize(resource->payload.size());CopyGuestIndicesToHost(host.data(),resource->payload.data(),host.size(),index32);}
      for(uint32_t i=0;i<count;++i) {
        if(index32)std::memcpy(&selected[i],host.data()+(size_t(start_index)+i)*4,4);
        else {uint16_t value;std::memcpy(&value,host.data()+(size_t(start_index)+i)*2,2);selected[i]=value;}
      }
    } else {
      if(uint64_t(first)+count>uint64_t(UINT32_MAX)+1)return reject("Metal rectangle first vertex overflow");
      for(uint32_t i=0;i<count;++i)selected[i]=first+i;
    }
    std::array<std::vector<r::FloatVertexField>,kVertexStreamCount> fields;
    std::vector<r::RectangleVertexStream> inputs;std::vector<uint32_t> slots;
    bool has_position=false;
    for(uint32_t slot=0;slot<kVertexStreamCount;++slot) {
      const auto& buffer=draw.vertices[slot];const auto& stream=draw.pipeline.streams[slot];
      if(!buffer.source||stream.per_instance)continue;
      uint32_t position=UINT32_MAX;
      for(const auto& element:state.vertex_declaration_resource->elements)if(element.stream==slot) {
        const uint32_t components=GetFloat32VertexElementComponentCount(element.type);
        if(components&&uint64_t(element.offset)+components*4<=stream.stride)
          fields[slot].push_back({element.offset,components});
        if(!has_position&&components>=2&&ConvertVertexUsageToLocation(element.usage,element.usage_index)==0&&
           uint64_t(element.offset)+8<=stream.stride) {position=element.offset;has_position=true;}
      }
      inputs.push_back({std::span<const uint8_t>(buffer.source->value).subspan(buffer.offset,buffer.length),
                        stream.stride,fields[slot],position});slots.push_back(slot);
    }
    std::vector<std::vector<uint8_t>> expanded;
    if(!r::ExpandGuestRectangleStreams(inputs,selected,indexed?base_vertex:0,expanded,error))return false;
    for(size_t i=0;i<slots.size();++i) {
      auto bytes=make_bytes(expanded[i],{primitive,slots[i],count,uint32_t(base_vertex)});
      if(!bytes)return reject("Metal rectangle vertex packet allocation failed");
      draw.vertices[slots[i]]={bytes,0,bytes->value.size()};
    }
    count=count/3*4;first=0;indexed=false;base_vertex=0;draw.base_vertex=0;
  }
  phase(timing.vertices);
  if(indexed) {
    const auto& resource=command.index_buffer;const bool index32=(resource->flags&kIndex32Flag)!=0;
    const size_t element=index32?4:2;
    if((uint64_t(start_index)+count)*element>resource->payload.size())return reject("Metal guest index bounds");
    auto& host=index32?resource->host_index32_payload:resource->host_index16_payload;
    if(host.empty()){host.resize(resource->payload.size());CopyGuestIndicesToHost(host.data(),resource->payload.data(),host.size(),index32);}
    auto selected=std::span<const uint8_t>(host).subspan(size_t(start_index)*element,size_t(count)*element);
    bool sentinel=false;
    std::shared_ptr<const r::Bytes> original;
    if(!expand) {
      original=cached_bytes({2,resource->generation,index32},NativeResourceView<NativeBufferResource>(resource).Share(),host,{2,index32});
      if(!original)return reject("Metal index packet allocation failed");
      r::IndexRange range;
      if(!native_metal_frame_->index_ranges.Analyze({original,size_t(start_index)*element,size_t(count)*element},count,uint32_t(element),range,error))return false;
      sentinel=range.has_restart;
    }
    if(expand||sentinel) {
      const auto cache_key=std::array<uint64_t,8>{2,resource->generation,primitive,start_index,count,restart,marker};
      auto found=native_metal_frame_->buffers.find(cache_key);std::shared_ptr<const r::Bytes> bytes;
      if(found!=native_metal_frame_->buffers.end()&&!found->second.resource.expired()) {
        bytes=found->second.owner;found->second.used=next_native_metal_sequence_;native_metal_frame_->buffers.Touch(found);
      } else {
        std::vector<uint32_t> input(count),expanded;
        for(uint32_t i=0;i<count;++i)if(index32)std::memcpy(&input[i],selected.data()+i*4,4);
          else {uint16_t v;std::memcpy(&v,selected.data()+i*2,2);input[i]=v;}
        r::Primitive topology;
        if(!r::ExpandGuestIndices(primitive,input,restart,marker,topology,expanded,error))return false;
        if(expanded.empty()){output={};error.clear();return true;}
        bytes=cached_bytes(cache_key,NativeResourceView<NativeBufferResource>(resource).Share(),
            {reinterpret_cast<const uint8_t*>(expanded.data()),expanded.size()*4},{primitive,start_index,count,marker});
      }
      if(!bytes)return reject("Metal expanded index packet allocation failed");
      draw.indices={bytes,0,bytes->value.size()};draw.index_bytes=4;draw.index_count=uint32_t(bytes->value.size()/4);
      draw.primitive=primitive==2||primitive==3?r::Primitive::Line:r::Primitive::Triangle;
    } else {
      draw.indices={original,size_t(start_index)*element,size_t(count)*element};draw.index_bytes=uint32_t(element);draw.index_count=count;
    }
    draw.vertex_count=0;draw.first_vertex=0;draw.primitive_restart=false;
  } else if(expand) {
    if(uint64_t(first)+count>uint64_t(UINT32_MAX)+1)return reject("Metal expanded first vertex overflow");
    const auto cache_key=std::array<uint64_t,8>{5,primitive,first,count};
    auto found=native_metal_frame_->buffers.find(cache_key);std::shared_ptr<const r::Bytes> bytes;
    if(found!=native_metal_frame_->buffers.end()) {
      bytes=found->second.owner;found->second.used=next_native_metal_sequence_;native_metal_frame_->buffers.Touch(found);
    } else {
      std::vector<uint32_t> input(count),expanded;for(uint32_t i=0;i<count;++i)input[i]=first+i;
      r::Primitive topology;
      if(primitive==8) {
        if(count%4)return reject("Metal rectangle expansion count");
        for(uint32_t i=0;i<count;i+=4)for(uint32_t corner:{0u,1u,2u,2u,1u,3u})expanded.push_back(i+corner);
      } else if(!r::ExpandGuestIndices(primitive,input,false,0,topology,expanded,error))return false;
      if(expanded.empty()){output={};error.clear();return true;}
      // Procedural topology is independent of mutable game resource versions.
      // It remains subject to the frontend's ordinary byte budget and eviction.
      static const auto topology_owner=std::make_shared<const uint8_t>(0);
      bytes=cached_bytes(cache_key,topology_owner,
          {reinterpret_cast<const uint8_t*>(expanded.data()),expanded.size()*4},{primitive,first,count});
    }
    if(!bytes)return reject("Metal immediate index allocation failed");
    draw.indices={bytes,0,bytes->value.size()};draw.index_bytes=4;draw.index_count=uint32_t(bytes->value.size()/4);
    draw.first_vertex=draw.vertex_count=0;draw.primitive=r::Primitive::Triangle;
  }

  phase(timing.indices);
  r::FrameDraw result;
  const uint32_t used=state.vertex_shader_resource->used_texture_mask|
      (state.pixel_shader_resource?state.pixel_shader_resource->used_texture_mask:0);
  for(uint32_t slot=0;slot<kTextureStageCount;++slot)if(used&(1u<<slot)) {
    r::HostFetch fetch;if(!PrepareNativeMetalFetch(NativeResourceView<NativeTextureResource>(command.textures[slot]),
        command.texture_fetches[slot],frame_state,fetch,error))return false;
    draw.fetches[slot].image=fetch.image;draw.fetches[slot].sampler=fetch.sampler;
    result.produced.Set(slot,fetch.produced);
  }
  phase(timing.fetches);
  r::DrawValidationIssue validation_issue;
  if(!r::Validate(*capture,error,&validation_issue,&native_metal_frame_->index_ranges,nullptr,
                  native_metal_frame_->resource_admission.get())) {
    if(validation_issue!=r::DrawValidationIssue::VertexRange)return false;
    // RecordPrimitive already discards title draws exceeding captured buffers.
    // Match that admission policy without letting one malformed particle draw
    // freeze every valid scene draw. Do not pad, truncate, submit or claim any
    // attachment content for the rejected draw. Backend validation stays strict.
    static std::map<std::pair<uint64_t,uint64_t>,uint64_t> rejected_ranges;
    const auto shader_pair=std::pair{draw.pipeline.vertex.hash,draw.pipeline.fragment.hash};
    if(!theft4_retail_mode()&&(rejected_ranges.size()<32||rejected_ranges.contains(shader_pair))) {
      auto& failures=rejected_ranges[shader_pair];++failures;
      if(failures==1||failures%1024==0) {
        REXLOG_WARN("gta4-metal-draw-rejected: count={} type={} primitive={} guest-first={} guest-count={} "
            "vs={:016X} ps={:016X} reason={} action=discard-draw-only",
            failures,uint32_t(command.type),primitive,first,count,
            draw.pipeline.vertex.hash,draw.pipeline.fragment.hash,error);
      }
    }
    output={};error.clear();return true;
  }
  phase(timing.validate);
  result.capture=std::move(capture);output=std::move(result);error.clear();return true;
}
