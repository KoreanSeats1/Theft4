# Lab 10: reuse command-transfer storage

The Lab 9 driving capture measured 4.35 ms/frame in worker batch transfer and
1.60 ms/frame in dispatch setup. Most slow renderer intervals had no limiter
sleep overlapping them. Lab 10 tests reducing allocator traffic inside that
command path. This is a candidate for device comparison, not a verified frame
rate fix.

## Changes

- For Lab builds, replace the worker's staging deque with 64 reusable optional
  command slots. The queue still transfers at most 64 commands, in order, and
  dispatch still processes its front. Popping destroys that command immediately;
  moved owners remain with their destination. Fallback resource scans iterate
  only live commands, including when the slot sequence wraps.
- For Lab builds, use a private unsynchronized pool for each texture-protection
  map. Queue-map operations remain under the existing queue mutex; batch-map
  operations remain on the worker. Pools recycle allocation storage, not
  protection identities. Reference multiplicity, zero-count removal, invalid
  index fallback and active/frame protection are unchanged.
- Pool storage follows peak allocation demand and is released with the index.
  It holds no texture shared pointers. An empty or reset map protects no IDs,
  even though its pool retains reusable storage. Fixed staging reserves space
  for 64 commands rather than acquiring/releasing deque blocks during transfers.

Both changes are compiled only with `THEFT4_LAB_BUILD`. The ordinary renderer
continues using its existing deque and default map allocator. Batch size,
command ABI/order, queue backpressure, frame slots, graphics settings, limiter,
VSync and pipeline behavior are unchanged. No binding-cache change is included.
This keeps the experiment focused on allocation churn.

## Checks

The new host tests compare command order with a deque through 30,000 randomized
operations; verify owner release on pop, move, clear and destruction; compare
queue/batch/active/frame protection with an independent resource scan through
10,240 commands and mid-batch frame boundaries; and cycle 307,200 different
texture generations through the pooled index. After pool warmup, the generation
test makes no further upstream allocations, retains no historical IDs, and
returns all pool memory on destruction.

Combined timing/limiter/lifetime tests passed with AddressSanitizer and
UndefinedBehaviorSanitizer: 32 cases / 3,671,357 assertions for Lab, and 31 cases /
1,823,368 assertions for the normal compile path. Twelve Lab identity/isolation
tests passed. The test source is registered in the unit-test CMake target.

A synthetic host benchmark with 160,000 large command transfers per trial
produced matching checksums. Across nine trials per variant, baseline median
was 73.216 ms and candidate median 72.595 ms; the difference is within timing
noise. This does not establish a device FPS improvement. Fixture, commands,
results and signed-build receipts live under
`out/m5-lab/validation/transfer-build10` and `out/m5-lab/artifacts`.

## Device comparison

Install only after the user's install command. First run the same route with
profiling disabled, then repeat with the existing manual 600-frame capture.
Compare interval P95/P99 and >40 ms counts, transfer/dispatch spans, queue pressure
and memory. Check textures and scene transitions for correctness. Keep Lab 9's
archived artifact as the control; do not infer improvement from the host timing
or subtract profiling cost from captured frame times. Main promotion requires
successful gameplay acceptance and review of this isolated change set.
