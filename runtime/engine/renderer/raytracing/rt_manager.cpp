/// RTManager Implementation — RT infrastructure lifecycle.

#include "rt_manager.h"
#include "rhi_device.h"
#include "core/cvar.h"
#include "log.h"

// ─────────────────────────────────────────────────────────────────────────────
// Ray Tracing CVars
// ─────────────────────────────────────────────────────────────────────────────

/// Master RT enable/disable toggle. When false, no TLAS is built and all RT
/// passes are skipped. Checked per-frame so it can be toggled at runtime.
static auto* cv_RTEnabled     = UZ_CVAR_BOOL("r.RayTracing", false,
    "Enable DXR ray tracing (requires DXR 1.0+ GPU)");

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

/// RT reflections enable toggle. When true, the RT reflection pass replaces SSR.
static auto* cv_RTReflections = UZ_CVAR_BOOL("r.RayTracing.Reflections", false,
    "Enable ray-traced reflections");

/// RT ambient occlusion enable toggle. When true, RT AO replaces GTAO.
static auto* cv_RTAO          = UZ_CVAR_BOOL("r.RayTracing.AO", false,
    "Enable ray-traced ambient occlusion");

/// RT global illumination enable toggle.
static auto* cv_RTGI          = UZ_CVAR_BOOL("r.RayTracing.GI", false,
    "Enable ray-traced global illumination");

namespace uz {

bool RTManager::Init(IRHIDevice* device, uint32_t maxInstances) {
    m_device = device;

    if (!device->SupportsRayTracing()) {
        UZ_LOG(Renderer, Info, "RTManager: Ray tracing not supported on this device");
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
    UZ_LOG(Renderer, Info, "RTManager: Initialized (maxInstances={}, r.RayTracing={})",
           maxInstances, cv_RTEnabled->value >= 0.5f ? "true" : "false");
    return true;
}

void RTManager::BeginFrame(const DrawCommand* drawCommands, uint32_t drawCount,
                            const GPUMesh* meshes, uint32_t meshCount,
                            IRHICommandList* cmdList) {
    if (!m_initialized) return;

    // Check master RT toggle each frame — allows runtime enable/disable
    if (cv_RTEnabled->value < 0.5f) return;

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

bool RTManager::IsTestPassEnabled() const {
    return IsEnabled() && cv_RTTestPass->value >= 0.5f;
}

bool RTManager::IsShadowsEnabled() const {
    return IsEnabled() && cv_RTShadows->value >= 0.5f;
}

bool RTManager::IsReflectionsEnabled() const {
    return IsEnabled() && cv_RTReflections->value >= 0.5f;
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
