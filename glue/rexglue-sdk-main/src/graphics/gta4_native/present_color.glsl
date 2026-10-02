// Shared SDR/HDR/dither contract for regular and fused presentation.
float luma(vec3 color) { return dot(color, vec3(0.2126, 0.7152, 0.0722)); }
float display_dither(ivec2 coordinate) {
  float noise = fract(dot(vec2(coordinate), vec2(0.75487766, 0.56984029)));
  return noise * (1.0 / 255.0) - (0.5 / 255.0);
}

vec3 dither_encoded_color(vec3 color, ivec2 coordinate) {
  return clamp(color + vec3(display_dither(coordinate)), vec3(0.0), vec3(1.0));
}

vec3 linear_to_srgb(vec3 color) {
  bvec3 use_linear_segment = lessThanEqual(color, vec3(0.0031308));
  vec3 linear_segment = color * 12.92;
  vec3 power_segment = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
  return mix(power_segment, linear_segment, use_linear_segment);
}

vec3 srgb_to_linear(vec3 color) {
  bvec3 use_linear_segment = lessThanEqual(color, vec3(0.04045));
  vec3 linear_segment = color / 12.92;
  vec3 power_segment = pow((color + 0.055) / 1.055, vec3(2.4));
  return mix(power_segment, linear_segment, use_linear_segment);
}

// Auto HDR curve based on Filoppi/PumboAutoHDR (MIT). Dolphin applies the
// same hue-preserving luminance expansion as an HDR post-process. Liberty's
// source reaches this shader before paper-white scaling, so the operations
// below retain Pumbo's original order explicitly.
vec3 apply_auto_hdr(vec3 linear_color) {
  const float sdr_white_nits = 80.0;
  float paper_white = present_constants.paper_white_nits / sdr_white_nits;
  vec3 color = linear_color * paper_white;
  color /= paper_white;

  float sdr_ratio = luma(color);
  float auto_hdr_max_white =
      max(present_constants.peak_nits /
              (present_constants.paper_white_nits / sdr_white_nits),
          sdr_white_nits) /
      sdr_white_nits;
  if (sdr_ratio > present_constants.shoulder_start &&
      present_constants.shoulder_start < 1.0 && sdr_ratio > 0.000001) {
    float shoulder_ratio =
        1.0 - (max(1.0 - sdr_ratio, 0.0) /
               (1.0 - present_constants.shoulder_start));
    float extra_ratio =
        pow(clamp(shoulder_ratio, 0.0, 1.0), present_constants.shoulder_power) *
        (auto_hdr_max_white - 1.0);
    float total_ratio = sdr_ratio + extra_ratio;
    color *= total_ratio / sdr_ratio;
  }
  return color * paper_white;
}

vec3 sanitize_hdr_color(vec3 color) {
  color = mix(color, vec3(0.0), isnan(color));
  return clamp(color, vec3(0.0), vec3(65504.0));
}


vec4 present_color(vec4 source, ivec2 destination_coordinate, bool ssaa_enabled) {
  bool source_is_srgb_encoded =
      (present_constants.output_mode & 4u) != 0u && !ssaa_enabled;
  bool hdr_enabled = present_constants.hdr_mode != 0u;
  bool output_dither_enabled =
      (present_constants.output_mode & 8u) != 0u && !hdr_enabled;
  vec3 source_color = max(source.rgb, vec3(0.0));
  vec3 encoded_source_color = clamp(source_color, vec3(0.0), vec3(1.0));
  if (source_is_srgb_encoded && output_dither_enabled) {
    encoded_source_color =
        dither_encoded_color(encoded_source_color, destination_coordinate);
  }
  vec3 linear_color = source_is_srgb_encoded
                          ? srgb_to_linear(encoded_source_color)
                          : source_color;

  if (hdr_enabled) {
    const float sdr_white_nits = 80.0;
    if (present_constants.hdr_mode == 2u) {
      linear_color = apply_auto_hdr(linear_color);
    } else {
      linear_color *= present_constants.paper_white_nits / sdr_white_nits;
    }
    linear_color = sanitize_hdr_color(linear_color);
    float configured_headroom =
        max(1.0, present_constants.peak_nits / sdr_white_nits);
    float effective_headroom =
        min(max(1.0, present_constants.hdr_headroom), configured_headroom);
    float maximum_component = max(max(linear_color.r, linear_color.g), linear_color.b);
    if (maximum_component > effective_headroom) {
      linear_color *= effective_headroom / maximum_component;
    }
    return vec4(linear_color, source.a);
  } else {
    // GTA IV's resolved 8-bit frontbuffer is already sRGB-encoded. Preserve it
    // exactly for SDR output. Only an actual linear FP16 source needs encoding.
    vec3 sdr_color = source_is_srgb_encoded
                         ? encoded_source_color
                         : linear_to_srgb(clamp(linear_color, vec3(0.0), vec3(1.0)));
    if (!source_is_srgb_encoded && output_dither_enabled) {
      sdr_color = dither_encoded_color(sdr_color, destination_coordinate);
    }
    return vec4(sdr_color, source.a);
  }
}
