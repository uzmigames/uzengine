/// UzEngine Fog Renderer — Implementation.
///
/// Extracts HeightFogComponent from ECS, pre-computes UE5 RayOriginTerms,
/// and registers the height fog render graph pass.

#include "fog_renderer.h"

#include "world.h"
#include "height_fog_component.h"
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
    float vpHeight)
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

        fogCB.secondFogDensity       = fogComp.secondFogDensity;
        fogCB.secondFogHeightFalloff = fogComp.secondFogHeightFalloff;
        fogCB.secondFogBaseHeight    = fogComp.secondFogBaseHeight;

        fogCB.directionalInscatteringColor = {
            fogComp.directionalInscatteringColor.x,
            fogComp.directionalInscatteringColor.y,
            fogComp.directionalInscatteringColor.z, 0.f};
        fogCB.directionalInscatteringExponent      = fogComp.directionalInscatteringExponent;
        fogCB.directionalInscatteringStartDistance  = fogComp.directionalInscatteringStartDistance;

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
