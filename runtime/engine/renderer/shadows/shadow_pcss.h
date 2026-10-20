/// UzEngine PCSS (percentage-closer soft shadows) — penumbra estimate for the directional CSM.
///
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — blocker search + similar-triangles penumbra.
///
/// Fernando's estimate for a perspective light,
///
///     w_penumbra = (d_receiver - d_blocker) / d_blocker * w_light,
///
/// takes the light's physical width and distances measured from the light. The sun is a directional
/// light: its cascades are orthographic and the light sits at infinity with a fixed angular size, so
/// w_light / d_blocker becomes tan(half source angle) and the same similar triangles give
///
///     w_penumbra = (d_receiver - d_blocker) * tan(theta_half)        [world units, radius]
///
/// with the receiver-blocker distance measured along the light direction, i.e. along the cascade's
/// depth axis. The shadow map stores that depth normalised over the cascade's ortho range, so a
/// normalised gap dz spans dz * depthRange world units, and in shadow-map texels the radius is
///
///     texels = dz * depthRange * tan(theta_half) * lightSizeScale / worldUnitsPerTexel.
///
/// Everything but dz is constant per cascade: `PcssDepthToPenumbraTexels` folds it into one factor
/// that `SetupShadowFrame` uploads per cascade (`ShadowDataCB::cascadePcssDepthToTexels`), and
/// `shadow.hlsli` multiplies it by the measured depth gap (`PcssPenumbraTexels` there mirrors the
/// function here). `lightSizeScale` is `r.Shadows.SoftPCSS.LightSize` (1 = the light's own angular
/// diameter, `DirectionalLightComponent::lightSourceAngle`).

#pragma once

#include <algorithm>

namespace uz {

/// Penumbra radius in shadow texels per unit of normalised shadow-map depth gap, for one
/// orthographic cascade. 0 when the texel size is not positive (no cascade).
[[nodiscard]] inline float PcssDepthToPenumbraTexels(float tanHalfSourceAngle, float lightSizeScale,
                                                     float depthRangeWorld, float worldUnitsPerTexel) {
    if (worldUnitsPerTexel <= 0.0f) return 0.0f;
    const float tanEffective = std::max(tanHalfSourceAngle * lightSizeScale, 0.0f);
    return tanEffective * std::max(depthRangeWorld, 0.0f) / worldUnitsPerTexel;
}

/// Penumbra radius in texels for a receiver at normalised depth `receiverDepth` whose blockers
/// average `avgBlockerDepth` (both in the cascade's [0,1] depth, closer to the light = smaller).
/// 0 when the receiver touches its blocker, growing linearly with the gap; a blocker behind the
/// receiver (a search false positive) also gives 0 rather than a negative radius.
[[nodiscard]] inline float PcssPenumbraTexels(float receiverDepth, float avgBlockerDepth, float depthToTexels) {
    return std::max(receiverDepth - avgBlockerDepth, 0.0f) * depthToTexels;
}

/// Clamp of a kernel radius to the CVar window (r.Shadow.PCSSMinPenumbra / PCSSMaxPenumbra, texels).
/// A max below the min collapses onto the min instead of producing an inverted range.
[[nodiscard]] inline float PcssClampRadiusTexels(float radiusTexels, float minTexels, float maxTexels) {
    return std::min(std::max(radiusTexels, minTexels), std::max(maxTexels, minTexels));
}

/// Blocker-search radius in texels: the widest penumbra any blocker in front of this receiver can
/// produce (a blocker at the cascade's near plane, depth gap = receiverDepth), clamped like the
/// filter. Because the gap never exceeds the receiver depth, the filter radius never exceeds the
/// search radius, so "no blocker found" is exact for the filter footprint.
[[nodiscard]] inline float PcssSearchRadiusTexels(float receiverDepth, float depthToTexels,
                                                  float minTexels, float maxTexels) {
    return PcssClampRadiusTexels(receiverDepth * depthToTexels, minTexels, maxTexels);
}

} // namespace uz
