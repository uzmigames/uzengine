/// UzEngine Light Renderer — Implementation.
///
/// Extracts directional, point, and spot light data from ECS components,
/// builds LightingCB, and uploads to the CB ring.

#include "light_renderer.h"

#include "world.h"
#include "light_component.h"
#include "scene_component.h"
#include "color_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

LightFrameData ExtractLightingFromECS(
    World* world,
    CBRingAllocator& cbRing)
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

            result.perLightShadowBias      = dirLight.shadowBias;
            result.perLightShadowSlopeBias = dirLight.shadowSlopeBias;
            result.lightSourceAngleDeg     = dirLight.lightSourceAngle;
            result.farShadowCascadeCount   = dirLight.farShadowCascadeCount;
            result.farShadowDistance       = dirLight.farShadowDistance;
            result.farShadowLodBias        = dirLight.farShadowLodBias;
            break;
        }
    }

    // ── Build LightingCB from ECS point and spot lights ─────────────────
    LightingCB lightingData{};
    if (world) {
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
            lightingData.pointLightCount++;
        }

        // ── Spot lights ──
        // @source UE5 SpotLightComponent.cpp:48-54 — cone angle clamping and SpotAngles packing
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

            // UE5 SpotAngles packing:
            // @source UE5 SpotLightComponent.cpp:48-54
            // ClampedInnerConeAngle in [0, 89] degrees → radians
            // ClampedOuterConeAngle in (inner+0.001, 89+0.001] degrees → radians
            static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
            const float innerRad = std::clamp(sl.innerAngleDeg, 0.0f, 89.0f) * kDegToRad;
            const float outerRad = std::clamp(sl.outerAngleDeg * kDegToRad,
                                              innerRad + 0.001f,
                                              89.0f * kDegToRad + 0.001f);

            const float cosInner = std::cos(innerRad);
            const float cosOuter = std::cos(outerRad);
            // @source UE5 SpotLightComponent.cpp:54
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

            // Shadow index — if the spot light casts shadows and we have room
            // in the local shadow array, assign a shadow slot index. Otherwise
            // mark as unshadowed (0xFFFFFFFF).
            if (sl.castShadows && result.spotShadowCount < kMaxLocalLightShadows) {
                const uint32_t shadowIdx = result.spotShadowCount;
                sld.shadowIndex = shadowIdx;

                SpotLightShadowInfo& shadowInfo = result.spotShadows[shadowIdx];
                shadowInfo.position   = pos;
                shadowInfo.direction  = dir;
                shadowInfo.outerAngle = outerRad;
                shadowInfo.radius     = sl.range;
                shadowInfo.bias       = sl.spotShadowBias;
                shadowInfo.lightIndex = idx;
                result.spotShadowCount++;
            } else {
                sld.shadowIndex = UINT32_MAX;
            }

            lightingData.spotLightCount++;
        }
    }

    result.lightingCBAlloc = cbRing.Allocate(sizeof(LightingCB));
    std::memcpy(result.lightingCBAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    return result;
}

} // namespace uz
