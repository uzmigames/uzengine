// uz_brixelizer_dll.cpp — UzEngine wrapper DLL for AMD FidelityFX Brixelizer GI.
//
// PROBLEM
// -------
// The pre-built FFX SDK 1.1.4 static libraries (`ffx_backend_dx12_x64.lib`,
// `ffx_brixelizer_x64.lib`, `ffx_brixelizergi_x64.lib`) were produced with
// MSVC and depend on the MSVC C++ runtime / STL ABI. UzEngine compiles with
// Zig, which uses mingw libc++ — name mangling and CRT entry points differ,
// so direct linkage from Zig fails (`unable to resolve external symbol
// __std_*`, `??$_Vector_alloc@*`, etc.).
//
// SOLUTION
// --------
// This wrapper is a tiny CMake/MSVC project that:
//   1. Statically links the three FFX libraries inside an MSVC-built DLL.
//   2. Re-exports the public C entry points used by `BrixelizerGIPass`.
//   3. Hides the C++ STL / CRT dependencies inside the DLL boundary — only
//      C function symbols cross the ABI line, which is link-compatible with
//      Zig/mingw at runtime via `LoadLibrary` + `GetProcAddress`.
//
// Same pattern as the DLSS integration, which `LoadLibrary`'s `_nvngx.dll`
// for the same reason. See `dlss_upscaler.cpp`.
//
// EXPORTS
// -------
// Every export below is a thin pass-through to an `FFX_API` C function from
// the FFX SDK headers. No new logic, no STL on the boundary, no surprises.
// The thunk style (`uz_brix_*`) is what `brixelizer_loader.cpp` resolves via
// `GetProcAddress` — using the original names risks colliding with any
// linker-supplied stub.

#define UZ_BRIX_EXPORT extern "C" __declspec(dllexport)

#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

// ─── DX12 backend ──────────────────────────────────────────────────────────

UZ_BRIX_EXPORT size_t uz_brix_GetScratchMemorySizeDX12(size_t maxContexts) {
    return ffxGetScratchMemorySizeDX12(maxContexts);
}

UZ_BRIX_EXPORT FfxDevice uz_brix_GetDeviceDX12(ID3D12Device* device) {
    return ffxGetDeviceDX12(device);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_GetInterfaceDX12(
    FfxInterface* backendInterface,
    FfxDevice     device,
    void*         scratchBuffer,
    size_t        scratchBufferSize,
    size_t        maxContexts) {
    return ffxGetInterfaceDX12(
        backendInterface, device, scratchBuffer, scratchBufferSize, maxContexts);
}

UZ_BRIX_EXPORT FfxCommandList uz_brix_GetCommandListDX12(ID3D12CommandList* cmdList) {
    return ffxGetCommandListDX12(cmdList);
}

UZ_BRIX_EXPORT FfxResource uz_brix_GetResourceDX12(
    const ID3D12Resource*  dx12Resource,
    FfxResourceDescription ffxResDescription,
    const wchar_t*         ffxResName,
    FfxResourceStates      state) {
    return ffxGetResourceDX12(dx12Resource, ffxResDescription, ffxResName, state);
}

// ─── Brixelizer context lifecycle ─────────────────────────────────────────

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerContextCreate(
    const FfxBrixelizerContextDescription* desc,
    FfxBrixelizerContext*                  context) {
    return ffxBrixelizerContextCreate(desc, context);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerContextDestroy(
    FfxBrixelizerContext* context) {
    return ffxBrixelizerContextDestroy(context);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerBakeUpdate(
    FfxBrixelizerContext*               context,
    const FfxBrixelizerUpdateDescription* desc,
    FfxBrixelizerBakedUpdateDescription* outBaked) {
    return ffxBrixelizerBakeUpdate(context, desc, outBaked);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerUpdate(
    FfxBrixelizerContext*                context,
    FfxBrixelizerBakedUpdateDescription* baked,
    FfxResource                          scratch,
    FfxCommandList                       cmdList) {
    return ffxBrixelizerUpdate(context, baked, scratch, cmdList);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerRegisterBuffers(
    FfxBrixelizerContext*           context,
    const FfxBrixelizerBufferDescription* desc,
    uint32_t                              numBuffers) {
    return ffxBrixelizerRegisterBuffers(context, desc, numBuffers);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerCreateInstances(
    FfxBrixelizerContext*                   context,
    const FfxBrixelizerInstanceDescription* desc,
    uint32_t                                numInstances) {
    return ffxBrixelizerCreateInstances(context, desc, numInstances);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerDeleteInstances(
    FfxBrixelizerContext*       context,
    const FfxBrixelizerInstanceID* ids,
    uint32_t                       numIds) {
    return ffxBrixelizerDeleteInstances(context, ids, numIds);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerGetRawContext(
    FfxBrixelizerContext*     context,
    FfxBrixelizerRawContext** outRaw) {
    return ffxBrixelizerGetRawContext(context, outRaw);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerGetContextInfo(
    FfxBrixelizerContext*     context,
    FfxBrixelizerContextInfo* contextInfo) {
    return ffxBrixelizerGetContextInfo(context, contextInfo);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerRawContextGetDebugCounters(
    FfxBrixelizerRawContext*   rawContext,
    FfxBrixelizerDebugCounters* outCounters) {
    return ffxBrixelizerRawContextGetDebugCounters(rawContext, outCounters);
}

// ─── BrixelizerGI context lifecycle ───────────────────────────────────────

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerGIContextCreate(
    FfxBrixelizerGIContext*                       context,
    const FfxBrixelizerGIContextDescription*      desc) {
    return ffxBrixelizerGIContextCreate(context, desc);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerGIContextDestroy(
    FfxBrixelizerGIContext* context) {
    return ffxBrixelizerGIContextDestroy(context);
}

UZ_BRIX_EXPORT FfxErrorCode uz_brix_BrixelizerGIContextDispatch(
    FfxBrixelizerGIContext*                  context,
    const FfxBrixelizerGIDispatchDescription* desc,
    FfxCommandList                           cmdList) {
    return ffxBrixelizerGIContextDispatch(context, desc, cmdList);
}
