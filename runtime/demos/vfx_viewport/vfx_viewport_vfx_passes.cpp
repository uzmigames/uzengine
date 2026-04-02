/// VFX Viewport Demo — VFX-Specific Render Passes
///
/// Extracted from vfx_viewport_demo.cpp to keep files under 1500 lines.
/// Contains billboard, trail, decal, and distortion render passes.

#include "vfx_viewport_demo.h"
#include "root_slots.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderVFXPass(IRHICommandList* cmd, uint64_t perFrameGpu,
                                      float /*dt*/) {
    // Early-out if no emitters have a valid billboard PSO
    bool anyValid = false;
    for (const auto& e : m_emitters) {
        if (e.enabled && e.system->GetBillboardPSO().IsValid()) {
            anyValid = true;
            break;
        }
    }
    if (!anyValid) return;

    // Render into HDR target with depth read (no write — particles are translucent)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    const uint32_t depthSrvIdx = GetDevice()->GetTextureSRVIndex(m_depthBuffer);

    // Use the active preset material's blend mode
    const auto& mat = m_vfxMaterials[m_currentMaterialIndex];
    const uint32_t blendMode = mat.blendMode;

    // Draw each emitter's particles
    for (auto& emitter : m_emitters) {
        if (!emitter.enabled) continue;
        if (!emitter.system->GetBillboardPSO().IsValid()) continue;

        const Vec3 emitterPos(emitter.config.position[0],
                               emitter.config.position[1],
                               emitter.config.position[2]);

        emitter.system->DrawBillboards(cmd, GetCBRing(), perFrameGpu,
                                       GetCamera().GetView(), emitterPos,
                                       m_prevViewProj,
                                       emitter.config,
                                       blendMode,
                                       depthSrvIdx);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Trail Ribbon Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderTrailPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_trailPSO.IsValid()) return;
    if (!m_trailVB.IsValid() || !m_trailIB.IsValid()) return;
    if (m_trailVBSrvIndex == UINT32_MAX || m_trailIBSrvIndex == UINT32_MAX) return;

    auto renderData = m_trailSystem.GetRenderData();
    if (renderData.empty()) return;

    // Begin render pass: HDR target + depth read (no write)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_trailPSO);

    // PerFrameCB at b1 (root CBV slot 1)
    cmd->SetRootCBV(1, perFrameGpu);

    auto* device = GetDevice();
    const uint32_t depthSrvIdx = device->GetTextureSRVIndex(m_depthBuffer);

    for (const auto& trail : renderData) {
        if (trail.vertexCount == 0 || trail.vertexCount > kMaxTrailVertices) continue;
        if (trail.indexCount == 0 || trail.indexCount > kMaxTrailIndices) continue;

        // Upload vertices to the StructuredBuffer (bindless SRV)
        void* vbPtr = device->MapBuffer(m_trailVB);
        std::memcpy(vbPtr, trail.vertices, trail.vertexCount * sizeof(TrailVertex));
        device->UnmapBuffer(m_trailVB);

        // Upload indices to the StructuredBuffer<uint> (bindless SRV)
        void* ibPtr = device->MapBuffer(m_trailIB);
        std::memcpy(ibPtr, trail.indices, trail.indexCount * sizeof(uint32_t));
        device->UnmapBuffer(m_trailIB);

        // Set trail root constants at b0 (root param 0)
        // Includes bindless SRV indices for vertex/index buffers so the VS
        // can fetch vertex data via ResourceDescriptorHeap[srvIndex].
        TrailRootConstants rootConsts{};
        rootConsts.uvScrollRate         = 1.0f;
        rootConsts.softDepthRange       = 0.3f;
        rootConsts.alphaClipThreshold   = 1.0f / 255.0f; // kTrailAlphaClipMin
        rootConsts.trailTexSrvIndex     = UINT32_MAX;     // No trail texture — vertex color only
        rootConsts.depthBufferSrvIndex  = depthSrvIdx;
        rootConsts.vertexBufferSrvIndex = m_trailVBSrvIndex;
        rootConsts.indexBufferSrvIndex  = m_trailIBSrvIndex;
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rootConsts),
                              sizeof(rootConsts) / sizeof(uint32_t));

        // Draw with bindless vertex fetch: SV_VertexID [0, indexCount) maps into
        // the index buffer SRV, which then indexes into the vertex buffer SRV.
        // No SetVertexBuffer or SetIndexBuffer needed — no IA is used.
        cmd->Draw(trail.indexCount);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Decal Projection Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderDecalPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_decalPSO.IsValid()) return;

    auto* cbRing = GetCBRing();
    auto* device = GetDevice();

    // Render into HDR target with NO depth attachment (depth read as bindless SRV in PS)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.hasDepth = false;
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(GetWidth());
    vp.height = static_cast<float>(GetHeight());
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(GetWidth());
    sc.bottom = static_cast<int32_t>(GetHeight());
    cmd->SetScissor(sc);

    cmd->SetPipeline(m_decalPSO);

    // PerFrameCB at b1 (root CBV slot 1)
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    const uint32_t depthSrvIdx = device->GetTextureSRVIndex(m_depthBuffer);

    for (uint32_t i = 0; i < m_activeDecalCount; ++i) {
        const auto& decal = m_decals[i];

        // Build decal world matrix: translation + scale by half-extents.
        // The VS cube is [-0.5, 0.5], so scale by 2*halfExtents for the full OBB.
        const Mat4 decalScale = Mat4::Scaling(decal.halfExtents * 2.0f);
        const Mat4 decalTranslation = Mat4::Translation(decal.position);
        const Mat4 decalWorld = decalScale * decalTranslation;
        const Mat4 decalInvWorld = decalWorld.Inverse();

        // Fill DecalObjectCB
        DecalObjectCB decalCB{};
        decalCB.decalWorld = decalWorld;
        decalCB.decalInvWorld = decalInvWorld;
        decalCB.decalOpacity = decal.opacity;
        decalCB.decalFadeDistance = 10.0f;   // 10 cm edge fade
        decalCB.decalDepthSrv = depthSrvIdx;
        // No textures — PS will use constant white (decalAlbedoSrv = UINT32_MAX)

        auto decalAlloc = cbRing->Allocate(sizeof(DecalObjectCB));
        std::memcpy(decalAlloc.cpuPtr, &decalCB, sizeof(DecalObjectCB));

        // DecalObjectCB at b2 (root CBV slot 2 = kSlotPerObject)
        cmd->SetRootCBV(kSlotPerObject, decalAlloc.gpuAddr);

        // Draw unit cube (36 vertices via SV_VertexID, no vertex buffer)
        cmd->Draw(36, 1, 0, 0);
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Distortion Pass (Heat-Haze Accumulation + HDR Warp Resolve)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderDistortionPass(IRHICommandList* cmd, uint64_t /*perFrameGpu*/) {
    auto* device = GetDevice();

    // ── Step 1: Compute emitter screen UV ─────────────────────────────────
    // Project emitter world position through viewProj to get screen UV.
    const Camera& cam = GetCamera();
    const Mat4 viewProj = cam.GetViewProjection();
    const Vec3 emitterWorld(ActiveEmitter().config.position[0],
                            ActiveEmitter().config.position[1],
                            ActiveEmitter().config.position[2]);
    const Vec4 emitterClip = viewProj * Vec4(emitterWorld, 1.0f);

    // Perspective divide + NDC to UV
    float emitterScreenU = 0.5f;
    float emitterScreenV = 0.5f;
    if (emitterClip.w > 0.001f) {
        const float ndcX = emitterClip.x / emitterClip.w;
        const float ndcY = emitterClip.y / emitterClip.w;
        emitterScreenU = ndcX * 0.5f + 0.5f;
        emitterScreenV = 1.0f - (ndcY * 0.5f + 0.5f); // Flip Y for D3D UV convention
    }

    // ── Step 2: Accumulate distortion into m_distortionRT ──────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_distortionRT;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&barrier, 1);
    }

    {
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = m_distortionRT;
        rpDesc.colorAttachments[0].clear = true;
        rpDesc.colorAttachments[0].clearColor[0] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[1] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[2] = 0.0f;
        rpDesc.colorAttachments[0].clearColor[3] = 0.0f;
        rpDesc.hasDepth = false;
        cmd->BeginRenderPass(rpDesc);

        RHIViewport vp{};
        vp.width = static_cast<float>(GetWidth());
        vp.height = static_cast<float>(GetHeight());
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        cmd->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = static_cast<int32_t>(GetWidth());
        sc.bottom = static_cast<int32_t>(GetHeight());
        cmd->SetScissor(sc);

        cmd->SetPipeline(m_distortionHazePSO);

        // Root constants at b0: emitter screen UV, strength, time, radius, frequency
        DistortionHazeConstants hazeConsts{};
        hazeConsts.emitterScreenUV[0] = emitterScreenU;
        hazeConsts.emitterScreenUV[1] = emitterScreenV;
        hazeConsts.strength = m_distortionStrength;
        hazeConsts.time = m_totalTime;
        hazeConsts.radius = 0.25f;     // 25% of screen
        hazeConsts.frequency = 30.0f;  // Wave frequency
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&hazeConsts),
                              sizeof(hazeConsts) / sizeof(uint32_t));

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndRenderPass();
    }

    // Transition distortion RT to SRV
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_distortionRT;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter = RHIResourceState::Common;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Step 3: Copy HDR → preDistortionCopy ───────────────────────────────
    {
        // Transition HDR to CopySrc, preDistortionCopy to CopyDst
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture = m_hdrTarget;
        barriers[0].stateBefore = RHIResourceState::RenderTarget;
        barriers[0].stateAfter = RHIResourceState::CopySrc;
        barriers[1].texture = m_preDistortionCopy;
        barriers[1].stateBefore = RHIResourceState::Common;
        barriers[1].stateAfter = RHIResourceState::CopyDst;
        cmd->ResourceBarrier(barriers, 2);
    }

    cmd->CopyTexture(m_hdrTarget, m_preDistortionCopy);

    {
        // Transition preDistortionCopy to SRV, HDR back to RenderTarget
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture = m_preDistortionCopy;
        barriers[0].stateBefore = RHIResourceState::CopyDst;
        barriers[0].stateAfter = RHIResourceState::Common;
        barriers[1].texture = m_hdrTarget;
        barriers[1].stateBefore = RHIResourceState::CopySrc;
        barriers[1].stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(barriers, 2);
    }

    // ── Step 4: Resolve — warp HDR color by distortion offsets ─────────────
    {
        RHIRenderPassDesc rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments[0].texture = m_hdrTarget;
        rpDesc.hasDepth = false;
        cmd->BeginRenderPass(rpDesc);

        RHIViewport vp{};
        vp.width = static_cast<float>(GetWidth());
        vp.height = static_cast<float>(GetHeight());
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        cmd->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right = static_cast<int32_t>(GetWidth());
        sc.bottom = static_cast<int32_t>(GetHeight());
        cmd->SetScissor(sc);

        cmd->SetPipeline(m_distortionResolvePSO);

        // Root constants at b0: SRV indices for pre-distortion color and distortion RT
        DistortionResolveConstants resolveConsts{};
        resolveConsts.colorTextureSrvIndex = device->GetTextureSRVIndex(m_preDistortionCopy);
        resolveConsts.distortionTextureSrvIndex = device->GetTextureSRVIndex(m_distortionRT);
        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&resolveConsts),
                              sizeof(resolveConsts) / sizeof(uint32_t));

        // Fullscreen triangle
        cmd->Draw(3, 1, 0, 0);

        cmd->EndRenderPass();
    }

    // HDR is now in RenderTarget state with warped color — the caller will
    // transition it to Common/SRV for bloom and tonemap.
}

// OnResize
// ─────────────────────────────────────────────────────────────────────────────


} // namespace uz
