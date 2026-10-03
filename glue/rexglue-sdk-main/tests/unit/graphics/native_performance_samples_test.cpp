#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "graphics/gta4_native/native_performance_samples.h"

namespace rex::graphics::gta4_native::performance {
namespace {

TEST_CASE("GTA IV native performance ranges reject stale and duplicate tokens") {
  FrameBuilder builder;
  builder.Begin(7);
  const GpuSpanToken token = builder.BeginGpuRange(GpuRange::kDepthCopy, 11, 0, 1);
  REQUIRE(token);
  CHECK(builder.EndGpuRange(token));
  CHECK_FALSE(builder.EndGpuRange(token));

  builder.Begin(8);
  CHECK_FALSE(builder.EndGpuRange(token));
  CHECK(builder.sample().frame == 8);
}

TEST_CASE("GTA IV native performance ranges enforce query and nesting order") {
  FrameBuilder builder;
  builder.Begin(17);
  const GpuSpanToken parent = builder.BeginGpuRange(GpuRange::kFrame, 0, 0, 1);
  const GpuSpanToken child = builder.BeginGpuRange(GpuRange::kDepthCopy, 1, 2, 3);
  REQUIRE(parent);
  REQUIRE(child);
  CHECK_FALSE(builder.EndGpuRange(parent));
  CHECK(builder.EndGpuRange(child));
  CHECK(builder.EndGpuRange(parent));
  CHECK_FALSE(builder.BeginGpuRange(GpuRange::kComposite, 2, 2, 3));
  CHECK_FALSE(builder.BeginGpuRange(GpuRange::kComposite, 2, 4, 4));
  CHECK_FALSE(builder.BeginGpuRange(GpuRange::kComposite, 2, 4, kMaximumGpuQueriesPerFrame));
}

TEST_CASE("GTA IV native performance ranges aggregate only available ended spans") {
  FrameBuilder builder;
  builder.Begin(23);
  const GpuSpanToken available = builder.BeginGpuRange(GpuRange::kDeferredLightVolumes, 41, 0, 1);
  REQUIRE(builder.EndGpuRange(available));
  const GpuSpanToken unavailable = builder.BeginGpuRange(GpuRange::kDeferredLightVolumes, 47, 2, 3);
  REQUIRE(builder.EndGpuRange(unavailable));
  const GpuSpanToken incomplete =
      builder.BeginGpuRange(GpuRange::kTranslucentWaterSurface, 53, 4, 5);
  CHECK(builder.ResolveGpuRange(available, 100, true));
  CHECK(builder.ResolveGpuRange(unavailable, 200, false));
  CHECK_FALSE(builder.ResolveGpuRange(unavailable, 200, false));
  CHECK_FALSE(builder.ResolveGpuRange(incomplete, 300, true));

  const FrameSample& sample = builder.sample();
  CHECK(sample.gpu_ticks[size_t(GpuRange::kDeferredLightVolumes)] == 100);
  CHECK(sample.gpu_range_counts[size_t(GpuRange::kDeferredLightVolumes)] == 1);
  CHECK(sample.gpu_ticks[size_t(GpuRange::kTranslucentWaterSurface)] == 0);
  CHECK(sample.counters[size_t(Counter::kUnavailableGpuRanges)] == 1);
}

TEST_CASE("GTA IV native performance frame zero remains inactive") {
  FrameBuilder builder;
  builder.Begin(0);
  CHECK_FALSE(builder.active());
  CHECK_FALSE(builder.BeginGpuRange(GpuRange::kFrame, 0, 0, 1));
  CHECK(builder.sample().frame == 0);
}

TEST_CASE("GTA IV native performance timestamp deltas mask and wrap") {
  uint64_t delta = 0;
  REQUIRE(CalculateTimestampDelta(0xFFFFFFF8u, 0x5u, 32, &delta));
  CHECK(delta == 13);
  REQUIRE(CalculateTimestampDelta(UINT64_MAX - 7, 5, 64, &delta));
  CHECK(delta == 13);
  REQUIRE(CalculateTimestampDelta(1, 0, 1, &delta));
  CHECK(delta == 1);
  REQUIRE(CalculateTimestampDelta((uint64_t(1) << 63) - 3, 1, 63, &delta));
  CHECK(delta == 4);
  REQUIRE(CalculateTimestampDelta(0x100000005ull, 0x200000009ull, 32, &delta));
  CHECK(delta == 4);
  REQUIRE(CalculateTimestampDelta(9, 9, 32, &delta));
  CHECK(delta == 0);
  CHECK_FALSE(CalculateTimestampDelta(0, 1, 0, &delta));
  CHECK_FALSE(CalculateTimestampDelta(0, 1, 65, &delta));
  CHECK_FALSE(CalculateTimestampDelta(0, 1, 32, nullptr));
}

TEST_CASE("GTA IV native performance finalize counts unresolved spans once") {
  FrameBuilder builder;
  builder.Begin(55);
  const GpuSpanToken incomplete = builder.BeginGpuRange(GpuRange::kComposite, 77, 0, 1);
  REQUIRE(incomplete);
  const FrameSample& sample = builder.Finish();
  CHECK(sample.counters[size_t(Counter::kDroppedGpuRanges)] == 1);
  CHECK_FALSE(builder.ResolveGpuRange(incomplete, 500, true));
  CHECK(builder.Finish().counters[size_t(Counter::kDroppedGpuRanges)] == 1);
}

TEST_CASE("GTA IV native performance range names cover reflection families") {
  CHECK(std::string_view(GpuRangeName(GpuRange::kMirrorReflections)) == "mirror-reflections");
  CHECK(std::string_view(GpuRangeName(GpuRange::kWaterReflections)) == "water-reflections");
  CHECK(std::string_view(GpuRangeName(GpuRange::kEnvironmentReflections)) ==
        "environment-reflections");
}

TEST_CASE("GTA IV native performance keeps water surface and texture separate") {
  CHECK(std::string_view(GpuRangeName(GpuRange::kTranslucentWaterSurface)) ==
        "translucent-water-surface");
  CHECK(std::string_view(GpuRangeName(GpuRange::kTranslucentWaterTexture)) ==
        "translucent-water-texture");
}

TEST_CASE("GTA IV native performance ring overwrites oldest samples deterministically") {
  FrameSampleRing ring;
  FrameSample sample{};
  for (size_t index = 0; index < ring.capacity(); ++index) {
    sample.frame = uint32_t(index + 1);
    ring.Push(sample);
  }
  REQUIRE(ring.size() == ring.capacity());
  FrameSample copied{};
  REQUIRE(ring.CopyOldest(0, &copied));
  CHECK(copied.frame == 1);
  REQUIRE(ring.CopyNewest(0, &copied));
  CHECK(copied.frame == ring.capacity());

  sample.frame = 9001;
  ring.Push(sample);
  CHECK(ring.size() == ring.capacity());
  REQUIRE(ring.CopyOldest(0, &copied));
  CHECK(copied.frame == 2);
  REQUIRE(ring.CopyNewest(0, &copied));
  CHECK(copied.frame == 9001);
  CHECK_FALSE(ring.CopyNewest(0, nullptr));
  CHECK_FALSE(ring.CopyOldest(ring.size(), &copied));
}

TEST_CASE("GTA IV native performance CPU ranges and counters aggregate without allocation") {
  FrameBuilder builder;
  builder.Begin(99);
  builder.AddCpuRange(CpuRange::kCommandRecording, 17);
  builder.AddCpuRange(CpuRange::kCommandRecording, 19);
  builder.AddCounter(Counter::kUploadBytes, 130);
  builder.AddCounter(Counter::kUploadBytes, 26);
  builder.SetCounter(Counter::kSurfaceImagesLive, 12);
  builder.SetCounter(Counter::kSurfaceImagesLive, 9);

  const FrameSample& sample = builder.sample();
  CHECK(sample.cpu_ticks[size_t(CpuRange::kCommandRecording)] == 36);
  CHECK(sample.cpu_range_counts[size_t(CpuRange::kCommandRecording)] == 2);
  CHECK(sample.counters[size_t(Counter::kUploadBytes)] == 156);
  CHECK(sample.counters[size_t(Counter::kSurfaceImagesLive)] == 9);
}

TEST_CASE("GTA IV native performance housekeeping detail leaves parent totals independent") {
  FrameBuilder builder;
  builder.Begin(99);
  builder.AddCpuRange(CpuRange::kHousekeeping, 61);
  builder.AddCpuRange(CpuRange::kHousekeepingSurfaceRelease, 7);
  builder.AddCpuRange(CpuRange::kHousekeepingTextureReclamation, 4);
  builder.AddCpuRange(CpuRange::kHousekeepingTextureRetirement, 19);
  builder.AddCpuRange(CpuRange::kHousekeepingBufferReclamation, 20);
  builder.AddCpuRange(CpuRange::kHousekeepingPersistentBufferReclamation, 11);
  builder.AddCounter(Counter::kPipelineRequestReuses, 3);
  const FrameSample sample = builder.Finish();
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeeping)] == 61);
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeepingSurfaceRelease)] == 7);
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeepingTextureReclamation)] == 4);
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeepingTextureRetirement)] == 19);
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeepingBufferReclamation)] == 20);
  CHECK(sample.cpu_ticks[size_t(CpuRange::kHousekeepingPersistentBufferReclamation)] == 11);
  CHECK(sample.counters[size_t(Counter::kPipelineRequestReuses)] == 3);
  builder.Begin(100);
  CHECK(builder.sample().cpu_ticks[size_t(CpuRange::kHousekeepingTextureRetirement)] == 0);
  CHECK(builder.sample().counters[size_t(Counter::kPipelineRequestReuses)] == 0);
  CHECK(std::string_view(CpuRangeName(CpuRange::kHousekeepingPersistentBufferReclamation)) ==
        "housekeeping-persistent-buffer-reclamation");
  CHECK(std::string_view(CounterName(Counter::kPipelineRequestReuses)) == "pipeline-request-reuses");
}

TEST_CASE("GTA IV native performance exclusive GPU slices aggregate additively") {
  FrameBuilder builder;
  builder.Begin(101);
  builder.AddGpuRange(GpuRange::kFrame, 300);
  builder.AddGpuRange(GpuRange::kOpaqueControl, 110);
  builder.AddGpuRange(GpuRange::kComposite, 70);
  builder.AddGpuRange(GpuRange::kPresent, 90);
  builder.AddGpuRange(GpuRange::kUnattributed, 30);
  builder.AddGpuRange(GpuRange::kSmaaEdges, 25, false);

  const FrameSample& sample = builder.sample();
  CHECK(sample.gpu_ticks[size_t(GpuRange::kFrame)] == 300);
  CHECK(sample.gpu_ticks[size_t(GpuRange::kOpaqueControl)] == 110);
  CHECK(sample.gpu_ticks[size_t(GpuRange::kComposite)] == 70);
  CHECK(sample.gpu_ticks[size_t(GpuRange::kPresent)] == 90);
  CHECK(sample.gpu_ticks[size_t(GpuRange::kUnattributed)] == 30);
  CHECK(sample.gpu_range_counts[size_t(GpuRange::kSmaaEdges)] == 0);
  CHECK(sample.counters[size_t(Counter::kUnavailableGpuRanges)] == 1);
}

TEST_CASE("GTA IV native performance capture stores six hundred consecutive samples") {
  FrameSampleRing ring;
  CHECK(ring.capacity() == 600);
  FrameSample sample{};
  for (uint32_t frame = 1; frame <= 600; ++frame) {
    sample.frame = frame;
    ring.Push(sample);
  }
  REQUIRE(ring.size() == 600);
  FrameSample copied{};
  REQUIRE(ring.CopyOldest(0, &copied));
  CHECK(copied.frame == 1);
  REQUIRE(ring.CopyNewest(0, &copied));
  CHECK(copied.frame == 600);
}

TEST_CASE("GTA IV native performance ring clears without exposing stale samples") {
  FrameSampleRing ring;
  FrameSample sample{};
  sample.frame = 41;
  ring.Push(sample);
  REQUIRE(ring.size() == 1);

  ring.Clear();
  CHECK(ring.empty());
  CHECK(ring.size() == 0);
  CHECK_FALSE(ring.CopyOldest(0, &sample));
  CHECK_FALSE(ring.CopyNewest(0, &sample));

  sample.frame = 42;
  ring.Push(sample);
  REQUIRE(ring.CopyOldest(0, &sample));
  CHECK(sample.frame == 42);
}

TEST_CASE("GTA IV native completed slot samples publish in capture order without overwrite") {
  FrameSampleCompletionQueue completed;
  FrameSample slot_zero{};
  slot_zero.frame = 41;
  slot_zero.counters[size_t(Counter::kUploadBytes)] = 4100;
  FrameSample slot_one{};
  slot_one.frame = 42;
  slot_one.counters[size_t(Counter::kUploadBytes)] = 4200;

  REQUIRE(completed.Complete(1, 2, true, slot_one));
  CHECK(completed.occupied(1));
  CHECK_FALSE(completed.Complete(1, 3, true, slot_zero));
  CHECK_FALSE(completed.Complete(0, 2, true, slot_zero));
  bool publish = false;
  FrameSample published{};
  CHECK_FALSE(completed.PopNext(1, &publish, &published));

  REQUIRE(completed.Complete(0, 1, true, slot_zero));
  REQUIRE(completed.PopNext(1, &publish, &published));
  CHECK(publish);
  CHECK(published.frame == 41);
  CHECK(published.counters[size_t(Counter::kUploadBytes)] == 4100);
  REQUIRE(completed.PopNext(2, &publish, &published));
  CHECK(publish);
  CHECK(published.frame == 42);
  CHECK(published.counters[size_t(Counter::kUploadBytes)] == 4200);
  CHECK(completed.size() == 0);

  slot_one.frame = 43;
  REQUIRE(completed.Complete(1, 3, true, slot_one));
  REQUIRE(completed.PopNext(3, &publish, &published));
  CHECK(published.frame == 43);
}

TEST_CASE("GTA IV native performance names cover streaming validation and memory pressure") {
  CHECK(std::string_view(CounterName(Counter::kBufferCaptureReuses)) ==
        "buffer-capture-reuses");
  CHECK(std::string_view(CounterName(Counter::kBufferShadowMismatches)) ==
        "buffer-shadow-mismatches");
  CHECK(std::string_view(CounterName(Counter::kTextureImageEvictedBytes)) ==
        "texture-image-evicted-bytes");
  CHECK(std::string_view(CounterName(Counter::kTextureAllocationRetries)) ==
        "texture-allocation-retries");
  CHECK(std::string_view(CounterName(Counter::kTextureHeapBudget)) == "texture-heap-budget");
  CHECK(std::string_view(CounterName(Counter::kProcessPhysicalFootprintBytes)) ==
        "process-physical-footprint-bytes");
  CHECK(std::string_view(CounterName(Counter::kPendingTextureReleases)) ==
        "pending-texture-releases");
  CHECK(std::string_view(CpuRangeName(CpuRange::kPresenterAcquire)) == "presenter-acquire");
  CHECK(std::string_view(CpuRangeName(CpuRange::kPresenterTotal)) == "presenter-total");
}

}  // namespace
}  // namespace rex::graphics::gta4_native::performance
