/// UzEngine Brixelizer Loader — runtime LoadLibrary glue for `uz_brixelizer.dll`.
///
/// AMD's pre-built FidelityFX Brixelizer GI 1.1.4 static libraries are
/// MSVC-CRT bound and cannot be linked from Zig/mingw. The accompanying
/// `uz_brixelizer.dll` (built via `runtime/thirdparty/amd-fidelityfx-1.1/wrapper`)
/// statically links those libs inside an MSVC DLL boundary and re-exports
/// every public C entry point as `uz_brix_*`. This loader resolves those
/// thunks at startup and remaps the original `ffx*` names back via macros
/// so the existing `brixelizer_gi_pass.cpp` call sites stay unchanged.
///
/// The DLL is optional. When it is missing the loader returns `false` from
/// `Load()` and the BrixelizerGIPass falls back to the empty stub path.
///
/// @ref AMD 2024 "FidelityFX Brixelizer GI"
/// @ref UzEngine docs/analysis/dlss-integration-analysis.md — same MSVC/CRT
///                                                            workaround.

#pragma once

#include <FidelityFX/host/ffx_brixelizer.h>
#include <FidelityFX/host/ffx_brixelizergi.h>

struct ID3D12Device;
struct ID3D12CommandList;
struct ID3D12Resource;

namespace uz {

/// Loads `uz_brixelizer.dll` (next to the executable) and resolves every
/// `uz_brix_*` export. Subsequent calls are idempotent.
///
/// # Returns
/// `true` once all required symbols are bound, `false` otherwise. Once
/// `false` is returned the rest of the loader function pointers are
/// guaranteed to be `nullptr` and any FFX call will crash — callers must
/// check `BrixLoaderReady()` before invoking the API.
[[nodiscard]] bool BrixLoaderInit();

/// Returns `true` if `BrixLoaderInit()` succeeded.
[[nodiscard]] bool BrixLoaderReady();

/// Releases the DLL handle. Safe to call even when not loaded.
void BrixLoaderShutdown();

// ─── Function pointer table ────────────────────────────────────────────────
//
// One entry per `uz_brix_*` export. They are populated by `BrixLoaderInit()`
// and remapped to the original FFX names via the macros at the bottom of
// this header so call sites in `brixelizer_gi_pass.cpp` stay readable.

using PFN_uzGetScratchMemorySizeDX12 = size_t (*)(size_t);
using PFN_uzGetDeviceDX12            = FfxDevice (*)(ID3D12Device*);
using PFN_uzGetInterfaceDX12         = FfxErrorCode (*)(FfxInterface*, FfxDevice, void*, size_t, size_t);
using PFN_uzGetCommandListDX12       = FfxCommandList (*)(ID3D12CommandList*);
using PFN_uzGetResourceDX12          = FfxResource (*)(const ID3D12Resource*, FfxResourceDescription, const wchar_t*, FfxResourceStates);

using PFN_uzBrixelizerContextCreate   = FfxErrorCode (*)(const FfxBrixelizerContextDescription*, FfxBrixelizerContext*);
using PFN_uzBrixelizerContextDestroy  = FfxErrorCode (*)(FfxBrixelizerContext*);
using PFN_uzBrixelizerBakeUpdate      = FfxErrorCode (*)(FfxBrixelizerContext*, const FfxBrixelizerUpdateDescription*, FfxBrixelizerBakedUpdateDescription*);
using PFN_uzBrixelizerUpdate          = FfxErrorCode (*)(FfxBrixelizerContext*, FfxBrixelizerBakedUpdateDescription*, FfxResource, FfxCommandList);
using PFN_uzBrixelizerRegisterBuffers = FfxErrorCode (*)(FfxBrixelizerContext*, const FfxBrixelizerBufferDescription*, uint32_t);
using PFN_uzBrixelizerCreateInstances = FfxErrorCode (*)(FfxBrixelizerContext*, const FfxBrixelizerInstanceDescription*, uint32_t);
using PFN_uzBrixelizerDeleteInstances = FfxErrorCode (*)(FfxBrixelizerContext*, const FfxBrixelizerInstanceID*, uint32_t);
using PFN_uzBrixelizerGetContextInfo  = FfxErrorCode (*)(FfxBrixelizerContext*, FfxBrixelizerContextInfo*);
using PFN_uzBrixelizerGetRawContext   = FfxErrorCode (*)(FfxBrixelizerContext*, FfxBrixelizerRawContext**);
using PFN_uzBrixelizerRawDebugCounters = FfxErrorCode (*)(FfxBrixelizerRawContext*, FfxBrixelizerDebugCounters*);

using PFN_uzBrixelizerGIContextCreate   = FfxErrorCode (*)(FfxBrixelizerGIContext*, const FfxBrixelizerGIContextDescription*);
using PFN_uzBrixelizerGIContextDestroy  = FfxErrorCode (*)(FfxBrixelizerGIContext*);
using PFN_uzBrixelizerGIContextDispatch = FfxErrorCode (*)(FfxBrixelizerGIContext*, const FfxBrixelizerGIDispatchDescription*, FfxCommandList);

extern PFN_uzGetScratchMemorySizeDX12   pfn_uz_brix_GetScratchMemorySizeDX12;
extern PFN_uzGetDeviceDX12              pfn_uz_brix_GetDeviceDX12;
extern PFN_uzGetInterfaceDX12           pfn_uz_brix_GetInterfaceDX12;
extern PFN_uzGetCommandListDX12         pfn_uz_brix_GetCommandListDX12;
extern PFN_uzGetResourceDX12            pfn_uz_brix_GetResourceDX12;

extern PFN_uzBrixelizerContextCreate    pfn_uz_brix_BrixelizerContextCreate;
extern PFN_uzBrixelizerContextDestroy   pfn_uz_brix_BrixelizerContextDestroy;
extern PFN_uzBrixelizerBakeUpdate       pfn_uz_brix_BrixelizerBakeUpdate;
extern PFN_uzBrixelizerUpdate           pfn_uz_brix_BrixelizerUpdate;
extern PFN_uzBrixelizerRegisterBuffers  pfn_uz_brix_BrixelizerRegisterBuffers;
extern PFN_uzBrixelizerCreateInstances  pfn_uz_brix_BrixelizerCreateInstances;
extern PFN_uzBrixelizerDeleteInstances  pfn_uz_brix_BrixelizerDeleteInstances;
extern PFN_uzBrixelizerGetContextInfo   pfn_uz_brix_BrixelizerGetContextInfo;
extern PFN_uzBrixelizerGetRawContext    pfn_uz_brix_BrixelizerGetRawContext;
extern PFN_uzBrixelizerRawDebugCounters pfn_uz_brix_BrixelizerRawContextGetDebugCounters;

extern PFN_uzBrixelizerGIContextCreate   pfn_uz_brix_BrixelizerGIContextCreate;
extern PFN_uzBrixelizerGIContextDestroy  pfn_uz_brix_BrixelizerGIContextDestroy;
extern PFN_uzBrixelizerGIContextDispatch pfn_uz_brix_BrixelizerGIContextDispatch;

}  // namespace uz

// ─── Name remap macros ─────────────────────────────────────────────────────
//
// Include this header AFTER the FFX headers so that the original symbols
// are first declared (giving the type system the right signatures), then
// shadowed by macros that route every call through the loaded function
// pointer. The macros are namespaced via `::uz::pfn_*` so they cannot be
// confused with anything else in the project.

#define ffxGetScratchMemorySizeDX12   ::uz::pfn_uz_brix_GetScratchMemorySizeDX12
#define ffxGetDeviceDX12              ::uz::pfn_uz_brix_GetDeviceDX12
#define ffxGetInterfaceDX12           ::uz::pfn_uz_brix_GetInterfaceDX12
#define ffxGetCommandListDX12         ::uz::pfn_uz_brix_GetCommandListDX12
#define ffxGetResourceDX12            ::uz::pfn_uz_brix_GetResourceDX12

#define ffxBrixelizerContextCreate    ::uz::pfn_uz_brix_BrixelizerContextCreate
#define ffxBrixelizerContextDestroy   ::uz::pfn_uz_brix_BrixelizerContextDestroy
#define ffxBrixelizerBakeUpdate       ::uz::pfn_uz_brix_BrixelizerBakeUpdate
#define ffxBrixelizerUpdate           ::uz::pfn_uz_brix_BrixelizerUpdate
#define ffxBrixelizerRegisterBuffers  ::uz::pfn_uz_brix_BrixelizerRegisterBuffers
#define ffxBrixelizerCreateInstances  ::uz::pfn_uz_brix_BrixelizerCreateInstances
#define ffxBrixelizerDeleteInstances  ::uz::pfn_uz_brix_BrixelizerDeleteInstances
#define ffxBrixelizerGetContextInfo   ::uz::pfn_uz_brix_BrixelizerGetContextInfo
#define ffxBrixelizerGetRawContext    ::uz::pfn_uz_brix_BrixelizerGetRawContext
#define ffxBrixelizerRawContextGetDebugCounters ::uz::pfn_uz_brix_BrixelizerRawContextGetDebugCounters

#define ffxBrixelizerGIContextCreate   ::uz::pfn_uz_brix_BrixelizerGIContextCreate
#define ffxBrixelizerGIContextDestroy  ::uz::pfn_uz_brix_BrixelizerGIContextDestroy
#define ffxBrixelizerGIContextDispatch ::uz::pfn_uz_brix_BrixelizerGIContextDispatch
