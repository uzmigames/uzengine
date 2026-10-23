#pragma once

/// RTManager — Singleton owning the ray tracing infrastructure (BLAS, TLAS, SBT).
///
/// Coordinates BLAS building (per-mesh), TLAS rebuilding (per-frame), and
/// shader table construction. RT render graph passes query the TLAS SRV
/// index from this manager.
///
/// # Thread Safety
/// NOT thread-safe. All methods called from the render thread.

#include "blas_manager.h"
#include "tlas_manager.h"
#include "shader_table_builder.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
struct DrawCommand;
struct GPUMesh;

// ─────────────────────────────────────────────────────────────────────────────
// Phase B capability gate of the default-on reflections (pure; the gtests pin these)
// ─────────────────────────────────────────────────────────────────────────────

/// Whether the frame rebuilds the TLAS: every ray-traced feature under the `r.RayTracing` master
/// switch, reflections, or requested area-light shadows. Since Phase B reflections default on
/// and implies the TLAS build without switching any other RT feature on (DDGI, RT shadows / AO,
/// the path tracer's TLAS and the UV test pass stay under the master switch). Nothing on a
/// device without DXR (`dxrReady` = RTManager initialised).
[[nodiscard]] inline bool RTFrameNeedsTLAS(bool dxrReady, bool masterSwitch, bool reflectionsSwitch,
                                        bool areaLightShadowsSwitch = false) noexcept {
    return dxrReady && (masterSwitch || reflectionsSwitch || areaLightShadowsSwitch);
}

/// Whether the ray-traced reflection pass can run this frame: DXR initialised, the switch on and a
/// built, non-empty TLAS (an empty TLAS makes every ray miss: the DDGI writer's lesson). False is
/// the fallback — the pass is not registered and the frame renders the screen-space / probe
/// reflections it rendered before Phase B.
[[nodiscard]] inline bool RTReflectionsAvailable(bool dxrReady, bool reflectionsSwitch, uint32_t tlasInstances) noexcept {
    return dxrReady && reflectionsSwitch && tlasInstances > 0u;
}

// ─────────────────────────────────────────────────────────────────────────────
// RTManager
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the ray tracing sub-systems: BLAS cache, TLAS, shader tables.
///
/// # Lifecycle
/// 1. `Init(device, maxInstances)` — creates TLAS, initializes sub-managers.
/// 2. `BeginFrame(drawCommands, ..., cmdList)` — rebuilds TLAS from visible meshes.
/// 3. `GetTLASSRVIndex()` — used by RT passes to bind the TLAS in shaders.
/// 4. `Shutdown()` — destroys all RT resources.
class RTManager {
public:
    RTManager() = default;
    ~RTManager() = default;

    /// Initializes RT infrastructure. Returns false if RT is not supported (or
    /// `r.RayTracing.SimulateUnsupported` pretends it is not).
    ///
    /// # Arguments
    /// * `device` — RHI device with potential DXR support.
    /// * `maxInstances` — Maximum TLAS instance count (e.g., 4096).
    bool Init(IRHIDevice* device, uint32_t maxInstances = 4096);

    /// Rebuilds the TLAS from the current frame's draw commands when `NeedsTLAS()`.
    ///
    /// Call once per frame before any RT render passes.
    void BeginFrame(const DrawCommand* drawCommands, uint32_t drawCount,
                    const GPUMesh* meshes, uint32_t meshCount,
                    IRHICommandList* cmdList, bool hasAreaLights);

    /// Returns the TLAS SRV index in the global descriptor heap.
    [[nodiscard]] uint32_t GetTLASSRVIndex() const;
    /// Instances in the TLAS after the last BeginFrame() rebuild (0 when `r.RayTracing` is off:
    /// BeginFrame returns before Rebuild, so the TLAS stays empty and every RayQuery misses).
    [[nodiscard]] uint32_t GetTLASInstanceCount() const;

    /// Returns true if RT is initialized and ready for use.
    [[nodiscard]] bool IsReady() const { return m_initialized; }

    // ── CVar-driven feature queries (polled each frame) ─────────────────

    /// Returns true if RT is initialized AND r.RayTracing is enabled.
    [[nodiscard]] bool IsEnabled() const;

    /// Master switch, reflections, or area shadows when the frame carries area lights.
    [[nodiscard]] bool NeedsTLAS(bool hasAreaLights) const;

    /// Returns true if the ray-traced reflection pass can run this frame: IsReflectionsEnabled()
    /// with a built, non-empty TLAS (`RTReflectionsAvailable`). False = the SSR / probe fallback.
    [[nodiscard]] bool ReflectionsAvailable() const;

    [[nodiscard]] bool AreaLightShadowsAvailable() const;

    /// Returns true if the RT UV test pass (r.RayTracing.TestPass) is active.
    [[nodiscard]] bool IsTestPassEnabled() const;

    /// Returns true if RT shadows (r.RayTracing.Shadows) are active.
    [[nodiscard]] bool IsShadowsEnabled() const;

    /// Returns true if RT is initialized AND r.RayTracing.Reflections is on. Since Phase B the
    /// reflections do not need the master switch: the switch implies the TLAS build (NeedsTLAS).
    [[nodiscard]] bool IsReflectionsEnabled() const;

    /// Returns true if RT ambient occlusion (r.RayTracing.AO) is active.
    [[nodiscard]] bool IsAOEnabled() const;

    /// Returns true if RT global illumination (r.RayTracing.GI) is active.
    [[nodiscard]] bool IsGIEnabled() const;

    /// Returns a reference to the BLAS manager for direct access.
    [[nodiscard]] BLASManager& GetBLASManager() { return m_blasMgr; }

    /// Returns a reference to the TLAS manager for direct access.
    [[nodiscard]] TLASManager& GetTLASManager() { return m_tlasMgr; }

    /// Destroys all RT resources.
    void Shutdown();

private:
    IRHIDevice*   m_device      = nullptr;
    BLASManager   m_blasMgr;
    TLASManager   m_tlasMgr;
    bool          m_initialized = false;
};

} // namespace uz
