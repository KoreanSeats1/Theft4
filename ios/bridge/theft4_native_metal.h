#pragma once
// Direct Metal backend. The adapter supplies immutable, host-endian game
// resources and final pass/draw state. No Vulkan handles enter this interface.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace theft4::metal {
enum class Stage { Vertex, Fragment };
struct BufferView {
  id<MTLBuffer> buffer = nil;
  NSUInteger offset = 0, length = 0;
};
struct ShaderInterface {
  // Exporter supplies exact Metal binding indices, independently per stage.
  uint32_t textures = 0, samplers = 0;
  std::array<MTLTextureType, 31> texture_types{};
};
struct Shader {
  id<MTLFunction> function = nil;
  Stage stage = Stage::Vertex;
  ShaderInterface interface{};
};
struct Pipeline {
  id<MTLRenderPipelineState> state = nil;
  id<MTLDepthStencilState> depth_stencil = nil;
  ShaderInterface vertex{}, fragment{};
  std::array<MTLPixelFormat, 4> colors{};
  MTLPixelFormat depth = MTLPixelFormatInvalid, stencil = MTLPixelFormatInvalid;
  NSUInteger samples = 1;
  uint32_t vertex_streams = 0;
  std::array<NSUInteger, 16> strides{}, attribute_extents{};
};
struct TextureBinding { Stage stage; NSUInteger index; id<MTLTexture> texture; };
struct SamplerBinding { Stage stage; NSUInteger index; id<MTLSamplerState> sampler; };
struct Draw {
  std::shared_ptr<const Pipeline> pipeline;
  // ABI: VS=0 (4096 bytes), PS=1 (3584), shared=2 (1056).
  std::array<BufferView, 3> constants{};
  // Game streams occupy Metal slots 8..23. They never alias constant slots.
  std::array<BufferView, 16> vertices{};
  std::vector<TextureBinding> textures;
  std::vector<SamplerBinding> samplers;
  MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
  NSUInteger first_vertex = 0, vertex_count = 0, instance_count = 1;
  BufferView indices{};
  MTLIndexType index_type = MTLIndexTypeUInt16;
  NSUInteger index_count = 0;
  NSInteger base_vertex = 0;
  // Computed by the adapter while validating converted index data.
  NSUInteger maximum_vertex = 0;
  MTLViewport viewport{};
  MTLScissorRect scissor{};
  MTLCullMode cull = MTLCullModeNone;
  MTLWinding winding = MTLWindingCounterClockwise;
  NSUInteger stencil_reference = 0;
};
class Receipt {
 public:
  bool Wait(std::string& error) const;
  bool Completed() const;
  double GpuMilliseconds() const;
  explicit operator bool() const;
 private:
  friend class Frame;
  id<MTLCommandBuffer> buffer_ = nil;
};
class Frame {
 public:
  Frame(); ~Frame();
  Frame(Frame&&) noexcept; Frame& operator=(Frame&&) noexcept;
  Frame(const Frame&) = delete; Frame& operator=(const Frame&) = delete;
  bool BeginPass(MTLRenderPassDescriptor* pass, std::string& error);
  bool Encode(const Draw& draw, std::string& error);
  bool EndPass(std::string& error);
  bool Present(id<CAMetalDrawable> drawable, std::string& error);
  Receipt Submit(std::string& error);
  explicit operator bool() const;
 private:
  friend class Renderer;
  struct Impl;
  explicit Frame(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
class Renderer {
 public:
  explicit Renderer(NSUInteger maximum_frames = 1);
  ~Renderer();
  bool Ready() const;
  id<MTLDevice> Device() const;
  id<MTLBuffer> ImmutableBuffer(std::span<const uint8_t> bytes, std::string& error);
  id<MTLTexture> Texture(MTLTextureDescriptor* descriptor, std::string& error);
  Shader LoadShader(std::span<const uint8_t> library, Stage stage,
                    ShaderInterface interface, uint32_t specialization,
                    std::string& error);
  std::shared_ptr<const Pipeline> MakePipeline(
      const Shader& vertex, const Shader& fragment,
      MTLRenderPipelineDescriptor* fixed,
      MTLDepthStencilDescriptor* depth, std::string& error);
  Frame BeginFrame(std::string& error);
  // Readback is for validation/capture, never part of the normal draw path.
  std::vector<uint8_t> ReadRGBA8(id<MTLTexture> texture, std::string& error);
 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}
