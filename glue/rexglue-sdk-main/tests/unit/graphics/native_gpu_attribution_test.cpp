#include "graphics/gta4_native/native_gpu_attribution.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace rex::graphics::gta4_native::attribution {
namespace {

NativePassKey MakePassKey(uint64_t pixel_shader_hash,
                          NativePassCommandClass command_class = NativePassCommandClass::kDraw) {
  NativePassKeyInput input{};
  input.command_class = command_class;
  input.color_targets[0] = {0x1000, 7, 44, 4};
  input.depth_target = {0x2000, 9, 129, 4};
  input.reflection_family = 1;
  input.render_phase = 3;
  input.shader_family = 0xABC;
  input.vertex_shader_hash = 0x1111;
  input.pixel_shader_hash = pixel_shader_hash;
  input.color_write_mask = 0xF;
  input.has_color_target = true;
  input.has_pixel_shader = true;
  input.depth_enabled = true;
  input.depth_write_enabled = true;
  return BuildNativePassKey(input);
}

NativePassObservation MakeObservation(
    uint32_t command_index, const NativePassKey& key,
    NativePassClassification classification = NativePassClassification::kKnown) {
  NativePassObservation observation{};
  observation.command_index = command_index;
  observation.coarse_range = 7;
  observation.key = key;
  observation.classification = classification;
  observation.draw_count = 1;
  observation.primitive_count = 4;
  return observation;
}

NativeResolvedPassRegion MakeSlowRegion(uint32_t command, uint64_t pixel_shader_hash,
                                        uint64_t elapsed_nanoseconds) {
  NativeResolvedPassRegion resolved{};
  resolved.region.command_start = command;
  resolved.region.command_end = command;
  resolved.region.key = MakePassKey(pixel_shader_hash);
  resolved.region.classification = NativePassClassification::kUnknown;
  resolved.region.pass_key_count = 1;
  resolved.elapsed_ticks = elapsed_nanoseconds;
  resolved.elapsed_nanoseconds = elapsed_nanoseconds;
  resolved.available = true;
  return resolved;
}

}  // namespace

TEST_CASE("GTA IV native pass keys and depth blend classes are deterministic") {
  NativePassKeyInput input{};
  input.command_class = NativePassCommandClass::kDrawIndexed;
  input.color_targets[0] = {0x4010, 12, 44, 4};
  input.depth_target = {0x5010, 13, 129, 4};
  input.reflection_family = 2;
  input.render_phase = 4;
  input.shader_family = 0xCAFE;
  input.vertex_shader_hash = 0x123456789ABCDEF0ull;
  input.pixel_shader_hash = 0x0FEDCBA987654321ull;
  input.color_write_mask = 0xF;
  input.has_color_target = true;
  input.has_pixel_shader = true;
  input.depth_enabled = true;
  input.depth_write_enabled = true;

  const NativePassKey first = BuildNativePassKey(input);
  const NativePassKey second = BuildNativePassKey(input);
  CHECK(first == second);
  CHECK(first.depth_blend_class == NativeDepthBlendClass::kOpaque);

  input.blend_enabled = true;
  CHECK(BuildNativePassKey(input).depth_blend_class == NativeDepthBlendClass::kBlended);
  input.color_write_mask = 0;
  CHECK(BuildNativePassKey(input).depth_blend_class == NativeDepthBlendClass::kDepthOnlyWrite);
  input.depth_write_enabled = false;
  CHECK(BuildNativePassKey(input).depth_blend_class == NativeDepthBlendClass::kDepthOnlyRead);
  input.command_class = NativePassCommandClass::kResolve;
  CHECK(BuildNativePassKey(input).depth_blend_class == NativeDepthBlendClass::kTransfer);

  input.command_class = NativePassCommandClass::kDrawIndexed;
  input.color_targets[0].format = 45;
  const NativePassKey different_format = BuildNativePassKey(input);
  CHECK(different_format != first);
  CHECK(NativePassKeyLess(first, different_format) != NativePassKeyLess(different_format, first));

  // Python-verified topology results for 12 input elements:
  // point=12, lines=6, line-strip=11, triangles=4, triangle-strip=10,
  // rectangles=4, line-loop=12, quads=3, quad-strip=5.
  CHECK(EstimateNativePrimitiveCount(0x01, 12) == 12);
  CHECK(EstimateNativePrimitiveCount(0x02, 12) == 6);
  CHECK(EstimateNativePrimitiveCount(0x03, 12) == 11);
  CHECK(EstimateNativePrimitiveCount(0x04, 12) == 4);
  CHECK(EstimateNativePrimitiveCount(0x06, 12) == 10);
  CHECK(EstimateNativePrimitiveCount(0x08, 12) == 4);
  CHECK(EstimateNativePrimitiveCount(0x0C, 12) == 12);
  CHECK(EstimateNativePrimitiveCount(0x0D, 12) == 3);
  CHECK(EstimateNativePrimitiveCount(0x0E, 12) == 5);
}

TEST_CASE("GTA IV native pass grouping combines only contiguous compatible commands") {
  const NativePassKey pass_a = MakePassKey(0xAA);
  const NativePassKey pass_b = MakePassKey(0xBB);
  const std::array observations = {
      MakeObservation(10, pass_a),
      MakeObservation(11, pass_a),
      MakeObservation(12, pass_b, NativePassClassification::kOther),
      MakeObservation(15, pass_a),
  };

  const std::vector<NativePassGroup> groups = GroupNativePassObservations(observations);
  REQUIRE(groups.size() == 3);
  CHECK(groups[0].command_start == 10);
  CHECK(groups[0].command_end == 11);
  CHECK(groups[0].draw_count == 2);
  CHECK(groups[0].primitive_count == 8);
  CHECK(groups[1].command_start == 12);
  CHECK(groups[1].classification == NativePassClassification::kOther);
  CHECK(groups[2].command_start == 15);
  CHECK(groups[2].key == pass_a);
}

TEST_CASE("GTA IV native attribution budget coalesces detail without dropping accounting") {
  const NativePassKey pass_a = MakePassKey(0xAA);
  const NativePassKey pass_b = MakePassKey(0xBB);
  std::vector<NativePassObservation> observations;
  observations.reserve(70);
  for (uint32_t index = 0; index < 70; ++index) {
    NativePassObservation observation = MakeObservation(100 + index, index & 1 ? pass_b : pass_a);
    observation.primitive_count = 3;
    observations.push_back(observation);
  }

  NativeAttributionPlanBudget budget{};
  budget.regular_detail_boundaries = 2;
  budget.drilldown_boundaries = 0;
  const NativeAttributionPlan plan = BuildNativeAttributionPlan(observations, budget);
  const NativeAttributionCoverage coverage = CheckNativeAttributionCoverage(observations, plan);

  CHECK(plan.regular_detail_boundaries == 2);
  CHECK(plan.drilldown_boundaries == 0);
  REQUIRE(plan.regions.size() == 3);
  CHECK(plan.regions.back().detail == NativePassRegionDetail::kCoalesced);
  CHECK(coverage.complete);
  CHECK(coverage.observation_count == 70);
  CHECK(coverage.source_draw_count == 70);
  CHECK(coverage.planned_draw_count == 70);
  // Python verification: 70 observations * 3 primitives = 210.
  CHECK(coverage.source_primitive_count == 210);
  CHECK(coverage.planned_primitive_count == 210);
}

TEST_CASE("GTA IV native coarse transitions reuse flat boundaries without consuming detail") {
  const NativePassKey pass_a = MakePassKey(0xAA);
  const NativePassKey pass_b = MakePassKey(0xBB);
  std::vector<NativePassObservation> observations;
  observations.reserve(200);
  for (uint32_t index = 0; index < 200; ++index) {
    NativePassObservation observation = MakeObservation(300 + index, index & 1 ? pass_b : pass_a);
    observation.coarse_range = index & 1;
    observations.push_back(observation);
  }

  const NativeAttributionPlan plan = BuildNativeAttributionPlan(observations);
  const NativeAttributionCoverage coverage = CheckNativeAttributionCoverage(observations, plan);
  REQUIRE(plan.regions.size() == 200);
  CHECK(plan.regular_detail_boundaries == 0);
  CHECK(plan.drilldown_boundaries == 0);
  CHECK(plan.regular_detail_boundaries + plan.drilldown_boundaries <=
        kMaximumExtraDetailBoundaries);
  CHECK(coverage.complete);
  CHECK(coverage.source_draw_count == 200);
  CHECK(coverage.planned_draw_count == 200);
}

TEST_CASE("GTA IV native mixed query budget reserves every mandatory coarse boundary") {
  const NativePassKey pass_a = MakePassKey(0xAA);
  const NativePassKey pass_b = MakePassKey(0xBB);
  std::vector<NativePassObservation> observations;
  observations.reserve(330);
  for (uint32_t index = 0; index < 230; ++index) {
    NativePassObservation observation = MakeObservation(900 + index, index & 1 ? pass_b : pass_a);
    observation.coarse_range = index & 1;
    observations.push_back(observation);
  }
  for (uint32_t index = 230; index < 330; ++index) {
    NativePassObservation observation = MakeObservation(900 + index, index & 1 ? pass_b : pass_a);
    observation.coarse_range = 1;
    observations.push_back(observation);
  }

  NativeAttributionQueryBudgetInput input{};
  input.query_capacity = 256;
  const NativeAttributionQueryBudget query_budget =
      CalculateNativeAttributionQueryBudget(observations, input);
  const NativeAttributionPlan plan =
      BuildNativeAttributionPlan(observations, query_budget.plan_budget);
  const NativeAttributionCoverage coverage = CheckNativeAttributionCoverage(observations, plan);

  // Python verification: 230 mandatory coarse boundaries + 2 frame
  // endpoints leave 24 detail queries, for exactly 256 planned queries.
  CHECK(query_budget.mandatory_boundaries_fit);
  CHECK(query_budget.mandatory_coarse_boundaries == 230);
  CHECK(query_budget.mandatory_attribution_boundaries == 0);
  CHECK(query_budget.available_detail_boundaries == 24);
  CHECK(query_budget.maximum_detail_boundaries == 24);
  CHECK(plan.regular_detail_boundaries == 24);
  CHECK(plan.drilldown_boundaries == 0);
  CHECK(query_budget.maximum_planned_query_count == input.query_capacity);
  CHECK(query_budget.required_query_count + plan.regular_detail_boundaries +
            plan.drilldown_boundaries <=
        input.query_capacity);
  CHECK(plan.regular_detail_boundaries + plan.drilldown_boundaries <=
        kMaximumExtraDetailBoundaries);
  CHECK(coverage.complete);
  CHECK(coverage.source_draw_count == 330);
  CHECK(coverage.planned_draw_count == 330);

  const std::array same_range_observations = {MakeObservation(1400, pass_a)};
  NativeAttributionQueryBudgetInput same_range_input{};
  same_range_input.query_capacity = 3;
  same_range_input.current_coarse_range = same_range_observations.front().coarse_range;
  const NativeAttributionQueryBudget same_range_budget =
      CalculateNativeAttributionQueryBudget(same_range_observations, same_range_input);
  // Python verification: one begin + one end leave one query for the initial
  // same-range attribution boundary and zero further plan boundaries.
  CHECK(same_range_budget.mandatory_coarse_boundaries == 0);
  CHECK(same_range_budget.mandatory_attribution_boundaries == 1);
  CHECK(same_range_budget.maximum_detail_boundaries == 1);
  CHECK(same_range_budget.available_detail_boundaries == 0);
  CHECK(same_range_budget.maximum_planned_query_count == 3);

  same_range_input.query_capacity = 1;
  const NativeAttributionQueryBudget overflow_budget =
      CalculateNativeAttributionQueryBudget(same_range_observations, same_range_input);
  CHECK_FALSE(overflow_budget.mandatory_boundaries_fit);
  CHECK(overflow_budget.maximum_detail_boundaries == 0);
}

TEST_CASE("GTA IV native attribution clamps hostile budgets to the hard query allowance") {
  const NativePassKey pass_a = MakePassKey(0xAA);
  const NativePassKey pass_b = MakePassKey(0xBB);
  std::vector<NativePassObservation> observations;
  observations.reserve(200);
  for (uint32_t index = 0; index < 200; ++index) {
    observations.push_back(MakeObservation(600 + index, index & 1 ? pass_b : pass_a));
  }
  NativeAttributionPlanBudget hostile_budget{};
  hostile_budget.regular_detail_boundaries = UINT32_MAX;
  hostile_budget.drilldown_boundaries = UINT32_MAX;
  hostile_budget.total_detail_boundaries = UINT32_MAX;
  const NativeAttributionPlan plan = BuildNativeAttributionPlan(observations, hostile_budget);
  CHECK(plan.regular_detail_boundaries == kRegularDetailBoundaryBudget);
  CHECK(plan.drilldown_boundaries == 0);
  CHECK(plan.regular_detail_boundaries + plan.drilldown_boundaries <=
        kMaximumExtraDetailBoundaries);
  CHECK(CheckNativeAttributionCoverage(observations, plan).complete);
}

TEST_CASE("GTA IV native slow unknown pass targets only a bounded next capture and expires") {
  NativeDrilldownScheduler scheduler;
  const NativeResolvedPassRegion slow = MakeSlowRegion(30, 0xAA, 3'000'000);
  scheduler.ObserveCompleted(10, 1, std::span(&slow, 1));
  CHECK(scheduler.pending_count() == 1);
  CHECK_FALSE(scheduler.Acquire(10, 100));

  const auto reservation = scheduler.Acquire(11, 101);
  REQUIRE(reservation);
  CHECK(scheduler.in_flight_count() == 1);
  CHECK_FALSE(scheduler.Acquire(12, 102));

  const std::array observations = {
      MakeObservation(40, slow.region.key, NativePassClassification::kUnknown),
      MakeObservation(41, slow.region.key, NativePassClassification::kUnknown),
      MakeObservation(42, slow.region.key, NativePassClassification::kUnknown),
  };
  const NativeDrilldownTarget target{reservation->target_id, reservation->key};
  const NativeAttributionPlan plan = BuildNativeAttributionPlan(observations, {}, target);
  CHECK(plan.target_matched);
  CHECK(plan.drilldown_boundaries == 2);
  REQUIRE(plan.regions.size() == 3);
  CHECK(plan.regions[0].detail == NativePassRegionDetail::kDrilldown);
  CHECK(plan.regions[1].detail == NativePassRegionDetail::kDrilldown);
  CHECK(plan.regions[2].detail == NativePassRegionDetail::kDrilldown);
  CHECK(scheduler.Complete(reservation->target_id, reservation->reservation_id, 12, true));
  CHECK(scheduler.pending_count() == 0);

  NativeDrilldownScheduler expiring_scheduler;
  expiring_scheduler.ObserveCompleted(10, 1, std::span(&slow, 1));
  // Python verification for the eight-frame TTL: frame 18 is inclusive and
  // frame 19 is the first expired frame.
  expiring_scheduler.Expire(18);
  CHECK(expiring_scheduler.pending_count() == 1);
  expiring_scheduler.Expire(19);
  CHECK(expiring_scheduler.pending_count() == 0);
}

TEST_CASE("GTA IV native drilldown scheduler evicts and selects deterministically") {
  NativeDrilldownScheduler scheduler;
  std::array<NativeResolvedPassRegion, 5> candidates = {
      MakeSlowRegion(50, 0xA0, 3'000'000), MakeSlowRegion(51, 0xA1, 4'000'000),
      MakeSlowRegion(52, 0xA2, 5'000'000), MakeSlowRegion(53, 0xA3, 6'000'000),
      MakeSlowRegion(54, 0xA4, 7'000'000),
  };
  scheduler.ObserveCompleted(20, 1, candidates);
  CHECK(scheduler.pending_count() == kMaximumPendingDrilldownTargets);

  const auto reservation = scheduler.Acquire(21, 200);
  REQUIRE(reservation);
  CHECK(reservation->key.pixel_shader_hash == 0xA4);
}

TEST_CASE("GTA IV native attribution completion preserves sequence and slot ownership") {
  NativeAttributionCompletionQueue completed;
  NativeCompletedAttributionFrame second{};
  second.frame = 42;
  second.sequence = 2;
  second.publish = true;
  second.regions.push_back(MakeSlowRegion(20, 0xBB, 4'000'000));
  REQUIRE(completed.Complete(1, std::move(second)));
  CHECK(completed.occupied(1));

  NativeCompletedAttributionFrame overwrite{};
  overwrite.frame = 43;
  overwrite.sequence = 3;
  CHECK_FALSE(completed.Complete(1, std::move(overwrite)));

  NativeCompletedAttributionFrame output{};
  CHECK_FALSE(completed.PopNext(1, &output));

  NativeCompletedAttributionFrame first{};
  first.frame = 41;
  first.sequence = 1;
  first.publish = true;
  first.regions.push_back(MakeSlowRegion(10, 0xAA, 3'000'000));
  REQUIRE(completed.Complete(0, std::move(first)));
  REQUIRE(completed.PopNext(1, &output));
  CHECK(output.frame == 41);
  REQUIRE(output.regions.size() == 1);
  CHECK(output.regions[0].region.key.pixel_shader_hash == 0xAA);
  REQUIRE(completed.PopNext(2, &output));
  CHECK(output.frame == 42);
  CHECK(completed.size() == 0);
}

TEST_CASE("GTA IV native detail ticks never add to the flat GPU envelope") {
  std::array<NativeResolvedPassRegion, 3> details{};
  details[0].available = true;
  details[0].elapsed_ticks = 40;
  details[1].available = true;
  details[1].elapsed_ticks = 60;
  details[2].available = false;
  details[2].elapsed_ticks = 999;

  const NativeAttributionAccounting accounting = CalculateNativeAttributionAccounting(100, details);
  CHECK(accounting.flat_envelope_ticks == 100);
  CHECK(accounting.detail_ticks == 100);
  CHECK(accounting.exported_total_ticks == 100);
  CHECK(accounting.exported_total_ticks !=
        accounting.flat_envelope_ticks + accounting.detail_ticks);
}

}  // namespace rex::graphics::gta4_native::attribution
