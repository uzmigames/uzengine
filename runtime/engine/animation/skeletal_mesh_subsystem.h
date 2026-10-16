/// UzEngine SkeletalMeshSubsystem — Per-entity skeletal animation orchestration.
///
/// Singleton manager that owns the runtime state for every animated character
/// in the scene:
///
///   - `Skeleton` loaded from `.uzskel` (one per unique path, shared across
///     entities that use the same skeleton).
///   - `SkeletalMeshInstance` holding per-entity skinning state (pose, palette,
///     CPU-skinned 48 B vertex bytes, procedural-mesh ID).
///   - `AnimationController` per entity driving clip playback and state
///     machine transitions.
///
/// # Thread safety
/// All registration/mutation APIs lock `m_registryMutex`. Per-entity animation
/// updates (sampling + palette compute + CPU skinning) run on worker threads
/// via `JobSystem::ParallelFor` — each worker owns exactly one entity, so no
/// locking is needed inside the hot loop.
///
/// # Lifecycle
/// ```
/// Scene load  → RegisterEntity(...) for each SkeletalMeshComponent
/// Every tick  → TickAll(dt)   — samples, skins on workers, synced at barrier
/// Every frame → UploadAll()   — uploads skinned VB bytes to GPU (main thread)
/// Scene unload → Reset()
/// ```

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class Skeleton;
class SkeletalMeshInstance;
class AnimationController;
class SceneRenderer;
class IRHIDevice;
class IRHICommandList;
class PipelineLibrary;
class World;
struct Mesh;
class IVFS;
namespace rg { class RgStateRegistry; }

// ─────────────────────────────────────────────────────────────────────────────
// SkeletalMeshSubsystem
// ─────────────────────────────────────────────────────────────────────────────

class SkeletalMeshSubsystem {
public:
    /// Returns the process-wide singleton instance.
    static SkeletalMeshSubsystem& Get();

    // ── Registration ──────────────────────────────────────────────────

    struct RegisterInfo {
        const Mesh*          sourceMesh;          ///< CPU mesh with `skinnedVertexData` populated.
        const Skeleton*      skeleton;            ///< Skeleton shared with this entity.
        uint32_t             outputMeshId;        ///< ID of the procedural mesh registered with SceneRenderer.
        AnimationController* controller;          ///< Controller owned by the subsystem (see `CreateController`).
    };

    /// Registers an entity and returns a stable instance handle. Instances are
    /// persistent — the caller keeps the handle in its component to address
    /// future queries.
    [[nodiscard]] uint32_t RegisterEntity(const RegisterInfo& info);

    /// Unregisters an entity. The handle becomes invalid for future calls.
    void UnregisterEntity(uint32_t instanceId);

    /// Removes all registered entities and frees owned controllers + skeletons.
    void Reset();

    // ── Post-scene-load finalisation ───────────────────────────────────

    /// Iterates every entity carrying a `SkeletalMeshComponent` and registers
    /// it with the subsystem: loads the `.uzmesh` + `.uzskel`, creates a
    /// per-entity procedural output mesh on `renderer`, loads the listed
    /// `.uzanim` clips into a fresh controller, and binds everything.
    ///
    /// Must run once after `LoadScene` completes and before the first call to
    /// `TickAll()`.
    void Finalize(World& world, SceneRenderer& renderer, IVFS& vfs);

    // ── Resource loaders (shared across entities by path) ──────────────

    /// Loads a `.uzskel` through the VFS and caches it by path.
    /// Returns nullptr on failure (logged via UZ_LOG).
    const Skeleton* LoadSkeletonFromVfs(IVFS& vfs, const std::string& path);

    /// Creates a fresh AnimationController owned by the subsystem. Returned
    /// pointer is stable until `Reset()` or engine shutdown.
    [[nodiscard]] AnimationController* CreateController();

    // ── Per-tick orchestration ────────────────────────────────────────

    /// Advances every controller by `dt` seconds, samples poses, computes bone
    /// palettes, and runs CPU linear blend skinning into each instance's
    /// `m_outputVertexData`. Parallelized via `JobSystem::ParallelFor`.
    /// Blocks until all workers finish.
    void TickAll(float dt);

    /// Uploads each instance's latest skinned vertex bytes into the
    /// corresponding procedural GPU mesh.
    ///
    /// Must run on the render thread inside an active frame: the caller
    /// supplies the device for staging allocation and a command list in
    /// recording state. The `renderer` resolves mesh IDs to `GPUMesh`.
    ///
    /// # Deprecation
    /// Superseded by `DispatchSkinningGpu` when the GPU compute skinning
    /// pipeline is wired; the CPU path remains as an editor / debug
    /// fallback. Current production renderer calls `DispatchSkinningGpu`
    /// instead.
    void UploadAll(IRHIDevice* device, IRHICommandList* cmdList, SceneRenderer& renderer);

    /// Runs the `gpu_skinning_cs.hlsl` compute pass for every registered
    /// instance. Lazy-initialises per-entity GPU resources (input SRV,
    /// palette upload-buffer, output UAV) on first call once the procedural
    /// mesh finishes uploading. After the dispatch, the mesh's vertex
    /// buffer is left in the `VertexBuffer` state and is consumed by the
    /// GBuffer pass as-is.
    ///
    /// Must run on the render thread inside an active frame, before the
    /// GBuffer pass records its draws.
    void DispatchSkinningGpu(IRHIDevice* device,
                             IRHICommandList* cmdList,
                             SceneRenderer& renderer,
                             PipelineLibrary& pipelines,
                             rg::RgStateRegistry& states);

    // ── Lookup ────────────────────────────────────────────────────────

    /// Returns the instance matching `instanceId`, or nullptr if unknown.
    [[nodiscard]] SkeletalMeshInstance* GetInstance(uint32_t instanceId);

    /// Returns the controller handle stored at `controllerId`, or nullptr if
    /// unknown.
    [[nodiscard]] AnimationController* GetController(uint32_t controllerId);

    /// Number of registered instances (for diagnostics).
    [[nodiscard]] std::size_t GetInstanceCount() const;

private:
    SkeletalMeshSubsystem() = default;
    ~SkeletalMeshSubsystem() = default;
    SkeletalMeshSubsystem(const SkeletalMeshSubsystem&) = delete;
    SkeletalMeshSubsystem& operator=(const SkeletalMeshSubsystem&) = delete;

    mutable std::mutex m_registryMutex;

    struct InstanceRecord {
        std::unique_ptr<SkeletalMeshInstance> instance;
        AnimationController*                  controller = nullptr;
    };

    std::vector<InstanceRecord>                            m_records;    ///< Index is the handle.
    std::vector<std::unique_ptr<AnimationController>>      m_controllers;
    std::unordered_map<std::string, std::unique_ptr<Skeleton>> m_skeletons;

    /// CPU copies of skinned source meshes. Kept alive so `SkeletalMeshInstance`
    /// pointers stay valid. Keyed by `.uzmesh` VFS path.
    std::unordered_map<std::string, std::unique_ptr<Mesh>> m_sharedMeshes;
};

} // namespace uz
