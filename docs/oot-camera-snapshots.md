# Uniform snapshots across submission boundaries

`runtime/vk_exec.c` keeps the replay's uniform bindings in its persistent `VkExec` state. A later submission can
use a uniform buffer without issuing another `OP_BIND_UBO`. The snapshot collector in `runtime/vk_usnap.c` only
walked bind operations in the new command streams, so those inherited bindings could fall back to live memory.
CPU writes made before the GPU consumed that memory could therefore affect an earlier submitted draw.

`vk_usnap_begin` now collects the existing replay bindings as well as the bindings found in the new streams.
Range merging, the 64 KiB bound, imported-pool checks, GPU-write exclusions, invalidation on a GPU write and
fence-based arena reuse retain their existing behavior.

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
