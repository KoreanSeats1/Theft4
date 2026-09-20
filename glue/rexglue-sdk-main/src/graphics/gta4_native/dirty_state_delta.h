#ifndef REX_GRAPHICS_GTA4_NATIVE_DIRTY_STATE_DELTA_H_
#define REX_GRAPHICS_GTA4_NATIVE_DIRTY_STATE_DELTA_H_

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace rex::graphics::gta4_native {

inline constexpr size_t kNativeDirtyWordCount = 5;
using NativeDirtyWords = std::array<uint64_t, kNativeDirtyWordCount>;

enum class DirtyStateComponent : uint8_t {
  kVertexConstants,
  kPixelConstants,
  kTextureFetchStages,
  kSamplerStages,
  kBooleanConstants,
  kIntegerConstants,
  kFixedState,
  kDynamicState,
  kCount,
};

constexpr const char* DirtyStateComponentName(DirtyStateComponent component) {
  switch (component) {
    case DirtyStateComponent::kVertexConstants:
      return "vertex-constants";
    case DirtyStateComponent::kPixelConstants:
      return "pixel-constants";
    case DirtyStateComponent::kTextureFetchStages:
      return "texture-fetch-stages";
    case DirtyStateComponent::kSamplerStages:
      return "sampler-stages";
    case DirtyStateComponent::kBooleanConstants:
      return "boolean-constants";
    case DirtyStateComponent::kIntegerConstants:
      return "integer-constants";
    case DirtyStateComponent::kFixedState:
      return "fixed-state";
    case DirtyStateComponent::kDynamicState:
      return "dynamic-state";
    case DirtyStateComponent::kCount:
      break;
  }
  return "unknown";
}

inline constexpr size_t kDirtyStateComponentCount =
    static_cast<size_t>(DirtyStateComponent::kCount);
using DirtyStateComponentMask = uint16_t;

constexpr DirtyStateComponentMask DirtyStateComponentBit(DirtyStateComponent component) {
  return static_cast<DirtyStateComponentMask>(uint32_t{1} << static_cast<uint8_t>(component));
}

constexpr DirtyStateComponentMask AllDirtyStateComponents() {
  DirtyStateComponentMask result = 0;
  for (size_t index = 0; index < kDirtyStateComponentCount; ++index) {
    result |= DirtyStateComponentBit(static_cast<DirtyStateComponent>(index));
  }
  return result;
}

// Maps a contiguous run of transport dirty bits to a contiguous run of
// semantic elements. Layout ownership remains with the caller; this permits
// the title hook to describe the actual GTA IV dirty-bit ABI without coupling
// the renderer's state transport utilities to guest offsets.
struct DirtyBitSpan {
  uint8_t dirty_word = 0;
  uint8_t first_dirty_bit = 0;
  uint8_t bit_count = 0;
  uint32_t first_element = 0;
  // Xenos float-constant masks number 64-byte register groups from the most
  // significant bit. Other native state masks use the usual least-significant
  // bit ordering, so keep the direction explicit in the title-owned layout.
  bool reverse_bits = false;
};

struct DirtyComponentLayout {
  std::span<const DirtyBitSpan> spans;
  uint32_t element_count = 0;
};

struct DirtyStateLayout {
  DirtyComponentLayout vertex_constants;
  DirtyComponentLayout pixel_constants;
  DirtyComponentLayout texture_fetch_stages;
  DirtyComponentLayout sampler_stages;
  DirtyComponentLayout boolean_constants;
  DirtyComponentLayout integer_constants;
  DirtyComponentLayout fixed_state;
  DirtyComponentLayout dynamic_state;

  constexpr const DirtyComponentLayout& Get(DirtyStateComponent component) const {
    switch (component) {
      case DirtyStateComponent::kVertexConstants:
        return vertex_constants;
      case DirtyStateComponent::kPixelConstants:
        return pixel_constants;
      case DirtyStateComponent::kTextureFetchStages:
        return texture_fetch_stages;
      case DirtyStateComponent::kSamplerStages:
        return sampler_stages;
      case DirtyStateComponent::kBooleanConstants:
        return boolean_constants;
      case DirtyStateComponent::kIntegerConstants:
        return integer_constants;
      case DirtyStateComponent::kFixedState:
        return fixed_state;
      case DirtyStateComponent::kDynamicState:
        return dynamic_state;
      case DirtyStateComponent::kCount:
        break;
    }
    return dynamic_state;
  }
};

enum class DirtyLayoutError : uint8_t {
  kNone,
  kZeroLengthSpan,
  kDirtyWordOutOfRange,
  kDirtyBitRangeOutOfRange,
  kElementRangeOutOfRange,
};

struct DirtyLayoutValidationResult {
  DirtyLayoutError error = DirtyLayoutError::kNone;
  DirtyStateComponent component = DirtyStateComponent::kVertexConstants;
  size_t span_index = 0;

  constexpr bool valid() const { return error == DirtyLayoutError::kNone; }
};

inline DirtyLayoutValidationResult ValidateDirtyStateLayout(const DirtyStateLayout& layout) {
  for (size_t component_index = 0; component_index < kDirtyStateComponentCount; ++component_index) {
    const auto component = static_cast<DirtyStateComponent>(component_index);
    const DirtyComponentLayout& component_layout = layout.Get(component);
    for (size_t span_index = 0; span_index < component_layout.spans.size(); ++span_index) {
      const DirtyBitSpan& span = component_layout.spans[span_index];
      if (!span.bit_count) {
        return {DirtyLayoutError::kZeroLengthSpan, component, span_index};
      }
      if (span.dirty_word >= kNativeDirtyWordCount) {
        return {DirtyLayoutError::kDirtyWordOutOfRange, component, span_index};
      }
      if (span.first_dirty_bit >= std::numeric_limits<uint64_t>::digits ||
          span.bit_count > std::numeric_limits<uint64_t>::digits - span.first_dirty_bit) {
        return {DirtyLayoutError::kDirtyBitRangeOutOfRange, component, span_index};
      }
      if (span.first_element > component_layout.element_count ||
          span.bit_count > component_layout.element_count - span.first_element) {
        return {DirtyLayoutError::kElementRangeOutOfRange, component, span_index};
      }
    }
  }
  return {};
}

struct DirtyElementRange {
  uint32_t first = 0;
  uint32_t count = 0;

  constexpr bool operator==(const DirtyElementRange&) const = default;
};

struct DirtyRangeSet {
  std::vector<DirtyElementRange> ranges;

  bool Any() const { return !ranges.empty(); }
};

// Logical masks are dynamically sized so fixed state, constants, or sampler
// layouts are never silently limited to one host machine word.
struct DirtyElementMask {
  uint32_t element_count = 0;
  std::vector<uint64_t> words;

  bool Any() const {
    return std::any_of(words.begin(), words.end(), [](uint64_t word) { return word != 0; });
  }

  bool Test(uint32_t element) const {
    if (element >= element_count) {
      return false;
    }
    return (words[element / std::numeric_limits<uint64_t>::digits] &
            (uint64_t{1} << (element % std::numeric_limits<uint64_t>::digits))) != 0;
  }
};

struct DirtyStateDelta {
  DirtyRangeSet vertex_constant_ranges;
  DirtyRangeSet pixel_constant_ranges;
  DirtyRangeSet texture_fetch_stage_ranges;
  DirtyRangeSet sampler_stage_ranges;
  DirtyElementMask boolean_constant_mask;
  DirtyElementMask integer_constant_mask;
  DirtyElementMask fixed_state_mask;
  DirtyElementMask dynamic_state_mask;

  DirtyStateComponentMask TouchedComponents() const {
    DirtyStateComponentMask result = 0;
    if (vertex_constant_ranges.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kVertexConstants);
    }
    if (pixel_constant_ranges.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kPixelConstants);
    }
    if (texture_fetch_stage_ranges.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kTextureFetchStages);
    }
    if (sampler_stage_ranges.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kSamplerStages);
    }
    if (boolean_constant_mask.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kBooleanConstants);
    }
    if (integer_constant_mask.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kIntegerConstants);
    }
    if (fixed_state_mask.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kFixedState);
    }
    if (dynamic_state_mask.Any()) {
      result |= DirtyStateComponentBit(DirtyStateComponent::kDynamicState);
    }
    return result;
  }

  bool Any() const { return TouchedComponents() != 0; }
};

struct DirtyDeltaBuildResult {
  DirtyLayoutValidationResult validation;
  DirtyStateDelta delta;

  bool valid() const { return validation.valid(); }
};

// A caller may keep one scratch object with its frame context. Reusing it and
// the destination delta retains their vector capacities, so steady-state draw
// preparation does not need to allocate while coalescing dirty ranges.
struct DirtyDeltaScratch {
  std::vector<uint32_t> dirty_elements;
};

namespace dirty_state_delta_internal {

constexpr uint64_t LowBitMask(uint8_t bit_count) {
  return bit_count == std::numeric_limits<uint64_t>::digits ? std::numeric_limits<uint64_t>::max()
                                                            : (uint64_t{1} << bit_count) - 1;
}

template <typename Callback>
void ForEachDirtyElement(const NativeDirtyWords& dirty_words, const DirtyComponentLayout& layout,
                         Callback&& callback) {
  for (const DirtyBitSpan& span : layout.spans) {
    uint64_t bits =
        (dirty_words[span.dirty_word] >> span.first_dirty_bit) & LowBitMask(span.bit_count);
    while (bits) {
      const uint32_t bit = static_cast<uint32_t>(std::countr_zero(bits));
      const uint32_t element_bit = span.reverse_bits ? uint32_t(span.bit_count - 1) - bit : bit;
      callback(span.first_element + element_bit);
      bits &= bits - 1;
    }
  }
}

inline void BuildRangeSet(const NativeDirtyWords& dirty_words, const DirtyComponentLayout& layout,
                          DirtyRangeSet& result, DirtyDeltaScratch& scratch) {
  // A single span already defines unique, ordered elements. Walk its set bits
  // in semantic order instead of allocating an element list and sorting it.
  // In particular, Xenos constants use MSB-first groups, so reverse spans must
  // visit the highest bit first. The general path still handles overlapping or
  // unordered spans and deduplicates them exactly as before.
  if (layout.spans.size() == 1) {
    const DirtyBitSpan& span = layout.spans.front();
    uint64_t bits =
        (dirty_words[span.dirty_word] >> span.first_dirty_bit) & LowBitMask(span.bit_count);
    result.ranges.clear();
    scratch.dirty_elements.clear();
    while (bits) {
      const uint32_t bit = span.reverse_bits
          ? 63u - static_cast<uint32_t>(std::countl_zero(bits))
          : static_cast<uint32_t>(std::countr_zero(bits));
      const uint32_t element = span.first_element +
          (span.reverse_bits ? uint32_t(span.bit_count - 1) - bit : bit);
      if (!result.ranges.empty() &&
          result.ranges.back().first + result.ranges.back().count == element) {
        ++result.ranges.back().count;
      } else {
        result.ranges.push_back({element, 1});
      }
      bits &= ~(uint64_t{1} << bit);
    }
    return;
  }
  std::vector<uint32_t>& dirty_elements = scratch.dirty_elements;
  dirty_elements.clear();
  ForEachDirtyElement(dirty_words, layout,
                      [&](uint32_t element) { dirty_elements.push_back(element); });
  std::sort(dirty_elements.begin(), dirty_elements.end());
  dirty_elements.erase(std::unique(dirty_elements.begin(), dirty_elements.end()),
                       dirty_elements.end());

  result.ranges.clear();
  for (uint32_t element : dirty_elements) {
    if (!result.ranges.empty()) {
      DirtyElementRange& previous = result.ranges.back();
      if (element == previous.first + previous.count) {
        ++previous.count;
        continue;
      }
    }
    result.ranges.push_back({element, 1});
  }
}

inline void BuildElementMask(const NativeDirtyWords& dirty_words,
                             const DirtyComponentLayout& layout, DirtyElementMask& result) {
  result.element_count = layout.element_count;
  const uint32_t bits_per_word = std::numeric_limits<uint64_t>::digits;
  result.words.assign(
      layout.element_count / bits_per_word + (layout.element_count % bits_per_word != 0), 0);
  ForEachDirtyElement(dirty_words, layout, [&](uint32_t element) {
    result.words[element / bits_per_word] |= uint64_t{1} << (element % bits_per_word);
  });
}

}  // namespace dirty_state_delta_internal

inline void ClearDirtyStateDelta(DirtyStateDelta& delta) {
  delta.vertex_constant_ranges.ranges.clear();
  delta.pixel_constant_ranges.ranges.clear();
  delta.texture_fetch_stage_ranges.ranges.clear();
  delta.sampler_stage_ranges.ranges.clear();
  delta.boolean_constant_mask.element_count = 0;
  delta.boolean_constant_mask.words.clear();
  delta.integer_constant_mask.element_count = 0;
  delta.integer_constant_mask.words.clear();
  delta.fixed_state_mask.element_count = 0;
  delta.fixed_state_mask.words.clear();
  delta.dynamic_state_mask.element_count = 0;
  delta.dynamic_state_mask.words.clear();
}

inline DirtyLayoutValidationResult BuildDirtyStateDelta(const NativeDirtyWords& dirty_words,
                                                        const DirtyStateLayout& layout,
                                                        DirtyStateDelta& delta,
                                                        DirtyDeltaScratch& scratch) {
  const DirtyLayoutValidationResult validation = ValidateDirtyStateLayout(layout);
  if (!validation.valid()) {
    ClearDirtyStateDelta(delta);
    return validation;
  }

  dirty_state_delta_internal::BuildRangeSet(dirty_words, layout.vertex_constants,
                                            delta.vertex_constant_ranges, scratch);
  dirty_state_delta_internal::BuildRangeSet(dirty_words, layout.pixel_constants,
                                            delta.pixel_constant_ranges, scratch);
  dirty_state_delta_internal::BuildRangeSet(dirty_words, layout.texture_fetch_stages,
                                            delta.texture_fetch_stage_ranges, scratch);
  dirty_state_delta_internal::BuildRangeSet(dirty_words, layout.sampler_stages,
                                            delta.sampler_stage_ranges, scratch);
  dirty_state_delta_internal::BuildElementMask(dirty_words, layout.boolean_constants,
                                               delta.boolean_constant_mask);
  dirty_state_delta_internal::BuildElementMask(dirty_words, layout.integer_constants,
                                               delta.integer_constant_mask);
  dirty_state_delta_internal::BuildElementMask(dirty_words, layout.fixed_state,
                                               delta.fixed_state_mask);
  dirty_state_delta_internal::BuildElementMask(dirty_words, layout.dynamic_state,
                                               delta.dynamic_state_mask);
  return validation;
}

inline DirtyDeltaBuildResult BuildDirtyStateDelta(const NativeDirtyWords& dirty_words,
                                                  const DirtyStateLayout& layout) {
  DirtyDeltaBuildResult result;
  DirtyDeltaScratch scratch;
  result.validation = BuildDirtyStateDelta(dirty_words, layout, result.delta, scratch);
  return result;
}

// A two-word stamp makes version rollover explicit. Incrementing an exhausted
// stamp fails atomically instead of wrapping and making an obsolete draw state
// appear reusable.
struct StateVersionStamp {
  uint64_t epoch = 0;
  uint64_t revision = 0;

  constexpr bool operator==(const StateVersionStamp&) const = default;
};

struct StateVersionVector {
  std::array<StateVersionStamp, kDirtyStateComponentCount> components{};

  constexpr const StateVersionStamp& Get(DirtyStateComponent component) const {
    return components[static_cast<size_t>(component)];
  }
};

enum class StateVersionUpdateStatus : uint8_t {
  kApplied,
  kVersionSpaceExhausted,
};

struct StateVersionUpdateResult {
  StateVersionUpdateStatus status = StateVersionUpdateStatus::kApplied;
  DirtyStateComponentMask touched_components = 0;
  StateVersionVector before;
  StateVersionVector after;

  constexpr bool applied() const { return status == StateVersionUpdateStatus::kApplied; }
};

constexpr bool CanIncrementStateVersion(StateVersionStamp stamp) {
  return stamp.revision != std::numeric_limits<uint64_t>::max() ||
         stamp.epoch != std::numeric_limits<uint64_t>::max();
}

constexpr void IncrementStateVersion(StateVersionStamp& stamp) {
  if (stamp.revision != std::numeric_limits<uint64_t>::max()) {
    ++stamp.revision;
  } else {
    ++stamp.epoch;
    stamp.revision = 0;
  }
}

inline StateVersionUpdateResult ApplyDirtyStateVersions(StateVersionVector& versions,
                                                        const DirtyStateDelta& delta) {
  StateVersionUpdateResult result;
  result.before = versions;
  result.after = versions;
  result.touched_components = delta.TouchedComponents();

  for (size_t index = 0; index < kDirtyStateComponentCount; ++index) {
    const auto component = static_cast<DirtyStateComponent>(index);
    if ((result.touched_components & DirtyStateComponentBit(component)) &&
        !CanIncrementStateVersion(result.after.components[index])) {
      result.status = StateVersionUpdateStatus::kVersionSpaceExhausted;
      return result;
    }
  }
  for (size_t index = 0; index < kDirtyStateComponentCount; ++index) {
    const auto component = static_cast<DirtyStateComponent>(index);
    if (result.touched_components & DirtyStateComponentBit(component)) {
      IncrementStateVersion(result.after.components[index]);
    }
  }
  versions = result.after;
  return result;
}

struct DrawStateVersionToken {
  bool initialized = false;
  DirtyStateComponentMask dependencies = 0;
  StateVersionVector versions;
};

inline DrawStateVersionToken CaptureDrawStateVersionToken(const StateVersionVector& versions,
                                                          DirtyStateComponentMask dependencies) {
  return {true, dependencies, versions};
}

enum class DrawStateReuseStatus : uint8_t {
  kReusable,
  kUninitialized,
  kDependencySetMismatch,
  kComponentVersionMismatch,
};

struct DrawStateReuseDecision {
  DrawStateReuseStatus status = DrawStateReuseStatus::kUninitialized;
  DirtyStateComponentMask mismatched_components = 0;

  constexpr bool reusable() const { return status == DrawStateReuseStatus::kReusable; }
};

inline DrawStateReuseDecision EvaluateDrawStateReuse(
    const DrawStateVersionToken& token, const StateVersionVector& current_versions,
    DirtyStateComponentMask required_dependencies) {
  if (!token.initialized) {
    return {DrawStateReuseStatus::kUninitialized, required_dependencies};
  }
  if (token.dependencies != required_dependencies) {
    return {DrawStateReuseStatus::kDependencySetMismatch,
            DirtyStateComponentMask(token.dependencies ^ required_dependencies)};
  }

  DirtyStateComponentMask mismatches = 0;
  for (size_t index = 0; index < kDirtyStateComponentCount; ++index) {
    const auto component = static_cast<DirtyStateComponent>(index);
    const DirtyStateComponentMask bit = DirtyStateComponentBit(component);
    if ((required_dependencies & bit) &&
        !(token.versions.components[index] == current_versions.components[index])) {
      mismatches |= bit;
    }
  }
  return mismatches
             ? DrawStateReuseDecision{DrawStateReuseStatus::kComponentVersionMismatch, mismatches}
             : DrawStateReuseDecision{DrawStateReuseStatus::kReusable, 0};
}

// Exact byte views are deliberately diagnostics-only: this comparison neither
// changes version state nor chooses the render path. Callers can construct one
// view from delta-replayed authoritative state and one from a legacy snapshot
// to prove semantic parity before enabling delta transport.
using DiagnosticSemanticStateView =
    std::array<std::span<const std::byte>, kDirtyStateComponentCount>;

inline constexpr size_t kNoSemanticMismatch = std::numeric_limits<size_t>::max();

struct DiagnosticSemanticMismatch {
  size_t delta_size = 0;
  size_t snapshot_size = 0;
  size_t first_mismatch_offset = kNoSemanticMismatch;
};

struct DiagnosticSemanticComparisonResult {
  DirtyStateComponentMask compared_components = 0;
  DirtyStateComponentMask mismatched_components = 0;
  std::array<DiagnosticSemanticMismatch, kDirtyStateComponentCount> details{};

  constexpr bool equivalent() const { return mismatched_components == 0; }
};

inline DiagnosticSemanticComparisonResult CompareDiagnosticSemanticState(
    const DiagnosticSemanticStateView& delta_derived,
    const DiagnosticSemanticStateView& snapshot_derived,
    DirtyStateComponentMask components = AllDirtyStateComponents()) {
  DiagnosticSemanticComparisonResult result;
  result.compared_components = components & AllDirtyStateComponents();
  for (size_t index = 0; index < kDirtyStateComponentCount; ++index) {
    const auto component = static_cast<DirtyStateComponent>(index);
    const DirtyStateComponentMask bit = DirtyStateComponentBit(component);
    if (!(result.compared_components & bit)) {
      continue;
    }

    const std::span<const std::byte> delta_bytes = delta_derived[index];
    const std::span<const std::byte> snapshot_bytes = snapshot_derived[index];
    DiagnosticSemanticMismatch& detail = result.details[index];
    detail.delta_size = delta_bytes.size();
    detail.snapshot_size = snapshot_bytes.size();
    const size_t common_size = std::min(delta_bytes.size(), snapshot_bytes.size());
    size_t first_mismatch = 0;
    while (first_mismatch < common_size &&
           delta_bytes[first_mismatch] == snapshot_bytes[first_mismatch]) {
      ++first_mismatch;
    }
    if (first_mismatch != common_size) {
      detail.first_mismatch_offset = first_mismatch;
      result.mismatched_components |= bit;
    } else if (delta_bytes.size() != snapshot_bytes.size()) {
      detail.first_mismatch_offset = common_size;
      result.mismatched_components |= bit;
    }
  }
  return result;
}

}  // namespace rex::graphics::gta4_native

#endif  // REX_GRAPHICS_GTA4_NATIVE_DIRTY_STATE_DELTA_H_
