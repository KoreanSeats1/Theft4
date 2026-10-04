#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace rex::graphics::gta4_native {
// Reconstruct a logical guest image from native storage. Explicit texture
// locks may require a different physical extent; nearest texel centers retain
// packed integer/float storage without interpreting or changing its channels.
// Validate all addresses before the first guest write, including packed mips.
template<class DestinationOffset,class CopyBlock>
bool CopyNativeColorToGuest(std::span<const uint8_t> source,uint32_t source_width,
    uint32_t source_height,uint64_t source_row,uint32_t width,uint32_t height,
    uint32_t pixel_bytes,uint64_t capacity,DestinationOffset destination_offset,
    CopyBlock copy,std::string& error) {
  const auto reject=[&](const char* reason){error=reason;return false;};
  if(!source_width||!source_height||!width||!height||source_width>16384||source_height>16384||
     width>16384||height>16384||!pixel_bytes||pixel_bytes>16||
     (pixel_bytes&(pixel_bytes-1))||source_row<uint64_t(source_width)*pixel_bytes||
     source_row>UINT64_MAX/source_height||source_row*source_height>source.size())
    return reject("Invalid native color readback dimensions or storage");
  for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
    const int64_t offset=destination_offset(x,y);
    if(offset<0||uint64_t(offset)>capacity||pixel_bytes>capacity-uint64_t(offset))
      return reject("Guest color readback destination exceeds its backing");
  }
  for(uint32_t y=0;y<height;++y) {
    const uint32_t sy=uint32_t((uint64_t(y)*2+1)*source_height/(uint64_t(height)*2));
    for(uint32_t x=0;x<width;++x) {
      const uint32_t sx=uint32_t((uint64_t(x)*2+1)*source_width/(uint64_t(width)*2));
      copy(uint64_t(destination_offset(x,y)),source.data()+uint64_t(sy)*source_row+uint64_t(sx)*pixel_bytes,pixel_bytes);
    }
  }
  error.clear();return true;
}
}
