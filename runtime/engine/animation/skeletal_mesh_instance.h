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
    std::vector<uint8_t> m_outputVertexData;    ///< 48 B Vertex[] bytes, one per source vertex.
    bool                 m_outputDirty = false; ///< Set when m_outputVertexData changes; cleared after upload.
};

} // namespace uz
