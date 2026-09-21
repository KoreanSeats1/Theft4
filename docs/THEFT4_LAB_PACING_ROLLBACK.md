# Lab 12: retain the graph, restore Lab 10 timing

## Device feedback

Lab 11 with both Frame pacing experiment and Frame-time graph enabled was
reported to struggle to reach 30 FPS even in 2D scenes, and to stay around 28 FPS
in gameplay, with intervals consistently above the 33.3 ms target. The user
then disabled the pacing experiment while retaining the graph and reported
that performance was “much better.” This is direct user A/B evidence against
this pacing candidate. It is not a captured percentile or a GPU/CPU attribution.

The attempted read of the Lab runtime log failed when the device file service
socket closed. No usable log was recovered; no current-run instrumented timing
is claimed. Existing Lab 9 timing remains historical evidence only.

## Why this experiment can lower the cap

Lab 11's experimental path schedules the next deadline one full frame period
from the actual completion of each present submission. In a frame that reaches
the limiter early enough to wait, the next submission interval therefore adds
scheduler overshoot and submission cost to the intended period. Repeating this
rebase accumulates that overhead instead of maintaining the intended average
cadence. At 30 FPS, an illustrative extra 2.38 ms gives about 28 FPS:
1000 / (33.333 + 2.38) = 28.0. That overhead value is an illustration, not a
measurement of this run. The user comparison is consistent with this mechanism.

The original unit tests verified the deliberately strict minimum-gap behavior;
they did not establish an effective 30 FPS cap or physical display smoothness.
The candidate does not meet the user's performance objective and is withdrawn.

## Resulting behavior

- Restore the limiter, present hook, pacing capture schema, analyzer and their
  unit tests exactly to Lab 10 commit `d5f84502ef7f`.
- Remove the pacing experiment switch, preference reads/writes and launch
  environment hook. A saved On value from Lab 11 cannot reactivate the removed
  path; the obsolete key is inert and no longer consulted.
- Keep the optional saved frame-time graph and FPS toggle. Keep Lab 10's command
  transfer/storage optimizations and every unrelated existing Lab change.
- Build Theft4 Lab version 12 under `com.theft4.m5lab`. Main Theft4 is not the
  build or installation target. The user requested a fresh logging run on this build, authorizing installation
  and a diagnostic launch for that run.

## Acceptance and next steps

The expected behavior is Lab 11 with its pacing switch Off, with that failed
option removed. Confirm the same 2D scene and driving route after installation;
the retained graph should no longer show the experiment's persistent timing
penalty. This is a rollback of a regression, not a new claim of locked 30 FPS.

Do not promote a replacement limiter based only on average FPS or simulated
unit timing. Future pacing work needs actual same-route intervals and separate
submission, renderer-publication and display timestamps, plus input-latency and
thermal checks. A successful pacing implementation should become internal
behavior; only the diagnostic graph remains a user-facing toggle. Promote to
main only after device acceptance.
