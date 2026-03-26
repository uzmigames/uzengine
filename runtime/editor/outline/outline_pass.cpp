/// Editor Outline Pass Implementation
///
/// Two-pass stencil-free selection outline:
/// 1. Mask pass: re-renders selected entity mesh to R8Unorm mask RT.
/// 2. Detect pass: fullscreen edge detection kernel on the mask, composites
///    colored outline onto the backbuffer with alpha blending.

#include "outline_pass.h"

#include "camera.h"
#include "constant_buffers.h"
#include "draw_list.h"
#include "engine_shaders.h"
#include "gpu_mesh.h"
#include "log.h"
#include "rhi.h"
#include "root_slots.h"
#include "shader_pipeline.h"

#include <cstring>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(OutlinePass, Info);

// ─────────────────────────────────────────────────────────────────────────────
// OutlineCB — matches the HLSL cbuffer at b1 for the detect pass
// ─────────────────────────────────────────────────────────────────────────────

struct OutlineCB {
    float colorR;               ///< Outline color R (linear).
    float colorG;               ///< Outline color G.
    float colorB;               ///< Outline color B.
    float thickness;            ///< Outline width in pixels.
    float texelSizeX;           ///< 1.0 / viewport width.
    float texelSizeY;           ///< 1.0 / viewport height.
    float _pad0;
    float _pad1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown / Resize
// ─────────────────────────────────────────────────────────────────────────────

bool OutlinePass::Init(IRHIDevice* device, ShaderPipeline& pipeline,
                       RHIFormat rtvFormat, RHIFormat dsvFormat,
                       uint32_t width, uint32_t height) {
    m_device = device;
    m_width  = width;
    m_height = height;

    // ── Compile shaders ─────────────────────────────────────────────────

    // Mask pass: reuses mesh_vs + trivial outline_mask_ps
    auto maskVsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));
    if (maskVsBytecode.empty()) {
        UZ_LOG(OutlinePass, Error, "Failed to compile mask vertex shader (mesh_vs)");
        return false;
    }

    auto maskPsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OutlineMaskPS),
        EngineShaders::GetSource(EngineShader::OutlineMaskPS));
    if (maskPsBytecode.empty()) {
        UZ_LOG(OutlinePass, Error, "Failed to compile outline_mask_ps shader");
        return false;
    }

    // Detect pass: fullscreen_vs + outline_detect_ps
    auto detectVsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::FullscreenVS),
        EngineShaders::GetSource(EngineShader::FullscreenVS));
    if (detectVsBytecode.empty()) {
        UZ_LOG(OutlinePass, Error, "Failed to compile fullscreen_vs shader");
        return false;
    }

    auto detectPsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OutlineDetectPS),
        EngineShaders::GetSource(EngineShader::OutlineDetectPS));
    if (detectPsBytecode.empty()) {
        UZ_LOG(OutlinePass, Error, "Failed to compile outline_detect_ps shader");
        return false;
    }

    // ── Create shader handles ───────────────────────────────────────────

    {
        RHIShaderDesc desc{};
        desc.bytecode     = maskVsBytecode.data();
        desc.bytecodeSize = maskVsBytecode.size();
        desc.stage        = RHIShaderStage::Vertex;
        desc.debugName    = "outline_mask_vs";
        m_maskVs = device->CreateShader(desc);
    }
    {
        RHIShaderDesc desc{};
        desc.bytecode     = maskPsBytecode.data();
        desc.bytecodeSize = maskPsBytecode.size();
        desc.stage        = RHIShaderStage::Pixel;
        desc.debugName    = "outline_mask_ps";
        m_maskPs = device->CreateShader(desc);
    }
    {
        RHIShaderDesc desc{};
        desc.bytecode     = detectVsBytecode.data();
        desc.bytecodeSize = detectVsBytecode.size();
        desc.stage        = RHIShaderStage::Vertex;
        desc.debugName    = "outline_detect_vs";
        m_detectVs = device->CreateShader(desc);
    }
    {
        RHIShaderDesc desc{};
        desc.bytecode     = detectPsBytecode.data();
        desc.bytecodeSize = detectPsBytecode.size();
        desc.stage        = RHIShaderStage::Pixel;
        desc.debugName    = "outline_detect_ps";
        m_detectPs = device->CreateShader(desc);
    }

    // ── Create mask pipeline ────────────────────────────────────────────
    // Renders mesh geometry to R8Unorm mask RT with depth test (read-only).
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader  = m_maskVs;
        desc.pixelShader   = m_maskPs;
        desc.renderTargetFormats[0] = RHIFormat::R8Unorm;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = dsvFormat;

        // Depth disabled: outline must render on top of ALL geometry (UE5 behavior).
        // The outline should be visible even when the selected object is occluded.
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite  = false;

        // No culling — render both sides for correct outline on all geometry.
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.debugName = "OutlineMaskPipeline";

        m_maskPipeline = device->CreateGraphicsPipeline(desc);
        if (!m_maskPipeline.IsValid()) {
            UZ_LOG(OutlinePass, Error, "Failed to create outline mask pipeline");
            return false;
        }
    }

    // ── Create detect pipeline ──────────────────────────────────────────
    // Fullscreen pass: reads mask via bindless SRV, outputs outline with alpha blend.
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader  = m_detectVs;
        desc.pixelShader   = m_detectPs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown; // No depth test

        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite  = false;

        // No culling for fullscreen triangle
        desc.rasterizer.cullMode = RHICullMode::None;

        // Alpha blend: outline composites over the backbuffer
        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp  = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp  = RHIBlendOp::Add;

        desc.debugName = "OutlineDetectPipeline";

        m_detectPipeline = device->CreateGraphicsPipeline(desc);
        if (!m_detectPipeline.IsValid()) {
            UZ_LOG(OutlinePass, Error, "Failed to create outline detect pipeline");
            return false;
        }
    }

    // ── Create mask texture ─────────────────────────────────────────────
    CreateMaskTexture();
    if (!m_maskTexture.IsValid()) {
        UZ_LOG(OutlinePass, Error, "Failed to create outline mask texture");
        return false;
    }

    UZ_LOG(OutlinePass, Info, "Editor outline pass initialized ({}x{}, color=({:.2f},{:.2f},{:.2f}), thickness={})",
           m_width, m_height, m_colorR, m_colorG, m_colorB, m_thickness);
    return true;
}

void OutlinePass::Shutdown() {
    if (m_device && m_maskTexture.IsValid()) {
        m_device->DestroyTexture(m_maskTexture);
        m_maskTexture = RHITextureHandle{};
        m_maskSrvIndex = UINT32_MAX;
    }
    m_device = nullptr;
    UZ_LOG(OutlinePass, Info, "Editor outline pass shut down");
}

void OutlinePass::Resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;

    // Destroy old mask texture and recreate at new size.
    if (m_device && m_maskTexture.IsValid()) {
        m_device->DestroyTexture(m_maskTexture);
        m_maskTexture = RHITextureHandle{};
        m_maskSrvIndex = UINT32_MAX;
    }
    CreateMaskTexture();
}

void OutlinePass::CreateMaskTexture() {
    if (!m_device || m_width == 0 || m_height == 0) return;

    RHITextureDesc desc{};
    desc.width           = static_cast<uint16_t>(m_width);
    desc.height          = static_cast<uint16_t>(m_height);
    desc.format          = RHIFormat::R8Unorm;
    desc.usage           = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource;
    desc.debugName       = "OutlineMask";

    m_maskTexture = m_device->CreateTexture(desc);
    if (m_maskTexture.IsValid()) {
        m_maskSrvIndex = m_device->GetTextureSRVIndex(m_maskTexture);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

void OutlinePass::Draw(IRHICommandList* cmdList,
                       CBRingAllocator& cbRing,
                       const Camera& camera,
                       const std::vector<DrawCommand>& drawList,
                       const std::vector<GPUMesh>& meshes,
                       uint32_t selectedEntityId,
                       float viewportWidth, float viewportHeight,
                       RHITextureHandle depthTexture,
                       RHITextureHandle backbuffer,
                       bool bindless,
                       const CBAllocation& perFrameAlloc) {
    if (selectedEntityId == UINT32_MAX) return;
    if (!m_maskTexture.IsValid() || !m_maskPipeline.IsValid() || !m_detectPipeline.IsValid()) return;

    // ── Find draw commands for the selected entity ──────────────────────
    // Collect indices of matching commands. Typically just one, but an entity
    // could have multiple draw commands (e.g., multiple LODs or sub-meshes).
    bool foundAny = false;
    for (const auto& dc : drawList) {
        if (dc.entityId == selectedEntityId) {
            foundAny = true;
            break;
        }
    }
    if (!foundAny) return;

    // ── Pass 1: Mask — render selected mesh to R8Unorm ──────────────────

    // Transition mask to render target
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_maskTexture;
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::RenderTarget;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    {
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount        = 1;
        passDesc.colorAttachments[0].texture = m_maskTexture;
        passDesc.colorAttachments[0].clear   = true;
        passDesc.colorAttachments[0].clearColor[0] = 0.0f;
        passDesc.colorAttachments[0].clearColor[1] = 0.0f;
        passDesc.colorAttachments[0].clearColor[2] = 0.0f;
        passDesc.colorAttachments[0].clearColor[3] = 0.0f;

        passDesc.hasDepth                    = true;
        passDesc.depthAttachment.texture     = depthTexture;
        passDesc.depthAttachment.clear       = false;

        cmdList->BeginRenderPass(passDesc);

        RHIViewport vp{};
        vp.width    = viewportWidth;
        vp.height   = viewportHeight;
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right  = static_cast<int32_t>(viewportWidth);
        sc.bottom = static_cast<int32_t>(viewportHeight);
        cmdList->SetScissor(sc);

        cmdList->SetPipeline(m_maskPipeline);

        // Bind PerFrameCB at b1 — mesh_vs.hlsl reads viewProj from here.
        cmdList->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                            perFrameAlloc.offsetBytes, perFrameAlloc.cpuPtr,
                            perFrameAlloc.sizeBytes);

        // Draw each command matching the selected entity.
        for (const auto& dc : drawList) {
            if (dc.entityId != selectedEntityId) continue;
            if (dc.meshId >= static_cast<uint32_t>(meshes.size())) continue;

            const GPUMesh& mesh = meshes[dc.meshId];
            if (!mesh.vertexBuffer.IsValid()) continue;

            // Allocate PerObjectCB with the world matrix for this draw command.
            PerObjectCB objCb{};
            objCb.model        = dc.worldMatrix;
            objCb.normalMatrix = dc.normalMatrix;

            CBAllocation objAlloc = cbRing.Allocate(sizeof(PerObjectCB));
            std::memcpy(objAlloc.cpuPtr, &objCb, sizeof(PerObjectCB));
            cmdList->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle,
                                objAlloc.offsetBytes, objAlloc.cpuPtr, sizeof(PerObjectCB));

            if (bindless) {
                cmdList->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
            } else {
                cmdList->SetVertexBuffer(0, mesh.vertexBuffer, 0);
            }
            cmdList->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
            cmdList->DrawIndexed(mesh.indexCount);
        }

        cmdList->EndRenderPass();
    }

    // ── Transition mask to SRV for the detect pass ──────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_maskTexture;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Pass 2: Detect — fullscreen edge detection onto backbuffer ───────
    {
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount        = 1;
        passDesc.colorAttachments[0].texture = backbuffer;
        passDesc.colorAttachments[0].clear   = false;

        cmdList->BeginRenderPass(passDesc);

        RHIViewport vp{};
        vp.width    = viewportWidth;
        vp.height   = viewportHeight;
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right  = static_cast<int32_t>(viewportWidth);
        sc.bottom = static_cast<int32_t>(viewportHeight);
        cmdList->SetScissor(sc);

        cmdList->SetPipeline(m_detectPipeline);

        // Upload OutlineCB to b1 (the detect shader reads it at register(b1)).
        OutlineCB outCb{};
        outCb.colorR     = m_colorR;
        outCb.colorG     = m_colorG;
        outCb.colorB     = m_colorB;
        outCb.thickness  = m_thickness;
        outCb.texelSizeX = (viewportWidth > 0.f)  ? (1.0f / viewportWidth)  : 0.f;
        outCb.texelSizeY = (viewportHeight > 0.f) ? (1.0f / viewportHeight) : 0.f;
        outCb._pad0      = 0.f;
        outCb._pad1      = 0.f;

        CBAllocation outlineAlloc = cbRing.Allocate(sizeof(OutlineCB));
        std::memcpy(outlineAlloc.cpuPtr, &outCb, sizeof(OutlineCB));
        cmdList->SetRootCBV(kSlotPerFrame, outlineAlloc.bufferHandle,
                            outlineAlloc.offsetBytes, outlineAlloc.cpuPtr, sizeof(OutlineCB));

        // Pass the mask SRV index via root constants so the detect shader can
        // sample it via bindless. For SM5, bind as fragment texture at slot 0.
        if (bindless) {
            uint32_t rootConstants[8] = { m_maskSrvIndex, 0, 0, 0, 0, 0, 0, 0 };
            cmdList->SetRootConstants(kSlotRootConstants, rootConstants, 8);
        } else {
            cmdList->SetFragmentTexture(0, m_maskTexture);
        }

        cmdList->Draw(3); // Fullscreen triangle

        cmdList->EndRenderPass();
    }
}

} // namespace uz
