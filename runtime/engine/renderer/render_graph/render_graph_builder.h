#pragma once

/// Render Graph Builder — Frame-level DAG construction, compilation, and execution.
///
/// `RgBuilder` is the primary public API for the render graph. Each frame, the renderer
/// creates an `RgBuilder`, registers passes (which declare resource reads/writes),
/// compiles the graph (topological sort, barrier scheduling, pass culling, aliasing),
/// and executes it on a command list.
///
/// # Usage
/// ```cpp
/// RgBuilder rg(device, &pool);
///
/// auto albedo = rg.CreateTexture("Albedo", { .width=1920, .height=1080,
///     .format=RHIFormat::RGBA8Unorm, .usage=RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource });
/// auto backbuffer = rg.ImportTexture("Backbuffer", bbHandle, RHIResourceState::Present);
///
/// rg.AddPass("GBuffer", [&](RgPassBuilder& pb) {
///     pb.Write(albedo, RHIResourceState::RenderTarget);
///     pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
///         // record draw calls using res.GetTexture(albedo)
///     });
/// });
///
/// rg.Compile();
/// rg.Execute(cmd);
/// ```
///
/// # Thread Safety
/// Not thread-safe. All builder methods must be called from a single thread.
///
/// # Lifetime
/// Per-frame. Create a new `RgBuilder` each frame. The `RgResourcePool` is persistent.

#include <vector>
#include <functional>
#include <unordered_map>

#include "render_graph_types.h"
#include "render_graph_pass.h"

namespace uz {
class IRHIDevice;
class IRHICommandList;
} // namespace uz

namespace uz::rg {

class RgResourcePool;
class RgStateRegistry;

/// Builds, compiles, and executes a frame's render graph.
///
/// # Phases
/// 1. **Declaration** — `CreateTexture/Buffer`, `ImportTexture/Buffer`, `AddPass`
/// 2. **Compilation** — `Compile()` — topological sort, barrier scheduling, culling, aliasing
/// 3. **Execution** — `Execute()` — acquires physical resources, emits barriers, invokes callbacks
///
/// After `Execute()`, the builder's state is consumed. Do not reuse across frames.
class RgBuilder {
public:
    /// Constructs a render graph builder.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource queries (e.g., `GetTextureDesc`). Must outlive the builder.
    /// * `pool` — Persistent resource pool for physical resource reuse. Must outlive the builder.
    /// * `states` — Persistent resource-state registry (render_graph_states.h), optional. With it, tracked
    ///   imports take their state from it, explicit import claims are checked against it (observe mode),
    ///   and Execute commits the end state of every imported resource the graph transitioned.
    RgBuilder(IRHIDevice* device, RgResourcePool* pool, RgStateRegistry* states = nullptr);

    ~RgBuilder();

    // Non-copyable (owns pass/resource state for this frame).
    RgBuilder(const RgBuilder&) = delete;
    RgBuilder& operator=(const RgBuilder&) = delete;

    // ── Resource declaration ────────────────────────────────────────────────

    /// Creates a virtual texture resource in the graph.
    ///
    /// The texture is transient — it will be allocated from the resource pool
    /// at execution time and returned after the frame completes.
    ///
    /// # Arguments
    /// * `name` — Debug name (string literal expected; not copied).
    /// * `desc` — Virtual texture descriptor (dimensions, format, usage).
    ///
    /// # Returns
    /// A valid `RgResourceId` referencing this texture.
    [[nodiscard]] RgResourceId CreateTexture(const char* name, const RgTextureDesc& desc);

    /// Convenience wrapping `CreateTexture` for a transient 3D volume texture
    /// (phase 10.6.0). The 3D volume is separated from 2D arrays in the pool
    /// key so it cannot alias to a 2D array resource.
    ///
    /// # Arguments
    /// * `name`   — Debug name.
    /// * `width`, `height`, `depth` — Volume dimensions in voxels.
    /// * `format` — Texel format (e.g. `RHIFormat::R32Float` for density).
    /// * `usage`  — Texture usage flags. Defaults to
    ///   `ShaderResource | UnorderedAccess` (splat + sample pattern used
    ///   by the phase 9.10 volumetric renderer).
    ///
    /// # Returns
    /// A valid `RgResourceId` referencing the 3D texture.
    [[nodiscard]] RgResourceId CreateTransient3D(
        const char* name, uint32_t width, uint32_t height, uint32_t depth,
        RHIFormat format,
        RHITextureUsage usage = RHITextureUsage::ShaderResource |
                                RHITextureUsage::UnorderedAccess);

    /// Creates a virtual buffer resource in the graph.
    ///
    /// # Arguments
    /// * `name` — Debug name (string literal expected; not copied).
    /// * `desc` — Virtual buffer descriptor (size, stride, usage).
    ///
    /// # Returns
    /// A valid `RgResourceId` referencing this buffer.
    [[nodiscard]] RgResourceId CreateBuffer(const char* name, const RgBufferDesc& desc);

    /// Imports an externally owned texture into the graph (e.g., swapchain backbuffer).
    ///
    /// Imported resources are not managed by the resource pool — the caller retains
    /// ownership. The graph tracks state transitions but does not allocate or free
    /// the physical resource.
    ///
    /// # Arguments
    /// * `name` — Debug name.
    /// * `handle` — The existing RHI texture handle.
    /// * `currentState` — The resource's state at graph entry.
    ///
    /// # Returns
    /// A valid `RgResourceId` referencing the imported texture.
    [[nodiscard]] RgResourceId ImportTexture(const char* name, RHITextureHandle handle,
                                             RHIResourceState currentState);

    /// Imports an externally owned buffer into the graph.
    ///
    /// # Arguments
    /// * `name` — Debug name.
    /// * `handle` — The existing RHI buffer handle.
    /// * `currentState` — The resource's state at graph entry.
    ///
    /// # Returns
    /// A valid `RgResourceId` referencing the imported buffer.
    [[nodiscard]] RgResourceId ImportBuffer(const char* name, RHIBufferHandle handle,
                                            RHIResourceState currentState);

    /// Imports a texture whose state at graph entry the `RgStateRegistry` knows: its record (the end state
    /// of the last graph or raw barrier that transitioned it) or, with none, its creation state. Resolved
    /// at `Compile()`, so a registry transition recorded between this call and Compile is honoured. A
    /// handle already imported in this graph returns that import's id: one barrier chain per resource.
    /// First use is based on creation, not frameIndex: DX12 colour/UAV textures start in Common,
    /// depth-stencil textures in DepthStencilWrite. Later imports retain the actual last state;
    /// the first pass's requested state (for example GBuffer depth writes) is a transition target,
    /// not an import claim. Do not add a second Common->UAV barrier inside a tracked compute pass.
    /// Requires a registry (constructor).
    [[nodiscard]] RgResourceId ImportTrackedTexture(const char* name, RHITextureHandle handle);

    /// Imports a buffer whose state at graph entry the `RgStateRegistry` knows (Common after a submit:
    /// buffers decay). Otherwise as `ImportTrackedTexture`.
    [[nodiscard]] RgResourceId ImportTrackedBuffer(const char* name, RHIBufferHandle handle);

    /// Explicit import claims of this graph that disagreed with the registry at Compile (observe mode;
    /// 0 without a registry). Only imports a non-culled pass accesses are checked.
    [[nodiscard]] uint32_t GetImportClaimMismatchCount() const { return m_claimMismatches; }

    // ── Pass registration ───────────────────────────────────────────────────

    /// Registers a render pass in the graph.
    ///
    /// The setup callback receives an `RgPassBuilder&` to declare resource
    /// reads/writes and set the execution callback.
    ///
    /// # Arguments
    /// * `name` — Debug name for the pass (string literal expected).
    /// * `setupFn` — Setup callback that configures the pass via `RgPassBuilder`.
    void AddPass(const char* name, const std::function<void(RgPassBuilder&)>& setupFn);

    // ── Parallel recording ────────────────────────────────────────────────────

    /// Enables per-pass GPU timestamp profiling during Execute().
    ///
    /// When enabled, each non-culled pass gets a BeginTimestamp/EndTimestamp pair
    /// recorded via IRHIDevice::GpuProfilerBeginTimestamp/EndTimestamp. The pass
    /// name is used as the timestamp scope name.
    ///
    /// # Arguments
    /// * `enabled` — `true` to enable per-pass GPU timestamps.
    void SetGpuProfileEnabled(bool enabled) { m_gpuProfileEnabled = enabled; }

    /// Returns whether per-pass GPU timestamp profiling is enabled.
    [[nodiscard]] bool IsGpuProfileEnabled() const { return m_gpuProfileEnabled; }

    /// Returns the final tracked state of an imported resource after ScheduleBarriers.
    /// Call after Compile() to query the end-of-frame state for use as the next
    /// frame's import initial state.
    [[nodiscard]] RHIResourceState GetFinalState(RgResourceId id) const {
        if (!m_compiled || !id.IsValid() || id.index >= m_resources.size())
            return RHIResourceState::Undefined;
        return m_resources[id.index].currentState;
    }

    /// Enable parallel recording of independent passes.
    ///
    /// When enabled and an `RgParallelExecutor` is used, passes with no data
    /// dependencies between them can record their command lists on separate threads.
    /// Has no effect on the default `Execute(IRHICommandList*)` path.
    ///
    /// # Arguments
    /// * `enabled` — `true` to enable parallel recording.
    void SetParallelRecordingEnabled(bool enabled) { m_parallelRecordingEnabled = enabled; }

    /// Returns whether parallel recording is enabled.
    [[nodiscard]] bool IsParallelRecordingEnabled() const { return m_parallelRecordingEnabled; }

    // ── Compilation ─────────────────────────────────────────────────────────

    /// Compiles the render graph: topological sort, barrier scheduling, pass culling.
    ///
    /// Must be called after all passes are registered and before `Execute()`.
    /// Calling `Compile()` multiple times is an error.
    ///
    /// # Steps
    /// 1. Build adjacency lists from resource read/write declarations.
    /// 2. Topological sort with cycle detection (asserts on cycles).
    /// 3. Compute resource lifetimes (first-write to last-read pass indices).
    /// 4. Cull passes whose outputs are never consumed.
    /// 5. Schedule barriers based on resource state transitions between passes.
    ///
    /// # Panics
    /// Asserts if a cycle is detected in the dependency graph.
    void Compile();

    // ── Execution ───────────────────────────────────────────────────────────

    /// Executes all non-culled passes in topological order.
    ///
    /// For each pass:
    /// 1. Acquires physical resources for transient textures/buffers from the pool.
    /// 2. Emits scheduled barriers via `IRHICommandList::ResourceBarrier()`.
    /// 3. Invokes the pass execution callback with resolved handles.
    ///
    /// After all passes complete, transient resources are returned to the pool.
    ///
    /// # Arguments
    /// * `cmdList` — The command list to record all GPU commands into.
    ///   Must have been opened with `Begin()` by the caller.
    void Execute(IRHICommandList* cmdList);

    // ── Queries ─────────────────────────────────────────────────────────────

    /// Returns the total number of registered passes (including culled).
    [[nodiscard]] uint32_t GetPassCount() const;

    /// Returns the number of passes culled during compilation.
    [[nodiscard]] uint32_t GetCulledPassCount() const;

    /// Returns the total number of barriers scheduled during compilation.
    [[nodiscard]] uint32_t GetBarrierCount() const;

    /// Returns the total number of resources (transient + imported).
    [[nodiscard]] uint32_t GetResourceCount() const;

    /// Returns the resource entry at the given index (for `RgPassResources` lookup).
    ///
    /// # Arguments
    /// * `id` — A valid resource ID.
    ///
    /// # Returns
    /// Reference to the resource entry. Asserts if `id` is invalid.
    [[nodiscard]] const RgResourceEntry& GetResource(RgResourceId id) const;

private:
    /// Performs topological sort via Kahn's algorithm.
    /// Populates `m_sortedPassIndices` and asserts on cycles.
    void TopologicalSort();

    /// Computes resource lifetimes (first/last pass index) for aliasing.
    void ComputeLifetimes();

    /// Culls passes whose outputs are never read by any other pass
    /// and are not imported (external) resources.
    void CullPasses();

    /// Schedules barriers based on resource state transitions between consecutive passes.
    void ScheduleBarriers();

    /// Acquires physical resources from the pool for all transient resources.
    void AcquirePhysicalResources();

    /// Registry: tracked imports take their initial state from it (before ScheduleBarriers).
    void ResolveTrackedImportStates();

    /// Registry, observe mode: explicit claims of accessed imports that disagree with it (after culling).
    void ObserveImportClaims();

    /// Registry: the end state of every imported resource the graph transitioned (after Execute).
    void CommitImportStates();

    /// Returns all transient physical resources to the pool.
    void ReleasePhysicalResources();

    IRHIDevice* m_device = nullptr;
    RgResourcePool* m_pool = nullptr;
    RgStateRegistry* m_states = nullptr;

    std::unordered_map<uint64_t, RgResourceId> m_importedTextureIds;  ///< Registry key -> first import.
    std::unordered_map<uint64_t, RgResourceId> m_importedBufferIds;   ///< Registry key -> first import.
    uint32_t m_claimMismatches = 0;

    std::vector<RgResourceEntry> m_resources;      ///< All virtual resources.
    std::vector<RgPass> m_passes;                  ///< All registered passes.
    std::vector<uint32_t> m_sortedPassIndices;     ///< Topologically sorted pass order.

    bool m_compiled = false;                ///< True after `Compile()` has been called.
    bool m_executed = false;                ///< True after `Execute()` has been called.
    bool m_gpuProfileEnabled = false;       ///< When true, per-pass GPU timestamps are recorded.
    bool m_parallelRecordingEnabled = false; ///< Hint for RgParallelExecutor.
};

} // namespace uz::rg
