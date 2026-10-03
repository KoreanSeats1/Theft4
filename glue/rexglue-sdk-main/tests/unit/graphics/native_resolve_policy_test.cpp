#include <catch2/catch_test_macros.hpp>

#include "graphics/gta4_native/native_resolve_policy.h"
#include "graphics/gta4_native/native_resolve_sync.h"
#include "graphics/gta4_native/native_spirv_reflection.h"
#include "graphics/gta4_native/resolve_convert_ps.h"
#include "graphics/gta4_native/resolve_convert_msaa_ps.h"
#include "graphics/gta4_native/resolve_convert_hdr_ps.h"
#include "graphics/gta4_native/resolve_convert_hdr_msaa_ps.h"

namespace gta4 = rex::graphics::gta4_native;

TEST_CASE("GTA IV native resolve discards only complete destination subresources") {
  REQUIRE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 1920, 1080, 1920, 1080}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({1, 0, 1920, 1080, 1920, 1080}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 1, 1920, 1080, 1920, 1080}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 1919, 1080, 1920, 1080}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 1920, 1079, 1920, 1080}));
}

TEST_CASE("GTA IV native resolve treats one-pixel and zero-sized targets exactly") {
  REQUIRE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 1, 1, 1, 1}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 0, 0, 0, 0}));
  REQUIRE_FALSE(gta4::IsFullNativeResolveSubresourceOverwrite({0, 0, 0, 1, 1, 1}));
}

TEST_CASE("GTA IV native resolve detects physical scale boundaries") {
  REQUIRE_FALSE(gta4::RequiresNativeResolveScaleConversion(1920, 1080, 1920, 1080));
  REQUIRE(gta4::RequiresNativeResolveScaleConversion(3840, 2160, 1920, 1080));
  REQUIRE(gta4::RequiresNativeResolveScaleConversion(1920, 1080, 1919, 1080));
  REQUIRE(gta4::RequiresNativeResolveScaleConversion(1920, 1080, 1920, 1079));
}

TEST_CASE("GTA IV native resolved textures distinguish initialization from title content") {
  gta4::NativeResolvedTextureContentState content;
  REQUIRE_FALSE(content.defined);
  REQUIRE_FALSE(content.resolved);
  REQUIRE_FALSE(gta4::CanPreserveNativeResolvedTexture(content, false));
  REQUIRE_FALSE(gta4::CanPreserveNativeResolvedTexture(content, true));

  content.MarkDeterministicallyInitialized();
  REQUIRE(content.defined);
  REQUIRE_FALSE(content.resolved);
  REQUIRE_FALSE(gta4::CanPreserveNativeResolvedTexture(content, false));
  REQUIRE(gta4::CanPreserveNativeResolvedTexture(content, true));

  content.CommitResolve(73, 991);
  REQUIRE(content.defined);
  REQUIRE(content.resolved);
  REQUIRE(content.last_resolved_frame == 73);
  REQUIRE(content.last_source_write_serial == 991);
  REQUIRE_FALSE(gta4::CanPreserveNativeResolvedTexture(content, false));
  REQUIRE(gta4::CanPreserveNativeResolvedTexture(content, true));
}

TEST_CASE("GTA IV native fused resolve requires two distinct compatible complete outputs") {
  const gta4::NativeColorResolveFusionInput valid{
      {0, 0, 1920, 1080, 1920, 1080}, 1920, 1080, true, true, true, true};
  REQUIRE(gta4::CanFuseNativeColorResolve(valid));
  using Input = gta4::NativeColorResolveFusionInput;
  for (bool Input::* field : {&Input::color_attachments_supported,
                             &Input::mirror_is_single_level_fp16_color,
                             &Input::distinct_images, &Input::level_zero}) {
    auto input = valid;
    input.*field = false;
    REQUIRE_FALSE(gta4::CanFuseNativeColorResolve(input));
  }
  auto input = valid;
  input.destination.destination_x = 1;
  REQUIRE_FALSE(gta4::CanFuseNativeColorResolve(input));
  input = valid;
  input.destination.height = 1079;
  REQUIRE_FALSE(gta4::CanFuseNativeColorResolve(input));
  input = valid;
  input.mirror_width = 1919;
  REQUIRE_FALSE(gta4::CanFuseNativeColorResolve(input));
  input = valid;
  input.mirror_height = 1079;
  REQUIRE_FALSE(gta4::CanFuseNativeColorResolve(input));
  REQUIRE_FALSE(gta4::CanFuseNativeColorResolve({}));
}

TEST_CASE("GTA IV native resolve modules retain the title output and expose the HDR output") {
  // Python-verified masks: location 0 -> 1, locations 0 and 1 -> 3.
  REQUIRE(gta4::ReflectNativeFragmentColorOutputMask(gta4_native_resolve_convert_ps) == 1u);
  REQUIRE(gta4::ReflectNativeFragmentColorOutputMask(gta4_native_resolve_convert_msaa_ps) == 1u);
  REQUIRE(gta4::ReflectNativeFragmentColorOutputMask(gta4_native_resolve_convert_hdr_ps) == 3u);
  REQUIRE(gta4::ReflectNativeFragmentColorOutputMask(gta4_native_resolve_convert_hdr_msaa_ps) == 3u);
}

TEST_CASE("GTA IV native depth resolves synchronize color-output resolves and depth stores") {
  REQUIRE(gta4::kNativeAttachmentResolveStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  REQUIRE(gta4::kNativeAttachmentResolveRead == VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
  REQUIRE(gta4::kNativeAttachmentResolveWrite == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  REQUIRE((gta4::kNativeDepthStencilResolveStages &
           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) != 0);
  REQUIRE((gta4::kNativeDepthStencilResolveStages & VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) != 0);
  REQUIRE((gta4::kNativeStoredDepthStencilResolveSourceAccess &
           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) != 0);
  REQUIRE((gta4::kNativeStoredDepthStencilResolveSourceAccess &
           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT) != 0);
}

TEST_CASE("GTA IV native producer depth resolve rejects every unsupported operation class") {
  using Input = gta4::NativeProducerDepthResolveEligibility;
  const Input valid{{0, 0, 1920, 1080, 1920, 1080}, 1920, 1080, 1920, 1080,
                    true, true, true, true, true, true, true, true, true, true};
  REQUIRE(gta4::CanAttachNativeProducerDepthResolve(valid));
  for (bool Input::* field : {&Input::exact_source_view, &Input::full_source_rectangle,
                             &Input::combined_depth_stencil, &Input::matching_formats,
                             &Input::multisampled_source, &Input::sample_zero_requested,
                             &Input::sample_zero_supported, &Input::single_subresource_destination,
                             &Input::distinct_images, &Input::destination_unused_until_resolve}) {
    auto input = valid;
    input.*field = false;
    REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  }
  for (uint32_t Input::* extent : {&Input::source_width, &Input::source_height,
                                  &Input::render_width, &Input::render_height}) {
    auto input = valid;
    input.*extent = 1;
    REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  }
  auto input = valid;
  input.destination.destination_x = -1;
  REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  input = valid;
  input.destination.destination_y = 1;
  REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  input = valid;
  input.destination.width = 1919;
  REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  input = valid;
  input.destination.height = 1079;
  REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve(input));
  REQUIRE_FALSE(gta4::CanAttachNativeProducerDepthResolve({}));
}

TEST_CASE("GTA IV native producer resolve publication checks every incarnation and write") {
  using Stamp = gta4::NativeProducerDepthResolveStamp;
  const Stamp recorded{71, 83, 97, 101, 103, 107, 109, 113, 127};
  REQUIRE(gta4::CanPublishNativeProducerDepthResolve(recorded, recorded));
  for (uint64_t Stamp::* field : {&Stamp::command_index, &Stamp::submission,
                                 &Stamp::source_lifetime, &Stamp::source_image,
                                 &Stamp::destination_lifetime, &Stamp::destination_image,
                                 &Stamp::destination_generation, &Stamp::depth_serial,
                                 &Stamp::stencil_serial}) {
    auto changed = recorded;
    changed.*field = 0;
    REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve(recorded, changed));
    if (field != &Stamp::command_index) {
      REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve(changed, changed));
    }
  }
  auto invalid = recorded;
  invalid.command_index = UINT64_MAX;
  REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve(invalid, invalid));
  REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve({}, {}));
}

TEST_CASE("GTA IV native producer completion never publishes content before the title command") {
  gta4::NativeResolvedTextureContentState content;
  content.CommitResolve(1, 11);
  const gta4::NativeProducerDepthResolveStamp recorded{7, 83, 97, 101, 103, 107, 109, 113, 127};
  auto after_stencil_clear = recorded;
  after_stencil_clear.stencil_serial = 131;
  REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve(recorded, after_stencil_clear));
  REQUIRE(content.last_resolved_frame == 1);
  REQUIRE(content.last_source_write_serial == 11);
  auto after_alias_materialization = recorded;
  after_alias_materialization.source_lifetime = 137;
  REQUIRE_FALSE(gta4::CanPublishNativeProducerDepthResolve(recorded, after_alias_materialization));
  REQUIRE(gta4::CanPublishNativeProducerDepthResolve(recorded, recorded));
  content.CommitResolve(2, recorded.depth_serial);
  REQUIRE(content.last_resolved_frame == 2);
  REQUIRE(content.last_source_write_serial == recorded.depth_serial);
}

TEST_CASE("GTA IV native producer intervals require exact color depth usage and phase") {
  using Scope = gta4::NativeProducerDepthResolveScope;
  using Descriptor = gta4::SurfaceDescriptor;
  Scope baseline{};
  baseline.colors[0].handle = 71;
  baseline.depth_stencil.handle = 83;
  baseline.color_attachment_mask = 1;
  baseline.color_write_mask = 15;
  baseline.depth_stencil_aspects = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  REQUIRE(baseline == baseline);
  for (uint32_t Descriptor::* field : {
           &Descriptor::handle, &Descriptor::flags, &Descriptor::base, &Descriptor::address,
           &Descriptor::packed_dimensions, &Descriptor::format, &Descriptor::width,
           &Descriptor::height, &Descriptor::sample_type}) {
    auto changed = baseline;
    changed.colors[0].*field = 97;
    REQUIRE_FALSE(changed == baseline);
    changed = baseline;
    changed.depth_stencil.*field = 97;
    REQUIRE_FALSE(changed == baseline);
  }
  for (uint32_t Scope::* field : {&Scope::color_attachment_mask, &Scope::color_write_mask,
                                  &Scope::depth_stencil_aspects, &Scope::phase_object}) {
    auto changed = baseline;
    changed.*field = 97;
    REQUIRE_FALSE(changed == baseline);
  }
  auto changed = baseline;
  changed.colors[3].handle = 97;
  REQUIRE_FALSE(changed == baseline);
  changed = baseline;
  changed.phase = gta4::RenderPhase::kSceneToGBuffer;
  REQUIRE_FALSE(changed == baseline);
}

TEST_CASE("GTA IV native same-depth color switches attach only the final compatible interval") {
  gta4::NativeProducerDepthResolveScope first{};
  first.depth_stencil.handle = 83;
  first.colors[0].handle = 97;
  auto last = first;
  last.colors[0].handle = 101;
  const std::array scopes{first, first, last, last};
  gta4::NativeProducerDepthResolveScan scan;
  size_t inspections = 0;
  const auto find = [&](size_t begin) {
    return scan.FindResolve(begin, 5, [&](size_t index) {
      ++inspections;
      if (index == 4) { return gta4::NativeProducerDepthResolveScanStep::kResolve; }
      return scopes[index] == scopes[begin]
                 ? gta4::NativeProducerDepthResolveScanStep::kCompatible
                 : gta4::NativeProducerDepthResolveScanStep::kBoundary;
    });
  };
  REQUIRE(find(0) == SIZE_MAX);
  REQUIRE(scan.next_command_index == 2);
  REQUIRE(find(1) == SIZE_MAX);
  REQUIRE(inspections == 3);
  REQUIRE(find(2) == 4);
  // An unexpected restart within the final interval cannot attach another
  // full-image resolve, even if first-attempt resource preflight failed.
  REQUIRE(find(3) == SIZE_MAX);
  REQUIRE(inspections == 6);
}

TEST_CASE("GTA IV native producer scans stay linear under long repeated scope changes") {
  constexpr size_t draw_count = 20000;
  for (bool switch_color : {false, true}) {
    gta4::NativeProducerDepthResolveScan scan;
    gta4::NativeProducerDepthResolveScope first{};
    first.depth_stencil.handle = 83;
    first.colors[0].handle = 97;
    auto second = first;
    second.colors[0].handle = 101;
    size_t inspections = 0;
    size_t attachments = 0;
    for (size_t begin = 0; begin < draw_count; ++begin) {
      const auto& scope = switch_color && (begin % 2) ? second : first;
      const size_t resolve = scan.FindResolve(begin, 20001, [&](size_t index) {
        ++inspections;
        if (index == draw_count) { return gta4::NativeProducerDepthResolveScanStep::kResolve; }
        const auto& pending = switch_color && (index % 2) ? second : first;
        return pending == scope ? gta4::NativeProducerDepthResolveScanStep::kCompatible
                                : gta4::NativeProducerDepthResolveScanStep::kBoundary;
      });
      attachments += resolve != SIZE_MAX;
    }
    REQUIRE(attachments == 1);
    REQUIRE(inspections == (switch_color ? 40000 : 20001));
  }
}

TEST_CASE("GTA IV native producer scans stop at barriers and safely exhaust their budget") {
  gta4::NativeProducerDepthResolveScan scan;
  size_t inspections = 0;
  const auto compatible = [&](size_t) {
    ++inspections;
    return gta4::NativeProducerDepthResolveScanStep::kCompatible;
  };
  REQUIRE(scan.FindResolve(0, 20000, compatible) == SIZE_MAX);
  REQUIRE(inspections == gta4::kNativeProducerDepthResolveLookahead);
  REQUIRE(scan.FindResolve(1, 20000, compatible) == SIZE_MAX);
  REQUIRE(inspections == gta4::kNativeProducerDepthResolveLookahead);
  REQUIRE(scan.FindResolve(20000, 20000, compatible) == SIZE_MAX);
  REQUIRE(scan.FindResolve(SIZE_MAX, SIZE_MAX, compatible) == SIZE_MAX);
  scan = {};
  REQUIRE(scan.FindResolve(SIZE_MAX - 2, SIZE_MAX, compatible) == SIZE_MAX);
  REQUIRE(scan.next_command_index == SIZE_MAX);
  scan = {};
  REQUIRE(scan.FindResolve(0, 3, [](size_t) {
    return gta4::NativeProducerDepthResolveScanStep::kBoundary;
  }) == SIZE_MAX);
  REQUIRE(scan.next_command_index == 1);
}

TEST_CASE("GTA IV native producer preflight rejects logical resolve and clear failures") {
  gta4::ResolveCommand valid{};
  valid.flags = 0x214u;
  valid.source.width = 1920;
  valid.source.height = 1080;
  const auto eligible = [](const gta4::ResolveCommand& request) {
    return gta4::IsNativeProducerDepthResolveRequest(request, 1920, 1080, 1920, 1080, true, true);
  };
  REQUIRE(eligible(valid));
  auto request = valid;
  request.parameters_valid = 1;
  REQUIRE_FALSE(eligible(request));
  for (uint32_t flags : {0x210u, 0x211u, 0x212u, 0x213u, 0x215u, 0x216u, 0x217u,
                         0x314u, 0x21Cu, 0x04000214u}) {
    request = valid;
    request.flags = flags;
    REQUIRE_FALSE(eligible(request));
  }
  request = valid;
  request.destination_level = 1;
  REQUIRE_FALSE(eligible(request));
  request = valid;
  request.destination_slice_or_face = 1;
  REQUIRE_FALSE(eligible(request));
  request = valid;
  request.source.width = 1919;
  REQUIRE_FALSE(eligible(request));
  request = valid;
  request.source.height = 1079;
  REQUIRE_FALSE(eligible(request));
  request = valid;
  request.source_rectangle_valid = 1;
  for (const auto& rectangle : {
           gta4::ResolveRectangle{-1, 0, 1920, 1080}, {0, -1, 1920, 1080},
           {0, 0, 1919, 1080}, {0, 0, 1920, 1079}, {0, 0, 1921, 1080},
           {0, 0, 1920, 1081}, {1920, 1080, 0, 0}, {0, 0, 0, 0}}) {
    request.source_rectangle = rectangle;
    REQUIRE_FALSE(eligible(request));
  }
  request.source_rectangle = {0, 0, 1920, 1080};
  REQUIRE(eligible(request));
  request = valid;
  request.destination_point_valid = 1;
  for (const auto& point : {gta4::ResolvePoint{-1, 0}, {0, -1}, {1, 0}, {0, 1}, {1920, 1080}}) {
    request.destination_point = point;
    REQUIRE_FALSE(eligible(request));
  }
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, 1920, 1080, 1919, 1080, true, true));
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, 1920, 1080, 1920, 1079, true, true));
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, 1920, 1080, 1920, 1080, false, true));
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, 1920, 1080, 1920, 1080, true, false));
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, 0, 1080, 1920, 1080, true, true));
  REQUIRE_FALSE(gta4::IsNativeProducerDepthResolveRequest(valid, UINT32_MAX, 1080,
                                                         UINT32_MAX, 1080, true, true));
}
