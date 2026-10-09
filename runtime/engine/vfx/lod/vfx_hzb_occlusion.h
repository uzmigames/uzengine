#pragma once

/// VFX HZB occlusion culling.
///
/// Layers on top of the frustum cull (phase 3.89): an emitter whose bounds
/// are fully behind opaque geometry is skipped — sim *and* render — so
/// VFX cost scales with on-screen emitters, not authored emitters.
///
/// # Pipeline
///
/// 1. Project the emitter's world-space AABB into screen space. Compute
///    the axis-aligned bounding rectangle of the 8 projected corners and
///    the nearest-corner depth (reversed-Z: larger is closer).
/// 2. Select the HZB mip whose texel covers the projected rectangle
///    (smallest mip such that `max(rectW, rectH) ≤ mipTexelSize`).
/// 3. Sample the 2×2 texel neighbourhood at the rect centre in that mip;
///    take the min (reversed-Z farthest) — this is the farthest depth of
///    opaque geometry inside the rect.
/// 4. If the emitter's nearest-corner depth is *behind* this HZB depth
///    (reversed-Z: smaller value), the emitter is fully occluded.
/// 5. Apply a hysteresis band around the cull threshold so borderline
///    emitters don't thrash between visible/culled each frame.
///
/// The helpers here are pure CPU math so tests exercise the logic
/// without a GPU. The runtime integrates them with the existing HZB
/// pyramid (phase 3.36) by sampling the appropriate mip level.
///
/// # Thread safety
/// All helpers are pure functions. Instance `VfxOcclusionHysteresis`
/// carries per-emitter state and is not thread-safe; the caller owns
/// one instance per emitter.
///
/// @ref Niemeyer 2007 "Hierarchical Z-Buffer Occlusion Culling"
/// @ref docs/engine/25-vfx/37-hzb-occlusion.md

#include "vector3.h"
#include "vector4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Types
// ─────────────────────────────────────────────────────────────────────────────

/// World-space axis-aligned bounding box.
struct VfxAABB {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};
};

/// Screen-space rectangle in UV coordinates ([0, 1]) plus the nearest-
/// corner reversed-Z depth. The rect is clamped to the screen so callers
/// don't need to validate bounds.
struct VfxScreenRect {
    float uvMinX = 0.0f;
    float uvMinY = 0.0f;
    float uvMaxX = 0.0f;
    float uvMaxY = 0.0f;
    float nearestDepth = 0.0f; ///< Reversed-Z: larger = closer.
    bool  offscreen = false;    ///< True when the rect is entirely outside [0,1]^2.
};

// ─────────────────────────────────────────────────────────────────────────────
// AABB projection
// ─────────────────────────────────────────────────────────────────────────────

/// Projects `aabb` through `viewProj` and returns its screen-space rect
/// plus the nearest-corner depth.
///
/// When at least one corner has negative `w` (behind the near plane), the
/// rect is considered unsafe to cull against — returns a full-screen rect
/// with `nearestDepth = 1.0` so the emitter is treated as visible.
///
/// # Arguments
/// - `aabb`       — emitter world-space AABB.
/// - `viewProj`   — 4×4 view-projection matrix, row-major.
///
/// # Returns
/// A `VfxScreenRect` with UV bounds in [0, 1] and the nearest reversed-Z
/// depth across the 8 corners. `offscreen == true` when the AABB is
/// completely outside the screen (caller can short-circuit rendering).
[[nodiscard]] VfxScreenRect VfxProjectAABBToScreen(const VfxAABB& aabb,
                                                     const float viewProj[16]) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// HZB mip selection
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the HZB mip level whose texel footprint covers `rect`.
///
/// Selects the smallest mip whose texel size (in UV space) is ≥ the
/// rectangle's longer side so a 2×2 fetch at the rect centre covers the
/// entire projected AABB.
///
/// # Arguments
/// - `rect`        — projected rect from `VfxProjectAABBToScreen`.
/// - `baseWidth`   — mip 0 width in pixels.
/// - `baseHeight`  — mip 0 height in pixels.
/// - `mipCount`    — number of mips in the HZB pyramid (including mip 0).
///
/// # Returns
/// Mip index in `[0, mipCount - 1]`. 0 for full-screen emitters, maxMip
/// for pinpoint ones. Never out-of-range.
[[nodiscard]] uint32_t VfxSelectHZBMip(const VfxScreenRect& rect,
                                         uint32_t baseWidth,
                                         uint32_t baseHeight,
                                         uint32_t mipCount) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Occlusion test
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true when the emitter is occluded by the sampled HZB depth.
///
/// Reversed-Z semantics: the HZB stores the farthest (smallest) depth
/// across the mip footprint, and the emitter's nearest corner is the
/// largest depth. Occluded iff `nearestCorner < hzbFarthest` (the emitter
/// is strictly behind the opaque geometry).
///
/// # Arguments
/// - `nearestDepth` — emitter nearest-corner reversed-Z depth.
/// - `hzbDepth`     — farthest reversed-Z depth from the HZB fetch.
/// - `epsilon`      — conservative slack (default 0.0005) to avoid
///                    self-occlusion on planar surfaces.
[[nodiscard]] bool VfxIsOccludedByHZB(float nearestDepth,
                                        float hzbDepth,
                                        float epsilon = 5e-4f) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Hysteresis — prevents thrashing on borderline cases
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter hysteresis band: requires N consecutive frames with the
/// same cull decision before flipping. Avoids one-frame pops when an
/// emitter is right on the HZB edge.
class VfxOcclusionHysteresis {
public:
    /// `confirmFrames` is the run length required to flip state. 3 is a
    /// good default (about 50 ms at 60 fps).
    explicit VfxOcclusionHysteresis(uint32_t confirmFrames = 3u)
        : confirmFrames_(confirmFrames) {}

    /// Feeds a per-frame raw cull vote. Returns the filtered (state-
    /// machine) cull decision the caller should act on.
    ///
    /// - When the raw vote matches the current committed state, the
    ///   dissenting-run counter resets to zero.
    /// - When the raw vote disagrees, the counter increments; only after
    ///   `confirmFrames` matching votes in a row does the committed
    ///   state flip.
    [[nodiscard]] bool Update(bool rawOccludedVote) noexcept;

    /// Resets to the "visible" state with no dissent.
    void Reset() noexcept {
        committedOccluded_ = false;
        dissentCount_      = 0u;
    }

    /// Returns the currently committed decision (debug / inspection).
    [[nodiscard]] bool CommittedOccluded() const noexcept { return committedOccluded_; }

    /// Returns the current dissent counter (0 when the raw vote matches
    /// the committed state).
    [[nodiscard]] uint32_t DissentCount() const noexcept { return dissentCount_; }

private:
    uint32_t confirmFrames_;
    bool     committedOccluded_ = false;
    uint32_t dissentCount_       = 0u;
};

} // namespace uz
