# Build 95 comparison candidate

Based on merged build 94 (`b6b4a823`). Kept on `codex/frame-preparation-95` while
the direct Metal backend is developed. The main checkout and its existing edits
remain unchanged. Signed device app preserved under the main project's
`out/device-apps/build95-frame-preparation-20261002`.

This candidate prepares only the constant registers used by all consumers of a
version, retains complete-bank fallbacks for unknown/dense shader reads, shares
pipeline snapshots in lifetime-safe pages, and reuses successful texture-binding
tuples in the older cached-descriptor backend. Normal descriptor publication and
GPU fences still run. Lookup tables mix aligned pointer hashes. Lightweight
captures report partial preparation and snapshot page retention.

Validation: full signed iOS Release build; release optimization audit; five host
checks including all 1,356 embedded SPIR-V modules; four ASan/UBSan checks; three
installation-routing and two capture-default checks. Signed app verification
passes outside the restricted keychain sandbox.

Repeated host microbenchmarks (not gameplay FPS): for 5,000 sparse constant
versions, complete preparation took 0.89–0.98 ms and covered preparation took
0.27–0.31 ms, including planning. Written bytes fell from 20,558,768 to about
2.6 million. A mixed 4,800-draw correctness case used 2,516,736 bytes within the
unchanged 6,553,600-byte complete-bank capacity bound. Pipeline-sized snapshot
page timings improved in these runs. Hash mixing helped strongly aligned keys
but added tens of microseconds to already well-distributed/16-byte-aligned keys.
Small shader snapshots retain the existing allocator. The attempted delta
ownership transfer was rejected and is not included.

The candidate is not installed, merged, or verified for a gameplay FPS gain.
Covered constant uploads can be disabled with `THEFT4_MASKED_CONSTANTS=0`, pipeline
snapshot pages with `THEFT4_SNAPSHOT_PAGES=0`, and cached binding tuple reuse with
`THEFT4_CACHED_BINDING_REUSE=0` for controlled comparisons.
