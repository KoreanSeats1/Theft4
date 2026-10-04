#pragma once
// Immutable CPU-side draw contract. This header has no graphics API types or
// platform headers. The game frontend supplies converted bytes and effective
// state; a backend owns shader, pipeline and GPU resource realization.
#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace theft4::render {
inline constexpr uint32_t kStreamCount = 17, kFetchCount = 26;
enum class Format : uint32_t {
  Invalid, R8Unorm, RG8Unorm, RGBA8Unorm, RGBA8Srgb, BGRA8Unorm, BGRA8Srgb,
  R16Unorm, RG16Unorm, RGBA16Unorm, R16Float, RG16Float, RGBA16Float,
  R32Float, RG32Float, RGBA32Float, RGB10A2Unorm,
  Depth32Float, Stencil8, Depth32FloatStencil8,
  BC1Unorm, BC1Srgb, BC2Unorm, BC2Srgb, BC3Unorm, BC3Srgb,
  BC4Unorm, BC4Snorm, BC5Unorm, BC5Snorm, ASTC4x4, ASTC4x4Srgb, Count
};
enum class VertexFormat : uint32_t {
  Invalid, Float, Float2, Float3, Float4, Int, Int2, Int3, Int4,
  UInt, UInt2, UInt3, UInt4, Half2, Half4, Short2, Short4,
  UShort2, UShort4, Short2Normalized, Short4Normalized,
  UShort2Normalized, UShort4Normalized, UChar4, UChar4Normalized,
  UChar4NormalizedBGRA, Int1010102Normalized, Count
};
enum class Primitive : uint32_t { Point, Line, LineStrip, Triangle, TriangleStrip, Count };
enum class Compare : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always, Count };
enum class StencilOp : uint32_t { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap, Count };
enum class BlendFactor : uint32_t {
  Zero, One, SourceColor, OneMinusSourceColor, DestinationColor,
  OneMinusDestinationColor, SourceAlpha, OneMinusSourceAlpha,
  DestinationAlpha, OneMinusDestinationAlpha, ConstantColor,
  OneMinusConstantColor, ConstantAlpha, OneMinusConstantAlpha,
  SourceAlphaSaturated, Source1Color, OneMinusSource1Color,
  Source1Alpha, OneMinusSource1Alpha, Count
};
enum class BlendOp : uint32_t { Add, Subtract, ReverseSubtract, Min, Max, Count };
enum class ImageKind : uint32_t { Texture2D, Texture2DArray, Texture3D, TextureCube, TextureCubeArray, Count };
enum class Address : uint32_t { Repeat, MirrorRepeat, ClampEdge, MirrorClampEdge, ClampBorder, Count };
enum class Swizzle : uint32_t { Zero, One, Red, Green, Blue, Alpha, Count };
struct Shader {
  uint64_t hash = 0;
  // 0 stock early, 1 stock late, 2 override early, 3 override late.
  uint32_t variant = 0, specialization = 0;
  auto operator<=>(const Shader&) const = default;
};
struct Attribute {
  uint32_t location = 0, stream = 0, offset = 0;
  VertexFormat format = VertexFormat::Invalid;
  auto operator<=>(const Attribute&) const = default;
};
struct Stream {
  uint32_t stride = 0;
  bool per_instance = false;
  auto operator<=>(const Stream&) const = default;
};
struct Blend {
  bool enabled = false;
  BlendFactor source_rgb = BlendFactor::One, destination_rgb = BlendFactor::Zero;
  BlendFactor source_alpha = BlendFactor::One, destination_alpha = BlendFactor::Zero;
  BlendOp rgb = BlendOp::Add, alpha = BlendOp::Add;
  uint32_t write_mask = 15;
  auto operator<=>(const Blend&) const = default;
};
struct Stencil {
  Compare compare = Compare::Always;
  StencilOp fail = StencilOp::Keep, pass = StencilOp::Keep, depth_fail = StencilOp::Keep;
  uint32_t read_mask = 255, write_mask = 255;
  auto operator<=>(const Stencil&) const = default;
};
struct Pipeline {
  Shader vertex{}, fragment{};
  std::vector<Attribute> attributes;
  std::array<Stream, kStreamCount> streams{};
  std::array<Format, 4> colors{};
  std::array<Blend, 4> blends{};
  Format depth = Format::Invalid, stencil = Format::Invalid;
  uint32_t samples = 1, sample_mask = ~uint32_t(0);
  bool depth_test = false, depth_write = false, stencil_test = false;
  Compare depth_compare = Compare::Always;
  Stencil front{}, back{};
  // Clip-space conversion must be handled explicitly by shader lowering.
  bool negative_one_to_one = false;
  auto operator<=>(const Pipeline&) const = default;
};
struct Sampler {
  bool min_linear = false, mag_linear = false, mip_linear = false;
  std::array<Address, 3> address{Address::Repeat, Address::Repeat, Address::Repeat};
  uint32_t anisotropy = 1;
  // Float bit patterns make the cache key exact and deterministic.
  uint32_t min_lod_bits = 0, max_lod_bits = 0;
  bool opaque_white_border = false;
  auto operator<=>(const Sampler&) const = default;
};
struct Bytes {
  uint64_t generation = 0;
  std::array<uint64_t, 4> conversion{};
  std::vector<uint8_t> value;
};
struct Buffer {
  std::shared_ptr<const Bytes> source;
  uint64_t offset = 0, length = 0;
};
struct Mip {
  uint32_t level = 0, slice = 0, width = 0, height = 0, depth = 1;
  uint64_t row_bytes = 0, image_bytes = 0, offset = 0, size = 0;
  bool operator==(const Mip&) const=default;
};
struct Image {
  std::shared_ptr<const Bytes> source;
  Format format = Format::Invalid;
  ImageKind kind = ImageKind::Texture2D;
  uint32_t width = 0, height = 0, depth = 1, layers = 1, levels = 1;
  std::array<Swizzle, 4> swizzle{Swizzle::Red, Swizzle::Green, Swizzle::Blue, Swizzle::Alpha};
  std::vector<Mip> mips;
};
struct Fetch {
  // A shader may declare more than one image kind for a fetch slot. The
  // frontend supplies the active image; typed dummy images fill unused kinds.
  std::shared_ptr<const Image> image;
  std::shared_ptr<const Sampler> sampler;
};
struct Draw {
  Pipeline pipeline{};
  std::array<Buffer, 3> constants{};
  std::array<Buffer, kStreamCount> vertices{};
  std::array<Fetch, kFetchCount> fetches{};
  Buffer indices{};
  Primitive primitive = Primitive::Triangle;
  uint32_t first_vertex = 0, vertex_count = 0, instances = 1;
  uint32_t index_count = 0, index_bytes = 2;
  int32_t base_vertex = 0;
  bool primitive_restart = false;
  std::array<double, 6> viewport{};
  std::array<uint32_t, 4> scissor{};
  std::array<float, 4> blend_color{};
  uint32_t stencil_front_reference = 0, stencil_back_reference = 0;
  uint32_t cull = 0;  // 0 none, 1 front, 2 back.
  bool clockwise = false, lines = false, depth_clamp = false;
  float depth_bias = 0, slope_bias = 0;
};
struct Capture {
  uint32_t frame = 0, command = 0, width = 0, height = 0;
  Draw draw;
  // Runtime-only identity for reusable storage; private file reads use zero.
  uint64_t allocation_generation = 0;
};
// Private capture files contain converted resources, never graphics API
// handles or process addresses. They must not be committed or published.
bool WriteCapture(const std::string& path, const Capture& capture, std::string& error);
bool ReadCapture(const std::string& path, Capture& capture, std::string& error);
struct IndexRange {
  uint32_t minimum=UINT32_MAX,maximum=0;
  uint32_t minimum_without_restart=UINT32_MAX,maximum_without_restart=0;
  bool has_restart=false,has_non_restart=false;
};
// Worker-local, bounded cache. Payloads are immutable; view bounds and draw
// state are checked on every use. Weak ownership rejects recycled addresses.
class IndexRangeCache {
 public:
  IndexRangeCache(); ~IndexRangeCache();
  IndexRangeCache(IndexRangeCache&&) noexcept;
  IndexRangeCache& operator=(IndexRangeCache&&) noexcept;
  bool Analyze(const Buffer&,uint32_t count,uint32_t index_bytes,IndexRange&,std::string& error);
  uint64_t ScannedIndices() const;
  uint64_t Hits() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
bool AnalyzeIndices(const Buffer&,uint32_t count,uint32_t index_bytes,IndexRange&,std::string& error);
enum class DrawValidationIssue { None, VertexRange };
// One admission batch only. Immutable image/sampler descriptors are fully
// checked once per owner; each draw still validates its views and byte budget.
// Exact pointer AND shared-owner identity protect against collisions/reuse.
class DrawResourceValidationCache {
 public:
  bool CheckImage(const std::shared_ptr<const Image>&,std::string&);
  bool CheckSampler(const std::shared_ptr<const Sampler>&,std::string&);
  void Clear(){images_={};samplers_={};hits=checks=0;}
  uint64_t hits=0,checks=0;
 private:
  template<class T> struct Entry {const T* pointer=nullptr;std::weak_ptr<const T> owner;};
  std::array<Entry<Image>,512> images_{};
  std::array<Entry<Sampler>,128> samplers_{};
};
bool Validate(const Capture& capture, std::string& error,DrawValidationIssue* issue=nullptr,
              IndexRangeCache* indices=nullptr,uint64_t* maximum_vertex=nullptr,
              DrawResourceValidationCache* resources=nullptr);
bool ValidateFixedPipeline(const Pipeline&,std::string& error);
bool ValidateSampler(const Sampler&,std::string& error);
bool ValidateImage(const Image&,std::string& error);
}
