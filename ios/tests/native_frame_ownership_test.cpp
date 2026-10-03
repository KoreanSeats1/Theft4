#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include "graphics/gta4_native/native_frame_resource_owners.h"
#include "graphics/gta4_native/native_command_recycler.h"
using namespace rex::graphics::gta4_native;
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; std::abort(); } } while(0)

struct Resource { int handle; int generation; };
struct Payload {
  std::vector<unsigned char> bytes;
  void ResetForReuse() { bytes.clear(); }
};

int main() {
  // Repeated draw references do not take additional resource owners, even as
  // the table rehashes. Two live generations of the same handle stay distinct.
  auto first = std::make_shared<const Resource>(Resource{7,1});
  auto second = std::make_shared<const Resource>(Resource{7,2});
  std::weak_ptr<const Resource> weak = first;
  auto page = std::make_shared<NativeFrameResourceOwners>(1);
  auto a=page->Capture(first); auto b=page->Capture(first);
  CHECK(page->size()==1); CHECK(first.use_count()==2); CHECK(a==b);
  auto c=page->Capture(second); CHECK(a!=c); CHECK(c->generation==2);
  NativeResourceView<Resource> view(a);
  CHECK(view->generation==1); CHECK(first.use_count()==2);
  auto retained=view.Share(); CHECK(first.use_count()==3);
  std::atomic<bool> started=false, stop=false;
  std::thread reader([&] {
    started=true;
    while (!stop.load()) { CHECK(a->handle==7); CHECK(b->generation==1); }
  });
  while (!started.load()) std::this_thread::yield();
  for (int i=0;i<20000;++i) page->Capture(std::make_shared<const Resource>(Resource{i,3}));
  stop=true; reader.join(); CHECK(a->generation==1);
  first.reset(); page.reset(); CHECK(!weak.expired()); CHECK(retained->generation==1);
  retained.reset(); CHECK(weak.expired());

  // Command copies made for diagnostic variants deep-copy pending bytes.
  // Once consumed, copying the draw cannot retain or reapply the old delta.
  NativeConsumedCapture<Payload> capture;
  capture=std::make_unique<Payload>(); capture->bytes={1,2,3};
  auto copied=capture; copied->bytes[0]=9; CHECK(capture->bytes[0]==1);
  auto consumed=capture.Take(); CHECK(!capture); CHECK(consumed->bytes.size()==3);
  auto empty_copy=capture; CHECK(!empty_copy);
  capture=capture; CHECK(!capture);

  // Exercise the actual producer/worker recycler exchange while owning and
  // consuming independent capture bytes. Each lease begins reset.
  NativeCommandRecycler<Payload,8,64> recycler;
  recycler.InitializePayloadReuse(true);
  std::vector<std::unique_ptr<Payload>> batch;
  for (int i=0;i<64;++i) {
    auto p=recycler.Acquire(); p->bytes.assign(64,static_cast<unsigned char>(i));
    batch.push_back(std::move(p));
  }
  std::thread cleanup([&] { recycler.RecycleExternalBatch(batch); });
  cleanup.join(); CHECK(batch.empty()); CHECK(recycler.SharedSize()==64);
  for (int i=0;i<64;++i) { auto p=recycler.Acquire(); CHECK(p->bytes.empty()); }
  CHECK(recycler.RetainedAcquires()==64);
  std::cout << "Frame ownership, generation lifetime, concurrent cell reads, capture copies and recycler checks passed.\n";
}
