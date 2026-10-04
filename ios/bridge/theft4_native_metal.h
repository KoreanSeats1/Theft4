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
#include "theft4_metal_shader_catalog.h"

namespace theft4::metal {
inline constexpr NSUInteger kGameVertexStreamCount = 17;
struct BufferView {
  id<MTLBuffer> buffer = nil;
  NSUInteger offset = 0, length = 0;
};
struct ShaderInterface {
  // Exporter supplies exact Metal binding indices, independently per stage.
  uint32_t textures = 0, samplers = 0;
  std::array<MTLTextureType, 31> texture_types{};
  // Guest shaders retain the three-bank ABI. Host utility shaders supply
  // their reflected push-constant size, or zero for unused banks.
  std::array<NSUInteger,3> constant_bytes{4096,3584,1056};
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
  uint32_t instance_streams = 0;
  std::array<NSUInteger, kGameVertexStreamCount> strides{}, attribute_extents{};
  std::array<NSUInteger,3> constant_bytes{};
};
struct TextureBinding { Stage stage; NSUInteger index; id<MTLTexture> texture; };
struct SamplerBinding { Stage stage; NSUInteger index; id<MTLSamplerState> sampler; };
struct Draw {
  std::shared_ptr<const Pipeline> pipeline;
  // ABI: VS=0 (4096 bytes), PS=1 (3584), shared=2 (1056).
  std::array<BufferView, 3> constants{};
  // Game streams occupy Metal slots 8..24. They never alias constant slots.
  std::array<BufferView, kGameVertexStreamCount> vertices{};
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
  NSUInteger stencil_back_reference = 0;
  std::array<float,4> blend_color{};
  float depth_bias = 0, slope_bias = 0;
  bool depth_clamp = false, lines = false;
};
struct Clear {
  uint32_t colors=0;
  bool depth=false,stencil=false;
  MTLScissorRect rectangle{};
  std::array<float,4> color{};
  float depth_value=1;
  uint32_t stencil_value=0;
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
  bool ClearRectangle(const Clear&, std::string& error);
  bool CopyTexture(id<MTLTexture> source,id<MTLTexture> destination,
                   MTLOrigin source_origin,MTLOrigin destination_origin,
                   MTLSize extent,std::string& error,bool combined_depth_stencil=false);
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
                    std::string& error,const char* entry="theft4_shader");
  std::shared_ptr<const Pipeline> MakePipeline(
      const Shader& vertex, const Shader& fragment,
      MTLRenderPipelineDescriptor* fixed,
      MTLDepthStencilDescriptor* depth, std::string& error);
  std::shared_ptr<const Pipeline> MakeDepthPipeline(
      const Shader& vertex, MTLRenderPipelineDescriptor* fixed,
      MTLDepthStencilDescriptor* depth, std::string& error);
  Frame BeginFrame(std::string& error);
  // Readback is for validation/capture, never part of the normal draw path.
  std::vector<uint8_t> ReadRGBA8(id<MTLTexture> texture, std::string& error,
                               NSUInteger level = 0, NSUInteger slice = 0,
                               NSUInteger depth_plane = 0);
  std::vector<uint8_t> ReadColorBytes(id<MTLTexture> texture,std::string& error,
                                    NSUInteger level=0,NSUInteger slice=0,
                                    NSUInteger depth_plane=0);
 private:
  friend class Frame;
  std::shared_ptr<const Pipeline> MakePipelineInternal(
      const Shader& vertex, const Shader* fragment, MTLRenderPipelineDescriptor* fixed,
      MTLDepthStencilDescriptor* depth, std::string& error);
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}
