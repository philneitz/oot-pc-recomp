# Mesh draws: plan

Ocarina of Time draws part of its world through NVN 2 mesh draws (`DrawMeshTasks*`). The replay drops all of
them today (`NVN2_MESH_NOT_RECORDED` in `runtime/nvn_api_nvn2.c`), so that geometry is missing: in Kokiri Forest
at least the door of Link's house (Lichii names its mesh program `8159e91e0097876a`), and very likely grass,
bushes and vines.

## What a mesh log run shows

`OOT_MESH_LOG=1`, Kokiri Forest, a few minutes:

- about 30 programs (4034-4095) drawing with `DrawMeshTasksIndirect`, over 4800 calls in the first minutes;
- most have two stages that lower (mesh + fragment), some a third (task, SPH type 3) that does not;
- two programs (14404, 14408) draw with direct `DrawMeshTasks`, both with an unlowered task stage;
- the indirect arguments live in buffers the frame's first compute passes write (GPU culling).

The log prints a native hash only for unlowered stages, so it cannot yet say which program is the door.

## Pieces

1. **Translation.** The translator (`tools/xlate`, Ryujinx 1.3.2) has no mesh or task stages. Lichii's
   `shader_recompiler` (yuzu / Eden lineage, GPL-3.0-or-later like this repository) has them, including the stage
   buffer instructions (`ISBERD`/`ISBEWR`) and the SPIR-V mesh and task execution models. Two ways:
   - port mesh output, primitive output and task payload handling into the Ryujinx translator, or
   - build Lichii's recompiler as a second offline tool for mesh and task stages only, fed the lowered Maxwell code
     the runtime already produces, with an environment that answers its constant buffer and texture queries.
   The second reuses tested code and is the likely choice; it needs an environment adapter and its own cache key.
2. **Lowering of task stages.** SPH type 3 stages do not lower today. Compare with the current Lichii
   `lower_to_maxwell.h`.
3. **Recording.** Record `DrawMeshTasks` and `DrawMeshTasksIndirect` (then the 3D and multi-draw variants) as
   replay ops with their program, bindings and arguments, like ordinary draws.
4. **Vulkan.** Enable `VK_EXT_mesh_shader` when the device has it, build pipelines from task + mesh + fragment,
   and issue `vkCmdDrawMeshTasksEXT` / `vkCmdDrawMeshTasksIndirectEXT`. Check that NVN's indirect layout matches
   `VkDrawMeshTasksIndirectCommandEXT` or convert it. Devices without the extension keep dropping these draws for
   now (a compute fallback is later work).
5. **Culling inputs.** The indirect counts come from compute passes that read instance data through pointers.
   Lichii had to fix several such reads (`f9acc60a5`, `d854ff0de`, `4dcbd1cb9`); check that ours deliver non-zero
   counts before blaming the mesh path.

## Order

1. Extend `OOT_MESH_LOG` with the native hash of every stage, find the door's program and its draw arguments.
2. Make one two-stage program (no task stage) draw: translation, recording, pipeline, direct draw. Goal: the door.
3. Indirect draws with the culling buffers, checking the counts in a dump.
4. Task stages (lowering and translation), then the remaining draw variants.

## Checks

- The door of Link's house is visible; `OOT_MESH_LOG` counts the draws as replayed, not dropped.
- `--selftest` passes; no new Vulkan validation errors in a short run.
- Frame rate before and after on the same spot.
