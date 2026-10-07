#pragma once

/// TLASManager — Builds and maintains the top-level acceleration structure each frame.
///
/// Collects all visible mesh instances (transform + BLAS reference) and rebuilds
/// the TLAS for ray tracing passes. The instance descriptors are hashed each
/// frame (block B.6): an unchanged set skips the build, a set of the same size
/// with different transforms or BLAS addresses is refit in place (a full build
/// every `kRefitsBeforeRebuild` refits keeps the refit quality), and a changed
/// instance count is fully rebuilt. Before that the TLAS was rebuilt every
/// frame, static scene or not.
///
/// @ref Microsoft DXR spec — D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL.
///
/// # Thread Safety
/// NOT thread-safe. Call from the render thread only.

#include "rhi_types.h"
#include "blas_manager.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
struct DrawCommand;

// ─────────────────────────────────────────────────────────────────────────────
// TLASManager
// ─────────────────────────────────────────────────────────────────────────────

/// Builds and maintains the TLAS from scene draw commands each frame.
///
/// # Usage
/// ```cpp
/// TLASManager tlasMgr;
/// tlasMgr.Init(device, 4096); // max 4096 instances
///
/// // Each frame:
/// tlasMgr.Rebuild(drawList, blasMgr, cmdList);
/// uint32_t srvIndex = tlasMgr.GetTLASSRVIndex();
///
/// tlasMgr.Shutdown();
/// ```
class TLASManager {
public:
    TLASManager() = default;
    ~TLASManager() = default;

    /// Initializes the TLAS and instance buffer for a maximum instance count.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `maxInstances` — Maximum number of instances the TLAS supports.
    ///
    /// # Returns
    /// `true` on success, `false` if TLAS or instance buffer creation fails.
    bool Init(IRHIDevice* device, uint32_t maxInstances);

    /// Rebuilds the TLAS from the current frame's draw commands.
    ///
    /// Iterates the draw list, resolves each mesh's BLAS via `blasMgr`, fills
    /// the instance buffer, and builds the TLAS on the command list.
    ///
    /// # Arguments
    /// * `drawCommands` — Array of draw commands for visible meshes.
    /// * `drawCount` — Number of draw commands.
    /// * `meshes` — GPUMesh array indexed by meshId.
    /// * `meshCount` — Number of meshes in the array.
    /// * `blasMgr` — BLAS manager for resolving per-mesh BLAS handles.
    /// * `cmdList` — Command list for recording TLAS build commands.
    void Rebuild(const DrawCommand* drawCommands, uint32_t drawCount,
                 const GPUMesh* meshes, uint32_t meshCount,
                 BLASManager& blasMgr, IRHICommandList* cmdList);

    /// Returns the SRV descriptor index for the TLAS in the global heap.
    ///
    /// Shaders access the TLAS via `ResourceDescriptorHeap[index]` (SM6.6).
    [[nodiscard]] uint32_t GetTLASSRVIndex() const;
    /// Instances written by the last Rebuild(); 0 means the TLAS is empty (e.g. `r.RayTracing` off).
    [[nodiscard]] uint32_t GetLastInstanceCount() const noexcept { return m_lastInstanceCount; }

    /// Returns true if the TLAS was successfully built at least once.
    [[nodiscard]] bool IsReady() const { return m_tlasHandle.IsValid(); }

    /// Build statistics since Init() (block B.6): full builds, in-place refits and frames whose
    /// build was skipped because the instance descriptors were unchanged.
    [[nodiscard]] uint32_t GetFullBuildCount() const noexcept { return m_fullBuilds; }
    [[nodiscard]] uint32_t GetRefitCount() const noexcept     { return m_refits; }
    [[nodiscard]] uint32_t GetSkipCount() const noexcept      { return m_skips; }

    /// Refits between two full builds; a refit keeps the previous tree topology, so a long
    /// run of them under motion degrades trace performance (DXR spec, "Acceleration
    /// structure update constraints").
    static constexpr uint32_t kRefitsBeforeRebuild = 64;

    /// Destroys the TLAS, instance buffer, and frees all GPU resources.
    void Shutdown();

private:
    IRHIDevice*      m_device           = nullptr;
    RHITLASHandle    m_tlasHandle;
    RHIBufferHandle  m_instanceBuffer;
    uint32_t         m_maxInstances     = 0;
    uint32_t         m_lastInstanceCount = 0;
    uint64_t         m_lastInstanceHash = 0;     ///< FNV-1a 64 of the instance descriptors last written.
    uint32_t         m_fullBuilds       = 0;
    uint32_t         m_refits           = 0;
    uint32_t         m_skips            = 0;
    uint32_t         m_refitsSinceBuild = 0;
    bool             m_built            = false; ///< The TLAS holds a completed build.
    bool             m_skipLogged       = false;
};

} // namespace uz
