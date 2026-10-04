#include "theft4_render_plan.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
using namespace theft4::render;
static auto MakeImage() {
  auto image=std::make_shared<Image>();image->format=Format::RGBA8Unorm;image->width=image->height=1024;image->levels=11;
  auto source=std::make_shared<Bytes>();source->generation=1;
  for(size_t level=0,offset=0;level<11;++level) {
    const auto width=std::max(size_t(1),size_t(1024)>>level),size=width*width*4;
    image->mips.push_back({uint32_t(level),0,uint32_t(width),uint32_t(width),1,uint64_t(width*4),uint64_t(size),uint64_t(offset),uint64_t(size)});
    offset+=size;source->value.resize(offset);
  }
  image->source=source;return image;
}
static Capture MakeDraw() {
  Capture c;c.width=c.height=64;auto& d=c.draw;d.pipeline.vertex.hash=1;d.pipeline.fragment.hash=2;
  d.vertex_count=3;d.viewport={0,0,64,64,0,1};d.scissor={0,0,64,64};
  for(size_t i=0;i<3;++i){auto b=std::make_shared<Bytes>();b->generation=i+1;b->value.resize(i==0?4096:i==1?3584:1056);d.constants[i]={b,0,b->value.size()};}
  auto image=MakeImage();auto sampler=std::make_shared<Sampler>();
  for(size_t i=0;i<8;++i)d.fetches[i]={image,sampler};return c;
}
int main(int argc,char**) {
  std::string error;DrawResourceValidationCache cache;auto c=MakeDraw();
  assert(Validate(c,error,nullptr,nullptr,nullptr,&cache));assert(cache.checks==2&&cache.hits==14);
  assert(Validate(c,error,nullptr,nullptr,nullptr,&cache));assert(cache.checks==2&&cache.hits==30);
  auto invalid=std::make_shared<Image>(*c.draw.fetches[0].image);invalid->mips.back().offset=invalid->source->value.size();
  auto bad=c;bad.draw.fetches[7].image=invalid;
  assert(!Validate(bad,error,nullptr,nullptr,nullptr,&cache));
  assert(!Validate(bad,error));
  bad=c;bad.draw.constants[0].length=4095;
  assert(!Validate(bad,error,nullptr,nullptr,nullptr,&cache));
  bad=c;auto sampler=std::make_shared<Sampler>();sampler->min_lod_bits=0x7f800000;bad.draw.fetches[7].sampler=sampler;
  assert(!Validate(bad,error,nullptr,nullptr,nullptr,&cache));
  // Identical raw pointer with a distinct aliasing control block is a miss.
  auto image=c.draw.fetches[0].image;
  auto first_owner=std::make_shared<int>(1),second_owner=std::make_shared<int>(2);
  std::shared_ptr<const Image> first(first_owner,image.get()),second(second_owner,image.get());
  cache.Clear();assert(cache.CheckImage(first,error)&&cache.checks==1);
  assert(cache.CheckImage(second,error)&&cache.checks==2);
  first.reset();first_owner.reset();assert(cache.CheckImage(second,error)&&cache.hits==1);
  // Reset before another admission batch: changes between batches are checked.
  auto changing=MakeImage();assert(cache.CheckImage(changing,error));changing->width=0;
  cache.Clear();assert(!cache.CheckImage(changing,error));
  std::cout<<"PASS: immutable descriptor reuse, malformed new owners, aliasing control blocks, batch reset and per-draw bank bounds\n";
  if(argc>1) {
    std::array<double,9> full{},cached{};constexpr size_t draws=5000;
    auto measure=[&](bool reuse) {
      DrawResourceValidationCache local;auto began=std::chrono::steady_clock::now();
      for(size_t i=0;i<draws;++i)assert(Validate(c,error,nullptr,nullptr,nullptr,reuse?&local:nullptr));
      return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
    };
    for(size_t i=0;i<9;++i){if(i%2){cached[i]=measure(true);full[i]=measure(false);}else{full[i]=measure(false);cached[i]=measure(true);}}
    std::sort(full.begin(),full.end());std::sort(cached.begin(),cached.end());
    std::cout<<"BENCH 5000 draws, 8 repeated 11-mip images: full_ms="<<full[4]<<" cached_ms="<<cached[4]<<'\n';
  }
}
