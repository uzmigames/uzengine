// TAA sub-pixel jitter applied to a view-projection matrix.
//
// The jitter must translate the image by exactly (jitterX, jitterY) render pixels
// for every vertex, whatever its depth or world position. In clip space that is
//     clip'.xy = clip.xy + jNdc * clip.w,   jNdc = (jx * 2/W, -jy * 2/H)
// i.e. a translation in NDC. With the engine's row-vector convention
// (clip = v · VP) clip.w is the dot product of v with column 3 of VP, so the
// translation is folded into the matrix by adding jNdc * column3 to columns 0/1:
//     VP'[i][0] = VP[i][0] + jNdc.x * VP[i][3]
//     VP'[i][1] = VP[i][1] + jNdc.y * VP[i][3]      for i = 0..3
// (equivalently VP' = VP · T with T = identity, T[3][0] = jNdc.x, T[3][1] = jNdc.y).
//
// The previous code added jNdc to VP[2][0] / VP[2][1] directly. That is only
// correct for a pure PROJECTION matrix, where row 2 multiplies view-space z
// (= clip.w). In the combined VIEW-PROJECTION matrix row 2 multiplies the
// vertex's WORLD z, so the offset became  jNdc * world_z / clip.w : a shear that
// grows as geometry gets close to the camera (a wall 40 cm away at z ~ 576 cm
// moved by ~14x the jitter, ±7 px per frame) and vanishes for far geometry.
// The TAA resolve, which un-jitters by (jitterX, jitterY) pixels, could not
// follow it: near surfaces "wobbled" while the camera stood still (Sponza
// report 2026-09-08, phase0_sponza-followup-07). Also shared with the DLSS /
// FSR2 / TSR upscalers, which receive (jitterX, jitterY) in pixels.
#pragma once

#include "mat4.h"

namespace uz {

/// Returns `viewProj` translated in NDC by (jitterX, jitterY) render pixels.
/// `renderW/renderH` are the dimensions of the target the geometry is
/// rasterised into (render resolution, not display resolution).
[[nodiscard]] inline Mat4 ApplyTaaJitter(const Mat4& viewProj,
                                         float jitterX, float jitterY,
                                         float renderW, float renderH) noexcept {
    if (renderW <= 0.0f || renderH <= 0.0f) return viewProj;
    // +2/W: NDC spans 2 units over W pixels. Y negated: pixel Y grows downward,
    // clip-space Y grows upward.
    const float jxNdc = jitterX * 2.0f / renderW;
    const float jyNdc = jitterY * -2.0f / renderH;
    Mat4 out = viewProj;
    for (int i = 0; i < 4; ++i) {
        const float w = viewProj.m.m[i][3];
        out.m.m[i][0] += jxNdc * w;
        out.m.m[i][1] += jyNdc * w;
    }
    return out;
}

} // namespace uz
