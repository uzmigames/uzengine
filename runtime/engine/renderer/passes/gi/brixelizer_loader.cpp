/// UzEngine Brixelizer Loader — Implementation.
///
/// Resolves every `uz_brix_*` export from `uz_brixelizer.dll`. The DLL is
/// optional — when missing, BrixLoaderInit() returns false and the rest
/// of the engine continues without Brixelizer GI.

#include "brixelizer_loader.h"

#include "log.h"
#include "paths.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <atomic>
#include <filesystem>
#include <string>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─── Function pointer storage ─────────────────────────────────────────────

PFN_uzGetScratchMemorySizeDX12   pfn_uz_brix_GetScratchMemorySizeDX12   = nullptr;
PFN_uzGetDeviceDX12              pfn_uz_brix_GetDeviceDX12              = nullptr;
PFN_uzGetInterfaceDX12           pfn_uz_brix_GetInterfaceDX12           = nullptr;
PFN_uzGetCommandListDX12         pfn_uz_brix_GetCommandListDX12         = nullptr;
PFN_uzGetResourceDX12            pfn_uz_brix_GetResourceDX12            = nullptr;

PFN_uzBrixelizerContextCreate    pfn_uz_brix_BrixelizerContextCreate    = nullptr;
PFN_uzBrixelizerContextDestroy   pfn_uz_brix_BrixelizerContextDestroy   = nullptr;
PFN_uzBrixelizerBakeUpdate       pfn_uz_brix_BrixelizerBakeUpdate       = nullptr;
PFN_uzBrixelizerUpdate           pfn_uz_brix_BrixelizerUpdate           = nullptr;
PFN_uzBrixelizerRegisterBuffers  pfn_uz_brix_BrixelizerRegisterBuffers  = nullptr;
PFN_uzBrixelizerCreateInstances  pfn_uz_brix_BrixelizerCreateInstances  = nullptr;
PFN_uzBrixelizerDeleteInstances  pfn_uz_brix_BrixelizerDeleteInstances  = nullptr;
PFN_uzBrixelizerGetContextInfo   pfn_uz_brix_BrixelizerGetContextInfo   = nullptr;
PFN_uzBrixelizerGetRawContext    pfn_uz_brix_BrixelizerGetRawContext    = nullptr;
PFN_uzBrixelizerRawDebugCounters pfn_uz_brix_BrixelizerRawContextGetDebugCounters = nullptr;

PFN_uzBrixelizerGIContextCreate   pfn_uz_brix_BrixelizerGIContextCreate   = nullptr;
PFN_uzBrixelizerGIContextDestroy  pfn_uz_brix_BrixelizerGIContextDestroy  = nullptr;
PFN_uzBrixelizerGIContextDispatch pfn_uz_brix_BrixelizerGIContextDispatch = nullptr;

#ifdef _WIN32
static HMODULE g_brixDll = nullptr;
#else
static void*   g_brixDll = nullptr;
#endif

static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_loadAttempted{false};

// ─── Helpers ──────────────────────────────────────────────────────────────

#ifdef _WIN32

static void* BrixGetProc(const char* name) {
    if (!g_brixDll) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(g_brixDll, name));
}

template <typename FnPtr>
static bool ResolveSymbol(FnPtr& outPtr, const char* name) {
    void* sym = BrixGetProc(name);
    if (!sym) {
        UZ_LOG(Renderer, Warning, "BrixLoader: missing export '%s' in uz_brixelizer.dll", name);
        return false;
    }
    outPtr = reinterpret_cast<FnPtr>(sym);
    return true;
}

#endif

// ─── Public API ───────────────────────────────────────────────────────────

bool BrixLoaderInit() {
    if (g_initialized.load()) return true;
    if (g_loadAttempted.load()) return g_initialized.load();
    g_loadAttempted.store(true);

#ifndef _WIN32
    // Brixelizer GI is DX12-only — no Linux/macOS path.
    return false;
#else
    // Look next to the executable first, then in the working directory.
    const std::filesystem::path dllPath =
        uz::GetExeDir() / "uz_brixelizer.dll";
    g_brixDll = LoadLibraryW(dllPath.wstring().c_str());
    if (!g_brixDll) {
        g_brixDll = LoadLibraryW(L"uz_brixelizer.dll");
    }
    if (!g_brixDll) {
        UZ_LOG(Renderer, Info,
               "BrixLoader: uz_brixelizer.dll not found "
               "(Brixelizer GI disabled). Build it via "
               "runtime/thirdparty/amd-fidelityfx-1.1/wrapper/CMakeLists.txt");
        return false;
    }

    bool ok = true;
    ok &= ResolveSymbol(pfn_uz_brix_GetScratchMemorySizeDX12,   "uz_brix_GetScratchMemorySizeDX12");
    ok &= ResolveSymbol(pfn_uz_brix_GetDeviceDX12,              "uz_brix_GetDeviceDX12");
    ok &= ResolveSymbol(pfn_uz_brix_GetInterfaceDX12,           "uz_brix_GetInterfaceDX12");
    ok &= ResolveSymbol(pfn_uz_brix_GetCommandListDX12,         "uz_brix_GetCommandListDX12");
    ok &= ResolveSymbol(pfn_uz_brix_GetResourceDX12,            "uz_brix_GetResourceDX12");

    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerContextCreate,    "uz_brix_BrixelizerContextCreate");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerContextDestroy,   "uz_brix_BrixelizerContextDestroy");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerBakeUpdate,       "uz_brix_BrixelizerBakeUpdate");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerUpdate,           "uz_brix_BrixelizerUpdate");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerRegisterBuffers,  "uz_brix_BrixelizerRegisterBuffers");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerCreateInstances,  "uz_brix_BrixelizerCreateInstances");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerDeleteInstances,  "uz_brix_BrixelizerDeleteInstances");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerGetContextInfo,   "uz_brix_BrixelizerGetContextInfo");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerGetRawContext,    "uz_brix_BrixelizerGetRawContext");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerRawContextGetDebugCounters,
                        "uz_brix_BrixelizerRawContextGetDebugCounters");

    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerGIContextCreate,   "uz_brix_BrixelizerGIContextCreate");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerGIContextDestroy,  "uz_brix_BrixelizerGIContextDestroy");
    ok &= ResolveSymbol(pfn_uz_brix_BrixelizerGIContextDispatch, "uz_brix_BrixelizerGIContextDispatch");

    if (!ok) {
        UZ_LOG(Renderer, Error,
               "BrixLoader: uz_brixelizer.dll loaded but one or more exports "
               "are missing. Brixelizer GI disabled.");
        FreeLibrary(g_brixDll);
        g_brixDll = nullptr;
        return false;
    }

    UZ_LOG(Renderer, Info,
           "BrixLoader: uz_brixelizer.dll loaded successfully (18 entry points bound)");
    g_initialized.store(true);
    return true;
#endif
}

bool BrixLoaderReady() {
    return g_initialized.load();
}

void BrixLoaderShutdown() {
#ifdef _WIN32
    if (g_brixDll) {
        FreeLibrary(g_brixDll);
        g_brixDll = nullptr;
    }
#endif
    g_initialized.store(false);
    g_loadAttempted.store(false);
}

}  // namespace uz
