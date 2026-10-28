/// RTManager Implementation — RT infrastructure lifecycle.

#include "rt_manager.h"
#include "rhi_device.h"
#include "core/cvar.h"
#include "log.h"

// ─────────────────────────────────────────────────────────────────────────────
// Ray Tracing CVars
// ─────────────────────────────────────────────────────────────────────────────

/// Master RT enable/disable toggle. When false, no TLAS is built and all RT
/// passes are skipped — except the default-on reflections (Phase B), which
/// build the TLAS on their own (see RTFrameNeedsTLAS). Checked per-frame so it
/// can be toggled at runtime.
static auto* cv_RTEnabled     = UZ_CVAR_BOOL("r.RayTracing", false,
    "Enable DXR ray tracing (requires DXR 1.0+ GPU)");

/// Diagnostic: pretend the device has no DXR support, so RTManager::Init fails as on a
/// non-DXR GPU and every ray-traced feature — the default-on reflections included — takes
/// its fallback. Read once at Init (set it from the project or the command line).
static auto* cv_RTSimulateUnsupported = UZ_CVAR_BOOL("r.RayTracing.SimulateUnsupported", false,
    "Diagnostic: treat the device as having no DXR support (RT init fails, every RT feature falls back)");

/// Maximum number of TLAS instances. Determines allocation size for the
/// instance buffer and TLAS scratch. Increase for dense scenes.
static auto* cv_RTMaxInstances = UZ_CVAR_INT("r.RayTracing.MaxInstances", 4096,
    "Maximum TLAS instance count", 256, 65536);

/// Enable the RT UV test pass (debug visualization — UV gradient via DispatchRays).
static auto* cv_RTTestPass    = UZ_CVAR_BOOL("r.RayTracing.TestPass", false,
    "Enable RT UV test pass (debug — outputs UV gradient via DispatchRays)");

/// RT shadow enable toggle. When true, the RT shadow pass replaces rasterized shadows.
static auto* cv_RTShadows     = UZ_CVAR_BOOL("r.RayTracing.Shadows", false,
    "Enable ray-traced shadows");

/// RT reflections toggle — the real-time default since Phase B: on a DXR device the
/// reflection pass runs on a TLAS built for it (no master switch needed), filling the
/// screen trace's holes or owning the reflection buffer with SSR off. Without DXR, or
/// with an empty TLAS, the frame falls back to its screen-space / probe reflections;
/// 0 restores that pre-Phase-B path exactly (no TLAS, no pass).
static auto* cv_RTReflections = UZ_CVAR_BOOL("r.RayTracing.Reflections", true,
    "Hardware ray-traced reflections (default on where DXR is supported; SSR/probe fallback otherwise)");

/// RT ambient occlusion enable toggle. When true, RT AO replaces GTAO.
static auto* cv_RTAO          = UZ_CVAR_BOOL("r.RayTracing.AO", false,
    "Enable ray-traced ambient occlusion");

/// RT global illumination enable toggle.
static auto* cv_RTGI          = UZ_CVAR_BOOL("r.RayTracing.GI", false,
    "Enable ray-traced global illumination");
static auto* cv_FinalGatherDxr = UZ_CVAR_INT("r.GI.FinalGather.DXR", 0,
    "Final gather visibility: 0=SDF, 1=DXR gather and sun, 2=SDF gather with DXR sun (diagnostic); "
    "requires plain world gather, falls back to SDF without DXR", 0, 2);

static auto* cv_AreaLightShadows = UZ_CVAR_BOOL("r.Lighting.AreaLightShadows", true,
    "DXR soft shadows for LTC rectangle, disk and tube lights (requires a non-empty TLAS; "
    "frames without area lights or without DXR are unchanged)");

namespace uz {

bool RTManager::Init(IRHIDevice* device, uint32_t maxInstances) {
    m_device = device;

    if (!device->SupportsRayTracing()) {
        UZ_LOG(Renderer, Info, "RTManager: Ray tracing not supported on this device");
        return false;
    }
    if (cv_RTSimulateUnsupported->value >= 0.5f) {
        UZ_LOG(Renderer, Info, "RTManager: r.RayTracing.SimulateUnsupported=1 — treating the device as having no DXR support");
        return false;
    }

    // Use CVar for max instances if caller passed default
    if (maxInstances == 4096) {
        maxInstances = static_cast<uint32_t>(cv_RTMaxInstances->value);
    }

    m_blasMgr.Init(device);

    if (!m_tlasMgr.Init(device, maxInstances)) {
        UZ_LOG(Renderer, Error, "RTManager: Failed to initialize TLAS manager");
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "RTManager: Initialized (maxInstances={}, r.RayTracing={}, r.RayTracing.Reflections={})",
           maxInstances, cv_RTEnabled->value >= 0.5f ? "true" : "false",
           cv_RTReflections->value >= 0.5f ? "true" : "false");
    return true;
}

void RTManager::BeginFrame(const DrawCommand* drawCommands, uint32_t drawCount,
                            const GPUMesh* meshes, uint32_t meshCount,
                            IRHICommandList* cmdList, bool hasAreaLights) {
    // Checked each frame — the master switch and the reflections switch can be toggled at
    // runtime; with both off nothing is built and the TLAS stays empty.
    if (!NeedsTLAS(hasAreaLights)) return;

    m_tlasMgr.Rebuild(drawCommands, drawCount, meshes, meshCount, m_blasMgr, cmdList);
}

uint32_t RTManager::GetTLASSRVIndex() const {
    if (!m_initialized) return UINT32_MAX;
    return m_tlasMgr.GetTLASSRVIndex();
}

uint32_t RTManager::GetTLASInstanceCount() const {
    if (!m_initialized) return 0;
    return m_tlasMgr.GetLastInstanceCount();
}

bool RTManager::IsEnabled() const {
    return m_initialized && cv_RTEnabled->value >= 0.5f;
}

bool RTManager::NeedsTLAS(bool hasAreaLights) const {
    return RTFrameNeedsTLAS(m_initialized, cv_RTEnabled->value >= 0.5f,
                           cv_RTReflections->value >= 0.5f || cv_FinalGatherDxr->value >= 1.0f,
                           hasAreaLights && cv_AreaLightShadows->value >= 0.5f);
}

bool RTManager::AreaLightShadowsAvailable() const {
    return RTReflectionsAvailable(m_initialized, cv_AreaLightShadows->value >= 0.5f, GetTLASInstanceCount());
}

bool RTManager::ReflectionsAvailable() const {
    return RTReflectionsAvailable(m_initialized, cv_RTReflections->value >= 0.5f, GetTLASInstanceCount());
}

bool RTManager::IsTestPassEnabled() const {
    return IsEnabled() && cv_RTTestPass->value >= 0.5f;
}

bool RTManager::IsShadowsEnabled() const {
    return IsEnabled() && cv_RTShadows->value >= 0.5f;
}

bool RTManager::IsReflectionsEnabled() const {
    // Phase B: the reflections stand on the DXR capability alone, not on the master switch.
    return m_initialized && cv_RTReflections->value >= 0.5f;
}

bool RTManager::IsAOEnabled() const {
    return IsEnabled() && cv_RTAO->value >= 0.5f;
}

bool RTManager::IsGIEnabled() const {
    return IsEnabled() && cv_RTGI->value >= 0.5f;
}

void RTManager::Shutdown() {
    if (!m_initialized) return;

    m_tlasMgr.Shutdown();
    m_blasMgr.Shutdown();
    m_initialized = false;
    m_device = nullptr;
    UZ_LOG(Renderer, Info, "RTManager: Shutdown complete");
}

} // namespace uz
