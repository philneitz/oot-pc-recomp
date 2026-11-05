// NVN 2 procs Donkey Kong Bananza asks for beyond TotK's NVN (runtime/nvn_procs_nvn2.inc). Only the calls whose effect
// the host can honour without knowing a new NVN 2 structure layout are here; the rest stop with their name
// (nvn_procs.c) until a call shows what they need.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "host.h"
#include "nvn_api.h"
#include "nvn_mode_trace.h"
#include "nvn_obj.h"
#include "platform/platform.h"

// ---- Queues. The host has one queue model: a queue's primary queue and its ray-tracing memory change nothing.
// void nvnQueueBuilderSetPrimaryQueue(NVNqueueBuilder *b, NVNqueue *primary)
NVN_API(nvnQueueBuilderSetPrimaryQueue) { NVN_LOG("QueueBuilderSetPrimaryQueue %p", arg_p(c, 1)); }
// void nvnQueueBuilderSetRayTracingMemorySize(NVNqueueBuilder *b, size_t n)
NVN_API(nvnQueueBuilderSetRayTracingMemorySize) { (void)c; }
// size_t nvnQueueBuilderGetRayTracingMemorySize(const NVNqueueBuilder *b)
NVN_API(nvnQueueBuilderGetRayTracingMemorySize) { ret_x(c, 0); }
// size_t nvnQueueGetTotalRayTracingMemoryUsed(const NVNqueue *q)
NVN_API(nvnQueueGetTotalRayTracingMemoryUsed) { ret_x(c, 0); }

// ---- Device
// int nvnDeviceGetFlags(const NVNdevice *d): the flags the device was initialized with
NVN_API(nvnDeviceGetFlags) { ret_x(c, ((const dev_t_ *)arg_p(c, 0))->flags); }
// NVNboolean nvnDeviceRegisterFastClear{Color,Colori,Colorui}Index(NVNdevice *, const T *color, NVNformat, int index),
// ...Stencil(NVNdevice *, int value): like the NVN 1 forms (nvn_api_core.c), "not registered" keeps the normal clear
NVN_API(nvnDeviceRegisterFastClearColorIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearColoriIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearColoruiIndex) { ret_x(c, 0); }
NVN_API(nvnDeviceRegisterFastClearStencil) { ret_x(c, 0); }

// ---- Device message buffer (driver debug messages): the host driver writes none
NVN_API(nvnDeviceMessageBufferSetDefaults) { (void)c; }
NVN_API(nvnDeviceMessageBufferSetDevice) { (void)c; }
NVN_API(nvnDeviceMessageBufferSetStorage) { (void)c; }
NVN_API(nvnDeviceMessageBufferBeginMessages) { (void)c; }
NVN_API(nvnDeviceMessageBufferEndMessages) { (void)c; }
NVN_API(nvnDeviceMessageBufferReset) { (void)c; }
// const NVNdeviceMessage *nvnDeviceMessageBufferGetNextMessage(...): no message
NVN_API(nvnDeviceMessageBufferGetNextMessage) { ret_x(c, 0); }

// ---- Multisample state: coverage truncation is a hint the Vulkan back end has no switch for
NVN_API(nvnMultisampleStateSetCoverageTruncationEnable) { (void)c; }
NVN_API(nvnMultisampleStateGetCoverageTruncationEnable) { ret_x(c, 0); }

// ---- Window colour space and HDR metadata: the host presents SDR sRGB
// void nvnWindowBuilderSetColorSpace(NVNwindowBuilder *, NVNcolorSpace), NVNcolorSpace ...GetColorSpace(...): 0, sRGB
NVN_API(nvnWindowBuilderSetColorSpace) { NVN_LOG("WindowBuilderSetColorSpace %d", (int)arg_x(c, 1)); }
NVN_API(nvnWindowBuilderGetColorSpace) { ret_x(c, 0); }
// void ...Set{Hdr}Display/ContentMetadata(obj, const metadata *): kept by no one
NVN_API(nvnWindowBuilderSetHdrDisplayMetadata) { (void)c; }
NVN_API(nvnWindowBuilderSetHdrContentMetadata) { (void)c; }
NVN_API(nvnWindowSetHdrDisplayMetadata) { (void)c; }
NVN_API(nvnWindowSetHdrContentMetadata) { (void)c; }

// void nvnCommandBufferSetComputeOverlapMode(NVNcommandBuffer *cmd, uint32_t mode)
// NVN 2 changes compute/graphics scheduling, not shader or resource state (docs/nvn2-command-modes.md).
// Our dispatch and graphics-pass boundaries already insert full barriers on one queue, so both scheduling modes
// retain that conservative ordering. Allowing overlap does not require the host to execute work concurrently.
NVN_API(nvnCommandBufferSetComputeOverlapMode) { (void)c; }

// ---- Command buffer calls new in NVN 2. Until the Vulkan back end learns them (mesh shaders, ray tracing, DLSS,
// variable-rate shading, conditional enables, device command buffers), they are accepted and
// recorded nowhere, and the first call of each is logged, so a run lists what the back end still has to do. None of
// them returns a value.
#define NVN2_NOT_RECORDED(n)                                                                                         \
    NVN_API(n) {                                                                                                     \
        static volatile int32_t logged;                                                                              \
        (void)c;                                                                                                     \
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) rt_log("nvn2: " #n " accepted, not recorded yet");   \
    }
#define NVN2_MODE_NOT_RECORDED(n, kind)                                                                               \
    NVN_API(n) {                                                                                                    \
        static volatile int32_t logged;                                                                             \
        nvn_mode_trace(kind, arg_x(c, 0), 0, (uint32_t)arg_x(c, 1), 0);                                                \
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) rt_log("nvn2: " #n " accepted, not recorded yet");  \
    }
NVN2_MODE_NOT_RECORDED(nvnCommandBufferSetRecordingMode, NVN_MODE_RECORDING)
NVN2_NOT_RECORDED(nvnCommandBufferBeginDeviceCommands)
NVN2_NOT_RECORDED(nvnCommandBufferEndDeviceCommands)
NVN2_NOT_RECORDED(nvnCommandBufferSubmitDeviceCommands)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructures)
NVN2_NOT_RECORDED(nvnCommandBufferBindAccelerationStructuresDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferBuildAccelerationStructures)
NVN2_NOT_RECORDED(nvnCommandBufferCompactAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferCopyAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferRelocateAccelerationStructure)
NVN2_NOT_RECORDED(nvnCommandBufferDispatchRays)
NVN2_NOT_RECORDED(nvnCommandBufferDispatchRaysIndirect)
NVN2_NOT_RECORDED(nvnCommandBufferBindDeviceMessageBuffer)
NVN2_NOT_RECORDED(nvnCommandBufferBindDeviceMessageBufferDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferResetDeviceMessageBuffer)
NVN2_NOT_RECORDED(nvnCommandBufferResetDeviceMessageBufferDeferred)
NVN2_NOT_RECORDED(nvnCommandBufferBindShadingRateImage)
NVN2_NOT_RECORDED(nvnCommandBufferSetShadingRatePaletteMode)
NVN2_NOT_RECORDED(nvnCommandBufferShadingRateImagePalette)
NVN2_NOT_RECORDED(nvnCommandBufferComputeDirtyTileMap)
// Direct and single indirect mesh draws are recorded in nvn_api_cmd.c. Remaining variants report their entry point
// without attributing a program: a recording-thread TLS binding cannot represent inherited replay state.
#define NVN2_MESH_NOT_RECORDED(n) NVN_API(nvnCommandBuffer##n) { \
    static volatile int32_t told; \
    if (getenv("OOT_MESH_LOG") && !__atomic_exchange_n(&told, 1, __ATOMIC_RELAXED)) \
        rt_log("mesh: " #n " is not recorded yet (caller %#llx)", (unsigned long long)c->x[30]); \
}
NVN2_MESH_NOT_RECORDED(DrawMeshTasks3D)
NVN2_MESH_NOT_RECORDED(DrawMeshTasks3DIndirect)
NVN2_MESH_NOT_RECORDED(MultiDrawMeshTasks3DIndirect)
NVN2_MESH_NOT_RECORDED(MultiDrawMeshTasks3DIndirectCount)
NVN2_MESH_NOT_RECORDED(MultiDrawMeshTasksIndirect)
NVN2_MESH_NOT_RECORDED(MultiDrawMeshTasksIndirectCount)
NVN2_NOT_RECORDED(nvnCommandBufferEnableExclusiveScissor)
NVN2_NOT_RECORDED(nvnCommandBufferSetExclusiveScissor)
NVN2_NOT_RECORDED(nvnCommandBufferSetExclusiveScissors)
NVN2_NOT_RECORDED(nvnCommandBufferEvaluateDlss)
NVN2_NOT_RECORDED(nvnCommandBufferInsertDebugMarkerCompute)
NVN2_NOT_RECORDED(nvnCommandBufferInsertDebugMarkerGraphics)
NVN2_NOT_RECORDED(nvnCommandBufferPushDebugGroupCompute)
NVN2_NOT_RECORDED(nvnCommandBufferPushDebugGroupGraphics)
NVN2_NOT_RECORDED(nvnCommandBufferPopDebugGroupCompute)
NVN2_NOT_RECORDED(nvnCommandBufferPopDebugGroupGraphics)
// nvnCommandBufferPush{Graphics,Compute}Constants and their Deferred forms are recorded in nvn_api_cmd.c (OP_PUSH_CONST).
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeEnable)
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeEnableConditional)
NVN2_NOT_RECORDED(nvnCommandBufferSetCopyEnable)
NVN2_NOT_RECORDED(nvnCommandBufferSetCopyEnableConditional)
NVN2_NOT_RECORDED(nvnCommandBufferSetConservativeRasterMode)
NVN2_NOT_RECORDED(nvnCommandBufferSetRepresentativeFragmentTest)

// ---- Procs OoT 1.1.1 asks for in addition (SDK 22). Same policy as above: accepted, nothing recorded, the first call of each logged.
#define NVN2_RET0(n)                                                                                               \
    NVN_API(n) {                                                                                                   \
        static volatile int32_t logged;                                                                            \
        ret_x(c, 0);                                                                                               \
        if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED)) rt_log("nvn2: " #n " answers 0");                  \
    }
// NVNboolean: not registered, keeps the normal clear (as nvnDeviceRegisterFastClearStencil)
NVN2_RET0(nvnDeviceRegisterFastClearDepthIndex)
// NVNboolean: not registered, keeps the normal clear
NVN2_RET0(nvnDeviceRegisterFastClearStencilIndex)
// NVNqueue *: the host keeps no primary queue (SetPrimaryQueue only logs), so none
NVN2_RET0(nvnQueueBuilderGetPrimaryQueue)
// ray-tracing modules: none
NVN2_RET0(nvnProgramBuilderGetRayTracingModuleCount)
// ray-tracing module data: none
NVN2_RET0(nvnProgramBuilderGetRayTracingModuleData)
// DLSS network builder: no texture pool
NVN2_RET0(nvnDlssNetworkBuilderGetTexturePool)
// DLSS network builder: no texture ids
NVN2_RET0(nvnDlssNetworkBuilderGetTextureIDs)
// cooperative vectors are not offered: size 0
NVN2_RET0(nvnDeviceGetCooperativeVectorMatrixSize)
// cooperative vectors are not offered: not converted
NVN2_RET0(nvnDeviceConvertCooperativeVectorMatrix)
// dirty tile maps are not offered
NVN2_RET0(nvnDirtyTileMapParametersGetTexture)
// dirty tile maps are not offered
NVN2_RET0(nvnDirtyTileMapParametersGetInputs)
// dirty tile maps are not offered
NVN2_RET0(nvnDirtyTileMapParametersGetOutputs)
// uint64_t acquire timeout: the host waits for the swapchain image as long as it takes (an assumption; no title seen sets one)
NVN_API(nvnWindowBuilderGetAcquireTimeout) { ret_x(c, ~0ull); }
NVN_API(nvnWindowGetAcquireTimeout) { ret_x(c, ~0ull); }
// calls with no host effect
NVN2_NOT_RECORDED(nvnCommandBufferComputeDirtyTileMapEx)
NVN2_NOT_RECORDED(nvnCommandBufferConvertCooperativeVectorMatrix)
NVN2_NOT_RECORDED(nvnCommandBufferSetComputeWorkloadPriority)
NVN2_NOT_RECORDED(nvnCommandBufferSetGraphicsWorkloadPriority)
NVN2_NOT_RECORDED(nvnCommandBufferSetResourceOccupancy)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetConstantColorTable)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetDefaults)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetDepthStencilMask)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetExpandFootprint)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetInputs)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetOutputs)
NVN2_NOT_RECORDED(nvnDirtyTileMapParametersSetTexture)
NVN2_NOT_RECORDED(nvnDlssNetworkResize)
NVN2_NOT_RECORDED(nvnMultisampleStateGetSampleLocations)
NVN2_NOT_RECORDED(nvnSamplerPoolWriteDescriptorTableData)
NVN2_NOT_RECORDED(nvnTexturePoolWriteDescriptorTableData)
NVN2_NOT_RECORDED(nvnWindowBuilderSetAcquireTimeout)
NVN2_NOT_RECORDED(nvnWindowSetAcquireTimeout)
// more procs seen in the same run
NVN2_RET0(nvnDirtyTileMapParametersGetConstantColorTable)
NVN2_RET0(nvnDirtyTileMapParametersGetDepthStencilMask)
NVN2_RET0(nvnDirtyTileMapParametersGetExpandFootprint)
NVN2_NOT_RECORDED(nvnCommandBufferSetFloatBlendClamp)
NVN2_RET0(nvnCommandBufferGetAccelerationStructureVisualizationData)
