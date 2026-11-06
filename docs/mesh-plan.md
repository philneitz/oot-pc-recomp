# Native task and mesh replay

The scene comparison targets are floor grass, ivy and material/lighting details in Link's house. The initial Lichii
reference was `test/all-in-one-mt` at `9b1a32dbb160d2d55ad1409961e87ec4a1256711`; a later working reference reports
`7ea801af3556b4862e779d99ba292dae15fe09a5`. The door is already open in the user's original reference save. An open
doorway alone is not evidence of missing geometry. A separate reference profile with a closed door now provides a
visual comparison without modifying the original save.
The recomp still has missing vegetation and a washed-out lighting/fog result. Successful shader compilation or
submitting a mesh command is not a visual fix.

## Implemented foundation

- `DrawMeshTasks`, `DrawMeshTasksIndirect` and geometry processing mode are command-stream operations. Other mesh
  draw variants remain unimplemented. Program attribution happens during replay, including inherited state.
- NVN resource bindings retain all eight stage slots. The native control stage tags are task 6 and mesh 7; SPH type
  alone is insufficient (the observed task stage has vertex SPH type 1 and the mesh stage has tessellation SPH type 3).
- `VK_EXT_mesh_shader` is enabled when supported. The pipeline builder shares the ordinary raster, depth, blend and
  fragment path, and omits vertex-input/input-assembly state for mesh pipelines. Recorder wrappers preserve ordering.
- `--probe-vkmesh` rasterizes an original mesh triangle, then a task-payload colour variant, reads back their pixels
  and checks Vulkan validation. `python tests/vk_mesh_probe.py --exe <exe>` compiles the original GLSL fixtures first.
- `tools/build_mesh.py` builds a separate offline translator from pinned Lichii sources and dependencies. It lowers
  native instructions with explicit task/mesh context and emits SPIR-V plus resource metadata. Reference sources are
  unchanged; build inputs and outputs containing game data stay under ignored `build/`.

## Experimental native pipeline

`OOT_MESH_CACHE=<absolute directory>` opts into direct draws and experimental single-indirect conversion with a
compiled matching module. Without that option,
mesh draws remain skipped. Cache filenames use the full native dump key, including the control and header, rather than
Lichii's instruction-only key, which can alias variants. Files are `<dump-key>-mesh.spv` or `<dump-key>-task.spv`, with
an adjacent `.bindings` file in MSH3 format. MSH2 artifacts are rejected: their compiler profile omitted subgroup
support for task and mesh stages, producing scalar lane IDs and incorrect vote/shuffle operations. The compiler
requires three explicit hardware mesh register values:

```text
python tools/build_mesh.py --toolchain <setup-directory> --jobs 8
build/mesh/oot_mesh_xlate inspect build/capture/<key>.bin
build/mesh/oot_mesh_xlate compile build/capture/<mesh-key>.bin build/mesh-cache/<mesh-key>-mesh.spv <mesh_A> <mesh_B> <mesh_init>
build/mesh/oot_mesh_xlate compile-task build/capture/<task-key>.bin build/mesh-cache/<task-key>-task.spv <mesh_A> <mesh_B> <mesh_init>
spirv-val --target-env vulkan1.3 build/mesh-cache/<mesh-key>-mesh.spv
```

The working reference's push buffers establish two payload classes: world meshes use `mesh_init=0x00802020`
(256 payload bytes), and observed vegetation candidates use `mesh_init=0x06419020` (3200 bytes). These are measured
scene values, not universal defaults. A task module must match the mesh module's init register; mesh A/B need not
match because shared task programs can feed meshes with different output limits.

The compiler fails when an environment answer such as a texture type or constant value is missing. An optional final
`textures.context` argument supplies locally captured environment answers. Its first line is `MTC1`; `T <slot>
<handle> <shape>` records native bank-2 texture handles (shape 4 for 2D or 5 for 2D array), `C 1 <byte-offset> <word>`
supplies inline constant words, and `P 1 <byte-offset> <0-or-1>` classifies an inline value as a storage pointer.
Unknown inline-pointer classifications fail compilation rather than treating small scalar constants as GPU addresses.
Context files and compiled game shaders stay under ignored `build/`.

Metadata includes indirect pointer-table offsets and single sampled 2D/array descriptors. The compiler explicitly
enables task/mesh subgroup votes and shuffles; the runtime requires 32-lane subgroups with those features in the
actual stage before loading the cache.
The adapter enables both native-program and native-instruction decoding; otherwise extended register copies can be
silently discarded as masked MOVs. Stubbed-instruction warnings now fail compilation instead of producing a cache entry.

The runtime adapter supports single buffer descriptors, driver bank 0, inline bank 1 and user banks 3..16, with storage addresses from
the observed task c0+0x670 / mesh c0+0x770 tables. It reads indirect pointer holders through ordered GPU backing before
opening rendering. This incurs a readback and split per holder and needs optimization after correctness. Single-indirect
conversion reads two words as count/first, then uses the direct path; this interpretation still needs a populated reference
comparison. Observed zero-count calls do not establish that culling works. Direct group counts are checked against host
task/mesh dispatch limits. Read-only indirect pools can span up to 64 MiB, following Lichii's grass-buffer handling;
writable/direct pools retain the smaller bound. Sampled native texture shapes are checked against current bindings.
Fragment sampler-only arrays, buffer images, nonzero first-task offsets and additional draw forms remain unsupported.
Unsupported fragment sampler arrays skip the pipeline instead of creating an incompatible combined-sampler layout.
A missing or incompatible module does not replace an ordinary shader.

Mesh pipeline keys use the ordinary builder's normalized state rather than unused vertex input and dynamic state.
The earlier 128-entry limit could silently drop later colour passes and has been removed.

`OOT_MESH_STATS=1` collects the first four native draws' task/mesh and fragment invocations. Legacy clipping counters
are excluded because mesh draws cannot run inside those queries (VUID 07076). `OOT_MESH_TEST_RASTER=1` disables depth and culling only for this experimental path to isolate rejection;
it is a diagnostic, not a proposed rendering fix. `OOT_MESH_SHADER=0` disables device mesh features for comparisons.
`OOT_PIPE_BUILD_LOG=1` logs individual compute and full graphics pipeline build start/end events.

## Confirmed locally

- The closed-door reference associates mesh instruction key `8159e91e0097876a` with the wooden door's colour pass:
  rejecting that shader changes the door rendering. The paired task key is `1a55fa57a335b963` and fragment key
  `0e489e48a0772be7`. Program serials vary between runs and must not be used as shader identities.
- Correct subgroup operations and measured world payload registers restore the wooden door in recomp screenshots
  and remove the giant stretched white/coloured polygons seen with the old compiler profile. The restricted world
  cache completed a 60-second game-time test without a fatal error. Vegetation and bright haze remain unresolved.
- Compiled modules pass `spirv-val`; this does not establish correct resources or rendered output. Enabling additional
  programs caused an NVIDIA-driver access violation. Inspection found a fragment shader with separate image and
  sampler arrays that the mesh adapter had incorrectly declared as combined image samplers. That unsupported case
  now skips replay. A subsequent 210-second game-time run (223 seconds wall time) reached the same door view and
  triggered the guard without a fatal error. This is a local regression check, not a claim of whole-game stability.
- The real-game validation run contains ordinary `vkCmdDrawIndexed` depth-comparison-format errors. They must be
  investigated separately; the resource-free mesh probe passes validation. A clean whole-game result is not claimed.
- Diagnostic validation plus buffer readbacks ran around 14 FPS; the native run without validation around 37-41 FPS.
  These are short scene observations, not a controlled performance benchmark.
- A cold NVIDIA compiler spent about 52-80 seconds on individual existing compute pipelines. The game resumed around
  44 FPS with mesh features enabled. A black window during these builds was not evidence of a crash. One interrupted
  experiment was terminated by the test helper when free RAM fell below 5 GiB during another emulator build.

## Remaining checks

1. Identify the grass and ivy programs in a working reference capture, then compare resource bindings and draw arguments.
2. Validate vegetation payload contents, culling outputs and matching depth/colour passes before broadening replay.
3. Investigate the bright haze and material/reflection differences without masking them with a visual workaround.
4. Implement and validate fragment sampler-table arrays before enabling the affected material programs.
5. Keep normal physical input active during tests. Let the user position the camera when that is faster.
