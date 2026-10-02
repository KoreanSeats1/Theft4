#version 450
#extension GL_GOOGLE_include_directive : require
#define SMAA_GLSL_4 1
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1
layout(push_constant) uniform HDRPresentConstants {
  ivec2 source_extent;
  ivec2 destination_extent;
  float hdr_headroom;
  uint output_mode;
  uint hdr_mode;
  float paper_white_nits;
  float peak_nits;
  float shoulder_start;
  float shoulder_power;
} present_constants;


#define SMAA_RT_METRICS vec4(1.0 / vec2(present_constants.source_extent), vec2(present_constants.source_extent))
#include "SMAA.hlsl"
layout(set = 0, binding = 0) uniform sampler2D color_gamma_tex;
layout(set = 0, binding = 1) uniform sampler2D blend_tex;
#ifdef SMAA_HARDWARE_SRGB
layout(set = 0, binding = 2) uniform sampler2D color_linear_tex;
#endif
layout(location = 0) out vec4 output_color;
#include "smaa_neighborhood_common.glsl"
#include "../present_color.glsl"
void main() {
  vec2 texcoord = gl_FragCoord.xy * SMAA_RT_METRICS.xy;
  vec4 offset;
  SMAANeighborhoodBlendingVS(texcoord, offset);
  vec4 color = smaa_neighborhood_srgb(texcoord, offset);
  // Preserve rounding formerly introduced by the FP16 neighborhood target.
  color = vec4(unpackHalf2x16(packHalf2x16(color.xy)),
               unpackHalf2x16(packHalf2x16(color.zw)));
  output_color = present_color(color, ivec2(gl_FragCoord.xy), false);
}
