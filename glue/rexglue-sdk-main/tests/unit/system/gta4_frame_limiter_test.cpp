#include <catch2/catch_test_macros.hpp>

#include "gta4_frame_limiter.h"

namespace frame_limiter = gta4::frame_limiter;

TEST_CASE("GTA IV frame limiter accepts only menu-supported rates") {
  REQUIRE(frame_limiter::IsSupportedLimit(0));
  REQUIRE(frame_limiter::IsSupportedLimit(30));
  REQUIRE(frame_limiter::IsSupportedLimit(60));
  REQUIRE(frame_limiter::IsSupportedLimit(120));
  REQUIRE_FALSE(frame_limiter::IsSupportedLimit(1));
  REQUIRE_FALSE(frame_limiter::IsSupportedLimit(144));
}

TEST_CASE("GTA IV frame limiter distributes fractional nanoseconds without drift") {
  frame_limiter::State state{};
  auto decision = frame_limiter::Plan(state, 60, 1'000);
  REQUIRE(decision.mode_changed);
  REQUIRE(decision.next_state.next_deadline_ns == 16'667'666);

  decision =
      frame_limiter::Plan(decision.next_state, 60, decision.next_state.next_deadline_ns);
  decision =
      frame_limiter::Plan(decision.next_state, 60, decision.next_state.next_deadline_ns);
  REQUIRE(decision.next_state.next_deadline_ns == 50'001'000);
}

TEST_CASE("GTA IV frame limiter waits against the persistent deadline") {
  auto first = frame_limiter::Plan({}, 30, 5'000);
  auto second = frame_limiter::Plan(first.next_state, 30, 6'000);
  REQUIRE(second.should_wait(6'000));
  REQUIRE(second.wait_until_ns == first.next_state.next_deadline_ns);
  REQUIRE_FALSE(second.late_reset);
}

TEST_CASE("GTA IV frame limiter changes rates live and disables immediately") {
  auto limited = frame_limiter::Plan({}, 30, 5'000);
  auto changed = frame_limiter::Plan(limited.next_state, 120, 6'000);
  REQUIRE(changed.mode_changed);
  REQUIRE(changed.next_state.frames_per_second == 120);
  REQUIRE(changed.wait_until_ns == 0);

  auto unlocked = frame_limiter::Plan(changed.next_state, 0, 7'000);
  REQUIRE(unlocked.mode_changed);
  REQUIRE(unlocked.next_state.frames_per_second == 0);
  REQUIRE(unlocked.next_state.next_deadline_ns == 0);
  REQUIRE_FALSE(unlocked.should_wait(7'000));
}

TEST_CASE("GTA IV frame limiter resets after a complete missed interval") {
  auto first = frame_limiter::Plan({}, 60, 1'000);
  const int64_t severely_late = first.next_state.next_deadline_ns + 20'000'000;
  auto recovered = frame_limiter::Plan(first.next_state, 60, severely_late);
  REQUIRE(recovered.late_reset);
  REQUIRE(recovered.wait_until_ns == 0);
  REQUIRE(recovered.next_state.next_deadline_ns > severely_late);
}

TEST_CASE("submission pacing rebases late frames and scheduler oversleep", "[frame-limiter]") {
  using namespace gta4::frame_limiter;
  State state;
  auto first = PlanSubmission(state, 30, 1'000'000'000);
  CHECK_FALSE(first.should_wait(1'000'000'000));
  state = first.next_state;
  CompleteSubmission(state, 1'000'000'000);
  // A 45 ms frame is immediately eligible; the next 5 ms frame cannot catch up.
  auto late = PlanSubmission(state, 30, 1'045'000'000);
  CHECK(late.late_reset);
  CHECK_FALSE(late.should_wait(1'045'000'000));
  state = late.next_state;
  CompleteSubmission(state, 1'045'000'000);
  auto early = PlanSubmission(state, 30, 1'050'000'000);
  CHECK(early.wait_until_ns == 1'078'333'333);
  state = early.next_state;
  // Oversleep is absorbed at completion, rather than shortening the next gap.
  CompleteSubmission(state, 1'081'000'000);
  CHECK(state.next_deadline_ns == 1'114'333'334);
  auto stall = PlanSubmission(state, 30, 2'000'000'000);
  CHECK_FALSE(stall.should_wait(2'000'000'000));
  CompleteSubmission(stall.next_state, 2'000'000'000);
  CHECK(stall.next_state.next_deadline_ns == 2'033'333'333);
}

TEST_CASE("submission pacing handles rates, disabling and deadline overflow", "[frame-limiter]") {
  using namespace gta4::frame_limiter;
  for (auto fps : {30u, 60u, 120u}) {
    auto decision = PlanSubmission({}, fps, 1'000'000'000);
    auto state = decision.next_state;
    auto completion = int64_t{1'000'000'000};
    for (unsigned i = 0; i < fps; ++i) {
      CompleteSubmission(state, completion);
      completion = state.next_deadline_ns;
    }
    CHECK(completion == 2'000'000'000); // Fractional nanoseconds remain exact.
    auto changed = PlanSubmission(state, fps == 30 ? 60 : 30, completion);
    CHECK(changed.mode_changed);
    CHECK_FALSE(changed.should_wait(completion));
    for (auto disabled : {0u, 17u}) {
      auto off = PlanSubmission(state, disabled, completion);
      CHECK(off.next_state.frames_per_second == 0);
      CHECK_FALSE(off.should_wait(completion));
    }
    CompleteSubmission(state, std::numeric_limits<int64_t>::max() - 1);
    CHECK(state.next_deadline_ns == std::numeric_limits<int64_t>::max());
  }
}
