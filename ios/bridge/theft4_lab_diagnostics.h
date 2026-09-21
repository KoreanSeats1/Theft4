#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The interval after one native PublishFrame returns and before the next
// begins, measured only during a bounded Lab capture. Off-core includes both
// voluntary waits and scheduler delay; it is not a run-queue measurement.
typedef struct theft4_lab_guest_gap_snapshot {
  uint32_t frame;
  double wall_ms;
  double on_core_ms;
  double off_core_ms;
  bool cpu_valid;
} theft4_lab_guest_gap_snapshot;

bool rex_gta4_native_profile_guest_gap_copy(theft4_lab_guest_gap_snapshot* snapshot);

#ifdef __cplusplus
}
#endif
