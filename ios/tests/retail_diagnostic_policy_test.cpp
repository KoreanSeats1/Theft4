#include "theft4_retail_mode.h"
#include "theft4_metal_profiling.h"
#include "theft4_vector_storage_pool.h"
#include <rex/diagnostics/policy.h>
#include "native_snapshot_pages.h"
#include "native_shared_frame_arena.h"
#include "native_command_recycler.h"
#include "native_deferred_cleanup.h"
#include <cassert>
#include <string_view>
struct Record {int value=0;void ResetForReuse(){value=0;}};
int main(int argc,char** argv) {
  assert(argc==2);const bool retail=std::string_view(argv[1])=="retail";
  setenv("THEFT4_RETAIL_MODE",retail?"1":"0",1);
  setenv("THEFT4_DIAGNOSTICS","1",1);
  setenv("THEFT4_DIAGNOSTIC_CAPTURE","1",1);
  setenv("THEFT4_NATIVE_CONTENT_PROBE","1",1);
  setenv("THEFT4_AUDIO_TIMING","1",1);
  setenv("THEFT4_GPU_FLIGHT_TRACE","1",1);
  setenv("THEFT4_PERFORMANCE_CAPTURE","1",1);
  setenv("THEFT4_MOTION_BLUR_TRACE","1",1);
  setenv("THEFT4_METAL_CAPTURE","1",1);
  setenv("THEFT4_METAL_PASS_PROFILING","1",1);
  setenv("THEFT4_DRAW_BOUNDS_METRICS","1",1);
  setenv("THEFT4_TRANSFER_METRICS","1",1);
  setenv("REX_GTA4_HELP_TRACE","1",1);
  setenv("REX_GTA4_BULB_SOURCE_TRACE","1",1);
  setenv("REX_GTA4_BULB_PIPELINE_TRACE","1",1);
  setenv("REX_GTA4_EMISSION_TRACE","1",1);
  setenv("REX_GTA4_EMISSION_PROBES","1",1);
  setenv("REX_GTA4_EMISSION_VARIANTS","1",1);
  setenv("REX_GTA4_EMISSION_COLOR_READBACK","1",1);
  setenv("REX_GTA4_CUTOUT_BOOLEAN_TRACE","1",1);
  setenv("REX_GTA4_GLASS_OUTPUT_TRACE","1",1);
  setenv("REX_GPU_FLIGHT_TRACE_PATH","/tmp/diagnostic-test-only",1);
  setenv("REX_AUDIO_HANDOFF_DIR","/tmp/diagnostic-test-only",1);
  setenv("THEFT4_FRAME_CAPTURE_DIR","/tmp/diagnostic-test-only",1);
  setenv("REX_GTA4_FADE_ARM_FILE","/tmp/diagnostic-test-only",1);
  setenv("REX_GTA4_EMISSION_ARM_FILE","/tmp/diagnostic-test-only",1);
  setenv("REX_GTA4_EMISSION_OUTPUT","/tmp/diagnostic-test-only",1);
  theft4_apply_retail_diagnostic_policy();
  assert(theft4_retail_mode()==retail);
  assert(std::string_view(getenv("THEFT4_DIAGNOSTICS"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_DIAGNOSTIC_CAPTURE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_NATIVE_CONTENT_PROBE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_AUDIO_TIMING"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_GPU_FLIGHT_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_PERFORMANCE_CAPTURE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_MOTION_BLUR_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_METAL_CAPTURE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_METAL_PASS_PROFILING"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_DRAW_BOUNDS_METRICS"))==(retail?"0":"1"));
  assert(std::string_view(getenv("THEFT4_TRANSFER_METRICS"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_HELP_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_BULB_SOURCE_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_BULB_PIPELINE_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_EMISSION_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_EMISSION_PROBES"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_EMISSION_VARIANTS"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_EMISSION_COLOR_READBACK"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_CUTOUT_BOOLEAN_TRACE"))==(retail?"0":"1"));
  assert(std::string_view(getenv("REX_GTA4_GLASS_OUTPUT_TRACE"))==(retail?"0":"1"));
  assert((getenv("REX_GPU_FLIGHT_TRACE_PATH")!=nullptr)==!retail);
  assert((getenv("REX_AUDIO_HANDOFF_DIR")!=nullptr)==!retail);
  assert((getenv("THEFT4_FRAME_CAPTURE_DIR")!=nullptr)==!retail);
  assert((getenv("REX_GTA4_FADE_ARM_FILE")!=nullptr)==!retail);
  assert((getenv("REX_GTA4_EMISSION_ARM_FILE")!=nullptr)==!retail);
  assert((getenv("REX_GTA4_EMISSION_OUTPUT")!=nullptr)==!retail);
  assert(rex::diagnostics::Configure(!retail,retail?"":"all"));
  for(size_t i=0;i<size_t(rex::diagnostics::Category::kCount);++i)
    assert(rex::diagnostics::IsEnabled(rex::diagnostics::Category(i))==!retail);
  assert(theft4::metal::DetailedGpuProfilingEnabled()==!retail);
  theft4::VectorStoragePool<int,int> pool(1024,8,!retail);
  auto first=pool.Acquire();first.first.reserve(8);pool.Recycle(first.first,first.second);
  auto reused=pool.Acquire();assert(reused.first.capacity()>=8);
  using namespace rex::graphics::gta4_native;
  NativeSnapshotPages<int,4> snapshots;snapshots.InitializeDiagnostics(!retail);
  auto snapshot=snapshots.Create(73);snapshots.ReleaseActive();assert(*snapshot==73);
  assert(snapshots.pages_created()==(retail?0:1));assert(snapshots.objects_created()==(retail?0:1));
  assert(retail?snapshots.live_bytes()==0:snapshots.live_bytes()>0);
  NativeSharedFrameArena<Record,4> arena(4096,!retail);
  arena.BeginBatch([](Record& r){r={};});auto pinned=arena.Acquire();pinned->value=91;
  arena.BeginBatch([](Record& r){r={};});auto other=arena.Acquire();assert(pinned->value==91);
  assert(arena.AllocatedBytes()>0);assert(arena.page_allocations==(retail?0:2));
  assert(arena.pooled_objects==(retail?0:2));
  NativeCommandRecycler<Record,4,8> recycler;recycler.InitializePayloadReuse(true,!retail);
  NativeDeferredCleanup<Record,decltype(recycler)> cleanup;cleanup.Initialize(true,4,!retail);
  std::vector<std::unique_ptr<Record>> records;
  records.push_back(std::make_unique<Record>());records.back()->value=99;
  assert(cleanup.TryStart(records,sizeof(Record),recycler));cleanup.Wait();assert(records.empty());
  assert(cleanup.started()==(retail?0:1));assert(cleanup.completed()==(retail?0:1));
  assert(retail?cleanup.cpu_ns()==0&&cleanup.wall_ns()==0:cleanup.wall_ns()>0);
  auto recycled=recycler.Acquire();assert(recycled->value==0);
  assert(recycler.RetainedAcquires()==(retail?0:1));

  assert(pool.Hits()==(retail?0:1));assert(pool.Misses()==(retail?0:1));
}
