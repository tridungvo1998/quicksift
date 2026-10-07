# Maximum-performance image pipeline

QuickSift no longer uses runtime CPU/RAM/GPU pressure to throttle image work.

For thumbnails and Single/Compare image loading:
- visual worker lanes use the full logical CPU count;
- image worker threads use the interactive CPU scheduling policy;
- decode admission is not reduced by runtime pressure;
- mapped-input and reusable-buffer ceilings are no longer pressure-scaled;
- retained decoder sessions and read-ahead are sized to the full logical CPU count;
- large bundled decodes are no longer serialized on low-memory tiers;
- view-image retention is fixed at 30 minutes instead of shrinking under pressure.

CPU/memory/resource observations may still exist as telemetry or as an explicit forced
emergency cleanup path, but they do not adaptively throttle normal visual decoding.

### Thumbnail viewport priority (2026-08-16)

Thumbnail mode treats the currently visible thumbnail window as a foreground visual task. Visible thumbnails are queued at `Interactive` priority in strict left-to-right, row-major order. On viewport change, queued thumbnail decode work is discarded and rebuilt from the new viewport; in-flight thumbnail work that is no longer visible is canceled at worker cancellation checkpoints. Predictive/background thumbnails remain lower priority and are repopulated only after the current visible window. Thumbnail cache writes inherit the same priority and visible thumbnail cache writes are persisted ahead of metadata and lower-tier cache work.


## Performance HUD
The Settings menu exposes a live, click-through performance overlay. It updates independently of image work and reports queue depth, active visual work, decode latency, cache usage, persistent-cache hits/misses, free RAM, and the current foreground mode.


## Shutdown policy

Shutdown is cancellation-first and handle-release-first. Background work is signaled to stop before waiting for any subsystem, queued speculative cache writes are discarded, and the persistent cache is closed without forcing a WAL checkpoint. The cache is disposable; committed WAL frames remain recoverable on the next startup. This prevents shutdown from turning into a synchronous disk-maintenance operation after the UI has disappeared.
