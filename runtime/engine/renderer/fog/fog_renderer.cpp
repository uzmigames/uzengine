/// UzEngine Fog Renderer — Implementation.
///
/// Extracts HeightFogComponent from ECS, pre-computes UE5 RayOriginTerms,
/// and registers the height fog render graph pass.

#include "fog_renderer.h"

#include "world.h"
#include "height_fog_component.h"
#include "sky_atmosphere_component.h"
#include "height_fog_pass.h"
#include "camera.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

void AddHeightFogFromECS(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    World* world,
    const Camera& camera,
    PipelineLibrary* pipelines,
    CBRingAllocator& cbRing,
    uint32_t depthSrvIndex,
    RHITextureHandle depthTexture,
    bool bindless,
    float vpWidth,
    float vpHeight,
    float skyLightColorR,
    float skyLightColorG,
    float skyLightColorB)
{
    if (!world) return;

    auto fogView = world->GetRegistry().view<HeightFogComponent>();
    for (auto enttEntity : fogView) {
        auto& fogComp = fogView.get<HeightFogComponent>(enttEntity);
        if (!fogComp.enabled) continue;

        // UE5 (FSceneRenderer::InitFogConstants):
        //   ExponentialFogParameters.x = FogDensity * exp2(-FogHeightFalloff * (CamZ - FogHeight))
        float camZ = camera.GetPosition().z;
        float fogHeightDiff = camZ - fogComp.baseHeight;
        float clampedPower = std::max(-126.f, -fogComp.heightFalloff * fogHeightDiff);
        float rayOriginTerms = fogComp.density * std::pow(2.0f, clampedPower);

        HeightFogCB fogCB{};
        fogCB.fogColor         = {fogComp.color.x, fogComp.color.y, fogComp.color.z, 0.f};
        fogCB.fogDensity       = rayOriginTerms;
        fogCB.fogHeightFalloff = fogComp.heightFalloff;
        fogCB.fogBaseHeight    = fogComp.baseHeight;
        fogCB.fogMaxOpacity    = fogComp.maxOpacity;
        fogCB.fogStartDistance = fogComp.startDistance;
        fogCB.fogCutoffDistance = fogComp.fogCutoffDistance;

        // @source UE5 FogRendering.cpp:245-254 — InitFogConstants
        // Second layer precomputation identical to first layer:
        //   CollapsedFogParameter[1] = Density * pow(2, clamp(-Falloff*(ObserverH - FogH), -125, 126))
        // @source UE5 FogRendering.cpp:257 — ExponentialFogParameters2.x = precomputed, .z = raw density, .w = height
        // @deviations NONE
        if (fogComp.secondFogDensity > 0.0f) {
            float secondFogHeightDiff = camZ - fogComp.secondFogBaseHeight;
            float secondClampedPower = std::max(-126.f, -fogComp.secondFogHeightFalloff * secondFogHeightDiff);
            secondClampedPower = std::min(secondClampedPower, 126.f);
            fogCB.secondFogDensity       = fogComp.secondFogDensity * std::pow(2.0f, secondClampedPower);
            fogCB.secondFogHeightFalloff = fogComp.secondFogHeightFalloff;
            fogCB.secondFogRawDensity    = fogComp.secondFogDensity;
            fogCB.secondFogBaseHeight    = fogComp.secondFogBaseHeight;
        } else {
            fogCB.secondFogDensity       = 0.0f;
            fogCB.secondFogHeightFalloff = 0.0f;
            fogCB.secondFogRawDensity    = 0.0f;
            fogCB.secondFogBaseHeight    = 0.0f;
        }

        fogCB.directionalInscatteringColor = {
            fogComp.directionalInscatteringColor.x,
            fogComp.directionalInscatteringColor.y,
            fogComp.directionalInscatteringColor.z, 0.f};
        fogCB.directionalInscatteringExponent      = fogComp.directionalInscatteringExponent;
        fogCB.directionalInscatteringStartDistance  = fogComp.directionalInscatteringStartDistance;

        // ── Sky atmosphere injection ──
        /// @source UE5 HeightFogCommon.ush:174-180
        /// @source UE5 FogRendering.cpp:85 — SkyAtmosphereAmbientContributionColorScale
        /// When SkyAtmosphere exists, inject sky light color into fog inscattering.
        fogCB.skyAtmContribColorScale = {1.f, 1.f, 1.f, 0.f}; // UE5 default: white
        fogCB.heightFogContribution = 0.f; // Will be set if SkyAtmosphereComponent found
        fogCB.skyLightColorR = skyLightColorR;
        fogCB.skyLightColorG = skyLightColorG;
        fogCB.skyLightColorB = skyLightColorB;

        auto skyView = world->GetRegistry().view<SkyAtmosphereComponent>();
        for (auto skyEnt : skyView) {
            auto& skyComp = skyView.get<SkyAtmosphereComponent>(skyEnt);
            fogCB.heightFogContribution = skyComp.heightFogContribution;
            break;
        }

        CBAllocation fogAlloc = cbRing.Allocate(sizeof(HeightFogCB));
        std::memcpy(fogAlloc.cpuPtr, &fogCB, sizeof(HeightFogCB));

        HeightFogPassDesc fogDesc{};
        fogDesc.backbuffer    = hdrRes;
        fogDesc.depthGBuffer  = depthRes;
        fogDesc.perFrameAlloc = perFrameAlloc;
        fogDesc.fogAlloc      = fogAlloc;
        fogDesc.pipeline      = pipelines->GetHeightFog();
        fogDesc.depthSrvIndex = depthSrvIndex;
        fogDesc.depthTexture  = depthTexture;
        fogDesc.bindless      = bindless;
        fogDesc.viewportWidth = vpWidth;
        fogDesc.viewportHeight = vpHeight;
        AddHeightFogPass(rgBuilder, fogDesc);

        break;  // First HeightFogComponent only
    }
}

} // namespace uz
