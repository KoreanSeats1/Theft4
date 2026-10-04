#include "native_virtual_resource_registry.h"

#include <cassert>
#include <iostream>

using namespace rex::graphics::gta4_native;

int main() {
  NativeVirtualResourceRegistry producer;
  RegisterVirtualResourceCommand command;
  command.resource = 0x1000;
  command.kind = VirtualResourceKind::kSurface;
  command.wrapper = 0x2000;
  command.companion = 0x3000;
  command.guest_backing_width = command.guest_backing_height = 1;
  command.logical_width = 1280;
  command.logical_height = 720;
  command.physical_width = 640;
  command.physical_height = 360;
  command.scale_domain = VirtualResourceScaleDomain::kPrimaryScene;
  command.constructor_caller = 0x12345678;
  command.packed_depth_source = 0x4000;
  assert(producer.Register(command) == VirtualResourceRegistrationResult::kCreated);
  const auto original = *producer.Find(command.resource);
  NativeVirtualResourceJournal worker;
  assert(worker.ApplyCapturedRecord(original, false));

  // Producer advances ahead of queued draws. Worker still sees their old size.
  command.physical_width = 960;
  command.physical_height = 540;
  assert(producer.Register(command) == VirtualResourceRegistrationResult::kReplaced);
  const auto replacement = *producer.Find(command.resource);
  assert(replacement.lifetime > original.lifetime);
  assert(*worker.Latest().Find(command.resource) == original);
  assert(worker.ApplyCapturedRecord(replacement, true));
  assert(*worker.Latest().Find(command.resource) == replacement);
  assert(*worker.BatchBase().Find(command.resource) == original);
  assert(!worker.ApplyCapturedRecord(replacement, true));
  auto replay = worker.BatchBase();
  assert(replay.Find(command.resource)->physical_width == 640);
  assert(replay.ApplyCapturedRecord(replacement));
  assert(*replay.Find(command.resource) == replacement);
  assert(replay.Find(command.resource)->packed_depth_source == 0x4000);

  // Release and recreation within the SAME batch preserve its initial state.
  assert(worker.Erase(command.resource, true));
  assert(!worker.Latest().Find(command.resource));
  producer.Erase(command.resource);
  assert(producer.Register(command) == VirtualResourceRegistrationResult::kCreated);
  const auto recreated = *producer.Find(command.resource);
  assert(recreated.lifetime > replacement.lifetime);
  assert(worker.ApplyCapturedRecord(recreated, true));
  assert(*worker.BatchBase().Find(command.resource) == original);
  assert(replay.Erase(command.resource));
  assert(!replay.Find(command.resource));
  assert(replay.ApplyCapturedRecord(recreated));
  assert(*replay.Find(command.resource) == *worker.Latest().Find(command.resource));

  // Rejecting GPU work discards the batch snapshot, not CPU registrations.
  worker.FinishBatch();
  assert(*worker.BatchBase().Find(command.resource) == recreated);
  assert(!worker.ApplyCapturedRecord(recreated, true));
  command.resource = 0x5000;
  command.kind = VirtualResourceKind::kTexture;
  assert(producer.Register(command) == VirtualResourceRegistrationResult::kCreated);
  const auto other = *producer.Find(command.resource);
  assert(worker.ApplyCapturedRecord(other, false));
  assert(!worker.Erase(0xdead, true));
  auto changed = recreated;
  changed.guest_write_conflict = true;
  assert(worker.ApplyCapturedRecord(changed, true));
  // An unchanged registration did not prematurely freeze the batch base.
  assert(*worker.BatchBase().Find(other.resource) == other);
  assert(!worker.BatchBase().Find(recreated.resource)->guest_write_conflict);
  assert(worker.Latest().Find(recreated.resource)->guest_write_conflict);
  worker.FinishBatch();
  assert(worker.Erase(other.resource, false));
  assert(!worker.BatchBase().Find(other.resource));

  // Captured lifetimes remain exact and future local allocation is monotonic.
  NativeVirtualResourceRegistry copied;
  assert(copied.ApplyCapturedRecord(other));
  copied.Clear();
  command.resource = 0x6000;
  copied.Register(command);
  assert(copied.Find(command.resource)->lifetime > other.lifetime);
  worker.Clear();
  assert(!worker.Latest().Find(recreated.resource));
  assert(!worker.BatchBase().Find(recreated.resource));
  std::cout << "Virtual registrations preserve command order and rejected-batch CPU state.\n";
}
