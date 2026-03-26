/// UzEngine Scene Renderer — Decal passes: deferred decal projection into the
/// G-Buffer (albedo, normal, roughness).
///
/// Registered between the geometry pass and the lighting pass so that decals
/// modify G-Buffer channels before the lighting pass reads them.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "pipeline_library.h"
#include "profiler.h"
#include "log.h"

#include "engine/renderer/passes/vfx/decal_pass.h"
#include "engine/vfx/decals/decal_manager.h"
#include "engine/vfx/decals/decal_component.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

void SceneRenderer::RegisterDecalPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // Skip if no decal manager is set or it has no active decals.
    if (!m_decalManager || !m_decalManager->IsInitialized()) return;
    if (m_decalManager->GetActiveCount() == 0) return;

    // Early out if the decal deferred pipeline isn't compiled yet.
    const auto decalPipeline = ctx.pipelines->GetDecalDeferred();
    if (!decalPipeline.IsValid()) return;

    // Early out if the unit cube buffers are not created yet.
    if (!m_decalCubeVB.IsValid() || !m_decalCubeIB.IsValid()) return;

    // ── Frustum cull and sort ─────────────────────────────────────────────
    // Build the view-projection matrix for frustum culling.
    const Mat4& viewMatrix = m_camera->GetView();
    const Mat4& projMatrix = m_camera->GetProjection();
    const Mat4 viewProj = projMatrix * viewMatrix;

    m_decalManager->FrustumCull(viewProj);

    const Vec3 camPos{
        ctx.perFrame.cameraPos.x,
        ctx.perFrame.cameraPos.y,
        ctx.perFrame.cameraPos.z
    };
    const auto& sortedDecals = m_decalManager->GetSortedDecals(camPos);
    if (sortedDecals.empty()) return;

    // ── Build inverse view-projection for depth reconstruction ────────────
    const Mat4 invViewProj = viewProj.Inverse();

    // ── Populate per-decal render data ────────────────────────────────────
    DecalPassDesc passDesc{};
    passDesc.decalPipeline    = decalPipeline;
    passDesc.cubeVertexBuffer = m_decalCubeVB;
    passDesc.cubeIndexBuffer  = m_decalCubeIB;
    passDesc.sceneDepthTexture = m_gbuffer.depth;

    // G-Buffer render graph resource IDs for write access.
    passDesc.gbufferAlbedo    = ctx.albedoRes;
    passDesc.gbufferNormal    = ctx.normalRes;
    // The GBuffer packs roughness into the normal texture's alpha channel.
    // DecalPassDesc::gbufferRoughness is left invalid — the decal shader
    // writes roughness through the normal target's alpha when roughnessOverride >= 0.
    passDesc.depthTarget      = ctx.depthRes;

    passDesc.decals.reserve(sortedDecals.size());

    for (const auto* decal : sortedDecals) {
        if (!decal || !decal->isActive) continue;
        if (decal->fadeAlpha <= 0.0f) continue;

        // Build per-decal constant buffer.
        DecalConstants decalCB{};

        // World-to-decal-local projection matrix.
        const Mat4 worldToDecal = decal->GetProjectionMatrix();
        std::memcpy(decalCB.worldToDecal, &worldToDecal, sizeof(float) * 16);

        // Inverse view-projection for depth-to-world reconstruction.
        std::memcpy(decalCB.invViewProj, &invViewProj, sizeof(float) * 16);

        // View-projection for vertex transform.
        std::memcpy(decalCB.viewProjection, &viewProj, sizeof(float) * 16);

        decalCB.normalIntensity   = decal->normalIntensity;
        decalCB.roughnessOverride = decal->roughnessOverride;
        decalCB.fadeAlpha         = decal->fadeAlpha;
        decalCB.blendMode         = static_cast<float>(static_cast<uint8_t>(decal->blendMode));
        decalCB.screenWidth       = ctx.renderWidth;
        decalCB.screenHeight      = ctx.renderHeight;

        const CBAllocation cbAlloc = m_cbRing->Allocate(sizeof(DecalConstants));
        if (!cbAlloc.cpuPtr) {
            UZ_LOG(VFX, Warning,
                   "RegisterDecalPasses: CB ring full, skipping decal '{}'",
                   decal->materialPath);
            continue;
        }
        std::memcpy(cbAlloc.cpuPtr, &decalCB, sizeof(DecalConstants));

        DecalRenderData renderData{};
        renderData.decal    = decal;
        renderData.decalCB  = cbAlloc;

        // Texture bindings: leave SRV indices at UINT32_MAX until material
        // system provides bindless descriptor indices for decal textures.
        // The decal pass will skip texture binding when indices are invalid.
        renderData.albedoSrvIndex     = UINT32_MAX;
        renderData.normalSrvIndex     = UINT32_MAX;
        renderData.roughnessSrvIndex  = UINT32_MAX;
        renderData.sceneDepthSrvIndex = m_gbuffer.depthSRV;

        passDesc.decals.push_back(renderData);
    }

    if (!passDesc.decals.empty()) {
        AddDecalPass(rgBuilder, passDesc);
    }
}

} // namespace uz
