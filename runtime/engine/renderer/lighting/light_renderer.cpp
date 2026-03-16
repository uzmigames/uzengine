/// UzEngine Light Renderer — Implementation.
///
/// Extracts directional and point light data from ECS components,
/// builds LightingCB, and uploads to the CB ring.

#include "light_renderer.h"

#include "world.h"
#include "light_component.h"
#include "scene_component.h"
#include "color_utils.h"

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

    // ── Build LightingCB from ECS point lights ──────────────────────────
    LightingCB lightingData{};
    if (world) {
        auto pointLightView = world->GetRegistry().view<PointLightComponent>();
        for (auto enttEntity : pointLightView) {
            if (lightingData.pointLightCount >= kMaxPointLights) break;

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
    }

    result.lightingCBAlloc = cbRing.Allocate(sizeof(LightingCB));
    std::memcpy(result.lightingCBAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    return result;
}

} // namespace uz
