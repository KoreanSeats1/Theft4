#include "theft4_primitive_expansion.h"
#include <cstring>
#include <limits>
namespace theft4::render {
bool ExpandGuestIndices(uint32_t primitive,std::span<const uint32_t> indices,
                        bool restart,uint32_t marker,Primitive& topology,
                        std::vector<uint32_t>& output,std::string& error) {
  const auto reject=[&](const char* message){error=message;return false;};
  constexpr size_t maximum=16*1024*1024; // Bounded immutable 64MiB index owner.
  if(indices.empty()||indices.size()>maximum)return reject("Invalid guest index count");
  if(primitive!=2&&primitive!=3&&primitive!=4&&primitive!=5&&primitive!=6&&primitive!=13)
    return reject("Guest primitive requires vertex expansion");
  const bool strip=primitive==3||primitive==5||primitive==6;
  if(restart&&!strip)return reject("Restart only applies to guest strips and fans");
  std::vector<uint32_t> next;const auto push=[&](uint32_t value) {
    if(next.size()>=maximum)return false;next.push_back(value);return true;
  };
  Primitive result=primitive==2||primitive==3?Primitive::Line:Primitive::Triangle;
  size_t begin=0;
  for(size_t end=0;end<=indices.size();++end) {
    if(end<indices.size()&&(!restart||indices[end]!=marker))continue;
    const auto run=indices.subspan(begin,end-begin);
    // Incomplete tails of lists/strips produce no primitive, as on the guest.
    if(primitive==2||primitive==4) {
      const size_t group=primitive==2?2:3;
      for(size_t i=0;i+group<=run.size();i+=group)
        for(size_t c=0;c<group;++c)if(!push(run[i+c]))return reject("Expanded index budget exceeded");
    } else if(primitive==13) {
      if(run.size()%4)return reject("Incomplete guest quad list");
      for(size_t i=0;i<run.size();i+=4)
        for(size_t c:{0u,1u,2u,0u,2u,3u})if(!push(run[i+c]))return reject("Expanded index budget exceeded");
    } else if(primitive==3) {
      for(size_t i=1;i<run.size();++i)
        if(!push(run[i-1])||!push(run[i]))return reject("Expanded index budget exceeded");
    } else {
      for(size_t i=2;i<run.size();++i) {
        const uint32_t a=primitive==5?run[0]:run[i-2+(i%2)];
        const uint32_t b=primitive==5?run[i-1]:run[i-1-(i%2)];
        if(!push(a)||!push(b)||!push(run[i]))return reject("Expanded index budget exceeded");
      }
    }
    begin=end+1;
  }
  // Empty expansion is a real no-op (only markers or incomplete primitive).
  topology=result;output=std::move(next);error.clear();return true;
}
bool ExpandGuestRectangles(std::span<const uint8_t> vertices,uint32_t stride,
                           std::span<const FloatVertexField> fields,uint32_t position_offset,
                           std::vector<uint8_t>& output,std::string& error) {
  const auto reject=[&](const char* message){error=message;return false;};
  if(!stride||vertices.empty()||vertices.size()%stride||vertices.size()/stride%3 ||
     vertices.size()/stride>uint64_t(64*1024*1024)/stride*3/4)
    return reject("Invalid guest rectangle vertex extent");
  if(position_offset!=UINT32_MAX && (position_offset>stride||stride-position_offset<8))
    return reject("Invalid guest rectangle position layout");
  for(const auto& field:fields)
    if(!field.components||field.components>4||field.offset>stride||field.components*4>stride-field.offset)
      return reject("Invalid guest rectangle float layout");
  const size_t rectangles=vertices.size()/stride/3;
  std::vector<uint8_t> next(rectangles*4*stride);
  for(size_t rectangle=0;rectangle<rectangles;++rectangle) {
    const uint8_t* input=vertices.data()+rectangle*3*stride;
    uint32_t corner=0;
    if(position_offset!=UINT32_MAX) {
      std::array<std::array<float,2>,3> position;
      for(size_t i=0;i<3;++i)std::memcpy(position[i].data(),input+i*stride+position_offset,8);
      float longest=-1;
      for(uint32_t i=0;i<3;++i) {
        float x=position[(i+1)%3][0]-position[(i+2)%3][0];
        float y=position[(i+1)%3][1]-position[(i+2)%3][1];
        float length=x*x+y*y;if(length>longest){longest=length;corner=i;}
      }
    }
    const uint8_t* a=input+corner*stride;
    const uint8_t* b=input+(corner+1)%3*stride;
    const uint8_t* c=input+(corner+2)%3*stride;
    uint8_t* out=next.data()+rectangle*4*stride;
    std::memcpy(out,a,stride);std::memcpy(out+stride,b,stride);
    std::memcpy(out+2*stride,c,stride);std::memcpy(out+3*stride,c,stride);
    for(const auto& field:fields)for(uint32_t component=0;component<field.components;++component) {
      const size_t offset=field.offset+component*4;float va,vb,vc;
      std::memcpy(&va,a+offset,4);std::memcpy(&vb,b+offset,4);std::memcpy(&vc,c+offset,4);
      const float value=vb-va+vc;std::memcpy(out+3*stride+offset,&value,4);
    }
  }
  output=std::move(next);error.clear();return true;
}
bool ExpandGuestRectangleStreams(std::span<const RectangleVertexStream> streams,
                                std::span<const uint32_t> indices, int32_t base_vertex,
                                std::vector<std::vector<uint8_t>>& output,
                                std::string& error) {
  const auto reject = [&](const char* message) { error = message; return false; };
  constexpr uint64_t budget = 64 * 1024 * 1024;
  if (streams.empty() || indices.empty() || indices.size() % 3)
    return reject("Invalid buffered rectangle extent");
  uint64_t total = 0;
  size_t position_stream = streams.size();
  for (size_t i = 0; i < streams.size(); ++i) {
    const auto& stream = streams[i];
    if (!stream.stride || stream.vertices.size() < stream.stride)
      return reject("Invalid buffered rectangle stream");
    const uint64_t size = uint64_t(indices.size() / 3) * 4 * stream.stride;
    if (size > budget || total > budget - size)
      return reject("Buffered rectangle upload budget exceeded");
    total += size;
    if (stream.position_offset != UINT32_MAX) {
      if (position_stream != streams.size() || stream.position_offset > stream.stride ||
          stream.stride - stream.position_offset < 8)
        return reject("Invalid buffered rectangle position stream");
      position_stream = i;
    }
    for (const auto& field : stream.fields)
      if (!field.components || field.components > 4 || field.offset > stream.stride ||
          field.components * 4 > stream.stride - field.offset)
        return reject("Invalid buffered rectangle field");
    for (const auto index : indices) {
      const int64_t vertex = int64_t(index) + base_vertex;
      if (vertex < 0 || uint64_t(vertex) >= stream.vertices.size() / stream.stride)
        return reject("Buffered rectangle vertex outside captured stream");
    }
  }
  std::vector<std::vector<uint8_t>> gathered(streams.size());
  for (size_t i = 0; i < streams.size(); ++i)
    gathered[i].resize(indices.size() * streams[i].stride);
  for (size_t rectangle = 0; rectangle < indices.size() / 3; ++rectangle) {
    uint32_t corner = 0;
    if (position_stream < streams.size()) {
      const auto& stream = streams[position_stream];
      std::array<std::array<float, 2>, 3> positions;
      for (size_t c = 0; c < 3; ++c) {
        const auto vertex = uint64_t(int64_t(indices[rectangle * 3 + c]) + base_vertex);
        std::memcpy(positions[c].data(), stream.vertices.data() + vertex * stream.stride +
                    stream.position_offset, 8);
      }
      float longest = -1;
      for (uint32_t c = 0; c < 3; ++c) {
        const float x = positions[(c + 1) % 3][0] - positions[(c + 2) % 3][0];
        const float y = positions[(c + 1) % 3][1] - positions[(c + 2) % 3][1];
        const float length = x * x + y * y;
        if (length > longest) { longest = length; corner = c; }
      }
    }
    for (size_t i = 0; i < streams.size(); ++i) {
      const auto& stream = streams[i];
      for (size_t c = 0; c < 3; ++c) {
        const auto vertex = uint64_t(int64_t(indices[rectangle * 3 + (corner + c) % 3]) + base_vertex);
        std::memcpy(gathered[i].data() + (rectangle * 3 + c) * stream.stride,
                    stream.vertices.data() + vertex * stream.stride, stream.stride);
      }
    }
  }
  std::vector<std::vector<uint8_t>> next(streams.size());
  for (size_t i = 0; i < streams.size(); ++i)
    if (!ExpandGuestRectangles(gathered[i], streams[i].stride, streams[i].fields,
                              UINT32_MAX, next[i], error)) return false;
  output = std::move(next); error.clear(); return true;
}

}
