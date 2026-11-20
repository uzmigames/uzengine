/// UzEngine SkyLight ambient fallback — the ambient a frame carries while the sky IBL is not ready.
///
/// The deferred pass takes its diffuse ambient from the sky irradiance SH (or the irradiance cubemap
/// when the SH is zero). Both come from the first sky bake (PostFrameAtmosphereBake: the captured
/// sky cubemap, the irradiance / prefiltered bake and the SH projection; for a specified cubemap, the
/// HDRI load), so the frames before it have no sky term at all and only the `skyLightMinAmbient`
/// floor remains. UE5 never invents an ambient: `SkyLightColor` is black without a sky light and the
/// engine has carried no constant since e2571a66 removed the `{0.08, 0.09, 0.12}` it used to add.
/// With a SkyLightComponent present, those frames use the hemisphere-average approximation of its
/// light instead (phase3a-3.51.0 fix 3):
///
///   fallback ambient = lightColor * intensity * kSkyLightFallbackIrradiance
///
/// On the deferred path this rides the existing `skyLightMinAmbient` floor: the shader floors the
/// sky irradiance with it and then multiplies by `skyLightColor * skyLightIntensity`
/// (deferred_lighting_ps.hlsl, `skyTintDiffuse`), so a floor of 0.1 yields exactly the product above
/// on every surface the sky SH would light. The forward paths (mesh_ps, translucent_ps) read the
/// same product from `PerFrameCB::ambientColor`. The frame the bake lands the floor returns to the
/// authored `minOcclusion` and `ambientColor` to zero, so the steady-state numerics are untouched.

#pragma once

#include "sh_projection.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

/// Irradiance floor the fallback frames use: the shader multiplies it by the sky light colour and
/// intensity, so the frame's ambient is `lightColor * intensity * 0.1`.
constexpr float kSkyLightFallbackIrradiance = 0.1f;

/// Frame index after which a still-active fallback is reported as a warning: a captured sky bakes
/// within the first frames (frame 1 on the demo scenes), so a later fallback means the bake never
/// happened (no SkyAtmosphere, no cubemap) and the ambient is the approximation, not the sky.
constexpr uint32_t kSkyLightFallbackReportFrame = 10;

/// True when every packed SH coefficient is zero (nothing projected yet, or the readback failed).
[[nodiscard]] inline bool IsSkySHZero(const SHCoefficients& sh) noexcept {
    for (const Vec4& c : sh.coeffs) {
        if (c.x != 0.0f || c.y != 0.0f || c.z != 0.0f || c.w != 0.0f) return false;
    }
    return true;
}

/// True when the deferred pass has a diffuse sky source: a non-zero SH, or an irradiance cubemap
/// bound (the shader samples the cubemap when the SH is zero, deferred_lighting_ps.hlsl).
[[nodiscard]] inline bool SkyIBLReady(const SHCoefficients& sh, uint32_t irradianceSrvIndex) noexcept {
    return !IsSkySHZero(sh) || irradianceSrvIndex != UINT32_MAX;
}

/// The ambient colour of a fallback frame: `lightColor * intensity * kSkyLightFallbackIrradiance`.
[[nodiscard]] inline Vec3 SkyLightFallbackAmbient(const Vec3& lightColor, float intensity) noexcept {
    const float s = intensity * kSkyLightFallbackIrradiance;
    return {lightColor.x * s, lightColor.y * s, lightColor.z * s};
}

} // namespace uz
