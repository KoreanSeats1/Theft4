# Lab 13: shorten the command-queue critical section

## Evidence and hypothesis

The fresh Lab 12 device capture contains 599 analyzed frame intervals over
20.045 seconds. Its mean interval was 33.329 ms, but P95 was 44.859 ms and 159
intervals exceeded 40 ms. Native GPU time remained modest (13.44 ms mean,
16.48 ms P95), and CPU publication time was nearly unchanged between slow and
other intervals (12.93 versus 13.01 ms). Slow intervals instead coincided with
larger gaps outside publication and longer worker condition/queue-lock spans.
The trace also showed about 17,241 commands and 1,681 worker refills per frame
on average.

Lab 12 transferred up to 64 commands while holding `render_mutex_`. For every
texture reference in those commands it also removed a multiplicity count from
the queue index and inserted one into the worker index. Producers need the same
mutex to publish commands, so repeated hash-table work and allocator activity
inside this critical section is a plausible contributor to delivery jitter.
The capture establishes correlation, not proof; Lab 13 is an isolated test of
that mechanism.

## Candidate behavior

- Keep command order, the 64-command maximum batch, queue backpressure and
  presentation behavior unchanged.
- Under `render_mutex_`, reconcile one compact count map from the completed
  batch, then move commands only. Per-reference staging-account construction
  now runs after releasing the queue mutex.
- Keep moved references temporarily counted in the queue index. Exact resource
  queries subtract the immutable deferred batch counts and add the remaining
  worker counts, so a texture is never exposed to early retirement.
- Preserve multiplicity when producers enqueue a generation already used by
  the staged batch. Bulk reconciliation performs a complete preflight before
  mutation; any mismatch invalidates the fast index and rebuilds it from the
  exact queue scan.
- Record outside-lock protection accounting separately as
  `worker_batch_protection_ms`. `worker_batch_transfer_ms` continues to measure
  the mutex-held reconciliation and command movement. The analyzer remains
  compatible with Lab 12 captures that lack the new column.

All behavior in this experiment is compiled only with `THEFT4_LAB_BUILD`.
The normal renderer retains its existing per-reference transfer path. The main
Theft4 app, graphics presets, resolution, frame limiter, VSync and frame-time
graph behavior are unchanged.

## Host validation

The tests cover repeated and shared texture generations, producer references
added while a batch is active, exact visible protection after deferred
subtraction, atomic failure behavior, FIFO movement and owner lifetimes. Lab
and normal variants compile with AddressSanitizer and UndefinedBehaviorSanitizer.
Lab passed 33 cases / 3,600,182 assertions; normal passed 32 cases / 1,752,193
assertions. The old Lab 12 device export still parses with the updated analyzer.

## Device acceptance

Use Lab 12 as the control and Lab 13 as the candidate on the same route and
settings. First assess ordinary gameplay with profiling disabled. Then capture
600 frames and compare interval P95/P99, intervals over 40 ms, producer queue
lock time, worker mutex/condition time, mutex-held batch transfer time, the new
outside-lock protection time, queue depth and GPU time. A lower transfer span
alone is insufficient: the candidate must reduce visible stutter without worse
frame intervals, input response, texture correctness, memory behavior or scene
loading. Keep this change isolated until the device result supports promotion.
