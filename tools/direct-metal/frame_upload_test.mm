#include "theft4_metal_resources.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>
using namespace theft4::metal;
using Clock=std::chrono::steady_clock;
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
nlohmann::json DelayedGpu(Renderer& renderer) {
  // Four pending batches must remain distinct while actual GPU reads are
  // deliberately blocked. The fifth admission falls back rather than reuses.
  FrameUploadPool pool(renderer,8*1024*1024);
  auto queue=[renderer.Device() newCommandQueue];auto event=[renderer.Device() newSharedEvent];
  Require(bool(event),"Missing Metal shared event");
  std::vector<id<MTLCommandBuffer>> commands;std::vector<id<MTLBuffer>> outputs;
  struct Completion {std::mutex mutex;std::condition_variable cv;size_t count=0;};
  auto completed=std::make_shared<Completion>();
  for(size_t frame=0;frame<4;++frame) {
    auto batch=pool.Acquire();Require(bool(batch),"Pending batch admission failed");
    // Cross a page boundary and verify padding/alignment as well as contents.
    std::vector<BufferView> views;
    for(size_t bank=0;bank<19;++bank) {
      std::vector<uint8_t> bytes(65520,uint8_t(frame*29+bank+1));
      auto view=batch->TryUpload(bytes);Require(view.buffer&&view.offset%256==0,"Batch upload failed");views.push_back(view);
    }
    auto output=[renderer.Device() newBufferWithLength:19*65520 options:MTLResourceStorageModeShared];
    auto command=[queue commandBuffer];[command encodeWaitForEvent:event value:1];
    auto blit=[command blitCommandEncoder];
    for(size_t bank=0;bank<views.size();++bank)
      [blit copyFromBuffer:views[bank].buffer sourceOffset:views[bank].offset toBuffer:output destinationOffset:bank*65520 size:65520];
    [blit endEncoding];
    [command addCompletedHandler:^(id<MTLCommandBuffer>) {
      (void)batch;
      std::lock_guard lock(completed->mutex);++completed->count;completed->cv.notify_all();
    }];
    [command commit];commands.push_back(command);outputs.push_back(output);
  }
  Require(!pool.Acquire(),"Reused storage before delayed GPU completion");
  ResourceCacheStats initial;pool.AddStats(initial);
  Require(initial.frame_upload_resident_bytes==8*1024*1024,"Unexpected page footprint");
  event.signaledValue=1;
  for(auto command:commands) {[command waitUntilCompleted];Require(command.status==MTLCommandBufferStatusCompleted,"Delayed GPU copy failed");}
  {std::unique_lock lock(completed->mutex);Require(completed->cv.wait_for(lock,std::chrono::seconds(5),[&]{return completed->count==4;}),"GPU callbacks did not finish");}
  commands.clear();
  for(size_t frame=0;frame<outputs.size();++frame) {
    const auto* bytes=static_cast<const uint8_t*>(outputs[frame].contents);
    for(size_t bank=0;bank<19;++bank)for(size_t i=0;i<65520;++i)
      Require(bytes[bank*65520+i]==uint8_t(frame*29+bank+1),"GPU observed overwritten upload data");
  }
  // Completion handler destruction may follow its invocation. A short bounded
  // yield here tests eventual retirement; no worker waits are added in game.
  std::shared_ptr<FrameUploadPool::Batch> reused;
  const auto deadline=Clock::now()+std::chrono::seconds(5);
  while(!(reused=pool.Acquire())&&Clock::now()<deadline)std::this_thread::yield();
  Require(bool(reused),"Completed batches did not retire");
  auto view=reused->TryUpload(std::vector<uint8_t>(65520,0xda));Require(bool(view.buffer),"Retired page reuse failed");
  ResourceCacheStats after;pool.AddStats(after);
  Require(after.frame_upload_buffer_creates==initial.frame_upload_buffer_creates,"Reuse allocated another Metal buffer");
  reused.reset();
  // CPU ownership must also delay recycling even when no command is submitted.
  std::array<std::shared_ptr<FrameUploadPool::Batch>,4> held;
  for(auto& batch:held){batch=pool.Acquire();Require(bool(batch),"Aborted batch acquire failed");}
  Require(!pool.Acquire(),"Ignored live CPU batch leases");held[0].reset();Require(bool(pool.Acquire()),"Aborted batch not recycled");
  FrameUploadPool bounded(renderer,1024*1024);auto small=bounded.Acquire();
  for(size_t i=0;i<16;++i)Require(bool(small->TryUpload(std::vector<uint8_t>(65536,0x63)).buffer),"Bounded upload unexpectedly failed");
  Require(!small->TryUpload(std::vector<uint8_t>(256)).buffer,"Exceeded fixed upload budget");
  // The completion lease must survive destruction of its adapter/pool.
  BufferView outlived;std::shared_ptr<FrameUploadPool::Batch> lease;
  {FrameUploadPool temporary(renderer,1024*1024);lease=temporary.Acquire();outlived=lease->TryUpload(std::vector<uint8_t>(256,0x7e));}
  Require(outlived.buffer&&static_cast<const uint8_t*>(outlived.buffer.contents)[outlived.offset]==0x7e,"Pool destruction invalidated lease");lease.reset();
  // Exercise the actual Frame completion hook, including an unsubmitted frame.
  std::weak_ptr<void> owner;std::string error;
  {auto frame=renderer.BeginFrame(error);auto batch=pool.Acquire();owner=batch;frame.RetainUntilCompletion(batch);batch.reset();Require(!owner.expired(),"Frame did not retain upload lease");}
  Require(owner.expired(),"Aborted frame leaked upload lease");
  auto frame=renderer.BeginFrame(error);auto batch=pool.Acquire();owner=batch;
  frame.RetainUntilCompletion(batch);batch.reset();auto receipt=frame.Submit(error);Require(bool(receipt)&&receipt.Wait(error),"Lease submission failed");
  const auto retired_by=Clock::now()+std::chrono::seconds(5);
  while(!owner.expired()&&Clock::now()<retired_by)std::this_thread::yield();
  Require(owner.expired(),"GPU-completed Frame retained its upload lease");
  return {{"delayed_gpu_batches",4},{"byte_parity",true},{"bounded_fallback",true},{"cpu_lease_protection",true},
    {"adapter_teardown_safe",true},{"frame_completion_hook",true},{"reused_metal_buffer_without_allocation",true}};
}
nlohmann::json Benchmark(Renderer& renderer,bool pooled,bool stable) {
  ResourceCache cache(renderer,384*1024*1024);FrameUploadPool pool(renderer,96*1024*1024);
  const std::array<size_t,3> sizes{512,1024,1056};std::array<std::vector<uint8_t>,3> banks;
  for(size_t i=0;i<3;++i)banks[i].resize(sizes[i],uint8_t(31+i));
  struct Hold {std::vector<std::shared_ptr<const uint64_t>> owners;std::vector<BufferView> views;std::shared_ptr<FrameUploadPool::Batch> batch;};
  std::deque<Hold> pending;std::vector<double> samples;uint64_t generation=0;std::string error;
  std::vector<std::shared_ptr<const uint64_t>> stable_owners;
  if(stable)for(size_t i=0;i<4500*3;++i)stable_owners.push_back(std::make_shared<const uint64_t>(++generation));
  uint64_t steady_creates=0,checksum=0;
  for(size_t frame=0;frame<28;++frame) {@autoreleasepool {
    Hold hold;hold.views.reserve(4500*3);hold.owners=stable_owners;
    if(!stable)for(size_t i=0;i<4500*3;++i)hold.owners.push_back(std::make_shared<const uint64_t>(++generation));
    ResourceCacheStats before=cache.Stats();pool.AddStats(before);
    const auto start=Clock::now();
    if(pooled)hold.batch=pool.Acquire();else {cache.BeginUploadBatch();cache.SweepRetired(true);}
    for(size_t draw=0;draw<4500;++draw)for(size_t bank=0;bank<3;++bank) {
      const auto& owner=hold.owners[draw*3+bank];
      auto view=pooled?hold.batch->TryUpload(banks[bank]):cache.UniformBuffer({owner,*owner,{}},banks[bank],error);
      Require(bool(view.buffer),"Benchmark upload failed");hold.views.push_back(view);
    }
    const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    ResourceCacheStats after=cache.Stats();pool.AddStats(after);
    if(frame>=8){samples.push_back(ms);steady_creates+=after.buffer_creates-before.buffer_creates;}
    // Simulate three still-owned submissions. Byte checks remain outside timing.
    for(size_t i=0;i<hold.views.size();i+=43) {
      const auto& view=hold.views[i];const auto* bytes=static_cast<const uint8_t*>(view.buffer.contents)+view.offset;
      Require(view.length==sizes[i%3]&&!std::memcmp(bytes,banks[i%3].data(),view.length),"Benchmark byte mismatch");checksum+=bytes[0];
    }
    pending.push_back(std::move(hold));if(pending.size()>3)pending.pop_front();
  }}
  std::sort(samples.begin(),samples.end());ResourceCacheStats stats=cache.Stats();pool.AddStats(stats);
  return {{"median_cpu_ms",samples[samples.size()/2]},{"p95_cpu_ms",samples[samples.size()*95/100]},
    {"steady_metal_buffer_creates",steady_creates},{"resident_bytes",stats.resident_buffer_bytes},
    {"frame_buffer_reuses",stats.frame_upload_buffer_reuses},{"checksum",checksum}};
}
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  try {@autoreleasepool {
    Renderer renderer(3);Require(renderer.Ready(),"Metal unavailable");nlohmann::json report;
    report["safety"]=DelayedGpu(renderer);
    for(size_t trial=0;trial<4;++trial)for(size_t order=0;order<4;++order) {
      const auto mode=(trial+order)%4;const bool pooled=mode&1,stable=mode&2;
      report[stable?(pooled?"stable_pooled":"stable_immutable"):(pooled?"streaming_pooled":"streaming_immutable")].push_back(Benchmark(renderer,pooled,stable));
    }
    report["draws_per_frame"]=4500;report["uploaded_bytes_per_streaming_frame"]=4500*(512+1024+1056);
    report["pending_cpu_owned_batches"]=3;report["scope"]="CPU constant upload A/B fixture and real delayed GPU lifetime validation; not game FPS";
    report["gpu"]=renderer.Device().name.UTF8String;std::ofstream(argv[1])<<report.dump(2)<<'\n';
  }}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
