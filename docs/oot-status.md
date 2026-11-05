# Ocarina of Time port: status and findings

Newest first. Title 0400486024DEC000, update 1.1.1, build `oot111`.

## 5 November 2026 (afternoon): the world renders on Linux

- **State:** Zuriel-Zurichi's `fix/oot-rendering-3d` (c3ad87b: NVN view formats, RGB9E5, followed-texture formats,
  integer sampled types, lowering fixes, guest setjmp/longjmp; docs/oot-rendering-investigation.md) merged over our
  memory work (8 MiB unsized storage cap, windowed shadows), without its translator patches 0022/0023. On a
  GTX 1070 the intro video plays, the save loads and the world shows at a good frame rate. The generator changed
  (setjmp), so `recomp/gen.py` must be run again before `tools/build_game.py`; the translator must be rebuilt
  (`tools/build_xlate.py`, 21 patches).
- **Why the full merge froze on entering the world:** patches 0022/0023 made a compute shader (key `66c9e4a2…`)
  enumerate all 22 uniform/storage records of its c0 bank as guarded candidates. The 495 KB of SPIR-V took the
  NVIDIA compiler 122 s and then 95 s inside `vkCreateComputePipelines` on the replay thread, with 2.6 GB of heap
  (jemalloc profile: `nvvm` / `nvGetCompilerInterface`), so the process passed 10 GB and the PC ran out of memory.
  An RTX 5080 on Windows absorbs it. Without the patches that shader is skipped again; the enumeration can come back
  with a bound on candidates or with compute pipeline builds off the replay thread.
- **Still open: the world's light is blown out and flickers.** The scene is already white in the HDR target before
  bloom and upscale (Zuriel's capture and ours). Leads: GPU-computed values whose compute passes are skipped
  (exposure, 128x72x96 light volumes: 64-bit atomics the translator rejects), and mesh draws.
- **Mesh draws are not replayed at all.** `DrawMeshTasks*` are accepted and dropped (nvn_api_nvn2.c); there is no
  `VK_EXT_mesh_shader` path, and a GTX 1070 has none. `OOT_MESH_LOG=1` found ~30 programs (4034-4095) drawing through
  `DrawMeshTasksIndirect` from the title on, with a mesh stage in the vertex slot, a fragment stage, and in some a
  third (task) stage that does not lower; their indirect arguments live in the buffers the frame's first compute
  passes write (GPU-driven culling). Replaying them needs `VK_EXT_mesh_shader` where present and task/mesh stages run
  as compute (writing vertices and an indirect draw) elsewhere.
- **The neural upscaler** (HMMA/LDSM compute stages) is still replaced by a linear blit; it ran only in the video
  scenes in our captures (the world's upscale into the HUD layer is program 109, an ordinary per-tap gather shader).
  On GPUs without tensor cores, FSR 1 (already in the runtime for the output) is the cheap stand-in; FSR 2/3 would
  need the game's motion vectors.
- Lichii b8c812b18 (#112) lowers Ocarina's 1080p upscaler gathers (LEA in the per-tap offset scan, F16 and guarded
  offsets, depth-compare gathers); most of it already came with c3ad87b.

## 5 November 2026 (morning)

- **2 fps in the world was a 1.4 GB write-back every submission.** Once gameplay starts the game creates a 2.4 GB
  CPU-cached pool (`0x1bf470000+0x98000000`, flags 0x24) and a compute pass binds a guarded NVN 2 storage buffer at
  `0x200000000` with no declared size. `bind_range` bound it to the end of the pool, 1.4 GB, marked written, so each
  submission copied 1.4 GB from the VRAM mirror back to guest memory: a ~120 ms GPU part per frame of ~650 ms, PCIe at
  ~13 GB/s, the SMs mostly idle (`nvidia-smi dmon`), which is why CPU and GPU use looked low. Storage buffers with no
  size are now capped at 8 MiB, as yuzu/Eden do (`OOT_SBO_UNSIZED_MB`, 0 = old behaviour); the bind is now 8 MiB.
- **The same pool doubled RAM and VRAM; fixed with windowed shadows.** Its first buffer use made a whole-pool shadow
  (2.4 GB of host memory, `shadow_make`) and a whole-pool VRAM mirror (2.4 GB, `mirror_create`), though the GPU uses
  one 8 MiB window of it: the process went from 5.3 to 10.6 GB and VRAM to 8.1 of 8.2 GB within ~20 s of entering the
  world. A deferred-shadow pool of `OOT_POOL_WINDOW_MB` (default 256) or more now gets 64 MiB windows every 32 MiB,
  made on first use, with no VRAM mirror (vk_memory.c "windowed shadows"). The world run (win1, 170 s with
  `TOTK_RESOLVE_CHECK=1`) made one window, held ~8.6 GB with 5 GB of the PC still free, and found no resolve-cache
  mismatch.
- The `NVN 2 program (Ampere code): no translator yet` lines start after the memory jump, not before. They come from
  `req_init` in gfx_shader.c refusing an NVN 2 program on the Maxwell path before the failure cache key exists, so
  the same draws retry (and are skipped) twice a frame. Cheap; those objects are missing on screen.
- Diagnostics: `OOT_ALLOC_LOG=<MiB>` logs each device-memory allocation of that size or more with its memory type,
  the live total per type and the host return addresses (vk_headroom.c); `OOT_BIG_POOL_LOG=1` logs the first 60 binds
  inside pools with 256 MiB or more past the address (vk_exec_draw.c `bind_range`).

## 5 November 2026 (night)

- **Running out of memory with the GPU on** (c4b436d). With the GPU the process grew past 7.4 GB in 25 s at the title
  screen (4.1 GB headless) and a 16 GB PC ran out. 4 GB of it were `/dev/nvidiactl` mappings: in shadow mode (the Linux
  default) every CPU-visible pool got a host-visible copy of its whole size when created, and the game creates ~2150
  pools, 2.9 GB of them CPU-cached texture pools (flags 0x24) the GPU only reads through images. Shadows are now made
  at a pool's first buffer use: 19 MB of them at the title, the process stays near 5.2 GB. `TOTK_SHADOW=0` (imports)
  was no help on NVIDIA: importing pins and fills the guest pages, 7 GB in 9 s.
- **Gameplay still needs ~9 GB.** Headless, the process reaches 6.9 GB once Kokiri Forest loads (boot134), ~5.3 GB of
  it guest memory the game fills while loading (a 2.6 GB jump in about 2 s). With the GPU that is ~1.5-2 GB more; the
  PC had ~10.5 GB available with Discord and Brave open (~3 GB together), and the run (boot133) was stopped by the
  watchdog at 1.5 GB available, during the load. Next: close those, or look at the GPU-side host memory (anonymous
  driver maps ~1.2 GB, 256 MiB host-visible VMA blocks).
- `tools/oot_run_watchdog.sh <name> <seconds> [ENV=...]` runs the game with screenshots, memory samples and a
  MemAvailable floor (`WD_KB`, default 2.5 GB).

- **Slow gameplay loading was the hard disk** (299f32e). The game opens thousands of files per area (Kokiri Forest
  ~8000, 2.5 GB), each at its own place in the 25 GB `romfs.bin`. On the spinning disk that holds the image it was
  seek-bound: 99% busy at 10-25 MB/s, ~45 files/s, minutes of black loading screen. Measured with `OOT_FS_STATS=1` and
  /proc/diskstats; the guest scheduler was ruled out first (`TOTK_TRACE=sched` with the GPU on: ~1 of 6 virtual cores
  busy, the loader threads 80-96% in host calls at 1% CPU). Fix: whole-file read-ahead on open, and
  `runtime/romfs_cache.c`, which copies each file the game reads to `~/.cache/oot-pc-recomp/romfs` (8 GB at most, keeps
  10 GB free; `OOT_ROMFS_CACHE=0` turns it off). Cold start to the world clock: 4.5 s instead of 16-50 s; 4500 files in
  20 s instead of ~800.
- **The endless reload of the Great Fairy Fountain files is a damaged dump.** The loader thread (main+0x504ff8 ->
  main+0xe8e80 -> open at main+0x4bbfc) reopens `Land_FairyFountain_Base_A_01.bfres.zs` and
  `CmnTex_Map_Soil_FairyFountain_Floor_D_Nrm.bntx.zs` forever. The second file's first 12 KB are zeros in the `.nup`
  itself (its SHA-256 record matches the zeros), so it never decompresses. A scan of all 102185 `.zs` files found 3
  damaged ones:
  - `Tex/CmnTex_Map_Soil_FairyFountain_Floor_D_Nrm.bntx.zs`
  - `Tex/RBake_Scene_GanonStage_G_2_1_d0_t0.bntx.zs`
  - `Tex/Rock_WaterTemple_Gate_M_01_Nrm.bntx.zs`
  `tools/oot_standin_damaged.py` finds them (streams `romfs.bin` once) and writes stand-ins to
  `<build>/romfs-patches`, which the runtime loads by itself: the game's flat normal map for the two `_Nrm` files and
  `RBake_Scene_GanondorfStage_G_2_1_d0_t0` for the bake. With them the save loads Kokiri Forest in ~40 s and the
  reloading stops. The real fix is a fresh dump of those files. Debug aids: `OOT_FS_REPEAT=1` logs the guest stack of
  the 20th open of one path.
- After loading, the game sometimes gets `LimitReached` from MapPhysicalMemory (alias ~2.8 GB + heap 6.3 GB against
  the 9 GiB pool, `TOTK_APP_POOL`, an estimate). It treats it as recoverable; not investigated further yet.
- A run with the GPU on was stopped by Claude Code when the PC ran low on memory (15.9 GB RAM): watch the game's
  footprint.

## Open work, by priority

1. The world's 3D scene: see docs/oot-rendering-investigation.md (storage/sampled view formats, lowering fixes,
   overexposed HDR scene still open).
2. The composite fills only the top-left 1706x960 of 2560x1440: the dropped neural upscaler did that scaling. The
   stand-in blit (vk_exec_draw.c `upscale_standin`) only finds compute-written sources; in gameplay the source is a
   render-pass output. `OOT_TENSOR_LOG=1` lists what each dropped stage binds.
3. 62 tessellation shaders fail lowering ("native tessellation stage buffer read mode", ISBERD 0x923).
4. Frame rate.
