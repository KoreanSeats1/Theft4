#pragma once
#include "theft4_render_plan.h"
namespace theft4::render {
// Host-endian Xenos geometry lowering. Custom restart markers are consumed
// before index masking/Metal submission; independent triangles preserve strip
// parity across degenerate vertices and reset it at each restart boundary.
bool ExpandGuestIndices(uint32_t guest_primitive,std::span<const uint32_t> indices,
                        bool restart,uint32_t marker,Primitive&,
                        std::vector<uint32_t>& output,std::string& error);
struct FloatVertexField {uint32_t offset=0,components=0;};
bool ExpandGuestRectangles(std::span<const uint8_t> vertices,uint32_t stride,
                           std::span<const FloatVertexField> fields,
                           uint32_t position_offset,std::vector<uint8_t>& output,
                           std::string& error);
struct RectangleVertexStream {
  std::span<const uint8_t> vertices;
  uint32_t stride = 0;
  std::span<const FloatVertexField> fields;
  uint32_t position_offset = UINT32_MAX;
};
// Gather selected host-endian vertices, applying the signed base once. All
// streams share the corner ordering chosen by the position stream.
bool ExpandGuestRectangleStreams(std::span<const RectangleVertexStream> streams,
                                std::span<const uint32_t> indices, int32_t base_vertex,
                                std::vector<std::vector<uint8_t>>& output,
                                std::string& error);
}
