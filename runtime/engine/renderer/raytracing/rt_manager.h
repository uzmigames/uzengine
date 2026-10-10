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

    /// Initializes RT infrastructure. Returns false if RT is not supported.
    ///
    /// # Arguments
    /// * `device` — RHI device with potential DXR support.
    /// * `maxInstances` — Maximum TLAS instance count (e.g., 4096).
    bool Init(IRHIDevice* device, uint32_t maxInstances = 4096);

    /// Rebuilds the TLAS from the current frame's draw commands.
    ///
    /// Call once per frame before any RT render passes.
    void BeginFrame(const DrawCommand* drawCommands, uint32_t drawCount,
                    const GPUMesh* meshes, uint32_t meshCount,
                    IRHICommandList* cmdList);

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

    /// Returns true if the RT UV test pass (r.RayTracing.TestPass) is active.
    [[nodiscard]] bool IsTestPassEnabled() const;

    /// Returns true if RT shadows (r.RayTracing.Shadows) are active.
    [[nodiscard]] bool IsShadowsEnabled() const;

    /// Returns true if RT reflections (r.RayTracing.Reflections) are active.
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
