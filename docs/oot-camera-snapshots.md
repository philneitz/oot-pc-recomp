# Uniform snapshots across submission boundaries

`runtime/vk_exec.c` keeps the replay's uniform bindings in its persistent `VkExec` state. A later submission can
use a uniform buffer without issuing another `OP_BIND_UBO`. The snapshot collector in `runtime/vk_usnap.c` only
walked bind operations in the new command streams, so those inherited bindings could fall back to live memory.
CPU writes made before the GPU consumed that memory could therefore affect an earlier submitted draw.

`vk_usnap_begin` now collects the existing replay bindings as well as the bindings found in the new streams.
The 64 KiB bound, imported-pool checks and fence-based arena reuse retain their existing behavior.

## Regression

`oot.exe --probe-vkusnap` uses a real Vulkan pool and the production submission/recording path, without loading
game files or translating shaders. It binds a uniform buffer once, then makes two subsequent submissions with
no rebind. Both must find a snapshot; a GPU-write notification must invalidate each snapshot. Before the fix the
first inherited lookup fails. After the fix both lookups and both invalidations pass.

Private logs: `build/test/usnap-inherited-before.log`, `usnap-inherited-after.log` and
`usnap-inherited-validation.log`.

## What this establishes

This repairs a demonstrated snapshot coverage hole. It does not establish that every visible camera jump is
fixed. Snapshots are still taken when replay begins, and GPU-written ranges intentionally stay live when the
copy cannot be used safely. The existing `OOT_GPU_SYNC=1` launcher setting stays in place pending controlled
gameplay validation.

The first post-startup-fix gameplay runs with GPU sync off and on both loaded the copied save and exited normally
after 120 game-clock seconds (about 141 and 144 wall-clock seconds). Their screenshots have different player/camera
poses, so their visual appearance and frame-rate samples are not a controlled A/B comparison. During the sync-off
run, 98 logged vertex-uniform snapshots differed from the current live matrix by bind time; the sync-on run had
none. This shows ongoing CPU rewrites, not that the saved snapshots were incorrect. Raw evidence is private under
`build/test/camera-study-sync0` and `camera-study-sync1`.

After the inherited-binding fix, another sync-off run with physical input and motion disabled loaded a fresh copy
of the same save and exited normally at 120 game-clock seconds (about 126 wall-clock seconds). Evidence is under
`build/test/camera-study-inherited-sync0`. Live camera bindings remain in the log, so this is not sufficient to
remove the launcher's GPU-sync workaround or to close the camera investigation.

## Adjacent uniforms and GPU writes

Snapshot allocations merge adjacent uniform ranges. Two further exclusions used that allocation boundary instead
of the individual data affected by a GPU write:

- A write during replay discarded the entire merged snapshot. `vk_usnap_drop` now keeps sorted, disjoint write
  intervals separately; `vk_usnap_find` rejects only lookups intersecting those intervals. Untouched prefixes,
  suffixes and neighboring uniforms keep their snapshots. Repeated contained writes do not change the generation.
- At the next snapshot, a still-pending write excluded an entire merged allocation. The collector now excludes
  affected bindings before merging the remaining safe ranges. A binding overlapping pending GPU writes stays live.

`--probe-vkusnap` now covers adjacent bindings, partial writes, reverse-order insertion, adjacent and overlapping
intervals, preserved gaps, and a write touching only the end of a snapshot. A second recording callback starts a
fresh snapshot before its write fence can complete, checking that the affected binding stays live while its untouched
neighbor is copied. The callbacks exercise the production snapshot functions on the real Vulkan submission path;
the write notifications are synthetic, rather than shader writes.

Before each correction its corresponding check fails; both pass after the changes. Private logs are
`build/test/usnap-adjacent-before.log`, `usnap-intervals-after.log`, `usnap-pending-before.log` and
`usnap-pending-after.log`. These tests establish snapshot coverage and invalidation, not a complete visual camera fix.

## Screenshot interpretation

The original automated images under `graphics-adjacent-before`, `graphics-adjacent-after` and
`graphics-viewport-capture` included the full presentation allocation. The last draw uses a 1706x960 viewport and
scissor in a 2560x1440 texture, and the window supplies a matching 1706x960 presentation crop. The unused right and
bottom margins in those images are therefore not evidence of an output-scaling failure. `boot.c` now applies the
window crop to both `TOTK_SHOTS` and `--shot`. The resulting images show the active game image before host scaling
and colour adjustment. A copied-save run under `build/test/graphics-cropped-shots` exits normally and confirms that
the corrected screenshots fill their image; the excessive haze remains visible.

After both adjacent-range corrections, `build/test/graphics-pending-fixed` also exits normally with GPU sync off
after 110 world-clock seconds (about 121 wall-clock seconds). The log reports zero Vulkan validation errors. Its
camera log still contains live vertex-uniform bindings, and its corrected screenshots still show excessive haze.
The test does not justify removing `OOT_GPU_SYNC=1` from the normal launcher.
