/// UzEngine Reflection Probe Renderer — Cubemap capture, IBL bake, and GPU upload.
///
/// Manages the lifecycle of reflection probe cubemaps: gathers enabled
/// ReflectionProbeComponent entities from the ECS, captures dirty probes
/// (one per frame to amortize cost), bakes irradiance + prefiltered specular
/// cubemaps via IBLBaker, and uploads a StructuredBuffer<ReflectionProbeGPU>
/// for the deferred lighting shader to consume.
///
/// # Current Implementation (Phase 1)
/// All probes currently reuse the global sky cubemap as their source. Per-probe
/// scene rendering (capturing the scene from the probe position) is a future
/// enhancement. The IBLBaker takes the sky cubemap and produces per-probe
/// irradiance + prefiltered cubemaps, which is correct for probes that only
/// reflect sky.
///
/// # Ownership
/// Owns the GPU structured buffer for probe data. Does NOT own the IRHIDevice,
/// IBLBaker, or any ECS entities. Both the device and baker must outlive this
/// object.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.
///
/// @ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
/// @ref McGuire 2017 "Efficient GPU Screen-Space Ray Tracing" (probe blending)

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IBLBaker;
class World;

/// GPU data returned by Update() for writing into PerFrameCB.
struct ProbeFrameResult {
    uint32_t probeCount = 0;                    ///< Number of active probes uploaded.
    uint32_t probeBufferSrvIndex = UINT32_MAX;  ///< SRV index of the probe StructuredBuffer.
};

/// Manages reflection probe cubemap capture, IBL baking, and GPU buffer upload.
///
/// Each probe gets its own irradiance + prefiltered cubemap pair via IBLBaker.
/// The probe data buffer (StructuredBuffer<ReflectionProbeGPU>) is uploaded
/// per-frame and indexed by the deferred lighting shader.
///
/// # Lifecycle
/// ```cpp
/// ReflectionProbeRenderer probeRenderer;
/// probeRenderer.Init(device, &iblBaker);
///
/// // Per frame:
/// auto result = probeRenderer.Update(world, skyCubemapSrvIndex, skyCubemapTexture);
/// perFrameCB.reflectionProbeCount = result.probeCount;
/// perFrameCB.reflectionProbeBufferSrvIndex = result.probeBufferSrvIndex;
///
/// probeRenderer.Shutdown();
/// ```
class ReflectionProbeRenderer {
public:
    /// Maximum number of reflection probes supported simultaneously.
    /// Matches the structured buffer allocation. Higher probe counts would
    /// require a larger buffer and more per-frame bake budget.
    static constexpr uint32_t kMaxProbes = 8;

    ReflectionProbeRenderer() = default;
    ~ReflectionProbeRenderer();

    /// Non-copyable — owns GPU resources.
    ReflectionProbeRenderer(const ReflectionProbeRenderer&) = delete;
    ReflectionProbeRenderer& operator=(const ReflectionProbeRenderer&) = delete;

    /// Movable.
    ReflectionProbeRenderer(ReflectionProbeRenderer&&) noexcept;
    ReflectionProbeRenderer& operator=(ReflectionProbeRenderer&&) noexcept;

    /// Initializes the probe renderer with device and IBL baker references.
    ///
    /// Creates the GPU structured buffer for probe data upload. Does NOT
    /// capture any probes — that happens lazily in Update().
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this renderer.
    /// * `iblBaker` — Initialized IBL baker. Must outlive this renderer.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation fails (error logged).
    [[nodiscard]] bool Init(IRHIDevice* device, IBLBaker* iblBaker);

    /// Releases all GPU resources owned by this renderer.
    ///
    /// Safe to call multiple times or without prior Init().
    void Shutdown();

    /// Gathers all ReflectionProbeComponent entities, captures dirty probes
    /// (one per frame), bakes IBL, and uploads the probe data structured buffer.
    ///
    /// At most one dirty probe is captured per frame to amortize GPU cost.
    /// Probes are gathered, sorted by priority (descending), and the top
    /// kMaxProbes are tracked. When a probe is dirty, its cubemap is baked
    /// via IBLBaker using the provided sky cubemap as source.
    ///
    /// # Arguments
    /// * `world` — ECS world containing ReflectionProbeComponent entities.
    /// * `skyCubemapSrvIndex` — SRV index of the sky cubemap (input to IBLBaker).
    /// * `skyCubemapTexture` — Texture handle of the sky cubemap (for SM5 binding).
    ///
    /// # Returns
    /// `ProbeFrameResult` with the count and SRV index for PerFrameCB.
    [[nodiscard]] ProbeFrameResult Update(
        World& world,
        uint32_t skyCubemapSrvIndex,
        RHITextureHandle skyCubemapTexture);

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Per-probe tracking slot — maps an ECS entity to its baked IBL resources.
    struct ProbeSlot {
        bool     active                = false;       ///< Whether this slot is in use.
        bool     captured              = false;       ///< Whether IBL has been baked for this probe.
        uint32_t irradianceSrvIndex    = UINT32_MAX;  ///< Baked irradiance cubemap SRV.
        uint32_t prefilteredSrvIndex   = UINT32_MAX;  ///< Baked prefiltered specular SRV.
    };

    /// Uploads the probe data to the GPU structured buffer.
    ///
    /// Maps the buffer, writes ReflectionProbeGPU entries for all active probes,
    /// then unmaps. Returns the number of probes written.
    ///
    /// # Arguments
    /// * `world` — ECS world for reading probe component data and positions.
    uint32_t UploadProbeData(World& world);

    IRHIDevice* m_device   = nullptr;
    IBLBaker*   m_iblBaker = nullptr;
    bool        m_initialized = false;

    // ── Per-probe tracking ────────────────────────────────────────────────
    ProbeSlot m_probes[kMaxProbes] = {};

    // ── GPU structured buffer for probe data ──────────────────────────────
    RHIBufferHandle m_probeBuffer;
    uint32_t        m_probeBufferSrvIndex = UINT32_MAX;
};

} // namespace uz
