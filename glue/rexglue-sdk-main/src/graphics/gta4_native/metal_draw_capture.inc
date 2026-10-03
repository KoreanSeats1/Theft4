// Included only by the optional diagnostic build, inside gta4_native.
// The Vulkan frontend translates its effective state into an API-free packet;
// no Vulkan handles are serialized or consumed by the Metal runtime.
struct Gta4NativeGraphicsSystem::MetalCaptureState {
  theft4::render::DrawCaptureRecorder recorder;
  std::optional<theft4::render::Capture> pending;
  bool pending_indexed = false;
  std::unordered_map<VkPipeline, theft4::render::Draw> templates;
  std::unordered_set<VkPipeline> unsupported;
};

Gta4NativeGraphicsSystem::MetalCaptureState& Gta4NativeGraphicsSystem::MetalCapture() {
  if (!metal_capture_) {
    metal_capture_ = std::make_unique<MetalCaptureState>();
    if (metal_capture_->recorder.Active())
      REXLOG_INFO("theft4-metal-capture: bounded private draw capture enabled: {}",
                  metal_capture_->recorder.Directory());
  }
  return *metal_capture_;
}

void Gta4NativeGraphicsSystem::CaptureMetalTexture(
    NativeTextureImage& image, std::span<const uint8_t> payload,
    std::span<const NativeTextureResource::MipLevel> mips) {
  using namespace theft4::render;
  auto& capture = MetalCapture();
  if (!capture.recorder.Active() || !image.source || image.source->gpu_produced ||
      image.aspect != VK_IMAGE_ASPECT_COLOR_BIT || payload.size() > 8 * 1024 * 1024) return;
  auto out = std::make_shared<Image>();
  out->format = source::PixelFormat(image.format);
  const auto block = source::TextureBlock(out->format);
  if (!block.bytes) { capture.recorder.Reject("sampled image format needs lowering"); return; }
  switch (image.view_type) {
    case VK_IMAGE_VIEW_TYPE_2D: out->kind = ImageKind::Texture2D; break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY: out->kind = ImageKind::Texture2DArray; break;
    case VK_IMAGE_VIEW_TYPE_3D: out->kind = ImageKind::Texture3D; break;
    case VK_IMAGE_VIEW_TYPE_CUBE: out->kind = ImageKind::TextureCube; break;
    default: capture.recorder.Reject("sampled image view needs lowering"); return;
  }
  out->width = image.width; out->height = image.height;
  out->levels = image.mip_levels;
  out->depth = out->kind == ImageKind::Texture3D ? image.source->info.depth + 1 : 1;
  out->layers = out->kind == ImageKind::Texture2DArray ? image.source->info.depth + 1 : 1;
  const std::array components{image.view_components.r, image.view_components.g,
                              image.view_components.b, image.view_components.a};
  for (size_t i = 0; i < 4; ++i)
    out->swizzle[i] = components[i] == VK_COMPONENT_SWIZZLE_IDENTITY ? Swizzle(uint32_t(Swizzle::Red) + i)
                           : Swizzle(uint32_t(components[i]) - 1);
  for (const auto& m : mips) {
    const uint64_t row = (uint64_t(m.buffer_row_length ? m.buffer_row_length : m.width) + block.width - 1) /
                         block.width * block.bytes;
    const uint64_t plane = row * ((uint64_t(m.buffer_image_height ? m.buffer_image_height : m.height) +
                                  block.height - 1) / block.height);
    const uint64_t slice = plane * m.depth;
    for (uint32_t layer = 0; layer < m.layer_count; ++layer) {
      const uint64_t offset = m.payload_offset + layer * slice;
      if (offset > payload.size() || slice > payload.size() - offset ||
          uint64_t(layer + 1) * slice > m.payload_size) {
        capture.recorder.Reject("sampled image upload bounds"); return;
      }
      out->mips.push_back({m.level, m.base_array_layer + layer, m.width, m.height, m.depth,
                           row, plane, offset, slice});
    }
  }
  out->source = capture.recorder.Copy(payload, image.source->generation,
      {uint64_t(image.format), image.width, image.height, image.mip_levels});
  if (out->source) image.metal_capture_image = std::move(out);
}

void Gta4NativeGraphicsSystem::BeginMetalCapture(
    const NativeCommand& command, VkPipeline pipeline, uint32_t width, uint32_t height,
    const uint8_t* shared_constants, const VkViewport& viewport, const VkRect2D& scissor,
    const std::array<float, 4>& blend, const NativeStencilMaskRefState& front,
    const NativeStencilMaskRefState& back, float depth_bias, float slope_bias) {
  namespace r = theft4::render;
  auto& state = MetalCapture(); state.pending.reset();
  if (!state.recorder.Active() || !native_pipeline_compiler_ || !shared_constants ||
      state.unsupported.contains(pipeline)) return;
  const bool indexed = command.type == CommandType::kDrawIndexedPrimitive;
  if (indexed) {
    DrawIndexedPrimitiveCommand draw{}; std::memcpy(&draw, command.bytes.data(), sizeof(draw));
    // Reserve indexed families for scene geometry rather than tiny UI draws.
    if (draw.index_count < 48) return;
  }
  auto found = state.templates.find(pipeline);
  if (found == state.templates.end()) {
    auto native = std::find_if(native_pipelines_.begin(), native_pipelines_.end(),
        [pipeline](const auto& item) { return item.second.pipeline == pipeline; });
    if (native == native_pipelines_.end()) return;
    NativePipelineRecipe::Snapshot recipe{};
    {
      std::lock_guard lock(native_pipeline_compiler_->records_mutex);
      const auto it = native_pipeline_compiler_->records.find(native->first);
      if (it == native_pipeline_compiler_->records.end()) {
        state.recorder.Reject("pipeline recipe unavailable"); return;
      }
      recipe = it->second;
    }
    r::Draw draw; std::string error;
    if (!r::source::Pipeline(recipe, draw, error) || draw.pipeline.vertex.variant > 1 ||
        draw.pipeline.fragment.variant > 1) {
      state.recorder.Reject(error.empty() ? "shader variant needs Metal lowering" : error);
      state.unsupported.insert(pipeline); return;
    }
    found = state.templates.emplace(pipeline, std::move(draw)).first;
  }
  if (diagnostic_command_index_ > UINT32_MAX) return;
  r::Capture candidate{diagnostic_submitted_frame_, uint32_t(diagnostic_command_index_), width, height, found->second};
  auto& d = candidate.draw;
  d.pipeline.front.read_mask = front.compare_mask; d.pipeline.front.write_mask = front.write_mask;
  d.pipeline.back.read_mask = back.compare_mask; d.pipeline.back.write_mask = back.write_mask;
  if (!state.recorder.Wants(d.pipeline, indexed)) return;
  for (uint32_t slot = 0; slot < r::kFetchCount; ++slot) {
    if (!(command.used_texture_mask & (1u << slot))) continue;
    const auto& texture = command.textures[slot];
    const NativeTextureImage* image = nullptr;
    if (texture) {
      const auto it = native_texture_images_.find(texture->generation);
      if (it == native_texture_images_.end() || !it->second->metal_capture_image) {
        state.recorder.Reject(texture->gpu_produced ? "draw needs preceding GPU-produced image" :
                                                     "static image absent from bounded capture");
        return;
      }
      image = it->second.get(); d.fetches[slot].image = image->metal_capture_image;
    }
    // Packet production consumes normalized CPU sampler state, without
    // allocating a VkSampler or reserving a Vulkan descriptor for the capture.
    const auto* provider = static_cast<ui::vulkan::VulkanProvider*>(provider_.get());
    const auto* device = provider ? provider->vulkan_device() : nullptr;
    if (!device) { state.recorder.Reject("sampler capabilities unavailable"); return; }
    const auto& properties = device->properties();
    const NativeSamplerCapabilities capabilities{properties.samplerMirrorClampToEdge,
                                                properties.samplerAnisotropy,
                                                properties.maxSamplerAnisotropy};
    const auto format = image ? image->format : ConvertTextureFormat(command.texture_fetches[slot].format);
    if (format == VK_FORMAT_UNDEFINED) { state.recorder.Reject("sampler image format unavailable"); return; }
    const auto support = GetNativeFormatProperties(format);
    NativeSamplerKey key; VkSamplerCreateInfo info{};
    auto sampler = std::make_shared<r::Sampler>(); std::string error;
    if (!PrepareNativeSampler(command.texture_fetches[slot], image, capabilities,
        (support.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0,
        key, info) || !r::source::DecodeSampler(info, *sampler, error)) {
      state.recorder.Reject(error.empty() ? "sampler preparation unavailable" : error); return;
    }
    d.fetches[slot].sampler = std::move(sampler);
  }
  const auto* vertex = AuthoritativeConstantState::MaterializeView(command.shader_state->vertex_constants);
  const auto* pixel = AuthoritativeConstantState::MaterializeView(command.shader_state->pixel_constants);
  if (!vertex || !pixel) { state.recorder.Reject("authoritative constants unavailable"); return; }
  std::array<uint8_t, kVertexConstantsSize> host_vertex{};
  std::array<uint8_t, kPixelConstantsSize> host_pixel{};
  CopyGuestWordsToHost(host_vertex.data(), vertex->data(), host_vertex.size());
  CopyGuestWordsToHost(host_pixel.data(), pixel->data(), host_pixel.size());
  const std::array<std::span<const uint8_t>, 3> banks{
      host_vertex, host_pixel, std::span<const uint8_t>(shared_constants, sizeof(NativeSharedConstants))};
  for (size_t i = 0; i < banks.size(); ++i) {
    auto source = state.recorder.Copy(banks[i]); if (!source) return;
    d.constants[i] = {source, 0, banks[i].size()};
  }
  if (std::any_of(d.pipeline.attributes.begin(), d.pipeline.attributes.end(),
                 [](const auto& a) { return a.stream == kDefaultVertexBinding; })) {
    auto bytes = state.recorder.Copy({upload_buffer_.mapping, kDefaultVertexDataSize});
    if (!bytes) return;
    d.vertices[kDefaultVertexBinding] = {bytes, 0, bytes->value.size()};
  }
  d.viewport = {viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth};
  d.scissor = {uint32_t(scissor.offset.x), uint32_t(scissor.offset.y), scissor.extent.width, scissor.extent.height};
  d.blend_color = blend;
  d.stencil_front_reference = front.reference; d.stencil_back_reference = back.reference;
  if (command.fixed_function_state.depth_bias_enable) { d.depth_bias = depth_bias; d.slope_bias = slope_bias; }
  state.pending = std::move(candidate);
  state.pending_indexed = indexed;
}

void Gta4NativeGraphicsSystem::CaptureMetalVertices(
    uint32_t stream, const NativeUploadAllocation& allocation, size_t size, size_t offset) {
  auto& state = MetalCapture();
  if (!state.pending || stream >= theft4::render::kStreamCount || offset >= size) return;
  const auto* data = allocation.host_data ? allocation.host_data : allocation.mapping;
  if (!data) { state.pending.reset(); state.recorder.Reject("converted vertex bytes unavailable"); return; }
  auto source = state.recorder.Copy({data, size});
  if (!source) { state.pending.reset(); return; }
  state.pending->draw.vertices[stream] = {source, offset, size - offset};
}

void Gta4NativeGraphicsSystem::FinishMetalCapture(
    uint32_t first_vertex, uint32_t vertex_count, const uint8_t* indices,
    uint32_t index_count, uint32_t index_bytes, int32_t base_vertex) {
  auto& state = MetalCapture();
  if (!state.pending) return;
  auto candidate = std::move(*state.pending); state.pending.reset();
  auto& draw = candidate.draw;
  draw.first_vertex = first_vertex; draw.vertex_count = vertex_count;
  draw.index_count = index_count; draw.index_bytes = index_bytes; draw.base_vertex = base_vertex;
  if (index_count) {
    if (!indices) { state.recorder.Reject("converted index bytes unavailable"); return; }
    auto source = state.recorder.Copy({indices, size_t(index_count) * index_bytes});
    if (!source) return;
    draw.indices = {source, 0, source->value.size()};
  }
  if (state.recorder.Submit(std::move(candidate), state.pending_indexed))
    REXLOG_INFO("theft4-metal-capture: private draw recorded frame={} command={} vertices={} indices={}",
                diagnostic_submitted_frame_, diagnostic_command_index_, vertex_count, index_count);
}
