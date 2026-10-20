/// UzEngine Light Renderer — Implementation.
///
/// Extracts directional, point, and spot light data from ECS components,
/// builds LightingCB, and uploads to the CB ring.

#include "light_renderer.h"

#include "world.h"
#include "light_component.h"
#include "scene_component.h"
#include "sky_atmosphere_component.h"
#include "color_utils.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Kill switch for the rect area lights (phase3a 3.33.0). Scenes without a rect light are unaffected
// either way: the shader skips the branch when rectLightCount is 0.
static auto* cv_LightsAreaLTC = UZ_CVAR_BOOL("r.Lights.AreaLTC", true,
    "Rectangular area lights shaded with Linearly Transformed Cosines (Heitz 2016). 0 drops them from the lighting CB.");

namespace uz {

/// Computes shadow importance for a light based on visual impact.
///
/// The metric is `intensity * radius^2 / max(1.0, distance^2)` where:
/// - `intensity` = magnitude of the light's color vector (already pre-multiplied by intensity)
/// - `radius`    = attenuation radius (coverage area proxy via radius^2)
/// - `distance`  = distance from camera to light position
///
/// Nearby bright lights with large radii are ranked highest, ensuring they
/// receive shadow atlas tiles before dim/distant/small lights.
static float ComputeShadowImportance(const Vec3& lightColor, float radius,
                                     const Vec3& lightPos, const Vec3& cameraPos)
{
    const float intensity = lightColor.Length();
    const float distSq = (lightPos - cameraPos).LengthSq();
    return intensity * (radius * radius) / std::max(1.0f, distSq);
}

LightFrameData ExtractLightingFromECS(
    World* world,
    CBRingAllocator& cbRing,
    const Vec3& cameraPosition)
{
    LightFrameData result{};

    if (world) {
        // ── Directional light — first found ──────────────────────────────
        auto dirLightView = world->GetRegistry().view<DirectionalLightComponent>();
        for (auto enttEntity : dirLightView) {
            auto& dirLight = dirLightView.get<DirectionalLightComponent>(enttEntity);

            if (world->GetRegistry().all_of<SceneComponent>(enttEntity)) {
                auto& scene = world->GetRegistry().get<SceneComponent>(enttEntity);
                result.sunDir = (scene.GetRotation() * Vec3::Forward()).Normalized();
            } else {
                result.sunDir = dirLight.GetDirection().Normalized();
            }

            Vec3 dirCol = dirLight.color;
            if (dirLight.useTemperature) {
                dirCol = dirCol * KelvinToRgb(dirLight.temperature);
            }
            result.sunColor = dirCol * dirLight.intensity;
            result.hasDirectionalLight = true;

            result.perLightShadowBias      = dirLight.shadowBias;
            result.perLightShadowSlopeBias = dirLight.shadowSlopeBias;
            result.lightSourceAngleDeg     = dirLight.lightSourceAngle;
            result.farShadowCascadeCount   = dirLight.farShadowCascadeCount;
            result.farShadowDistance       = dirLight.farShadowDistance;
            result.dynamicShadowDistance     = dirLight.dynamicShadowDistance;
            result.farShadowLodBias        = dirLight.farShadowLodBias;
            break;
        }
        if (!result.hasDirectionalLight) {
            // No sun: the sky atmosphere still needs a sun direction for its own disc and
            // scattering, and it owns that value (scene key `sun_direction`). The light itself
            // stays zero and the cascade pass is skipped (frame_shadows.cpp).
            auto skyView = world->GetRegistry().view<SkyAtmosphereComponent>();
            for (auto skyEntity : skyView) {
                const Vec3 d = skyView.get<SkyAtmosphereComponent>(skyEntity).sunDirection;
                if (d.lengthSq() > 1e-8f) result.sunDir = d.Normalized();
                break;
            }
        }
    }

    // ── Build LightingCB from ECS point and spot lights ─────────────────
    LightingCB lightingData{};
    if (world) {
        // Temporary candidate arrays for importance sorting before budget trimming.
        // All shadow-casting lights are collected first, then sorted by visual
        // importance, and only the top-N by budget get atlas tiles.
        PointLightShadowInfo pointShadowCandidates[kMaxPointLights]{};
        uint32_t pointShadowCandidateCount = 0;

        SpotLightShadowInfo spotShadowCandidates[kMaxSpotLights]{};
        uint32_t spotShadowCandidateCount = 0;

        // ── Point lights ──
        auto pointLightView = world->GetRegistry().view<PointLightComponent>();
        for (auto enttEntity : pointLightView) {
            if (lightingData.pointLightCount >= kMaxPointLights) break;

            // Skip entities that also have SpotLightComponent — those are spot lights,
            // not point lights. SpotLightComponent inherits from PointLightComponent,
            // so EnTT's view<PointLightComponent> includes spot light entities.
            if (world->GetRegistry().all_of<SpotLightComponent>(enttEntity)) continue;

            auto& pl = pointLightView.get<PointLightComponent>(enttEntity);

            Vec3 pos{};
            if (world->GetRegistry().all_of<SceneComponent>(enttEntity)) {
                auto& scene = world->GetRegistry().get<SceneComponent>(enttEntity);
                pos = scene.GetWorldPosition();
            } else {
                pos = pl.GetWorldPosition();
            }

            Vec3 lightCol = pl.color;
            if (pl.useTemperature) {
                lightCol = lightCol * KelvinToRgb(pl.temperature);
            }
            const Vec3 col = lightCol * pl.intensity;
            const uint32_t idx = lightingData.pointLightCount;

            PointLightData& pld = lightingData.pointLights[idx];
            pld.position        = pos;
            pld.radius          = pl.range;
            pld.color           = col;
            pld.sourceRadius    = pl.sourceRadius;
            pld.falloffExponent = pl.lightFalloffExponent;
            pld.specularScale   = pl.specularScale;
            pld.diffuseScale    = pl.diffuseScale;
            pld.flags           = pl.useInverseSquaredFalloff ? kLightFlag_InverseSquared : 0u;
            pld.shadowIndex     = UINT32_MAX; // Assigned below after spot shadow count is known

            // Collect shadow candidate unconditionally — importance sorting and
            // budget trimming happen after all lights are collected.
            if (pl.castShadows && pointShadowCandidateCount < kMaxPointLights) {
                PointLightShadowInfo& shadowInfo = pointShadowCandidates[pointShadowCandidateCount];
                shadowInfo.position   = pos;
                shadowInfo.radius     = pl.range;
                shadowInfo.bias       = pl.pointShadowBias;
                shadowInfo.lightIndex = idx;
                pointShadowCandidateCount++;
            }

            lightingData.pointLightCount++;
        }

        // ── Spot lights ──
        // Cone angle clamping and SpotAngles packing.
        auto spotLightView = world->GetRegistry().view<SpotLightComponent>();
        for (auto enttEntity : spotLightView) {
            if (lightingData.spotLightCount >= kMaxSpotLights) break;

            auto& sl = spotLightView.get<SpotLightComponent>(enttEntity);

            Vec3 pos{};
            Vec3 dir{};
            if (world->GetRegistry().all_of<SceneComponent>(enttEntity)) {
                auto& scene = world->GetRegistry().get<SceneComponent>(enttEntity);
                pos = scene.GetWorldPosition();
                // Spot direction from component rotation, same as directional light
                dir = (scene.GetRotation() * Vec3::Forward()).Normalized();
            } else {
                pos = sl.GetWorldPosition();
                dir = (sl.GetRotation() * Vec3::Forward()).Normalized();
            }

            Vec3 lightCol = sl.color;
            if (sl.useTemperature) {
                lightCol = lightCol * KelvinToRgb(sl.temperature);
            }
            const Vec3 col = lightCol * sl.intensity;

            // SpotAngles packing:
            
            // ClampedInnerConeAngle in [0, 89] degrees → radians
            // ClampedOuterConeAngle in (inner+0.001, 89+0.001] degrees → radians
            static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
            const float innerRad = std::clamp(sl.innerAngleDeg, 0.0f, 89.0f) * kDegToRad;
            const float outerRad = std::clamp(sl.outerAngleDeg * kDegToRad,
                                              innerRad + 0.001f,
                                              89.0f * kDegToRad + 0.001f);

            const float cosInner = std::cos(innerRad);
            const float cosOuter = std::cos(outerRad);
            
            const float invDiff = 1.0f / (cosInner - cosOuter);

            const uint32_t idx = lightingData.spotLightCount;
            SpotLightData& sld = lightingData.spotLights[idx];
            sld.position              = pos;
            sld.radius                = sl.range;
            sld.color                 = col;
            sld.cosOuterAngle         = cosOuter;
            sld.direction             = dir;
            sld.invCosConeDifference  = invDiff;
            sld.falloffExponent       = sl.lightFalloffExponent;
            sld.specularScale         = sl.specularScale;
            sld.diffuseScale          = sl.diffuseScale;
            sld.flags                 = sl.useInverseSquaredFalloff ? kLightFlag_InverseSquared : 0u;

            // Collect shadow candidate unconditionally — importance sorting and
            // budget trimming happen after all spot lights are collected.
            sld.shadowIndex = UINT32_MAX; // Updated after sorting
            if (sl.castShadows && spotShadowCandidateCount < kMaxSpotLights) {
                SpotLightShadowInfo& shadowInfo = spotShadowCandidates[spotShadowCandidateCount];
                shadowInfo.position   = pos;
                shadowInfo.direction  = dir;
                shadowInfo.outerAngle = outerRad;
                shadowInfo.radius     = sl.range;
                shadowInfo.bias       = sl.spotShadowBias;
                shadowInfo.lightIndex = idx;
                spotShadowCandidateCount++;
            }

            lightingData.spotLightCount++;
        }

        // ── Rect area lights (phase3a 3.33.0): LTC in the deferred lighting ──
        // Orientation follows the spot convention: emits along the forward axis, width along
        // right, height along up. `color * intensity` is the radiance of the emitting surface.
        if (cv_LightsAreaLTC->value != 0.0f) {
            auto rectLightView = world->GetRegistry().view<RectLightComponent>();
            for (auto enttEntity : rectLightView) {
                if (result.rectLightCount >= kMaxRectLights) break;

                auto& rl = rectLightView.get<RectLightComponent>(enttEntity);

                Vec3 pos{};
                Quat rot{};
                if (world->GetRegistry().all_of<SceneComponent>(enttEntity)) {
                    auto& scene = world->GetRegistry().get<SceneComponent>(enttEntity);
                    pos = scene.GetWorldPosition();
                    rot = scene.GetRotation();
                } else {
                    pos = rl.GetWorldPosition();
                    rot = rl.GetRotation();
                }

                Vec3 lightCol = rl.color;
                if (rl.useTemperature) {
                    lightCol = lightCol * KelvinToRgb(rl.temperature);
                }

                RectLightData& rld = result.rectLights[result.rectLightCount];
                rld.position      = pos;
                rld.range         = rl.range;
                rld.right         = (rot * Vec3::Right()).Normalized();
                rld.halfWidth     = std::max(rl.width, 0.0f) * 0.5f;
                rld.up            = (rot * Vec3::Up()).Normalized();
                rld.halfHeight    = std::max(rl.height, 0.0f) * 0.5f;
                rld.color         = lightCol * rl.intensity;
                rld.flags         = rl.twoSided ? kRectLightFlag_TwoSided : 0u;
                rld.specularScale = rl.specularScale;
                rld.diffuseScale  = rl.diffuseScale;
                rld.shadowIndex   = UINT32_MAX;
                result.rectLightCount++;
            }
        }

        // ── Sort spot shadow candidates by importance (descending) ──────
        // Importance = intensity * radius^2 / max(1, distance^2).
        // Highest-importance lights get shadow atlas tiles first.
        std::sort(spotShadowCandidates, spotShadowCandidates + spotShadowCandidateCount,
            [&](const SpotLightShadowInfo& a, const SpotLightShadowInfo& b) {
                const float ia = ComputeShadowImportance(
                    lightingData.spotLights[a.lightIndex].color, a.radius,
                    a.position, cameraPosition);
                const float ib = ComputeShadowImportance(
                    lightingData.spotLights[b.lightIndex].color, b.radius,
                    b.position, cameraPosition);
                return ia > ib;
            });

        // Copy sorted winners into result and assign shadowIndex in lightingData
        result.spotShadowCount = std::min(spotShadowCandidateCount,
                                          static_cast<uint32_t>(kMaxLocalLightShadows));
        for (uint32_t si = 0; si < result.spotShadowCount; ++si) {
            result.spotShadows[si] = spotShadowCandidates[si];
            const uint32_t lightIdx = result.spotShadows[si].lightIndex;
            lightingData.spotLights[lightIdx].shadowIndex = si;
        }

        // ── Sort point shadow candidates by importance (descending) ──────
        std::sort(pointShadowCandidates, pointShadowCandidates + pointShadowCandidateCount,
            [&](const PointLightShadowInfo& a, const PointLightShadowInfo& b) {
                const float ia = ComputeShadowImportance(
                    lightingData.pointLights[a.lightIndex].color, a.radius,
                    a.position, cameraPosition);
                const float ib = ComputeShadowImportance(
                    lightingData.pointLights[b.lightIndex].color, b.radius,
                    b.position, cameraPosition);
                return ia > ib;
            });

        // ── Assign point light shadow indices (after spot shadow count is known) ──
        // Point light shadow faces are stored consecutively in the localShadows[]
        // array starting after all spot shadow entries. Each point light occupies
        // kPointLightShadowFaces (6) consecutive entries.
        //
        // Total tile budget: kMaxLocalLightShadows (16)
        // Spot lights occupy: spotShadowCount entries
        // Available for point lights: remaining / 6
        const uint32_t tilesUsedBySpots = result.spotShadowCount;
        const uint32_t tilesRemaining = (tilesUsedBySpots < kMaxLocalLightShadows)
            ? (kMaxLocalLightShadows - tilesUsedBySpots) : 0;
        const uint32_t maxPointShadowsAfterSpots = tilesRemaining / kPointLightShadowFaces;

        // Trim sorted candidates to budget
        result.pointShadowCount = std::min(pointShadowCandidateCount, maxPointShadowsAfterSpots);

        // Copy sorted winners into result and assign shadowIndex in lightingData
        for (uint32_t pi = 0; pi < result.pointShadowCount; ++pi) {
            result.pointShadows[pi] = pointShadowCandidates[pi];
            const uint32_t baseIdx = tilesUsedBySpots + pi * kPointLightShadowFaces;
            const uint32_t lightIdx = result.pointShadows[pi].lightIndex;
            lightingData.pointLights[lightIdx].shadowIndex = baseIdx;
        }
    }

    result.lightingCBAlloc = cbRing.Allocate(sizeof(LightingCB));
    std::memcpy(result.lightingCBAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    return result;
}

bool WorldHasRectLights(World* world)
{
    if (!world || cv_LightsAreaLTC->value == 0.0f) return false;
    auto view = world->GetRegistry().view<RectLightComponent>();
    return view.begin() != view.end();
}

} // namespace uz
