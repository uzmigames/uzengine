/// DX11 Command List Implementation
///
/// Records GPU commands via a deferred device context, then submits
/// via the immediate context. DX11 manages resource state transitions
/// implicitly — ResourceBarrier() is a no-op.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_command_list.h"
#include "dx11_device.h"

#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIIndexFormat to the corresponding DXGI_FORMAT.
static DXGI_FORMAT ToD3D11IndexFormat(RHIIndexFormat format) {
    switch (format) {
        case RHIIndexFormat::Uint16: return DXGI_FORMAT_R16_UINT;
        case RHIIndexFormat::Uint32: return DXGI_FORMAT_R32_UINT;
        default:                     return DXGI_FORMAT_R32_UINT;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX11CommandList::~DX11CommandList() {
    if (m_pendingCmdList) {
        m_pendingCmdList->Release();
        m_pendingCmdList = nullptr;
    }
    if (m_deferredCtx) {
        m_deferredCtx->Release();
        m_deferredCtx = nullptr;
    }
    UZ_LOG(RHI, Info, "DX11CommandList destroyed");
}

bool DX11CommandList::Init(DX11Device* device, RHIQueueType queueType) {
    UZ_PROFILE_FUNCTION();
    if (!device || !device->GetDevice()) {
        UZ_LOG(RHI, Error, "DX11CommandList::Init — null device");
        return false;
    }

    m_device = device;
    m_queueType = queueType;

    // DX11 only has one queue type — graphics handles everything
    if (queueType != RHIQueueType::Graphics) {
        UZ_LOG(RHI, Warning, "DX11 only supports Graphics queue — ignoring queue type {}",
               static_cast<int>(queueType));
    }

    UZ_LOG(RHI, Info, "DX11CommandList created");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::Begin() {
    UZ_PROFILE_FUNCTION();
    // Release previous deferred context and command list
    if (m_pendingCmdList) {
        m_pendingCmdList->Release();
        m_pendingCmdList = nullptr;
    }
    if (m_deferredCtx) {
        m_deferredCtx->Release();
        m_deferredCtx = nullptr;
    }

    // Create a fresh deferred context for recording
    HRESULT hr = m_device->GetDevice()->CreateDeferredContext1(0, reinterpret_cast<ID3D11DeviceContext1**>(&m_deferredCtx));
    if (FAILED(hr)) {
        // Fallback to base interface
        hr = m_device->GetDevice()->CreateDeferredContext(0, &m_deferredCtx);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DX11 CreateDeferredContext failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        }
    }
}

void DX11CommandList::End() {
    UZ_PROFILE_FUNCTION();
    if (!m_deferredCtx) {
        UZ_LOG(RHI, Error, "DX11CommandList::End — no deferred context (Begin() not called?)");
        return;
    }

    HRESULT hr = m_deferredCtx->FinishCommandList(FALSE, &m_pendingCmdList);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11 FinishCommandList failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
    }
}

void DX11CommandList::Execute() {
    UZ_PROFILE_FUNCTION();
    if (!m_pendingCmdList) {
        UZ_LOG(RHI, Warning, "DX11CommandList::Execute — no pending command list (End() not called?)");
        return;
    }

    m_device->GetImmediateContext()->ExecuteCommandList(m_pendingCmdList, FALSE);

    m_pendingCmdList->Release();
    m_pendingCmdList = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline and Resource Binding
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::SetPipeline(RHIPipelineHandle pipeline) {
    UZ_PROFILE_FUNCTION();
    if (!m_deferredCtx || !pipeline.IsValid()) {
        UZ_LOG(RHI, Warning, "SetPipeline — invalid state");
        return;
    }

    auto* dx11Pipeline = m_device->GetPipelinePool().Get(pipeline.index, pipeline.generation);
    if (!dx11Pipeline) {
        UZ_LOG(RHI, Warning, "SetPipeline — pipeline not found in pool");
        return;
    }

    // Bind input layout
    m_deferredCtx->IASetInputLayout(dx11Pipeline->inputLayout);

    // Bind topology
    m_deferredCtx->IASetPrimitiveTopology(dx11Pipeline->topology);

    // Bind vertex shader
    if (dx11Pipeline->vertexShader.IsValid()) {
        auto* vs = m_device->GetShaderPool().Get(
            dx11Pipeline->vertexShader.index, dx11Pipeline->vertexShader.generation);
        if (vs && vs->vs) {
            m_deferredCtx->VSSetShader(vs->vs, nullptr, 0);
        }
    }

    // Bind pixel shader
    if (dx11Pipeline->pixelShader.IsValid()) {
        auto* ps = m_device->GetShaderPool().Get(
            dx11Pipeline->pixelShader.index, dx11Pipeline->pixelShader.generation);
        if (ps && ps->ps) {
            m_deferredCtx->PSSetShader(ps->ps, nullptr, 0);
        }
    } else {
        // Depth-only pass — unbind pixel shader
        m_deferredCtx->PSSetShader(nullptr, nullptr, 0);
    }

    // Bind rasterizer, depth-stencil, blend state
    m_deferredCtx->RSSetState(dx11Pipeline->rasterizer);
    m_deferredCtx->OMSetDepthStencilState(dx11Pipeline->depthStencil, 0);

    float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    m_deferredCtx->OMSetBlendState(dx11Pipeline->blend, blendFactor, 0xFFFFFFFF);
}

void DX11CommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) {
    if (!m_deferredCtx || !buffer.IsValid()) return;

    auto* dx11Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx11Buffer || !dx11Buffer->buffer) {
        UZ_LOG(RHI, Warning, "SetVertexBuffer — buffer not found in pool");
        return;
    }

    UINT stride = dx11Buffer->desc.stride;
    UINT d3dOffset = static_cast<UINT>(offset);
    m_deferredCtx->IASetVertexBuffers(slot, 1, &dx11Buffer->buffer, &stride, &d3dOffset);
}

void DX11CommandList::SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) {
    if (!m_deferredCtx || !buffer.IsValid()) return;

    auto* dx11Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx11Buffer || !dx11Buffer->buffer) {
        UZ_LOG(RHI, Warning, "SetIndexBuffer — buffer not found in pool");
        return;
    }

    m_deferredCtx->IASetIndexBuffer(dx11Buffer->buffer, ToD3D11IndexFormat(format), offset);
}

void DX11CommandList::SetViewport(const RHIViewport& viewport) {
    if (!m_deferredCtx) return;

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = viewport.x;
    vp.TopLeftY = viewport.y;
    vp.Width = viewport.width;
    vp.Height = viewport.height;
    vp.MinDepth = viewport.minDepth;
    vp.MaxDepth = viewport.maxDepth;
    m_deferredCtx->RSSetViewports(1, &vp);
}

void DX11CommandList::SetScissor(const RHIScissorRect& scissor) {
    if (!m_deferredCtx) return;

    D3D11_RECT rect = {};
    rect.left = static_cast<LONG>(scissor.left);
    rect.top = static_cast<LONG>(scissor.top);
    rect.right = static_cast<LONG>(scissor.right);
    rect.bottom = static_cast<LONG>(scissor.bottom);
    m_deferredCtx->RSSetScissorRects(1, &rect);
}

void DX11CommandList::SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    // DX11 has no bindless — this maps to SRV slot binding.
    // The descriptorIndex is treated as an SRV slot to bind.
    // This is a simplified mapping: find the SRV by index and bind it.
    // In practice, the render graph above should use SetDescriptorTable for DX11.
    (void)rootParamIndex;
    (void)descriptorIndex;
    UZ_LOG(RHI, Trace, "SetDescriptorIndex: no-op on DX11 (rootParam={}, index={})",
           rootParamIndex, descriptorIndex);
}

void DX11CommandList::SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                                        uint32_t count, uint32_t offsetIn32BitValues) {
    // DX11 has no root constants — this would need to be mapped to a constant buffer update.
    // For now, log a trace and no-op. Higher-level code should use CBV binding on DX11.
    (void)rootParamIndex;
    (void)data;
    (void)count;
    (void)offsetIn32BitValues;
    UZ_LOG(RHI, Trace, "SetRootConstants: no-op on DX11 (rootParam={}, count={})",
           rootParamIndex, count);
}

void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
    // DX11 has no GPU virtual addresses. This must be mapped to a CB slot binding
    // by higher-level code that knows the actual buffer object.
    (void)rootParamIndex;
    (void)gpuAddress;
    UZ_LOG(RHI, Trace, "SetRootCBV: no-op on DX11 (rootParam={}, addr=0x{:X})",
           rootParamIndex, gpuAddress);
}

void DX11CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    // DX11 has no descriptor tables. SM5 slot-based binding is done directly.
    (void)rootParamIndex;
    (void)descriptorIndex;
    UZ_LOG(RHI, Trace, "SetDescriptorTable: no-op on DX11 (rootParam={}, index={})",
           rootParamIndex, descriptorIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw and Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    if (!m_deferredCtx) return;
    m_deferredCtx->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
                                         vertexOffset, firstInstance);
}

void DX11CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_deferredCtx) return;
    m_deferredCtx->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX11CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                uint32_t groupCountZ) {
    if (!m_deferredCtx) return;
    m_deferredCtx->Dispatch(groupCountX, groupCountY, groupCountZ);
}

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // DX11 manages resource state transitions implicitly — intentional no-op.
    (void)barriers;
    (void)count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::ClearRenderTarget(RHITextureHandle texture, const float color[4]) {
    if (!m_deferredCtx || !texture.IsValid()) return;

    auto* dx11Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx11Tex || !dx11Tex->rtv) {
        UZ_LOG(RHI, Warning, "ClearRenderTarget — texture has no RTV");
        return;
    }

    m_deferredCtx->ClearRenderTargetView(dx11Tex->rtv, color);
}

void DX11CommandList::ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) {
    if (!m_deferredCtx || !texture.IsValid()) return;

    auto* dx11Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx11Tex || !dx11Tex->dsv) {
        UZ_LOG(RHI, Warning, "ClearDepthStencil — texture has no DSV");
        return;
    }

    m_deferredCtx->ClearDepthStencilView(
        dx11Tex->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, stencil);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                                  RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) {
    if (!m_deferredCtx || !src.IsValid() || !dst.IsValid()) return;

    auto* srcBuf = m_device->GetBufferPool().Get(src.index, src.generation);
    auto* dstBuf = m_device->GetBufferPool().Get(dst.index, dst.generation);
    if (!srcBuf || !srcBuf->buffer || !dstBuf || !dstBuf->buffer) {
        UZ_LOG(RHI, Warning, "CopyBuffer — could not resolve buffer handles");
        return;
    }

    D3D11_BOX srcBox = {};
    srcBox.left = static_cast<UINT>(srcOffset);
    srcBox.right = static_cast<UINT>(srcOffset + size);
    srcBox.top = 0;
    srcBox.bottom = 1;
    srcBox.front = 0;
    srcBox.back = 1;

    m_deferredCtx->CopySubresourceRegion(
        dstBuf->buffer, 0, static_cast<UINT>(dstOffset), 0, 0,
        srcBuf->buffer, 0, &srcBox);
}

void DX11CommandList::CopyTexture(RHITextureHandle src, RHITextureHandle dst) {
    if (!m_deferredCtx || !src.IsValid() || !dst.IsValid()) return;

    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !srcTex->texture || !dstTex || !dstTex->texture) {
        UZ_LOG(RHI, Warning, "CopyTexture — could not resolve texture handles");
        return;
    }

    m_deferredCtx->CopyResource(dstTex->texture, srcTex->texture);
}

void DX11CommandList::CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                            uint32_t srcRowPitch,
                                            RHITextureHandle dstTexture,
                                            uint32_t mipLevel, uint32_t arraySlice) {
    if (!m_deferredCtx || !srcBuffer.IsValid() || !dstTexture.IsValid()) return;

    auto* buf = m_device->GetBufferPool().Get(srcBuffer.index, srcBuffer.generation);
    auto* tex = m_device->GetTexturePool().Get(dstTexture.index, dstTexture.generation);
    if (!buf || !buf->buffer || !tex || !tex->texture) {
        UZ_LOG(RHI, Warning, "CopyBufferToTexture — could not resolve handles");
        return;
    }

    // DX11 UpdateSubresource requires CPU-accessible data. For GPU-only staging,
    // we use CopySubresourceRegion from the buffer to the texture.
    const uint32_t subresource = D3D11CalcSubresource(mipLevel, arraySlice, tex->desc.mipLevels);

    // Calculate mip dimensions
    uint32_t mipWidth = (tex->desc.width >> mipLevel);
    uint32_t mipHeight = (tex->desc.height >> mipLevel);
    if (mipWidth == 0) mipWidth = 1;
    if (mipHeight == 0) mipHeight = 1;

    D3D11_BOX srcBox = {};
    srcBox.left = static_cast<UINT>(srcOffset);
    srcBox.right = static_cast<UINT>(srcOffset + static_cast<uint64_t>(srcRowPitch) * mipHeight);
    srcBox.top = 0;
    srcBox.bottom = 1;
    srcBox.front = 0;
    srcBox.back = 1;

    m_deferredCtx->CopySubresourceRegion(
        tex->texture, subresource, 0, 0, 0,
        buf->buffer, 0, &srcBox);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!m_deferredCtx) return;

    ID3D11RenderTargetView* rtvs[8] = {};
    uint32_t rtvCount = 0;

    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < 8; ++i) {
        const auto& att = desc.colorAttachments[i];
        if (!att.texture.IsValid()) continue;

        auto* tex = m_device->GetTexturePool().Get(att.texture.index, att.texture.generation);
        if (!tex || !tex->rtv) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has no RTV", i);
            continue;
        }

        if (att.clear) {
            m_deferredCtx->ClearRenderTargetView(tex->rtv, att.clearColor);
        }

        rtvs[rtvCount++] = tex->rtv;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (desc.hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* depthTex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index, desc.depthAttachment.texture.generation);
        if (depthTex && depthTex->dsv) {
            dsv = depthTex->dsv;
            if (desc.depthAttachment.clear) {
                m_deferredCtx->ClearDepthStencilView(
                    dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                    desc.depthAttachment.clearDepth, desc.depthAttachment.clearStencil);
            }
        }
    }

    m_deferredCtx->OMSetRenderTargets(rtvCount, rtvCount > 0 ? rtvs : nullptr, dsv);
}

void DX11CommandList::EndRenderPass() {
    // DX11 has no explicit render pass end — intentional no-op
}

} // namespace uz
