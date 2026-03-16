/// UzEngine Shadow Renderer — Per-Frame Shadow Cascade Setup
///
/// Extracts shadow configuration, computes cascade splits using UE5's exponential
/// accumulation formula, and uploads ShadowDataCB to the CB ring allocator.
///
/// Factored out of `scene_renderer.cpp` to keep `RenderFrame()` as an orchestrator.
/// This is a stateless free function, not a class member — it reads camera/light
/// parameters and writes the result into the returned `ShadowFrameData`.
///
/// # Thread Safety
/// Stateless — safe to call from any thread. However, the `CBRingAllocator` is
/// NOT thread-safe, so the caller must ensure single-threaded access.
///
/// # UE5 Reference
/// - Cascade splits: DLC.cpp:753-814 (exponential accumulation)
/// - CSM depth bias: ShadowRendering.cpp:32
/// - CSM slope bias: ShadowRendering.cpp:38
/// - Receiver bias: ShadowRendering.cpp:76
/// - Cascade bias distribution: ShadowSetup.cpp:1844-1846

#pragma once

#include "shadow_cascade.h"
#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "vector3.h"

namespace uz {

class Camera;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowFrameData — per-frame shadow setup result
// ─────────────────────────────────────────────────────────────────────────────

/// Everything needed by the render graph shadow pass for the current frame.
///
/// Contains computed cascade data (view-proj matrices, split distances, biases),
/// the shadow configuration, and the CB allocation pointing to the uploaded
/// ShadowDataCB in the ring buffer.
struct ShadowFrameData {
    ShadowCascade cascades[kMaxShadowCascades]; ///< Per-cascade view-proj, splits, and biases.
    ShadowConfig  config;                       ///< Shadow configuration used for this frame.
    CBAllocation  cbAllocation;                  ///< ShadowDataCB upload location in the CB ring.
    uint32_t      totalCascadeCount = 0;        ///< Actual cascade count (main + far). Set by ComputeCascades.
};

// ─────────────────────────────────────────────────────────────────────────────
// SetupShadowFrame
// ─────────────────────────────────────────────────────────────────────────────

/// Computes cascade shadow maps and uploads ShadowDataCB for the current frame.
///
/// Builds the shadow configuration with UE5 parity defaults, computes cascade
/// splits using the exponential accumulation formula, builds per-cascade bias
/// values scaled by world texel size, and uploads the full ShadowDataCB to the
/// CB ring allocator.
///
/// # Arguments
/// * `camera` — Current frame camera (for view/projection matrices).
/// * `sunDir` — World-space direction the sun is shining TOWARD (normalized).
///   For a sun at noon: `{0, 0, -1}` (Z-up, shining downward).
/// * `perLightShadowBias` — Per-light shadow bias multiplier (UE5: ShadowBias,
///   default 0.5). Read from `DirectionalLightComponent::shadowBias`.
/// * `perLightShadowSlopeBias` — Per-light slope bias multiplier (UE5:
///   ShadowSlopeBias, default 0.5). Read from `DirectionalLightComponent::shadowSlopeBias`.
/// * `shadowAtlasSrv` — SRV heap index of the shadow atlas depth texture.
/// * `shadowResolution` — Per-cascade resolution in texels (e.g., 2048).
/// * `cbRing` — CB ring allocator for uploading the ShadowDataCB.
///
/// # Returns
/// `ShadowFrameData` with computed cascades, config, and CB allocation.
[[nodiscard]] ShadowFrameData SetupShadowFrame(
    const Camera& camera,
    const Vec3& sunDir,
    float perLightShadowBias,
    float perLightShadowSlopeBias,
    float lightSourceAngleDeg,
    uint32_t shadowAtlasSrv,
    uint32_t shadowResolution,
    CBRingAllocator& cbRing,
    uint32_t farShadowCascadeCount = 0,
    float farShadowDistance = 300000.0f,
    int32_t farShadowLodBias = 0);

} // namespace uz
