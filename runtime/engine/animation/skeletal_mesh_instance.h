/// UzEngine SkeletalMeshInstance — Per-entity skinning state.
///
/// A SkeletalMeshInstance owns everything required to drive **one** animated
/// character at runtime:
///   - Pointer to a shared `Skeleton` (immutable after load).
///   - Pointer to the source `Mesh` holding the original 56 B SkinnedVertex[].
///   - A per-instance bone palette (kMaxBones matrices).
///   - A per-instance 48 B Vertex[] output buffer filled each frame by CPU
///     linear blend skinning and uploaded into the engine's mesh registry.
///   - The ID of that procedural mesh entry.
///
/// # Threading
/// `UpdatePaletteFromPose()` and `SkinCpu()` are side-effect-free pure CPU
/// functions. They only write to members of THIS instance, so invoking them
/// from a worker thread for a batch of instances (via JobSystem::ParallelFor)
/// is safe as long as no two jobs share the same instance.
///
/// `GetOutputVertexData()` and `GetOutputMeshId()` are read-only and return
/// the result of the last `SkinCpu()` call. The render thread reads them to
/// upload the bytes into the GPU vertex buffer — no locking required because
/// the game tick (which runs the animation) completes before the renderer
/// dispatches.

#pragma once

#include "bone_transform.h"
#include "pose.h"
#include "mat4.h"
#include "rhi_types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace uz {

class Skeleton;
struct Mesh;

// ─────────────────────────────────────────────────────────────────────────────
// SkeletalMeshInstance
// ─────────────────────────────────────────────────────────────────────────────

class SkeletalMeshInstance {
public:
    SkeletalMeshInstance() = default;

    /// Binds the source mesh and skeleton. `sourceMesh` must hold
    /// `skinnedVertexData` (56 B SkinnedVertex stride) for skinning to work.
    ///
    /// `outputMeshId` is the ID of the procedural Vertex mesh registered with
    /// the scene renderer — the instance owns the CPU copy and the subsystem
    /// uploads it each frame.
    void Bind(const Mesh* sourceMesh, const Skeleton* skeleton, uint32_t outputMeshId);

    /// Resets the current pose to the skeleton bind pose.
    /// Must be called after `Bind()` before the first `SkinCpu()`.
    void ResetToBindPose();

    /// Returns true if `Bind()` has been called with a non-null mesh + skeleton.
    [[nodiscard]] bool IsValid() const {
        return m_sourceMesh != nullptr && m_skeleton != nullptr;
    }

    // ── Pose state ──────────────────────────────────────────────────────

    /// Replaces the current pose. Size is validated against the skeleton.
    ///
    /// Thread-safe: takes `m_poseMutex` so AnimationController writes and
    /// SkinCpu reads don't race. In practice the subsystem writes the pose
    /// from a worker thread, then SkinCpu runs in the same worker before the
    /// main thread sync point — the mutex is a cheap safety net.
    void SetPose(const Pose& pose);

    /// Returns a copy of the current pose.
    [[nodiscard]] Pose GetPose() const;

    // ── Per-frame update (runs on worker thread) ────────────────────────

    /// Computes the bone palette (world bone matrix × inverseBind) from the
    /// current pose. Result is stored in `m_palette` (kMaxBones matrices).
    void UpdatePaletteFromPose();

    /// Applies linear blend skinning to the source SkinnedVertex[] and writes
    /// the 48 B Vertex output into `m_outputVertexData`.
    ///
    /// Must be called AFTER `UpdatePaletteFromPose()` — the palette is the
    /// input to skinning.
    void SkinCpu();

    // ── Render-thread access (read-only after game tick completes) ─────

    /// Returns the ID of the procedural mesh registered in the scene renderer.
    [[nodiscard]] uint32_t GetOutputMeshId() const { return m_outputMeshId; }

    /// Returns the bytes filled by the last `SkinCpu()`. Size = vertexCount × 48.
    [[nodiscard]] const std::vector<uint8_t>& GetOutputVertexData() const {
        return m_outputVertexData;
    }

    /// True when `m_outputVertexData` has been mutated since the last GPU
    /// upload. The render-thread uploader consumes + clears this flag so that
    /// the 5.76 MB VB copy is skipped on frames where nothing changed (static
    /// bind pose, or animations with no per-frame delta).
    [[nodiscard]] bool IsOutputDirty() const { return m_outputDirty; }

    /// Clears the dirty flag. Call exactly once after a successful GPU upload.
    void ClearOutputDirty() { m_outputDirty = false; }

    /// Returns the source mesh pointer (for GPU palette index-count decisions).
    [[nodiscard]] const Mesh* GetSourceMesh() const { return m_sourceMesh; }

    /// Returns the skeleton pointer.
    [[nodiscard]] const Skeleton* GetSkeleton() const { return m_skeleton; }

    /// Returns the current bone palette (model-space bone × inverseBind).
    [[nodiscard]] const std::vector<Mat4>& GetPalette() const { return m_palette; }

    /// Returns the model-space world TRS for each bone computed by the last
    /// `UpdatePaletteFromPose()` call. Empty until the first pose evaluation.
    /// Indexed by bone index; use `Skeleton::FindBoneIndex(name)` to resolve
    /// a name to an index.
    [[nodiscard]] const std::vector<BoneTransform>& GetWorldBoneTransforms() const {
        return m_worldBoneTransforms;
    }

    /// Composes the model-space world matrix for the bone at `boneIdx`.
    /// Returns `Mat4::Identity()` if the index is out of range or the world
    /// transforms have not yet been computed.
    [[nodiscard]] Mat4 GetBoneWorldMatrix(int32_t boneIdx) const;

    // ── GPU skinning resources (managed by SkeletalMeshSubsystem) ──────

    /// Input structured buffer holding the 56 B SkinnedVertex[] stream.
    /// Uploaded once at Finalize. Read as SRV by `gpu_skinning_cs.hlsl`.
    [[nodiscard]] RHIBufferHandle GetGpuInputBuffer() const { return m_gpuInputBuffer; }
    [[nodiscard]] uint32_t        GetGpuInputSrv() const    { return m_gpuInputSrv; }

    /// Per-frame bone palette buffer: kMaxBones × 64 B row-major Mat4[].
    /// Default heap (GPU-only memory). The shader reads this as a raw
    /// ByteAddressBuffer SRV. Each frame the runtime memcpys the latest
    /// `m_palette` into the per-instance UPLOAD-heap staging buffer below
    /// and issues a CopyBufferRegion into this Default-heap destination.
    [[nodiscard]] RHIBufferHandle GetGpuPaletteBuffer() const { return m_gpuPaletteBuffer; }
    [[nodiscard]] uint32_t        GetGpuPaletteSrv() const    { return m_gpuPaletteSrv; }
    [[nodiscard]] void*           GetGpuPaletteMappedPtr() const { return m_gpuPaletteMapped; }

    /// Per-instance UPLOAD-heap staging buffer used as the source for the
    /// per-frame palette CopyBufferRegion. Allocated ONCE in
    /// `EnsureGpuSkinningResources` and reused every frame — reallocating a
    /// fresh staging via `UploadContext::UploadBuffer` each frame fragmented
    /// the D3D12MA pool fast enough to start failing with
    /// `DXGI_ERROR_DEVICE_REMOVED` after a few hundred frames.
    [[nodiscard]] RHIBufferHandle GetGpuPaletteStagingBuffer() const { return m_gpuPaletteStaging; }
    [[nodiscard]] void*           GetGpuPaletteStagingMappedPtr() const { return m_gpuPaletteStagingMapped; }

    /// Bindless UAV descriptor index of the procedural mesh's vertex buffer,
    /// which the compute shader writes into. Set by the subsystem after the
    /// mesh is uploaded.
    [[nodiscard]] uint32_t GetGpuOutputUav() const { return m_gpuOutputUav; }

    /// Setters used by SkeletalMeshSubsystem during resource binding.
    void SetGpuInput(RHIBufferHandle buffer, uint32_t srv) {
        m_gpuInputBuffer = buffer;
        m_gpuInputSrv    = srv;
    }
    void SetGpuPalette(RHIBufferHandle buffer, uint32_t srv, void* mapped) {
        m_gpuPaletteBuffer = buffer;
        m_gpuPaletteSrv    = srv;
        m_gpuPaletteMapped = mapped;
    }
    void SetGpuPaletteStaging(RHIBufferHandle buffer, void* mapped) {
        m_gpuPaletteStaging       = buffer;
        m_gpuPaletteStagingMapped = mapped;
    }
    void SetGpuOutputUav(uint32_t uavIndex) { m_gpuOutputUav = uavIndex; }

    /// Copies the current palette into the mapped upload buffer. Safe to
    /// call from the render thread after pose evaluation. No-op if the
    /// palette or the mapped pointer isn't populated yet.
    void UpdatePaletteGpu();

private:
    // Shared, immutable resources.
    const Mesh*     m_sourceMesh = nullptr;
    const Skeleton* m_skeleton   = nullptr;

    // Output identification.
    uint32_t m_outputMeshId = UINT32_MAX;

    // Pose state (guarded by mutex to avoid races with external writers).
    mutable std::mutex m_poseMutex;
    Pose m_currentPose;

    // Computed each frame.
    std::vector<Mat4>    m_palette;             ///< World bone × inverseBind, per bone.
    std::vector<BoneTransform> m_worldBoneTransforms; ///< Model-space world TRS per bone (cached from last UpdatePaletteFromPose).
    std::vector<uint8_t> m_outputVertexData;    ///< 48 B Vertex[] bytes, one per source vertex.
    bool                 m_outputDirty = false; ///< Set when m_outputVertexData changes; cleared after upload.

    // ── GPU skinning handles (null until the subsystem binds GPU resources) ──
    RHIBufferHandle m_gpuInputBuffer       = {};         ///< SkinnedVertex[] SRV (Default heap, one-time upload).
    RHIBufferHandle m_gpuPaletteBuffer     = {};         ///< Per-frame palette destination (Default heap).
    void*           m_gpuPaletteMapped     = nullptr;    ///< Unused for Default-heap palettes; kept for the legacy Upload-heap path.
    RHIBufferHandle m_gpuPaletteStaging    = {};         ///< Persistent UPLOAD-heap staging buffer for per-frame palette uploads.
    void*           m_gpuPaletteStagingMapped = nullptr; ///< Mapped pointer of `m_gpuPaletteStaging` (write-only from CPU).
    uint32_t        m_gpuInputSrv          = UINT32_MAX;
    uint32_t        m_gpuPaletteSrv        = UINT32_MAX;
    uint32_t        m_gpuOutputUav         = UINT32_MAX; ///< UAV index of the procedural mesh VB.
};

} // namespace uz
