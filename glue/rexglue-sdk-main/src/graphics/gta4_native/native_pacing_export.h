#pragma once
#include <rex/graphics/gta4_native/pacing_profile.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace rex::graphics::gta4_native::pacing {

// Export-thread only. Keep raw clocks alongside derived durations so consumers
// can validate frame identity, missing boundaries and clock-domain assumptions.
inline bool Export(const std::filesystem::path& directory, const Snapshot& capture,
                   uint64_t capture_id, uint64_t host_frequency) {
  if (!host_frequency || (capture.started && !capture.stopped)) return false;
  const auto path = directory / "native-performance-pacing.csv";
  const auto temporary = path.string() + ".partial";
  std::ofstream out(temporary, std::ios::trunc);
  if (!out) return false;
  out << std::setprecision(12);
  out << "capture_id,frame,system_thread,guest_thread,submitted,requested_fps,applied_fps,"
         "wait_requested,late_reset,mode_changed,hook_begin_tick,submit_begin_tick,submit_end_tick,"
         "limiter_begin_tick,mutex_begin_tick,mutex_acquired_tick,sleep_begin_tick,wake_tick,"
         "limiter_end_tick,decision_ns,prior_deadline_ns,wait_until_ns,next_deadline_ns,"
         "sleep_begin_ns,wake_ns,present_hook_prepare_ms,present_submit_ms,limiter_mutex_wait_ms,"
         "decision_wait_ms,requested_sleep_ms,actual_sleep_ms,wake_overshoot_ms,entry_lateness_ms\n";
  const long double tick_ms = 1000.0L / host_frequency;
  const auto elapsed = [](uint64_t begin, uint64_t end) {
    return end >= begin ? end - begin : 0;
  };
  const auto ns_ms = [](int64_t begin, int64_t end) {
    return end > begin ? (static_cast<long double>(end) - begin) / 1000000.0L : 0.0L;
  };
  for (const auto& s : capture.samples) {
    out << capture_id << ',' << s.frame << ',' << s.system_thread << ',' << s.guest_thread
        << ',' << s.submitted << ',' << s.requested_fps << ',' << s.applied_fps << ','
        << s.wait_requested << ',' << s.late_reset << ',' << s.mode_changed << ','
        << s.hook_begin << ',' << s.submit_begin << ',' << s.submit_end << ','
        << s.limiter_begin << ',' << s.mutex_begin << ',' << s.mutex_acquired << ','
        << s.sleep_begin << ',' << s.wake << ',' << s.limiter_end << ',' << s.decision_ns
        << ',' << s.prior_deadline_ns << ',' << s.wait_until_ns << ',' << s.next_deadline_ns
        << ',' << s.sleep_begin_ns << ',' << s.wake_ns << ','
        << elapsed(s.hook_begin, s.submit_begin) * tick_ms << ','
        << elapsed(s.submit_begin, s.submit_end) * tick_ms << ',';
    if (s.submitted) {
      out << elapsed(s.mutex_begin, s.mutex_acquired) * tick_ms << ','
          << (s.wait_requested ? ns_ms(s.decision_ns, s.wait_until_ns) : 0) << ','
          << (s.wait_requested ? ns_ms(s.sleep_begin_ns, s.wait_until_ns) : 0) << ','
          << (s.wait_requested ? ns_ms(s.sleep_begin_ns, s.wake_ns) : 0) << ','
          << (s.wait_requested ? ns_ms(s.wait_until_ns, s.wake_ns) : 0) << ','
          << (!s.mode_changed && s.prior_deadline_ns > 0
                  ? ns_ms(s.prior_deadline_ns, s.decision_ns) : 0);
    } else {
      out << ",,,,,";  // Six unavailable limiter columns for rejected submissions.
    }
    out << '\n';
  }
  out.flush();
  if (!out) return false;
  out.close();
  if (!out) return false;
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  return !error;
}
}  // namespace rex::graphics::gta4_native::pacing
