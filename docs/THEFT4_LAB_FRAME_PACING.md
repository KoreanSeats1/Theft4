# Lab 11: frame-time graph and optional submission pacing

Lab 10 driving feedback was clearly better, with reported low HUD readings of
26–28 FPS, but occasional choppy motion while the averaged counter still read 30.
Coarse runtime logs cannot establish the duration or cause of those spikes.
This update adds visibility and one reversible timing experiment. It is not a
verified locked-30 fix and does not reduce scene complexity or GPU pixel cost.

## Display settings

- **Frame-time graph** is independently saved, default off. It shows the latest
  180 completed publication intervals (about six seconds at 30 FPS), a dashed
  33.3 ms target, the last interval and recent peak. Amber indicates >35 ms;
  red indicates >50 ms. A growing unfinished gap appears with a `+` suffix.
  The vertical scale expands to 200 ms; taller spikes clip at the top while the
  numeric value remains uncapped. It sits below FPS, or higher when FPS is off.
- **Frame pacing experiment** is Lab-only, independently saved, default off.
  Enable before starting the game; switching modes requires closing/relaunching
  Lab. Off retains Lab 10's original post-submit deadline limiter. On waits
  before submitting each present, then sets the next deadline from the actual
  successful submission completion. Late frames and scheduler oversleep cannot
  shorten the next submission gap to catch up. A rejected submission does not
  advance the deadline. Existing 0/30/60/120 limits and fractional ns handling
  remain supported.

The experiment serializes present producers across decision, wait, enqueue and
rebase using the existing limiter mutex. The queue consumer never takes that
mutex. It changes producer timing, not the swapchain, renderer command order,
frame slots, graphics presets, guest simulation timestep or VSync configuration.
Under load it may reduce average FPS or increase input latency. It does not
promise evenly spaced physical display refreshes: renderer/queue work can still
cause publication jitter downstream.

## Measurement and overhead

The graph samples the same successful unique-content publication hook as FPS,
after the swapchain present call. These are publication intervals, **not GPU
durations or measured physical scanout**. The HUD refreshes at 10 Hz but stores
every published interval in a fixed atomic ring. No disk writes, GPU readback,
allocation or blocking lock is added at the publication hook. Disabled sampling
performs only an atomic enabled check, without reading the clock. Backgrounding
or hiding the graph starts a new generation and discards the old history; paused
time cannot become a false gameplay spike. UIKit drawing still has some cost:
keep the graph setting identical when comparing pacing modes, then confirm the
chosen mode with the graph off.

Detailed captures append `limiter_before_submit` to the pacing CSV. The prepare
span ends before the limiter for the new mode; submit time excludes limiter
sleep. Rejected submissions still export a pre-submit wait when it happened.
The analyzer reports how many matched frames used the experiment. Raw host ticks
and steady-clock nanoseconds remain separate clock domains.

## Validation and acceptance

Host sanitizer tests: Lab 34 cases / 3,671,399 assertions; standard compile path
33 cases / 1,823,410 assertions. Added coverage exercises late frames, oversleep,
long stalls, disabled/changed rates, fractional intervals, saturation and CSV
phase attribution. The separate history test covers spikes, wraparound, bounded
copies, pending gaps, visibility epochs and concurrent publication/toggling; it
passes AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer. Twelve
Lab isolation tests pass. UI preview screenshots use explicitly synthetic timing
data and do not constitute a device performance test.

Validation artifacts and command receipts are under
`out/m5-lab/validation/graph-build11`; signed artifact receipts are under
`out/m5-lab/artifacts`. Lab remains `com.theft4.m5lab` / **Theft4 Lab**, version 11.
Main remains separate and is not installed or modified by this build.

For the device A/B test, enable the graph and drive the same route with pacing
Off, then fully close Lab, turn pacing On, and repeat. Keep output/filtering,
weather/traffic, route, warmup and device thermal conditions as similar as
possible. Compare spike height/frequency, 26–28 FPS dips and responsiveness;
include a new-area traversal. If On feels worse, switch it Off to restore the
Lab 10 timing path. Use a matching manual capture if quantifying P95/P99, then
confirm normal unprofiled play. No main promotion before gameplay acceptance.
