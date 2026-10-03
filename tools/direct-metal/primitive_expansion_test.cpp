#include "theft4_primitive_expansion.h"
#include <cassert>
#include <cstring>
using namespace theft4::render;
int main() {
  std::string error;std::vector<uint32_t> result;Primitive topology=Primitive::Point;
  const uint32_t strip[]{4,5,6,6,7,8,99,10,11,12,13};
  assert(ExpandGuestIndices(6,strip,true,99,topology,result,error));
  assert(topology==Primitive::Triangle);
  assert((result==std::vector<uint32_t>{4,5,6,6,5,6,6,6,7,7,6,8,10,11,12,12,11,13}));
  const uint32_t fan[]{99,4,5,6,7,99,99,8,9,10,99};
  assert(ExpandGuestIndices(5,fan,true,99,topology,result,error));
  assert((result==std::vector<uint32_t>{4,5,6,4,6,7,8,9,10}));
  assert(ExpandGuestIndices(3,fan,true,99,topology,result,error));
  assert(topology==Primitive::Line);
  assert((result==std::vector<uint32_t>{4,5,5,6,6,7,8,9,9,10}));
  const uint32_t quad[]{4,8,15,16,23,42,64,65};
  assert(ExpandGuestIndices(13,quad,false,0,topology,result,error));
  assert((result==std::vector<uint32_t>{4,8,15,4,15,16,23,42,64,23,64,65}));
  const uint32_t tail[]{0,1,2,3};
  assert(ExpandGuestIndices(4,tail,false,0,topology,result,error));
  assert((result==std::vector<uint32_t>{0,1,2}));
  const uint32_t sentinel[]{65534,65535,65536};
  assert(ExpandGuestIndices(6,sentinel,false,65535,topology,result,error));
  assert((result==std::vector<uint32_t>{65534,65535,65536})); // Disabled restart is a vertex.
  const auto saved=result;const auto saved_topology=topology;
  assert(!ExpandGuestIndices(13,tail,true,0,topology,result,error));assert(result==saved&&topology==saved_topology);
  assert(!ExpandGuestIndices(1,tail,false,0,topology,result,error));assert(result==saved);
  assert(!ExpandGuestIndices(4,{},false,0,topology,result,error));assert(result==saved);
  const uint32_t markers[]{7,7,7};assert(ExpandGuestIndices(6,markers,true,7,topology,result,error));assert(result.empty());
  // Three corners -> four strip vertices. Interpolate float UV and preserve
  // the copied integer/color field, matching the title's rectangle semantics.
  struct Vertex {float x,y,u,v;uint32_t packed;};
  const Vertex rectangle[]{{0,0,0,0,1},{2,0,1,0,2},{0,4,0,1,3}};
  const FloatVertexField fields[]{{0,4}};std::vector<uint8_t> expanded;
  auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(rectangle),sizeof(rectangle));
  assert(ExpandGuestRectangles(bytes,sizeof(Vertex),fields,0,expanded,error));
  assert(expanded.size()==4*sizeof(Vertex));Vertex fourth;std::memcpy(&fourth,expanded.data()+3*sizeof(Vertex),sizeof(Vertex));
  assert(fourth.x==2&&fourth.y==4&&fourth.u==1&&fourth.v==1&&fourth.packed==3);
  const auto saved_bytes=expanded;
  assert(!ExpandGuestRectangles(bytes,sizeof(Vertex),fields,sizeof(Vertex)-4,expanded,error));assert(expanded==saved_bytes);
  assert(!ExpandGuestRectangles(bytes.subspan(1),sizeof(Vertex),fields,0,expanded,error));assert(expanded==saved_bytes);
}
