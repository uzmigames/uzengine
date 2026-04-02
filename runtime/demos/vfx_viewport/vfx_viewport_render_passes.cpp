/// VFX Viewport Demo — Standard Render Passes
///
/// Extracted from vfx_viewport_demo.cpp to keep files under 1500 lines.
/// Contains shadow, sky, scene (forward PBR), tonemap, outline, and gizmo
/// render passes that are not VFX-specific.

#include "vfx_viewport_demo.h"
#include "root_slots.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderShadowPass(IRHICommandList* cmd) {
    auto* pipelines = GetPipelines();
    auto* cbRing = GetCBRing();

    // Compute light view-projection (orthographic from sun direction)
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    const float elevRad = m_sunElevation * kDegToRad;
    const Vec3 sunDir = Vec3(
        std::cos(elevRad) * 0.5f,
        std::sin(elevRad),
        std::cos(elevRad) * 0.866f
    ).Normalized();

    const Vec3 lightPos = sunDir * 20.0f;
    const Vec3 lightTarget = Vec3(0.0f, 0.0f, 0.0f);

    // Orthographic shadow projection covering the scene
    const Mat4 lightView = Mat4::LookAt(lightPos, lightTarget, Vec3::Up());
    // Shadow ortho projection: 30x30 world units centered on the scene
    const Mat4 lightProj = Mat4::Orthographic(30.0f, 30.0f, 0.1f, 50.0f);
    const Mat4 shadowViewProj = lightProj * lightView;

    // Transition shadow map to depth-write
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_shadowMap;
        barrier.stateBefore = RHIResourceState::Common;
        barrier.stateAfter = RHIResourceState::DepthStencilWrite;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // Begin shadow render pass
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 0;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_shadowMap;
    rpDesc.depthAttachment.clear = true;
    rpDesc.depthAttachment.clearDepth = 1.0f; // Shadow uses standard Z (not reverse-Z)
    cmd->BeginRenderPass(rpDesc);

    RHIViewport vp{};
    vp.width = static_cast<float>(kShadowMapSize);
    vp.height = static_cast<float>(kShadowMapSize);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    cmd->SetViewport(vp);

    RHIScissorRect sc{};
    sc.right = static_cast<int32_t>(kShadowMapSize);
    sc.bottom = static_cast<int32_t>(kShadowMapSize);
    cmd->SetScissor(sc);

    // Bind shadow depth PSO
    auto shadowPSO = pipelines->GetShadowDepth();
    if (shadowPSO.IsValid()) {
        cmd->SetPipeline(shadowPSO);

        // Upload shadow view-projection as a root CBV at slot 3 (b4)
        struct ShadowCB { Mat4 viewProj; };
        ShadowCB shadowCB{};
        shadowCB.viewProj = shadowViewProj;
        auto shadowAlloc = cbRing->Allocate(sizeof(ShadowCB));
        std::memcpy(shadowAlloc.cpuPtr, &shadowCB, sizeof(ShadowCB));
        static constexpr uint32_t kSlotShadow = 3; // root param 3 = CBV at b4
        cmd->SetRootCBV(kSlotShadow, shadowAlloc.gpuAddr);

        // ── Helper: upload PerObjectCB with a given world matrix for the shadow pass ──
        // The shadow depth VS reads PerObjectCB.world (register b2) to transform
        // vertices into object space before multiplying by shadowViewProj.
        auto uploadShadowPerObject = [&](const Mat4& worldMatrix) {
            PerObjectCB shadowObj{};
            shadowObj.model = worldMatrix;
            shadowObj.normalMatrix = Mat4::Identity(); // Unused in shadow pass
            auto alloc = cbRing->Allocate(sizeof(PerObjectCB));
            std::memcpy(alloc.cpuPtr, &shadowObj, sizeof(PerObjectCB));
            cmd->SetRootCBV(kSlotPerObject, alloc.gpuAddr);
        };

        // Draw current mesh (using per-mesh transform)
        uploadShadowPerObject(BuildMeshWorldMatrix(static_cast<uint32_t>(m_currentMesh)));
        const auto& mesh = m_meshes[m_currentMesh];
        if (mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, mesh.vertexBuffer);
            cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
            cmd->DrawIndexed(mesh.indexCount);
        }

        // ── Draw ground plane into shadow map ──
        const auto& planeMesh = m_meshes[2]; // index 2 = plane
        if (planeMesh.vertexBuffer.IsValid() && planeMesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, planeMesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, planeMesh.vertexBuffer);
            cmd->SetIndexBuffer(planeMesh.indexBuffer, RHIIndexFormat::Uint32);
            uploadShadowPerObject(BuildMeshWorldMatrix(2));
            cmd->DrawIndexed(planeMesh.indexCount);
        }
    }

    cmd->EndRenderPass();

    // Transition shadow map to SRV
    {
        RHIBarrierDesc barrier{};
        barrier.texture = m_shadowMap;
        barrier.stateBefore = RHIResourceState::DepthStencilWrite;
        barrier.stateAfter = RHIResourceState::Common;
        cmd->ResourceBarrier(&barrier, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderSkyPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    // Background preset clear colors (linear RGB)
    static constexpr float kBgColors[][3] = {
        {0.0f,  0.0f,  0.0f },   // 0: Sky (clear black, sky shader overwrites)
        {0.05f, 0.05f, 0.05f},   // 1: Dark Gray
        {0.0f,  0.0f,  0.0f },   // 2: Black
        {1.0f,  1.0f,  1.0f },   // 3: White
        {0.18f, 0.18f, 0.18f},   // 4: Neutral Gray (18% reflectance)
    };

    const int bg = std::clamp(m_bgPreset, 0, kBgPresetCount - 1);

    // The sky writes to HDR target with depth output (reverse-Z far plane = 0.0)
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = m_hdrTarget;
    rpDesc.colorAttachments[0].clear = true;
    rpDesc.colorAttachments[0].clearColor[0] = kBgColors[bg][0];
    rpDesc.colorAttachments[0].clearColor[1] = kBgColors[bg][1];
    rpDesc.colorAttachments[0].clearColor[2] = kBgColors[bg][2];
    rpDesc.colorAttachments[0].clearColor[3] = 1.0f;
    rpDesc.hasDepth = true;
    rpDesc.depthAttachment.texture = m_depthBuffer;
    rpDesc.depthAttachment.clear = true;
    // Reverse-Z: clear to 0.0 (far plane)
    rpDesc.depthAttachment.clearDepth = 0.0f;
    cmd->BeginRenderPass(rpDesc);

    // Skip sky rendering for solid-color backgrounds
    if (bg != 0 || !m_skyPSO.IsValid()) {
        cmd->EndRenderPass();
        return;
    }

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

    cmd->SetPipeline(m_skyPSO);
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    // ── Set SkyIndices root constants (b0) — depth + LUT bindless indices ──
    // The sky shader reads depth from ResourceDescriptorHeap[depthIdx] to skip
    // geometry pixels. Dummy 1x1 LUTs provide neutral values for the ray march
    // fallback (transmittance=1, multi-scatter=0). scatteringLut3DIdx=0xFFFFFFFF
    // triggers the ray march path instead of the precomputed 3D LUT path.
    {
        const uint32_t depthSrv = GetDevice()->GetTextureSRVIndex(m_depthBuffer);
        const uint32_t transmittanceSrv = GetDevice()->GetTextureSRVIndex(m_dummyTransmittanceLut);
        const uint32_t multiScatterSrv  = GetDevice()->GetTextureSRVIndex(m_dummyMultiScatterLut);
        const uint32_t skyIndices[6] = {
            depthSrv,           // depthIdx
            transmittanceSrv,   // transmittanceLutIdx (1x1 white = no extinction)
            multiScatterSrv,    // multiScatteringLutIdx (1x1 black = no multi-scatter)
            UINT32_MAX,         // apVolumeSrvIdx (none)
            UINT32_MAX,         // scatteringLut3DIdx (none → triggers ray march fallback)
            UINT32_MAX          // irradianceLutIdx (none)
        };
        cmd->SetRootConstants(kSlotRootConstants, skyIndices, 6);
    }

    // ── Fill SkyAtmosphereCB with Earth-default atmosphere parameters (b2) ──
    // Values from SkyAtmosphereComponent defaults and atmosphere_renderer.cpp
    // ozone tent calculation.
    {
        SkyAtmosphereCB skyCB{};

        // Rayleigh: xyz = scattering coefficients (km^-1), w = scale height (km)
        skyCB.rayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f, 8.0f};

        // Mie: x = coefficient (km^-1), y = anisotropy g, z = scale height (km)
        skyCB.mieScattering = {3.996e-3f, 0.8f, 1.2f, 0.0f};

        // Ground reflectance
        skyCB.groundAlbedo = {0.4f, 0.4f, 0.4f, 0.0f};

        // Sun: xyz = illuminance color, w = angular radius (rad)
        skyCB.sunIlluminance = {20.0f, 20.0f, 20.0f, 0.00872665f};

        skyCB.bottomRadiusKm       = 6360.0f;
        skyCB.atmosphereHeightKm   = 60.0f;
        skyCB.sunDiskIntensity     = 1.0f;
        skyCB.multiScatteringFactor = 1.0f;
        skyCB.rayleighScatteringScale = 1.0f;
        skyCB.mieScatteringScale   = 1.0f;
        skyCB.mieAbsorptionScale   = 1.0f;
        skyCB.mieAbsorption        = 4.44e-4f;
        skyCB.skyLuminanceFactor   = {1.0f, 1.0f, 1.0f, 1.0f};

        // Ozone tent distribution — piecewise-linear coefficients
        // from atmosphere_renderer.cpp: tipAltitude=25km, tipValue=1.0, width=15km
        constexpr float kOzoneTipAlt = 25.0f;
        constexpr float kOzoneTipVal = 1.0f;
        constexpr float kOzoneWidth  = 15.0f;
        constexpr float kOzoneSlope  = kOzoneTipVal / kOzoneWidth; // 1/15 = 0.06667

        skyCB.ozoneAbsorption = {0.000650f, 0.001881f, 0.000085f, kOzoneTipAlt};
        skyCB.ozoneLinTerm0   =  kOzoneSlope;                              //  1/15
        skyCB.ozoneConstTerm0 = kOzoneTipVal - kOzoneTipAlt * kOzoneSlope; // 1 - 25/15 = -0.6667
        skyCB.ozoneLinTerm1   = -kOzoneSlope;                              // -1/15
        skyCB.ozoneConstTerm1 = kOzoneTipVal - kOzoneTipAlt * (-kOzoneSlope); // 1 + 25/15 = 2.6667

        auto* cbRing = GetCBRing();
        auto skyAlloc = cbRing->Allocate(sizeof(SkyAtmosphereCB));
        std::memcpy(skyAlloc.cpuPtr, &skyCB, sizeof(SkyAtmosphereCB));
        cmd->SetRootCBV(kSlotPerObject, skyAlloc.gpuAddr);
    }

    // Fullscreen triangle (3 vertices, no vertex buffer)
    cmd->Draw(3, 1, 0, 0);

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene Pass (Forward PBR mesh)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderScenePass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    auto forwardPBR = GetPipelines()->GetForwardPBR();
    if (!forwardPBR.IsValid()) return;

    auto* cbRing = GetCBRing();

    // Render into HDR target with depth (don't clear — sky already wrote)
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

    cmd->SetPipeline(forwardPBR);

    // Slot 1 = PerFrameCB (b1) per the engine's standard root signature layout
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    // ── Fill PerObjectCB with material preset and loaded texture SRV indices ──
    const auto& mat = kMaterials[m_currentMaterial];

    PerObjectCB perObj{};
    // Texture SRV indices — 0 maps to the engine's default white/flat-normal/0.5 textures
    perObj.albedoIndex    = (m_albedoSrvIndex    != UINT32_MAX) ? m_albedoSrvIndex    : 0;
    perObj.normalIndex    = (m_normalSrvIndex    != UINT32_MAX) ? m_normalSrvIndex    : 0;
    perObj.roughnessIndex = (m_roughnessSrvIndex != UINT32_MAX) ? m_roughnessSrvIndex : 0;
    perObj.metallicIndex  = 0;
    perObj.aoIndex         = 0;
    perObj.emissiveIndex   = 0;

    // Object transform — from per-mesh transform + optional manual rotation (Alt+LMB)
    constexpr float kDeg2Rad = 3.14159265f / 180.0f;
    const Mat4 manualRotY = Mat4::RotationY(m_objectYaw * kDeg2Rad);
    const Mat4 manualRotX = Mat4::RotationX(m_objectPitch * kDeg2Rad);
    const Mat4 manualRotation = manualRotX * manualRotY;
    const Mat4 meshWorld = BuildMeshWorldMatrix(static_cast<uint32_t>(m_currentMesh));
    perObj.model        = manualRotation * meshWorld;
    const Mat4 meshRot = m_meshTransforms[m_currentMesh].rotation.ToMatrix();
    perObj.normalMatrix = manualRotation * meshRot; // Rotation-only for normals

    // Material color tints from the active preset
    perObj.baseColorTintR      = mat.baseColor[0];
    perObj.baseColorTintG      = mat.baseColor[1];
    perObj.baseColorTintB      = mat.baseColor[2];
    perObj.roughnessMultiplier = mat.roughness;
    perObj.metallicMultiplier  = mat.metallic;
    perObj.emissiveColorR      = mat.emissiveColor[0];
    perObj.emissiveColorG      = mat.emissiveColor[1];
    perObj.emissiveColorB      = mat.emissiveColor[2];
    perObj.emissiveIntensity   = mat.emissiveIntensity;

    auto objAlloc = cbRing->Allocate(sizeof(PerObjectCB));
    std::memcpy(objAlloc.cpuPtr, &perObj, sizeof(PerObjectCB));

    // Slot 2 = PerObjectCB (b2)
    cmd->SetRootCBV(kSlotPerObject, objAlloc.gpuAddr);

    // Draw the current mesh
    const auto& mesh = m_meshes[m_currentMesh];
    if (mesh.vertexBuffer.IsValid() && mesh.indexBuffer.IsValid()) {
        if (IsBindless()) {
            cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
        }
        cmd->SetVertexBuffer(0, mesh.vertexBuffer);
        cmd->SetIndexBuffer(mesh.indexBuffer, RHIIndexFormat::Uint32);
        cmd->DrawIndexed(mesh.indexCount);
    }

    // ── Draw ground plane ──
    {
        const auto& planeMesh = m_meshes[2]; // index 2 = plane
        if (planeMesh.vertexBuffer.IsValid() && planeMesh.indexBuffer.IsValid()) {
            if (IsBindless()) {
                cmd->SetDescriptorIndex(kSlotRootConstants, planeMesh.vbSrvIndex);
            }
            cmd->SetVertexBuffer(0, planeMesh.vertexBuffer);
            cmd->SetIndexBuffer(planeMesh.indexBuffer, RHIIndexFormat::Uint32);

            PerObjectCB groundObj{};
            groundObj.albedoIndex    = 0;
            groundObj.normalIndex    = 0;
            groundObj.roughnessIndex = 0;
            groundObj.metallicIndex  = 0;
            groundObj.aoIndex        = 0;
            groundObj.emissiveIndex  = 0;

            groundObj.model        = BuildMeshWorldMatrix(2);
            groundObj.normalMatrix = m_meshTransforms[2].rotation.ToMatrix(); // Rotation only for normals

            groundObj.baseColorTintR      = 0.15f;
            groundObj.baseColorTintG      = 0.15f;
            groundObj.baseColorTintB      = 0.15f;
            groundObj.roughnessMultiplier = 0.95f;
            groundObj.metallicMultiplier  = 0.0f;
            groundObj.emissiveColorR      = 0.0f;
            groundObj.emissiveColorG      = 0.0f;
            groundObj.emissiveColorB      = 0.0f;
            groundObj.emissiveIntensity   = 0.0f;

            auto groundAlloc = cbRing->Allocate(sizeof(PerObjectCB));
            std::memcpy(groundAlloc.cpuPtr, &groundObj, sizeof(PerObjectCB));
            cmd->SetRootCBV(kSlotPerObject, groundAlloc.gpuAddr);

            cmd->DrawIndexed(planeMesh.indexCount);
        }
    }

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Tonemap Pass (HDR → LDR backbuffer)
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderTonemapPass(IRHICommandList* cmd, uint64_t perFrameGpu) {
    if (!m_tonemapPSO.IsValid()) return;

    auto* swapChain = GetSwapChain();
    auto backBuffer = swapChain->GetCurrentBackBuffer();

    // Transition backbuffer from Present → RenderTarget
    {
        RHIBarrierDesc barrier{};
        barrier.texture = backBuffer;
        barrier.stateBefore = RHIResourceState::Present;
        barrier.stateAfter = RHIResourceState::RenderTarget;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // Begin render pass targeting the backbuffer
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments[0].texture = backBuffer;
    rpDesc.colorAttachments[0].clear = true;
    rpDesc.colorAttachments[0].clearColor[0] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[1] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[2] = 0.0f;
    rpDesc.colorAttachments[0].clearColor[3] = 1.0f;
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

    cmd->SetPipeline(m_tonemapPSO);
    // Slot 1 = PerFrameCB (b1), Slot 0 = TonemapIndices root constants (b0)
    cmd->SetRootCBV(kSlotPerFrame, perFrameGpu);

    if (IsBindless()) {
        auto* device = GetDevice();

        struct TonemapIndices {
            uint32_t hdrColorIdx;          ///< SRV index for HDR scene color.
            uint32_t bloomColorIdx;        ///< SRV index for bloom result (0 = no bloom).
            uint32_t bloomIntensityBits;   ///< Bloom intensity as float bits (0 = disabled).
            uint32_t lensFlareSrvIdx;      ///< SRV index for lens flare (0 = disabled).
        };

        TonemapIndices indices{};
        indices.hdrColorIdx = device->GetTextureSRVIndex(m_hdrTarget);

        if (m_bloomEnabled && m_bloomPass.IsInitialized()) {
            indices.bloomColorIdx = m_bloomPass.GetBloomResultSrvIndex();
            const float bloomIntensity = m_bloomPass.settings.intensity;
            std::memcpy(&indices.bloomIntensityBits, &bloomIntensity, sizeof(float));
        }
        // lensFlareSrvIdx stays 0 (no lens flare in this demo)

        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&indices), 4);
    }

    // Fullscreen triangle
    cmd->Draw(3, 1, 0, 0);

    cmd->EndRenderPass();
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Outline Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderOutlinePass(IRHICommandList* cmd,
                                         const CBAllocation& perFrameAlloc) {
    if (m_selectedMeshIndex == UINT32_MAX || !m_outlineEnabled) return;

    // Build a temporary DrawCommand + GPUMesh vector for the outline pass.
    // The outline pass expects engine DrawCommand/GPUMesh types, not ProceduralMesh.
    const auto& procMesh = m_meshes[m_selectedMeshIndex];
    if (!procMesh.vertexBuffer.IsValid() || !procMesh.indexBuffer.IsValid()) return;

    DrawCommand dc{};
    dc.worldMatrix = BuildMeshWorldMatrix(m_selectedMeshIndex);
    dc.normalMatrix = m_meshTransforms[m_selectedMeshIndex].rotation.ToMatrix();
    dc.entityId = 0; // Use entityId=0 for the selected mesh
    dc.meshId = 0;   // Index into the temporary meshes vector

    GPUMesh gpuMesh{};
    gpuMesh.vertexBuffer = procMesh.vertexBuffer;
    gpuMesh.indexBuffer = procMesh.indexBuffer;
    gpuMesh.vbSrvIndex = procMesh.vbSrvIndex;
    gpuMesh.indexCount = procMesh.indexCount;
    gpuMesh.vertexCount = 0; // Not used by outline pass
    gpuMesh.indexFormat = RHIIndexFormat::Uint32;

    std::vector<DrawCommand> drawList = { dc };
    std::vector<GPUMesh> meshes = { gpuMesh };

    // Transition depth to read for outline mask pass (outline reads depth via DSV).
    // Depth is already in DepthStencilWrite from the scene pass — outline pass
    // needs it in that same state (it reads via depth test, not SRV).
    m_outlinePass.Draw(cmd, *GetCBRing(), GetCamera(), drawList, meshes,
                       0, // selectedEntityId = 0 (matches dc.entityId above)
                       static_cast<float>(GetWidth()),
                       static_cast<float>(GetHeight()),
                       m_depthBuffer, m_hdrTarget, IsBindless(),
                       perFrameAlloc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo Pass
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportDemo::RenderGizmoPass(IRHICommandList* cmd) {
    if (m_selectedMeshIndex == UINT32_MAX) return;
    if (!m_gizmoRenderer.IsInitialized()) return;

    const auto& xform = m_meshTransforms[m_selectedMeshIndex];

    // Begin render pass with HDR target + depth bound.
    // Gizmo renders on top of the scene with depth read/no-write.
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

    // Convert GizmoHitResult to int axis index for the renderer:
    // None=-1, AxisX=0, AxisY=1, AxisZ=2, PlaneXY=3, PlaneXZ=4, PlaneYZ=5, Center=6
    auto hitToAxis = [](GizmoHitResult h) -> int {
        return (h == GizmoHitResult::None) ? -1 : (static_cast<int>(h) - 1);
    };

    m_gizmoRenderer.Draw(cmd, *GetCBRing(), GetCamera(),
                          xform.position, xform.rotation,
                          m_gizmoMode, m_gizmoSpace,
                          hitToAxis(m_gizmoInteraction.GetHoveredAxis()),
                          hitToAxis(m_gizmoInteraction.GetActiveAxis()));

    cmd->EndRenderPass();
}

} // namespace uz

} // namespace uz
