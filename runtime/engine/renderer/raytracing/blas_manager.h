#pragma once

/// BLASManager — Builds and caches per-mesh bottom-level acceleration structures.
///
/// Each unique mesh gets one BLAS, keyed by its mesh ID (index into the
/// SceneRenderer mesh array). On first request, the BLAS is created and
/// built on a command list. Subsequent requests return the cached handle.
///
/// @ref Microsoft DXR spec — D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL.
///
/// # Thread Safety
/// NOT thread-safe. Call from the render thread only.
///
/// # Lifetime
/// Owned by `RTManager`. Created once at RT init, destroyed at shutdown.

#include "rhi_types.h"
#include "mesh/gpu_mesh.h"

#include <cstdint>
#include <unordered_map>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// BLASManager
// ─────────────────────────────────────────────────────────────────────────────

/// Manages per-mesh BLAS creation, caching, and rebuild on mesh change.
///
/// # Usage
/// ```cpp
/// BLASManager blasMgr;
/// blasMgr.Init(device);
///
/// // Per-frame (or on mesh load):
/// RHIBLASHandle blas = blasMgr.GetOrBuildBLAS(meshId, gpuMesh, cmdList);
///
/// // On mesh change (e.g., LOD switch, deformation):
/// blasMgr.InvalidateMesh(meshId);
///
/// blasMgr.Shutdown();
/// ```
class BLASManager {
public:
    BLASManager() = default;
    ~BLASManager() = default;

    /// Initializes the BLAS manager with the given RHI device.
    ///
    /// # Arguments
    /// * `device` — RHI device for acceleration structure creation.
    void Init(IRHIDevice* device);

    /// Returns an existing BLAS for the given mesh ID, or builds a new one.
    ///
    /// On cache miss: creates a new BLAS from the mesh's vertex/index buffers,
    /// records the build command on `cmdList`, and inserts a UAV barrier.
    ///
    /// # Arguments
    /// * `meshId` — Index into the mesh array (cache key).
    /// * `mesh` — GPU mesh with vertex/index buffer handles.
    /// * `cmdList` — Command list for recording BLAS build commands.
    ///
    /// # Returns
    /// A valid `RHIBLASHandle` on success, `RHIBLASHandle::Invalid()` on failure.
    [[nodiscard]] RHIBLASHandle GetOrBuildBLAS(uint32_t meshId, const GPUMesh& mesh,
                                               IRHICommandList* cmdList);

    /// Invalidates the cached BLAS for a mesh, forcing rebuild on next request.
    ///
    /// Call when a mesh's geometry changes (LOD switch, vertex deformation, etc.).
    ///
    /// # Arguments
    /// * `meshId` — Mesh ID to invalidate.
    void InvalidateMesh(uint32_t meshId);

    /// Destroys all cached BLAS handles and frees GPU resources.
    void Shutdown();

    /// Returns the number of cached BLAS entries.
    [[nodiscard]] uint32_t GetCachedCount() const {
        return static_cast<uint32_t>(m_cache.size());
    }

private:
    IRHIDevice* m_device = nullptr;

    /// Cache: meshId → BLAS handle.
    std::unordered_map<uint32_t, RHIBLASHandle> m_cache;
};

} // namespace uz
