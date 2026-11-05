# Ocarina of Time rendering investigation

## Scope and current evidence

This work targets Ocarina of Time 1.1.1 on the `oot-port` branch. The menu renders and AV1 video plays, but the user reports a black 3D scene after the opening. Successful compilation and menu rendering do not establish that the 3D renderer works.

The initial live log contained 85 Ampere lowering failures, including 64 reports of `native tessellation stage buffer read mode`, and repeated downstream translation errors. These are log event counts, not counts of unique required shaders. The process continued running without a fatal error. A first offline capture contained 2,837 stages; 2,811 lowered successfully with the existing code. Reproduction subsequently captured a frozen corpus of 6,413 complete stages for the comparison below.

`runtime/gfx_shader.c`, `req_init`, rejects an unconverted NVN 2 control block with `no translator yet`. This message does not mean the translator executable is absent: `runtime/nvn_api_obj.c`, `nvnProgramSetShaders`, first attempts `nvn2_lower`, which can reject the native stage. A separate observed translator rejection concerns storage-candidate atomics limited to 32-bit integer operations.

All native shader captures, translated shader files, logs and replay packets remain local under ignored `build/oot-fix/`. They must not be committed or attached to public reports.

## Upstream branch audit

Repository: [zortos293/Lichii](https://github.com/zortos293/Lichii). Snapshot taken on 2026-11-05. Baseline: `724aede4ea51fae8224cdd02549cdbdf7ceecd25`, recorded by this checkout's `runtime/third_party/eglantine/UPSTREAM_COMMIT` before this investigation.

All 82 advertised branch tips were fetched and screened by commit identity, changed paths and Ampere directory tree identity. They contain 27 distinct Ampere source trees. The complete branch-tip inventory is in [oot-upstream-branches.csv](oot-upstream-branches.csv). This is a source inventory, not a claim that every branch was built or tested. Shallow history is insufficient to establish that a change has been merged; file comparisons are used instead.

Only changes relevant to Ocarina's observed failures are candidates for this port. Branches for unrelated games, packaging and emulator scheduling are outside the implementation scope unless a shared change can be tied to an Ocarina failure.

The 27 distinct lowering header snapshots were also screened for opcode cases absent from the baseline. None adds an explicit `0xc08` handler, one of the remaining local failures. This does not replace end-to-end testing or establish equivalence of their control-flow proofs.

| Branch | Pinned tip | Finding and decision |
| --- | --- | --- |
| `fix/oot-fog` | `82b95a65d46527f0cfad3d5661439228897fee49` | Contains newer per-tap gather offset proofs, half gathers, depth-comparison gathers, constant-buffer F2I modifiers, U32 surface reduction operations and LEA.HI base negation. The gather proofs directly match failures in the local log. Evaluate the lowering update first. Its Vulkan draw probes are diagnostic code for Lichii's renderer, not drop-in code for this runtime. |
| `fix/oot-bloom` | `6c1dd4e92c72a95b6abc09eaab1d5a74e90db795` | Ampere tree matches the baseline. Its texture reinterpretation tree differs from baseline by removing the combined depth/stencil aspect and destination-layout fixes. Do not replace our renderer wholesale with this snapshot or infer a bloom fix merely from the branch name. |
| `fix/oot-boot` | `a7f2ee336c8eafcf31f242c16b8e024ca74ac7b2` | Its lowering snapshot lacks several features present at our baseline. Review individual boot changes if an Ocarina service failure is reproduced; replacing the lowering would lose existing support. |
| `perf/oot-fast-draw` | `5620b58d7dcbd5a3ccb163fafc28cda296bf73fc` | Contains extensive emulator draw batching, dirty-memory tracking and parallel recording. Correct rendering must precede performance work. These systems do not map directly onto our NVN command replay. |
| `fix/shader-sweep`, `fix/shader-sweep-all-games`, integration/release branches | See inventory | Screened as possible carriers of shared Ocarina fixes. They are not supersets of `fix/oot-fog`: tree comparisons also remove newer lowering support. Use individual proven changes rather than merging branch tips. |

Relevant upstream source: Lichii `src/shader_recompiler/frontend/ampere/lower_to_maxwell.h`, `LowerToMaxwellWith`; tests in `src/tests/video_core/ampere_ported_textures.cpp`, `ampere_logged_textures.cpp`, `ampere_compute_lowering.cpp` and `ampere_ported_conversions.cpp`. Backend references include Lichii `TextureCacheRuntime::ReinterpretImage` in `src/video_core/renderer_vulkan/vk_texture_cache.cpp`.

## Implementation plan

1. Preserve the failing log and capture native shaders plus failed translation requests. Freeze one corpus so every comparison uses the same inputs.
2. Finish the branch inventory and inspect Ocarina-relevant deltas before selecting changes. Record exact source commits and exclusions.
3. Import selected Ocarina lowering corrections from `fix/oot-fog`, retaining upstream licensing and updating provenance. Test the upstream guarded proof against our existing local prototype before replacing either control-flow analysis. Do not import a complete header snapshot if it loses supported shaders.
4. Run offline lowering before and after on the frozen corpus. Require that previously accepted stages remain accepted. Check representative positive and negative gather/operand cases; then replay newly accepted stages through our actual Ryujinx-based shader translator. Lowering success alone is insufficient.
5. Rebuild the runtime and reproduce the transition from the rendered menu to the 3D scene. Verify that changed lowering output has a distinct shader input hash; preserve baseline logs rather than indiscriminately clearing caches.
6. Implement the confirmed missing mesh draw path described below, then investigate remaining blockers separately: stage-buffer semantics, register pressure, unsupported native instructions and storage-candidate atomic translation. Use the reference implementation's semantics; do not silence rejection checks or replace shaders with empty programs.
7. Report the observed runtime result, remaining failures and a reproducible build command. Do not mark 3D rendering fixed without observing the scene.

## Build identity

Ocarina must use `--version oot111`, not `--version v111`. The latter selects TotK-specific constructor counts and timing. Existing generated code and output can be reused explicitly:

```powershell
.venv\Scripts\python tools\build_game.py --version oot111 --gen generated\v111 --out build\v111 --expected tools\data\expected-oot111.json --exefs build\extracted\v111\exefs --romfs build\extracted\v111\romfs --jobs 24
```

The executable remains named `build/v111/dkb.exe` by the inherited build script.

## First implemented corrections and validation

The complete `fix/oot-fog` header was evaluated offline before selecting changes. It accepted three previously rejected stages but rejected 18 previously accepted stages. That wholesale import was not retained.

The selected update retains this port's existing control-flow proof and incorporates Lichii's matching-predicate IMAD offset proof, unconditional literal materialization for downstream constant propagation, depth-comparison gathers, F2I constant-buffer modifiers, surface reduction operation preservation and LEA.HI base negation. A local adaptation distinguishes UIMAD destinations in the uniform register bank from GPR destinations and recognizes that outgoing branches do not write GPRs. Predicate changes, incoming branch targets and nonconstant offset writes remain rejection conditions.

| Check | Result |
| --- | --- |
| Frozen native corpus | 6,413 stages |
| Lowered before changes | 6,328 |
| Lowered after selected changes | 6,333 |
| Previously accepted stages lost | 0 |
| Newly accepted stages | 5: four compute, one fragment |
| Previously accepted stages with changed output | 12 |
| New and changed stages through this port's actual `totk_xlate` | 17 succeeded, 0 failed |
| Synthetic operand, predicate, control-flow and encoding checks | 122 passed |
| Existing shader-control/profile guards at `-O0` and `-O2` | 45 checks passed at each optimization level |
| Windows runtime rebuild | Compiled and linked successfully |
| Relaunch smoke check | SDK initialization and AV1 decoding completed; process remained responsive; no fatal error observed |

The synthetic tests in `tests/oot_ampere_lowering.cpp` construct artificial instructions; they contain no captured game shader code. They check descriptor banks, comparison coordinates, depth-enable encoding, guarded literal offsets, offset range rejection, predicate clobbers, separate uniform/GPR banks, incoming control edges, F2I modifiers, LEA.HI negation and surface atomic operations. Build them with MSYS2 UCRT64 Clang, keeping the executable under `build/`.

```powershell
& 'C:\msys64\ucrt64\bin\clang++.exe' -std=c++20 -O1 -fuse-ld=lld -static -I runtime/third_party/eglantine tests/oot_ampere_lowering.cpp -o build/oot-fix/lowering-check.exe
& .\build\oot-fix\lowering-check.exe
.venv\Scripts\python tests\nvn2_shader_control_test.py --cc C:\msys64\ucrt64\bin\clang.exe
```

The runtime was rebuilt with the Ocarina build command above and reopened. A visible 3D scene has not been verified after this first batch, and the mesh draw no-ops remain in place. Baseline binaries and logs are retained under `build/oot-fix/runtime-before/`; native-corpus comparisons and standalone translation outputs are under `build/oot-fix/`.

Native lowering and standalone SPIR-V generation do not prove correct rendering or correct bindings in a live draw. The remaining 80 rejected stages include 64 stage-buffer cases, five uniform-load cases, five unsupported-opcode cases, five register-pressure cases and one call-linkage case. These results must not be described as a complete 3D fix.

## Red and black output: live frame investigation

The user screenshot and a reproduced live capture show conventional geometry, with
red and black output. Missing mesh draws alone do not explain this symptom. A private
capture of frame 11117 contains 184 passes/dispatches. Pass 178 still has a multicolour
1280x720 R11G11B10F scene; compute pass 181 turns its output into red-only values,
including NaNs. Its storage binding requests NVN format `0xb` (R32_UINT) over an
R11G11B10F image. `runtime/vk_exec_draw.c`, `get_view`, previously ignored that format
override and bound the floating-point base format instead.

The implementation now resolves compatible uncompressed colour format overrides,
separates sampled and storage views in the view cache, and restricts each view's usage.
RGB9E5 images gain extended storage usage through a compatible R32_UINT view, following
the approach in Lichii `vk_texture_cache.cpp`, `MakeImage`. This is needed because the
RTX 5080 reports RGB9E5 sampling support but no direct storage-image support.

`answers_for` also now supplies the effective storage image format to the translator.
Previously it supplied dimensions alone, causing unknown integer formats to default
to RGBA8 UNORM. The existing shader key includes these answers, so changed bindings
produce distinct translations. Synthetic compatibility regressions and the Windows
rebuild pass. Subsequent gameplay captures show that these changes alone do not
repair the final image; the following sections record the remaining failures.

The first attempt to load Continue stopped at guest PC `main+0x1661690`. The cause and
repair are recorded below. Raw frames, logs and SPIR-V captures remain private under
`build/oot-fix/red-output/`. Coloured title backgrounds and the introductory movie are
video playback, not evidence of working scene rendering.

## Continue crash: guest setjmp and longjmp

The failing PC is a return address inside the function beginning at `main+0x1661670`.
Its call to PLT stub `main+0x3c44140` resolves through GOT offset `0x4cc0268` to the
imported symbol `setjmp`. A later `longjmp` restores that return address. Ordinary
indirect dispatch only accepts function entries, so dispatching the saved return PC
fails. Treating it as another function start would also fail to unwind the host stack.

`recomp/gen.py`, `Emitter`, now identifies setjmp PLT stubs from symbol relocations and
the AArch64 stub instructions. A direct caller establishes a native continuation in
its own host frame. These callers retain registers and flags in `Cpu`, avoiding C's
indeterminate modified automatic locals after native longjmp. Cleanup removes their
registered continuations on ordinary return. Unresolved computed-branch functions
with setjmp are rejected explicitly until they can share a continuation scope.

`runtime/guest_jump.c` and `runtime/guest_jump.h` manage active continuations and an
override for imported `longjmp`. They restore the saved Cpu, normalize a zero return
value to one, restore host-call tracking depth, and discard contexts of unwound frames.
Contexts also match Cpu because multiple guest fibers can share a native thread.
The guest SDK setjmp still executes on the initial path and populates its guest buffer.
This implementation covers recognized direct PLT calls; arbitrary indirect setjmp
calls are not yet intercepted and must not be described as supported.

Validation so far:

- The synthetic `tests/guest_jump_test.c` passes at `-O2`, including nested unwinding,
  register/SP/SIMD restoration, zero-to-one conversion, negative values and repeated calls.
- Instruction differential tests: 60,000 encodings, 179,931 cases, zero failures.
- SIMD differential tests: 20,000 encodings, 159,976 cases, zero failures.
- Atomic differential tests: 203 tests, 12,992 cases, zero failures.
- Flag helper tests: 2,000,484 operand pairs each at `-O0` and `-O1`, zero mismatches.
- The separate existing exclusive-monitor contention test gets the expected totals,
  but its three ABA checks fail. Repeating it against the committed HEAD CPU/SIMD
  headers reproduces all three failures, confirming that they predate these changes.
- Whole-function differential testing: 415 functions and 2,572 completed cases, zero
  failures; 9,956 attempted cases were skipped by the oracle's safety/step limits.
- The user confirmed that Continue opens the existing save. Live gameplay UI was also
  captured after loading. The 3D output remains visibly corrupted.

The test executable is currently `build/v111/oot-jump.exe`, linked from the rebuilt
objects because Windows kept the previous `dkb.exe` image locked. It uses the same
profile and data paths. Its private test log is `build/oot-fix/continue-trace.log`.

The graphics log still reports sampled float/uint mismatches, including RG16_UINT
bound to the float sampler `cp_t_tcb_7C`. Storage format answers alone do not repair
sampled-image declarations. Enabling explicit storage formats also exposed visual
regressions in the title video background; those changes remain under investigation.
Mesh draws and unsupported native shader stages remain separate unresolved issues.

The next graphics experiment carries sampled uint/sint identity in the shader answers,
uses separate texture/image metadata lookups for both native compute and legacy handle
layouts, and declares integer sampled images with a bitcast of fetched register values.
The patch is `tools/xlate/patches/0020-preserve-integer-sampled-image-types.patch`;
vendor files remain unchanged. The translator builds, its six patch-selection tests
pass, and the private shader translation jobs complete (5 newly accepted and 17 in the
changed-job list). In the live `typed-trace.log`, the previous float/uint component
mismatch errors are absent. The final gameplay image is nevertheless posterized.

A second private capture, frame 1596 (242 passes), shows a valid multicolour HDR scene
at pass 236, followed by integer-packed colour at pass 239. The latter image's base
format is R11G11B10F, but a skipped tensor stage samples an RGB9E5 alias at the same
address. The linear upscale stand-in was choosing the last writer's base image. It
now looks for the tensor input's RGB9E5 alias at that exact address and shape. Merely
selecting the alias did not repair the output in `alias-scene.png`.

`runtime/vk_texture.c`, `entry_of`, copies live alias content when an image is created;
`make_fresh_snap` does not refresh existing GPU-owned aliases. Explicitly refreshing
that compatible alias from the recorded writer through the existing buffer-copy
path produced black gameplay output. That experiment was removed.

Selecting the packing dispatch's unique declared HDR input instead produced an
overexposed image, and the user reported repeated flashes of light. The packing pass
cannot be skipped as a colour-processing step. This diagnostic is now opt-in only
with `OOT_LINEAR_UNPACKED_SOURCE`; ordinary runs leave it disabled. The flashing
test process was stopped. Its private capture is `build/oot-fix/linear-scene.png` and
its log is `build/oot-fix/linear-trace.log`. Do not count the temporary appearance of
geometry or brighter output as a completed rendering fix.

The latest compiled executable is `build/v111/oot.exe`, with its matching translator
and identity alongside it. It includes the Continue repair and leaves the unpacked
diagnostic disabled by default. Further checks pass: 28 compute c0 checks, 45 shader
control/profile checks at each of `-O0` and `-O2`, 19 shader-service/cache tests, and
nine geometry translator tests. A full-tensor run was started with `OOT_RUN_TENSOR=1`,
but its log ended without a fatal message before a checked gameplay capture was
obtained. Whether that window was closed manually is still being established; this
does not count as a successful full-tensor rendering test.

A repeated full-tensor run generated a roughly 21 MB SPIR-V module and failed to
advance beyond the first automatic title capture while compilation continued. The
user reported that the initial menu no longer loaded. That run was stopped, and
the default tensor stand-in was restored for interactive testing. Full tensor
lowering is not currently a usable replacement merely because it translates.

An additional abort was recorded on the previous run's completion thread. Host address
symbolization points into `runtime/vk_sync.c`, `completion_main`, near the fence wait
and deferred callback processing. It is not the Continue dispatch failure and its
specific trigger has not yet been established.

## Confirmed missing mesh draw path

Runtime presentation requirement: the user wants every game execution to keep both
the game window and a live log console visible. Future launches must not hide the
console or redirect the only visible log exclusively to a file. The current live
log watcher is `build/oot-fix/Watch OOT log.ps1`; the reusable visible launcher is
`build/v111/Run OOT with log.ps1`. Both scripts are private build artifacts.

The latest build adds an independently allocated RGB9E5 transfer image for the
linear tensor replacement. The writer's 32-bit image is copied bit-for-bit into it,
then converted by the blit. This avoids both stale pool aliases and bypassing the
packing pass. `tests/vk_packed_hdr_probe.c` verifies that the same GPU operation
returns exactly (0.25, 0.5, 1.0, 1.0) from synthetic packed RGB9E5 data.
The translator also includes patch 0021, declaring StorageImageExtendedFormats
for explicitly typed storage images. The shader patch selection tests and the
private 5/17 shader job lists pass. Gameplay verification of this build is pending.
The user requested a build to test before further investigation of the native
oot-rgb9.exe access violation; crash investigation is deferred accordingly.

The user confirmed that the first RGB9E5 build remained blue/posterized. Its log
contained no independent-RGB9E5 decode message: the new conversion branch was not
being selected. `runtime/vk_texture.c`, `slot_resolve`, could follow a live image
at the same storage address while preserving the slot swizzle but discarding the
slot's format. A logical RGB9E5 descriptor could therefore still report R11G11B10F
as its sampling format. The resolver now preserves compatible colour format
reinterpretation as an implicit view, without overriding an explicit view format.
Implicit swizzle composition also retains that format. The tensor source lookup
now checks the effective view format, rather than the backing image format alone.

`tests/texture_followed_view_test.py` runs the real resolver with synthetic providers
at `-O0` and `-O2`: implicit format plus swizzle, explicit-format precedence,
unchanged-format and incompatible-format cases pass. Existing descriptor-pool
cache tests pass all 60 checks at both optimization levels. The new runtime build
is `build/v111/oot-follow.exe`; visual verification is pending. Failed native
shader translations remain independent unresolved problems, not evidence that
the final colour stage has been repaired.

The `fix/oot-bloom` history was fetched to depth 32 for a commit-level follow-up.
Its tip `6c1dd4e92c72a95b6abc09eaab1d5a74e90db795` changes H0_NH1 to swap
the FP16 halves and negate the new low half. That exact sequence is already in
the vendored baseline; copying the tip again would not change this port.
The preceding `abe66f465c7e72f75b1b55f84a5597f4adb948e5` adds hash-based program
rejection and asynchronous frame-capture diagnostics, rather than establishing
that every bloom failure is fixed. Excessive bloom remains a hypothesis to test
against the pass inputs and output, not a diagnosis inferred from the branch name.

The user reports that the blue output is gone in the followed-view build, but
textures and meshes flicker. The log now confirms that the independent RGB9E5
decode executes. Local captures also show overexposed/white gameplay frames;
therefore the colour repair is partial and visual stability is not established.
The next capture includes the stand-in's source and destination in the same frame,
plus optional raw HDR words (`TOTK_DUMP_RAW_HDR=1`), to distinguish corruption
before conversion from later saturation or stale frame content. These remain
private under `build/oot-fix/color-followed-01/`.

Game launch now uses `build/v111/Play OOT.cmd` with a visible native console,
launched through the desktop shell. This avoids relying on a redirected child
pipeline whose game window had already exited when its state was later checked.
Every launch must verify the live game process/window instead of assuming success
from a returned launch command. The native console log is also mirrored by the
runtime at `build/v111/logs/totk.log`.

The live Ocarina trace explicitly calls both `nvnCommandBufferDrawMeshTasks` and `nvnCommandBufferDrawMeshTasksIndirect`. `runtime/nvn_api_nvn2.c`, `NVN2_NOT_RECORDED`, currently accepts both without recording any draw. This is an independent, concrete reason the renderer cannot produce the mesh geometry; shader acceptance fixes alone cannot repair it.

Lichii `src/video_core/shader_environment.cpp`, the graphics environment constructor, documents that native task/mesh programs occupy vertex and tessellation hardware slots when mesh mode is enabled. Our `runtime/nvn2_lower.cpp`, `lower`, selects tessellation options from the SPH type alone. All 64 remaining stage-buffer failures have SPH shader type 3 and reject ISBERD. This is consistent with the missing mesh context, but the precise program role still needs to be traced through NVN program binding and geometry-processing mode before changing stage classification.

After validating the image-format repair, the remaining mesh implementation must cover
the complete path:

1. Recover and validate the NVN mesh command signatures, task counts, first-task values and indirect-buffer addressing. Record these commands with their program and geometry-processing state instead of accepting them as no-ops.
2. Preserve task/mesh stage identity through program construction, lowering and replay. Compare with Lichii's native graphics environment and stage-buffer pseudo-instructions. Do not reinterpret ISBERD as an ordinary vertex move to bypass a failure.
3. Query and enable Vulkan mesh/task features and limits before device creation. This port's Ryujinx-based translator does not automatically gain Lichii's task/mesh SPIR-V support by updating the Ampere header. Choose and implement a compatible translation route with correct descriptor reflection and output topology.
4. Replay direct and indirect mesh draws inside the appropriate render pass, with task/mesh visibility in resource barriers. Investigate the first-task workgroup offset correction from `fix/oot-fog` only after the basic draw path is working.
5. Validate a visible Ocarina 3D scene and check the native/translator failure logs. Record remaining fog, bloom, texture or atomic issues separately from missing geometry.

The reference changes to CPU/GPU cache flush decisions and draw scheduling are not shortcuts to copy: hardware-visible writes and synchronization must be preserved in this runtime.

## Tracing the white surfaces before post-processing

The private capture `build/oot-fix/color-followed-02/` establishes that excessive
brightness is present before bloom and before the upscale stand-in. Raw HDR words
from pass 0191 (sky), 0205 and 0207 were compared directly. At pixel (600,300),
all three contain `0x98674509`; at (120,680), all contain `0x97a6ecff`.
Other unchanged sky pixels cover parts of Link's cap and shirt. In contrast,
the depth capture contains the room and Link. This supports investigating missing
colour/material writes. It does not prove that every white pixel is caused by a
failed shader, or that the blend operation itself accumulates incorrect values.
The user correctly observed that both presented pass/upscale images were already
too bright; neither image should have been described as visually correct.

The next priority is shader and draw failures, rather than further RGB format
experiments. Native lowering diagnostics now identify the content hash and SPH
type. `runtime/gfx_shader.c`, `req_init`, describes an unlowered native stage as
such, instead of misleadingly reporting that no translator exists. During an
armed frame capture, `runtime/vk_dump.c`, `vk_dump_draw_skip`, groups missing
vertex stages, failed shaders and pending/failed pipelines by program and pass,
including raw native hashes. Successful draws alone cannot expose those omissions.

The first focused shader change accepts indexed unsigned 8/16-bit ULDC, using
the same Maxwell LDC width encoding already used for direct subword loads.
The existing bank, alignment, offset-range and operand guards remain. Synthetic
instruction checks pass (170 checks). The frozen corpus advances from 6,333 to
6,334 accepted stages out of 6,413. Native stage `abccf81b45c5eb50` also passes
the actual translator and `spirv-val --target-env vulkan1.3`. Four other shaders
advance to later rejections (LEA carry/high modes or an out-of-range ULDC offset);
they are not fixed merely because their first failure moved.

The runtime build `build/v111/oot-shader-trace.exe` was launched with a visible
live log console through its own immutable CMD launcher. Its frame 1116 capture
under `build/oot-fix/shader-trace-01/` has no recorded conventional draw skips
from the newly instrumented shader/pipeline rejection paths. The displayed 3D
frame remains white. Consequently, the general shader error list alone is not
evidence that a particular failed conventional draw caused this frame. Mesh
commands still disappear before Vulkan replay, and shaders that compile can
still have incorrect semantics. Both remain active investigation paths.

The next focused change extends `register_pressure.h`, `PrepareRegisterPressure`,
to immediate FMNMX, P2R and indexed LDC. These instructions already had semantic
lowering, but programs using them above the reserved register boundary failed
because their operand shapes were absent from the shadowing table. Five vertex
stages now lower completely (`046361b69e7dc674`, `c57d42c3b5eb1fb7`,
`bde5381230acfe7b`, `7b6ae1bb954d68c9`, `da5ace2b36f8be46`). All five also
translate and pass SPIR-V validation for Vulkan 1.3. Synthetic checks now total
186; the frozen corpus accepts 6,339 of 6,413 stages. No visual improvement is
claimed from that acceptance count. A visible runtime test still showed white.

Inspection of the rejection trace uncovered another concrete bug in
`runtime/vk_exec_draw.c`, `find_stage` and `find_stage_scan`: they read stage
and length at legacy offsets even when failed native lowering left a compact
control. This can read outside that control and misclassify an unlowered
fragment stage as absent, allowing an unintended depth-only draw. Both paths
now use `program_stage_info`, which reads native length at +0x38 and graphics
stage from the native SPH, retaining the stage even when its conversion failed.
`shader_for` rejects the raw stage before calling legacy control/descriptor
helpers. This correction does not implement mesh stage identity; its explicit
geometry-mode context is still required.

`tests/native_program_stage_test.py` exercises the actual production helper at
O0/O2, placing a compact control immediately before an inaccessible guard page.
Native graphics and compute, legacy metadata, missing pointers, zero lengths
and unknown graphics types pass. The new `oot-stage.exe` build includes this
lookup repair and the prior shader fixes; runtime capture remains in progress.

## Compute shaders with indirect global pointers

The live shader-failure capture at `build/oot-fix/shader-shadow-failures/capture-24708-*`
showed repeated NVN 2 compute failures with `No bounded cbuf-pair candidate`.
Replaying `d2fc3173250f602f` with translator-side SSA tracing showed that the
global address is formed through phis and loaded values; its expression graph
contains no recoverable cbuf root. The dispatch still declared four bound SSBOs
in its versioned c0 layout (`c0=[0,4,4,0]`, root counts all zero).

The first fallback experiment added those descriptors after the old unrestricted
expression walk. It then failed the translator's 32-storage-descriptor limit:
the walk had also contributed unrelated cbuf pairs. The corrected path in
`tools/xlate/patches/0023-enumerate-compute-storage-candidates.patch` uses the
compute c0 layout to enumerate only its actually bound SSBO base-address pairs.
`tools/xlate/Program.cs`, `Accessor.QueryStorageBufferCandidateOffset`, accounts
for the 16-byte header, padded root tables, UBO table and 16-byte SSBO records.
The existing guarded helper accepts an access only if its address is inside one
of those descriptors. The descriptor count is capped at the NVN 2 limit of 16.

Offline replay now translates `d2fc3173250f602f` and five further captured
compute shaders that previously failed candidate discovery; all six outputs
pass `spirv-val --target-env vulkan1.3`. The first replay's reflection contains
four guarded SSBO descriptors at c0 word offsets 20, 24, 28 and 32. This
confirms translator output validity, but does not establish that the game renders
correctly. The previous runtime process exited before a fresh render-pass capture
was armed, and no visual improvement has been measured from this change yet.

The latest runtime log also retains separate compute failures for non-32-bit
atomics and subword global loads, along with native tessellation/mesh-stage
rejections. Those remain independent translation paths to investigate.

### UBO candidates and the visible black transition

A later gameplay capture exposed a gap in the candidate list. Shader
`b7f1d8151e7d25e6` declared six UBOs and no SSBOs (`c0` counts 1, 6, 0, 4),
but its global load address came from a c0 buffer descriptor. The SSBO-only
enumeration therefore supplied zero candidates and rejected the shader. The
same pattern appeared in `fd91ff00f8f752db`.

`tools/xlate/Program.cs`, `Accessor.QueryStorageBufferCandidateCount` and
`Accessor.QueryStorageBufferCandidateOffset`, now enumerate the contiguous UBO
and SSBO descriptor records in c0. The guarded range check still decides which
candidate contains each address. Both captured shaders now translate; both
outputs pass `spirv-val --target-env vulkan1.3`. This fixes those translation
failures, but it does not establish that their execution fixes the scene.

The visible runtime test used `build/v111/shader-cache`, the default cache
directory. The prior isolated test cache was merged into it so the next run can
reuse translated shader outputs. The first gameplay transition remains black
while the game compiles pipelines. The latest runtime capture has no repeated
`No bounded cbuf-pair candidate` failures; it still reports 64-bit atomics and
subword loads as unsupported. Visual verification of the playable 3D scene is
still pending.

The earlier process that ended with `FATAL: abort (0x16)` was mapped through
`build/v111/totk.map` and `runtime/gfx_present.c`, `present_image`: the captured
return address follows `vkQueueSubmit2`. The executable stack report omits
frames from the Vulkan driver, so the driver-side cause is not yet known. The
10 Vulkan validation messages in that log were repeats of
`VUID-vkCmdDrawIndexed-None-06479`, about depth-comparison sampling; none
identified that queue submission. Do not treat this separate abort as fixed.
