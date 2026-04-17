#pragma once

/// VFX → DDGI emissive feed — GPU buffer management + frame upload.
///
/// Phase 9.17.0 — data-path wiring between the 4.13 VFX emissive readback
/// and the 8.23.0 DDGI probe-update CS.
///
/// Each frame the feed:
/// 1. Drains the readback queue (`VfxEmissiveSample[]`).
/// 2. Runs `VfxDDGISelectTopN` to pick the brightest N samples.
/// 3. Uploads the top-N buffer to a persistent GPU structured buffer.
/// 4. Publishes the SRV index + sample count in the DDGI per-frame CB.
///
/// The DDGI probe-update CS reads `VfxEmissiveSample[count]` via bindless
/// SRV and adds each sample's contribution to the probe's SH coefficients.
///
/// # Lifetime
///
/// Created alongside the DDGI system; destroyed when DDGI is torn down.
/// Reset on scene change (clears the sample buffer).
///
/// @ref engine/vfx/lighting/vfx_ddgi_injection.h (math helpers)
/// @ref engine/renderer/gi/ddgi_system.h (probe-update CS owner)
/// @ref docs/engine/25-vfx/40-ddgi-emissive-injection.md

#include "engine/vfx/lighting/vfx_ddgi_injection.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;

/// Manages the GPU structured buffer that feeds VFX emissive samples into
/// the DDGI probe-update compute shader.
class VfxDDGIEmissiveFeed {
public:
    /// Maximum number of samples uploaded per frame. Matches the DDGI CS's
    /// fixed-size loop bound.
    static constexpr uint32_t kMaxSamples = 32u;

    VfxDDGIEmissiveFeed() = default;
    ~VfxDDGIEmissiveFeed();

    VfxDDGIEmissiveFeed(const VfxDDGIEmissiveFeed&)            = delete;
    VfxDDGIEmissiveFeed& operator=(const VfxDDGIEmissiveFeed&) = delete;

    /// Allocates the GPU structured buffer for `kMaxSamples` entries.
    ///
    /// # Arguments
    /// - `device` — RHI device; must outlive the feed.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device);

    /// Releases the GPU buffer.
    void Shutdown();

    /// Selects top-N from `rawSamples`, uploads to the GPU buffer.
    ///
    /// # Arguments
    /// - `rawSamples` — this frame's readback output (may be empty).
    /// - `rawCount`   — number of samples in the array.
    ///
    /// After this call, `GetSampleCount()` and `GetBufferSrvIndex()`
    /// are valid for the current frame's DDGI CB.
    void UploadFrame(const VfxEmissiveSample* rawSamples, uint32_t rawCount);

    /// Clears the sample count to zero (scene change / reset).
    void Reset() noexcept;

    /// Returns the number of valid samples in the GPU buffer this frame.
    [[nodiscard]] uint32_t GetSampleCount() const noexcept { return sampleCount_; }

    /// Returns the bindless SRV index of the structured buffer.
    /// `UINT32_MAX` when not initialised.
    [[nodiscard]] uint32_t GetBufferSrvIndex() const noexcept { return srvIndex_; }

    /// Returns true if Init succeeded and the buffer is ready.
    [[nodiscard]] bool IsInitialized() const noexcept { return device_ != nullptr; }

    /// Whether emissive injection is enabled at runtime.
    /// Controlled by CVar `r.Vfx.DDGIInject`.
    bool enabled = true;

private:
    IRHIDevice*    device_      = nullptr;
    RHIBufferHandle buffer_;
    uint32_t       srvIndex_    = UINT32_MAX;
    uint32_t       sampleCount_ = 0;

    /// CPU-side staging for the top-N selected samples.
    VfxEmissiveSample staging_[kMaxSamples] = {};
};

} // namespace uz
