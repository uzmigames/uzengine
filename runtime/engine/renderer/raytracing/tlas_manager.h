#pragma once

/// TLASManager — Builds and maintains the top-level acceleration structure each frame.
///
/// Collects all visible mesh instances (transform + BLAS reference) and rebuilds
/// the TLAS for ray tracing passes. The TLAS is updated in-place (refit) when
/// only transforms change, or fully rebuilt when instances are added/removed.
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

    /// Returns true if the TLAS was successfully built at least once.
    [[nodiscard]] bool IsReady() const { return m_tlasHandle.IsValid(); }

    /// Destroys the TLAS, instance buffer, and frees all GPU resources.
    void Shutdown();

private:
    IRHIDevice*      m_device           = nullptr;
    RHITLASHandle    m_tlasHandle;
    RHIBufferHandle  m_instanceBuffer;
    uint32_t         m_maxInstances     = 0;
    uint32_t         m_lastInstanceCount = 0;
};

} // namespace uz
