#include "theft4_render_plan_source.h"
#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>

namespace theft4::render::source {
bool DecodeSampler(const VkSamplerCreateInfo& s,render::Sampler& output,std::string& error) {
  const auto reject=[&](const char* reason){error=reason;return false;};
  if(s.sType!=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO||s.flags||s.pNext||s.mipLodBias!=0||
     s.compareEnable||s.unnormalizedCoordinates||s.anisotropyEnable>VK_TRUE||
     (s.minFilter!=VK_FILTER_NEAREST&&s.minFilter!=VK_FILTER_LINEAR)||
     (s.magFilter!=VK_FILTER_NEAREST&&s.magFilter!=VK_FILTER_LINEAR)||
     (s.mipmapMode!=VK_SAMPLER_MIPMAP_MODE_NEAREST&&s.mipmapMode!=VK_SAMPLER_MIPMAP_MODE_LINEAR)||
     (s.borderColor!=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE&&s.borderColor!=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK))
    return reject("Sampler state needs explicit Metal lowering");
  const auto address=[](VkSamplerAddressMode a,Address& result) {
    switch(a) {
      case VK_SAMPLER_ADDRESS_MODE_REPEAT:result=Address::Repeat;return true;
      case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:result=Address::MirrorRepeat;return true;
      case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:result=Address::ClampEdge;return true;
      case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:result=Address::MirrorClampEdge;return true;
      case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:result=Address::ClampBorder;return true;
      default:return false;
    }
  };
  Sampler result;result.min_linear=s.minFilter==VK_FILTER_LINEAR;result.mag_linear=s.magFilter==VK_FILTER_LINEAR;
  result.mip_linear=s.mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR;
  if(!address(s.addressModeU,result.address[0])||!address(s.addressModeV,result.address[1])||
     !address(s.addressModeW,result.address[2]))return reject("Sampler address mode needs explicit Metal lowering");
  if(s.anisotropyEnable) {
    if(!std::isfinite(s.maxAnisotropy)||s.maxAnisotropy<1||s.maxAnisotropy>16||std::floor(s.maxAnisotropy)!=s.maxAnisotropy)
      return reject("Sampler anisotropy cannot be represented exactly");
    result.anisotropy=uint32_t(s.maxAnisotropy);
  }
  result.min_lod_bits=std::bit_cast<uint32_t>(s.minLod);result.max_lod_bits=std::bit_cast<uint32_t>(s.maxLod);
  result.opaque_white_border=s.borderColor==VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  if(!ValidateSampler(result,error))return false;
  output=result;error.clear();return true;
}
bool DecodeImageUpload(const SampledImageDescription& description,
    std::span<const TextureUploadMip> uploads,uint64_t payload_bytes,render::Image& output,std::string& error) {
  const auto reject=[&](const char* reason){error=reason;return false;};
  Image image;image.format=PixelFormat(description.format);
  const auto block=TextureBlock(image.format);
  if(!block.bytes||!payload_bytes||!description.width||!description.height||!description.depth||
     !description.layers||!description.levels||description.width>16384||description.height>16384||
     description.depth>2048||description.layers>2048||description.levels>15||uploads.empty()||uploads.size()>2048)
    return reject("Unsupported sampled image upload description");
  switch(description.kind) {
    case VK_IMAGE_VIEW_TYPE_2D:image.kind=ImageKind::Texture2D;break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY:image.kind=ImageKind::Texture2DArray;break;
    case VK_IMAGE_VIEW_TYPE_3D:image.kind=ImageKind::Texture3D;break;
    case VK_IMAGE_VIEW_TYPE_CUBE:image.kind=ImageKind::TextureCube;break;
    default:return reject("Sampled image view needs explicit Metal lowering");
  }
  const bool volume=image.kind==ImageKind::Texture3D;
  const bool array=image.kind==ImageKind::Texture2DArray;
  const bool cube=image.kind==ImageKind::TextureCube;
  if((!volume&&description.depth!=1)||(!array&&description.layers!=1)||
     (cube&&description.width!=description.height)||
     (volume&&(description.width>2048||description.height>2048||block.width!=1)))
    return reject("Unsupported sampled image upload dimensions");
  if(description.levels>std::bit_width(std::max({description.width,description.height,description.depth})))
    return reject("Sampled image has more mip levels than its dimensions");
  image.width=description.width;image.height=description.height;image.depth=description.depth;
  image.layers=description.layers;image.levels=description.levels;
  const auto& c=description.components;
  const std::array components{c.r,c.g,c.b,c.a};
  for(size_t i=0;i<4;++i) {
    if(components[i]<VK_COMPONENT_SWIZZLE_IDENTITY||components[i]>VK_COMPONENT_SWIZZLE_A)
      return reject("Unsupported sampled image channel swizzle");
    image.swizzle[i]=components[i]==VK_COMPONENT_SWIZZLE_IDENTITY ? Swizzle(uint32_t(Swizzle::Red)+i)
                                                                : Swizzle(uint32_t(components[i])-1);
  }
  const uint32_t slices=cube ? 6 : array ? description.layers : 1;
  if(uint64_t(slices)*description.levels>2048)return reject("Too many sampled image upload subresources");
  std::vector<bool> seen(size_t(slices)*description.levels);
  const auto multiply=[](uint64_t a,uint64_t b,uint64_t& result) {
    if(b&&a>std::numeric_limits<uint64_t>::max()/b)return false;
    result=a*b;return true;
  };
  for(const auto& m:uploads) {
    if(m.level>=description.levels||m.width!=std::max(1u,description.width>>m.level)||
       m.height!=std::max(1u,description.height>>m.level)||
       m.depth!=(volume ? std::max(1u,description.depth>>m.level) : 1)||!m.layer_count||
       m.base_array_layer>=slices||m.layer_count>slices-m.base_array_layer||
       m.payload_offset>payload_bytes||!m.payload_size||m.payload_size>payload_bytes-m.payload_offset||
       (m.buffer_row_length&&(m.buffer_row_length<m.width||m.buffer_row_length%block.width))||
       (m.buffer_image_height&&(m.buffer_image_height<m.height||m.buffer_image_height%block.height)))
      return reject("Invalid sampled image upload mip/plane range");
    const uint64_t columns=(uint64_t(m.width)+block.width-1)/block.width;
    const uint64_t rows=(uint64_t(m.height)+block.height-1)/block.height;
    const uint64_t row_columns=(uint64_t(m.buffer_row_length ? m.buffer_row_length : m.width)+block.width-1)/block.width;
    const uint64_t image_rows=(uint64_t(m.buffer_image_height ? m.buffer_image_height : m.height)+block.height-1)/block.height;
    uint64_t row=0,plane=0,stride=0,previous_images=0,previous_rows=0,last_row=0;
    if(!multiply(row_columns,block.bytes,row)||!multiply(row,image_rows,plane)||!multiply(plane,m.depth,stride)||
       !multiply(plane,m.depth-1,previous_images)||!multiply(row,rows-1,previous_rows)||
       !multiply(columns,block.bytes,last_row)||previous_images>UINT64_MAX-previous_rows||
       previous_images+previous_rows>UINT64_MAX-last_row)
      return reject("Sampled image upload pitch overflows");
    const uint64_t required=previous_images+previous_rows+last_row;
    for(uint32_t layer=0;layer<m.layer_count;++layer) {
      uint64_t relative=0;
      if(!multiply(layer,stride,relative)||relative>m.payload_size||required>m.payload_size-relative||
         relative>payload_bytes-m.payload_offset||required>payload_bytes-m.payload_offset-relative)
        return reject("Sampled image upload payload is truncated");
      const auto slice=m.base_array_layer+layer;
      const auto index=size_t(slice)*description.levels+m.level;
      if(seen[index])return reject("Sampled image upload repeats a mip/slice");
      seen[index]=true;
      image.mips.push_back({m.level,slice,m.width,m.height,m.depth,row,plane,m.payload_offset+relative,required});
    }
  }
  if(std::find(seen.begin(),seen.end(),false)!=seen.end())
    return reject("Sampled image upload omits a mip/slice");
  output=std::move(image);error.clear();return true;
}
Format PixelFormat(VkFormat format) {
  switch(format) {
#define P(vk,name) case VK_FORMAT_##vk:return Format::name
    P(R8_UNORM,R8Unorm);P(R8G8_UNORM,RG8Unorm);P(R8G8B8A8_UNORM,RGBA8Unorm);P(R8G8B8A8_SRGB,RGBA8Srgb);
    P(B8G8R8A8_UNORM,BGRA8Unorm);P(B8G8R8A8_SRGB,BGRA8Srgb);P(R16_UNORM,R16Unorm);
    P(R16G16_UNORM,RG16Unorm);P(R16G16B16A16_UNORM,RGBA16Unorm);
    P(R16_SFLOAT,R16Float);P(R16G16_SFLOAT,RG16Float);P(R16G16B16A16_SFLOAT,RGBA16Float);
    P(R32_SFLOAT,R32Float);P(R32G32_SFLOAT,RG32Float);P(R32G32B32A32_SFLOAT,RGBA32Float);
    P(A2B10G10R10_UNORM_PACK32,RGB10A2Unorm);P(D32_SFLOAT,Depth32Float);
    P(S8_UINT,Stencil8);P(D32_SFLOAT_S8_UINT,Depth32FloatStencil8);
    P(BC1_RGBA_UNORM_BLOCK,BC1Unorm);P(BC1_RGBA_SRGB_BLOCK,BC1Srgb);
    P(BC2_UNORM_BLOCK,BC2Unorm);P(BC2_SRGB_BLOCK,BC2Srgb);P(BC3_UNORM_BLOCK,BC3Unorm);P(BC3_SRGB_BLOCK,BC3Srgb);
    P(BC4_UNORM_BLOCK,BC4Unorm);P(BC4_SNORM_BLOCK,BC4Snorm);P(BC5_UNORM_BLOCK,BC5Unorm);P(BC5_SNORM_BLOCK,BC5Snorm);
    P(ASTC_4x4_UNORM_BLOCK,ASTC4x4);P(ASTC_4x4_SRGB_BLOCK,ASTC4x4Srgb);
#undef P
    default:return Format::Invalid;
  }
}
Block TextureBlock(Format f) {
  switch(f) {
    case Format::R8Unorm:return {1,1,1};case Format::RG8Unorm:case Format::R16Unorm:case Format::R16Float:return {1,1,2};
    case Format::RGBA8Unorm:case Format::RGBA8Srgb:case Format::BGRA8Unorm:case Format::BGRA8Srgb:
    case Format::RG16Unorm:case Format::RG16Float:case Format::R32Float:case Format::RGB10A2Unorm:return {1,1,4};
    case Format::RGBA16Unorm:case Format::RGBA16Float:case Format::RG32Float:return {1,1,8};
    case Format::RGBA32Float:return {1,1,16};
    case Format::BC1Unorm:case Format::BC1Srgb:case Format::BC4Unorm:case Format::BC4Snorm:return {4,4,8};
    case Format::BC2Unorm:case Format::BC2Srgb:case Format::BC3Unorm:case Format::BC3Srgb:
    case Format::BC5Unorm:case Format::BC5Snorm:case Format::ASTC4x4:case Format::ASTC4x4Srgb:return {4,4,16};
    default:return {};
  }
}
namespace {
VertexFormat Vertex(VkFormat format) {
  switch(format) {
#define V(vk,name) case VK_FORMAT_##vk:return VertexFormat::name
    V(R32_SFLOAT,Float);V(R32G32_SFLOAT,Float2);V(R32G32B32_SFLOAT,Float3);V(R32G32B32A32_SFLOAT,Float4);
    V(R32_SINT,Int);V(R32G32_SINT,Int2);V(R32G32B32_SINT,Int3);V(R32G32B32A32_SINT,Int4);
    V(R32_UINT,UInt);V(R32G32_UINT,UInt2);V(R32G32B32_UINT,UInt3);V(R32G32B32A32_UINT,UInt4);
    V(R16G16_SFLOAT,Half2);V(R16G16B16A16_SFLOAT,Half4);V(R16G16_SINT,Short2);V(R16G16B16A16_SINT,Short4);
    V(R16G16_UINT,UShort2);V(R16G16B16A16_UINT,UShort4);V(R16G16_SNORM,Short2Normalized);
    V(R16G16B16A16_SNORM,Short4Normalized);V(R16G16_UNORM,UShort2Normalized);V(R16G16B16A16_UNORM,UShort4Normalized);
    V(R8G8B8A8_UINT,UChar4);V(R8G8B8A8_UNORM,UChar4Normalized);V(B8G8R8A8_UNORM,UChar4NormalizedBGRA);
    V(A2B10G10R10_SNORM_PACK32,Int1010102Normalized);
#undef V
    default:return VertexFormat::Invalid;
  }
}
}
bool Pipeline(const rex::graphics::gta4_native::NativePipelineRecipe::Snapshot& s,render::Draw& draw,std::string& error) {
  if(!s.stage_count||s.stage_count>2||s.attribute_count>31||s.binding_count>32||s.color_count>4||
      s.topology>VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP||s.rasterization.rasterizerDiscardEnable||
      s.rasterization.lineWidth!=1||s.depth_stencil.depthBoundsTestEnable) {
    error="Game pipeline state needs a frontend expansion before Metal capture";return false;
  }
  render::Draw out;auto& p=out.pipeline;
  p.vertex={s.shaders[0].title_hash,s.shaders[0].variant,s.shaders[0].specialization_enabled ? s.specialization : 0};
  if(s.stage_count==2)p.fragment={s.shaders[1].title_hash,s.shaders[1].variant,s.shaders[1].specialization_enabled ? s.specialization : 0};
  for(uint32_t i=0;i<s.binding_count;++i) {
    const auto& b=s.bindings[i];if(b.binding>=kStreamCount){error="Game vertex binding exceeds the native 17-stream contract";return false;}
    p.streams[b.binding]={b.stride,b.inputRate==VK_VERTEX_INPUT_RATE_INSTANCE};
  }
  for(uint32_t i=0;i<s.attribute_count;++i) {
    const auto& a=s.attributes[i];const auto format=Vertex(a.format);
    if(format==VertexFormat::Invalid){error="Game vertex format needs explicit Metal lowering";return false;}
    p.attributes.push_back({a.location,a.binding,a.offset,format});
  }
  const auto pixel=[&](VkFormat f,Format& output) {
    output=PixelFormat(f);return f==VK_FORMAT_UNDEFINED||output!=Format::Invalid;
  };
  for(uint32_t i=0;i<s.color_count;++i) {
    if(!pixel(s.color_formats[i],p.colors[i])){error="Game color format needs explicit Metal lowering";return false;}
    const auto& b=s.color_attachments[i];p.blends[i]={b.blendEnable!=0,BlendFactor(b.srcColorBlendFactor),BlendFactor(b.dstColorBlendFactor),
        BlendFactor(b.srcAlphaBlendFactor),BlendFactor(b.dstAlphaBlendFactor),BlendOp(b.colorBlendOp),BlendOp(b.alphaBlendOp),b.colorWriteMask};
  }
  if(!pixel(s.depth_format,p.depth)||!pixel(s.stencil_format,p.stencil)){error="Game depth/stencil format needs explicit Metal lowering";return false;}
  if(p.depth==Format::Stencil8)p.depth=Format::Invalid;
  if(p.stencil!=Format::Stencil8&&p.stencil!=Format::Depth32FloatStencil8)p.stencil=Format::Invalid;
  p.samples=uint32_t(s.samples);p.sample_mask=s.sample_mask;p.negative_one_to_one=s.negative_one_to_one!=0;
  p.depth_test=s.depth_stencil.depthTestEnable!=0;p.depth_write=s.depth_stencil.depthWriteEnable!=0;
  p.stencil_test=s.depth_stencil.stencilTestEnable!=0;p.depth_compare=Compare(s.depth_stencil.depthCompareOp);
  const auto stencil=[](const VkStencilOpState& v){return Stencil{Compare(v.compareOp),StencilOp(v.failOp),StencilOp(v.passOp),
      StencilOp(v.depthFailOp),v.compareMask,v.writeMask};};
  p.front=stencil(s.depth_stencil.front);p.back=stencil(s.depth_stencil.back);
  out.primitive=Primitive(s.topology);out.primitive_restart=s.primitive_restart!=0;
  out.cull=uint32_t(s.rasterization.cullMode);out.clockwise=s.rasterization.frontFace==VK_FRONT_FACE_CLOCKWISE;
  out.lines=s.rasterization.polygonMode==VK_POLYGON_MODE_LINE;out.depth_clamp=s.rasterization.depthClampEnable!=0;
  draw=std::move(out);error.clear();return true;
}
}
