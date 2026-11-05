# Native task and mesh replay

The scene comparison targets are floor grass, ivy and material/lighting details in Link's house. The initial Lichii
reference was `test/all-in-one-mt` at `9b1a32dbb160d2d55ad1409961e87ec4a1256711`; a later working reference reports
`7ea801af3556b4862e779d99ba292dae15fe09a5`. The door is already open in the user's reference save. An open doorway
is not evidence of missing geometry, and the earlier identification of a specific shader as the door is unverified.
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
an adjacent `.bindings` file in MSH2 format. The compiler requires three explicit hardware mesh register values:

```text
python tools/build_mesh.py --toolchain <setup-directory> --jobs 8
build/mesh/oot_mesh_xlate inspect build/capture/<key>.bin
build/mesh/oot_mesh_xlate compile build/capture/<mesh-key>.bin build/mesh-cache/<mesh-key>-mesh.spv <mesh_A> <mesh_B> <mesh_init>
build/mesh/oot_mesh_xlate compile-task build/capture/<task-key>.bin build/mesh-cache/<task-key>-task.spv <mesh_A> <mesh_B> <mesh_init>
spirv-val --target-env vulkan1.3 build/mesh-cache/<mesh-key>-mesh.spv
```

Register values used in local experiments are not yet verified against the running emulator; they are not production
hardware defaults. The compiler fails when an environment answer such as a texture type or constant value is missing.
It currently exports buffer resources only. Metadata includes indirect pointer-table offsets, not just top-level buffers.
The adapter enables both native-program and native-instruction decoding; otherwise extended register copies can be
silently discarded as masked MOVs. Stubbed-instruction warnings now fail compilation instead of producing a cache entry.

The runtime adapter supports single buffer descriptors, driver bank 0 and user banks 3..16, with storage addresses from
the observed task c0+0x670 / mesh c0+0x770 tables. It reads indirect pointer holders through ordered GPU backing before
opening rendering. This incurs a readback and split per holder and needs optimization after correctness. Single-indirect
conversion reads two words as count/first, then uses the direct path; this interpretation still needs a populated reference
comparison. Observed zero-count calls do not establish that culling works. Direct group counts are checked against host
task/mesh dispatch limits. Native mesh textures, inline constant banks, nonzero first-task offsets and additional draw
forms remain unsupported. A missing or incompatible module does not replace an ordinary shader.

`OOT_MESH_STATS=1` collects the first four native draws' task/mesh invocations, clipping primitives and fragment
invocations. `OOT_MESH_TEST_RASTER=1` disables depth and culling only for this experimental path to isolate rejection;
it is a diagnostic, not a proposed rendering fix. `OOT_MESH_SHADER=0` disables device mesh features for comparisons.
`OOT_PIPE_BUILD_LOG=1` logs individual compute and full graphics pipeline build start/end events.

## Confirmed locally

- One inspected scene program has instruction keys mesh `8159e91e0097876a`, task `1a55fa57a335b963`, fragment
  `0e489e48a0772be7`. Replay establishes three stages, geometry mode 1, direct first 0 / count 3. Its scene object has
  not been established. Program serials vary between runs and must not be used as shader identities.
- Both actual task and mesh programs compile and pass `spirv-val` with experimental explicit register values.
- The experimental pipeline issues the actual draw. GPU statistics establish 96 task invocations, then 32 mesh
  invocations and 70 input clipping primitives on a populated frame. Its colour pass uses depth EQUAL with depth writes
  disabled, so matching depth geometry is one remaining diagnostic target. Vegetation is still missing. Broad native
  replay and diagnostic runs produced stretched coloured or white polygons; the native path remains experimental.
- The real-game validation run contains ordinary `vkCmdDrawIndexed` depth-comparison-format errors. They must be
  investigated separately; the resource-free mesh probe passes validation. A clean whole-game result is not claimed.
- Diagnostic validation plus buffer readbacks ran around 14 FPS; the native run without validation around 37-41 FPS.
  These are short scene observations, not a controlled performance benchmark.
- A cold NVIDIA compiler spent about 52-80 seconds on individual existing compute pipelines. The game resumed around
  44 FPS with mesh features enabled. A black window during these builds was not evidence of a crash. One interrupted
  experiment was terminated by the test helper when free RAM fell below 5 GiB during another emulator build.

## Remaining checks

1. Identify the grass and ivy programs in a working reference capture, then compare resource bindings and draw arguments.
2. Validate mesh/task registers, payload contents and matching depth/colour passes before broadening replay.
3. Investigate the bright haze and material/reflection differences without masking them with a visual workaround.
4. Validate the actual mesh registers, payload size and host compiler profile before enabling general native replay.
5. Keep normal physical input active during tests. Let the user position the camera when that is faster.
