/// UzEngine Async Mipmap Generation — Compute queue mip chain builder.
///
/// Generates mip chains using a compute shader dispatched on the async compute
/// queue, allowing overlap with graphics rendering. Each dispatch produces one
/// mip level by bilinear-filtering the previous level down by 2x.
///
/// # Usage
/// ```cpp
/// AsyncMipgen mipgen;
/// mipgen.Init(device, config);
///
/// // Per-frame:
/// mipgen.BeginFrame(computeCmdList);
/// mipgen.GenerateMips(texture, mipCount);
/// mipgen.EndFrame();  // submits compute work
///
/// // Shutdown:
/// mipgen.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the thread that owns the compute
/// command list (typically the render thread).

#pragma once

#include "../rhi.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for async mipmap generation.
///
/// Controls per-frame budgeting to avoid overloading the compute queue.
struct AsyncMipgenConfig {
    uint32_t maxMipsPerFrame = 4;  ///< Max mip levels to generate per frame (0 = unlimited)
};

// ─────────────────────────────────────────────────────────────────────────────
// AsyncMipgen
// ─────────────────────────────────────────────────────────────────────────────

/// Async mipmap generation on the compute queue.
///
/// Generates mip chains using a compute shader (`mipgen_cs.hlsl`) dispatched
/// on the async compute queue. Each mip level is produced by a single dispatch
/// that bilinear-filters the previous level.
///
/// The per-frame budget (`maxMipsPerFrame`) prevents stalling the compute queue
/// when many textures need mips simultaneously (e.g., during level load).
class AsyncMipgen {
public:
    AsyncMipgen() = default;
    ~AsyncMipgen();

    AsyncMipgen(const AsyncMipgen&) = delete;
    AsyncMipgen& operator=(const AsyncMipgen&) = delete;
    AsyncMipgen(AsyncMipgen&&) = delete;
    AsyncMipgen& operator=(AsyncMipgen&&) = delete;

    /// Initializes the mipgen system with the given device and config.
    ///
    /// Creates the compute PSO and root signature for `mipgen_cs.hlsl`.
    /// Must be called before any other method.
    ///
    /// # Arguments
    /// * `device` — RHI device (must outlive this object)
    /// * `config` — Generation parameters (defaults are reasonable)
    ///
    /// # Returns
    /// `true` on success, `false` if PSO creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, const AsyncMipgenConfig& config = {});

    /// Releases all GPU resources. Safe to call multiple times.
    void Shutdown();

    /// Begins a frame's mipgen batch on the given compute command list.
    ///
    /// Must be called before `GenerateMips()`. The command list must be in the
    /// recording state and belong to the compute queue.
    ///
    /// # Arguments
    /// * `computeCmdList` — Open compute command list for this frame
    void BeginFrame(IRHICommandList* computeCmdList);

    /// Queues mipmap generation for a texture.
    ///
    /// Generates up to `mipCount` mip levels (or fewer if the per-frame budget
    /// is reached). Each mip is produced by dispatching the compute shader once.
    ///
    /// # Arguments
    /// * `texture` — Texture to generate mips for (must have UAV-compatible format)
    /// * `mipCount` — Number of mip levels to generate (starting from mip 1)
    ///
    /// # Returns
    /// Number of mips that will actually be generated this frame (may be less
    /// than `mipCount` if the per-frame budget is exhausted).
    uint32_t GenerateMips(RHITextureHandle texture, uint32_t mipCount);

    /// Submits all queued mipgen work to the compute queue.
    ///
    /// Must be called after all `GenerateMips()` calls for the frame. After this
    /// call, the compute command list is no longer referenced.
    void EndFrame();

    /// Returns the number of mip levels generated in the current/last frame.
    [[nodiscard]] uint32_t GetMipsGeneratedThisFrame() const { return m_mipsThisFrame; }

    /// Returns true if Init() succeeded and Shutdown() has not been called.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

private:
    IRHIDevice* m_device = nullptr;
    IRHICommandList* m_computeCmdList = nullptr;
    AsyncMipgenConfig m_config;
    uint32_t m_mipsThisFrame = 0;
    bool m_initialized = false;
};

}  // namespace uz
