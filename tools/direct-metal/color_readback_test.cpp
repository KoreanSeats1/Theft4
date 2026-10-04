#include "native_color_readback.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using rex::graphics::gta4_native::CopyNativeColorToGuest;
int main() {
  std::string error;
  // Padded physical rows, a smaller logical image and an offset packed mip.
  std::vector<uint8_t> source(8*4,0xee),guest(32*16,0x7f);
  for(uint32_t y=0;y<4;++y)for(uint32_t x=0;x<6;++x)source[y*8+x]=uint8_t(y*10+x);
  const auto offset=[](uint32_t x,uint32_t y)->int64_t{return (y+8)*32+x+4;};
  const auto copy=[&](uint64_t at,const uint8_t* p,uint32_t n){std::memcpy(guest.data()+at,p,n);};
  assert(CopyNativeColorToGuest(source,6,4,8,3,2,1,guest.size(),offset,copy,error));
  for(uint32_t y=0;y<2;++y)for(uint32_t x=0;x<3;++x)
    assert(guest[offset(x,y)]==(y*2+1)*10+x*2+1);
  assert(guest[offset(0,0)-1]==0x7f&&guest[offset(2,1)+1]==0x7f);
  // Nonlinear destination offsets and block callbacks retain complete storage
  // values. The live caller supplies the guest's tiled and endian routines.
  std::array<uint8_t,16> blocks{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  std::array<uint8_t,16> nonlinear{};
  const auto shuffled=[](uint32_t x,uint32_t y)->int64_t{return (x*2+y)*4;};
  const auto swap=[&](uint64_t at,const uint8_t* p,uint32_t n) {
    assert(n==4);for(uint32_t b=0;b<n;++b)nonlinear[at+b]=p[n-1-b];
  };
  assert(CopyNativeColorToGuest(blocks,2,2,8,2,2,4,nonlinear.size(),shuffled,swap,error));
  assert((nonlinear==std::array<uint8_t,16>{4,3,2,1,12,11,10,9,8,7,6,5,16,15,14,13}));
  const auto saved=guest;size_t writes=0;
  const auto observed=[&](uint64_t at,const uint8_t* p,uint32_t n){++writes;copy(at,p,n);};
  const auto last_bad=[](uint32_t x,uint32_t y)->int64_t{return x==2&&y==1?512:(y+8)*32+x+4;};
  assert(!CopyNativeColorToGuest(source,6,4,8,3,2,1,guest.size(),last_bad,observed,error));
  assert(!writes&&guest==saved);
  assert(!CopyNativeColorToGuest(source,6,4,5,3,2,1,guest.size(),offset,observed,error));
  assert(!CopyNativeColorToGuest(std::span(source).first(31),6,4,8,3,2,1,guest.size(),offset,observed,error));
  assert(!CopyNativeColorToGuest(source,6,4,UINT64_MAX,3,2,1,guest.size(),offset,observed,error));
  assert(!writes&&guest==saved);
  std::cout<<"Logical texel-center readback, padded rows, packed offsets, block callbacks and rejection before writes passed\n";
}
