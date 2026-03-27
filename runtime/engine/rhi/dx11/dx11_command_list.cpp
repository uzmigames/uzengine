/// DX11 Command List Implementation
///
/// Records GPU commands directly on the immediate device context.
/// DX11 manages resource state transitions implicitly — ResourceBarrier()
/// is a no-op. Begin/End/Execute exist only to satisfy the RHI interface.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_command_list.h"
#include "dx11_device.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>

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
    for (auto& cb : m_scratchCBs) {
        if (cb) { cb->Release(); cb = nullptr; }
    }
    // m_ctx is not owned — it's the device's immediate context
    m_ctx = nullptr;
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

    // DX11 only has one queue type — reject Copy/Compute so the renderer
    // falls back to the graphics queue upload path (WaitIdle flush).
    if (queueType != RHIQueueType::Graphics) {
        UZ_LOG(RHI, Info, "DX11 does not support dedicated {} queue — returning null",
               queueType == RHIQueueType::Copy ? "Copy" : "Compute");
        return false;
    }

    UZ_LOG(RHI, Info, "DX11CommandList created (immediate context mode)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::Begin() {
    UZ_PROFILE_FUNCTION();
    // Store pointer to the device's immediate context — commands execute as recorded
    m_ctx = m_device->GetImmediateContext();
    m_computePipelineBound = false;

    // Clear all stale SRV/UAV/shader bindings from previous command list usage.
    // DX11's immediate context is shared across all command lists — state persists
    // between Begin/End/Execute cycles. Without this, a previous compute dispatch's
    // SRV (e.g., HDR scene color 2D) can leak into a subsequent IBL bake's CS slot 0
    // where a TextureCube is expected, causing DX11 debug layer dimension mismatches
    // and producing black output.
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_ctx->PSSetShaderResources(0, 16, nullSRVs);
    m_ctx->VSSetShaderResources(0, 16, nullSRVs);
    m_ctx->CSSetShaderResources(0, 16, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[8] = {};
    m_ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);

    // Bind all 4 default samplers matching DX12 static samplers s0-s3:
    //   s0 — anisotropic wrap (material textures)
    //   s1 — shadow PCF comparison (shadow atlas)
    //   s2 — bilinear clamp (post-process, LUT, IBL BRDF)
    //   s3 — bilinear wrap (3D noise, weather maps)
    ID3D11SamplerState* defaultSamplers[4] = {
        m_device->GetDefaultSampler(),
        m_device->GetShadowComparisonSampler(),
        m_device->GetBilinearClampSampler(),
        m_device->GetBilinearWrapSampler(),
    };
    m_ctx->PSSetSamplers(0, 4, defaultSamplers);
    m_ctx->VSSetSamplers(0, 4, defaultSamplers);
    m_ctx->CSSetSamplers(0, 4, defaultSamplers);
}

void DX11CommandList::End() {
    UZ_PROFILE_FUNCTION();
    // No-op on immediate context — commands are already submitted
}

void DX11CommandList::Execute() {
    UZ_PROFILE_FUNCTION();
    // No-op on immediate context — commands were already executed during recording
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline and Resource Binding
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::SetPipeline(RHIPipelineHandle pipeline) {
    UZ_PROFILE_FUNCTION();
    if (!m_ctx || !pipeline.IsValid()) {
        UZ_LOG(RHI, Warning, "SetPipeline — invalid state");
        return;
    }

    auto* dx11Pipeline = m_device->GetPipelinePool().Get(pipeline.index, pipeline.generation);
    if (!dx11Pipeline) {
        UZ_LOG(RHI, Warning, "SetPipeline — pipeline not found in pool");
        return;
    }

    if (dx11Pipeline->isCompute) {
        // Compute pipeline — bind only the compute shader.
        // Clear stale compute SRV/UAV bindings from previous dispatch to prevent
        // DX11 debug layer warnings about dimension mismatches (e.g., 2D SRV still
        // bound when the new CS expects a TextureCube).
        m_computePipelineBound = true;

        ID3D11ShaderResourceView* nullSRVs[8] = {};
        m_ctx->CSSetShaderResources(0, 8, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[4] = {};
        m_ctx->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

        if (dx11Pipeline->computeShader.IsValid()) {
            auto* cs = m_device->GetShaderPool().Get(
                dx11Pipeline->computeShader.index, dx11Pipeline->computeShader.generation);
            if (cs && cs->cs) {
                m_ctx->CSSetShader(cs->cs, nullptr, 0);
            } else {
                UZ_LOG(RHI, Error, "SetPipeline — compute shader object not found in pool");
            }
        }
        return;
    }

    // Graphics pipeline — bind all graphics state.
    m_computePipelineBound = false;

    // Bind input layout
    m_ctx->IASetInputLayout(dx11Pipeline->inputLayout);

    // Bind topology
    m_ctx->IASetPrimitiveTopology(dx11Pipeline->topology);

    // Bind vertex shader
    if (dx11Pipeline->vertexShader.IsValid()) {
        auto* vs = m_device->GetShaderPool().Get(
            dx11Pipeline->vertexShader.index, dx11Pipeline->vertexShader.generation);
        if (vs && vs->vs) {
            m_ctx->VSSetShader(vs->vs, nullptr, 0);
        }
    }

    // Bind pixel shader
    if (dx11Pipeline->pixelShader.IsValid()) {
        auto* ps = m_device->GetShaderPool().Get(
            dx11Pipeline->pixelShader.index, dx11Pipeline->pixelShader.generation);
        if (ps && ps->ps) {
            m_ctx->PSSetShader(ps->ps, nullptr, 0);
        }
    } else {
        // Depth-only pass — unbind pixel shader
        m_ctx->PSSetShader(nullptr, nullptr, 0);
    }

    // Bind rasterizer, depth-stencil, blend state
    m_ctx->RSSetState(dx11Pipeline->rasterizer);
    m_ctx->OMSetDepthStencilState(dx11Pipeline->depthStencil, 0);

    float blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    m_ctx->OMSetBlendState(dx11Pipeline->blend, blendFactor, 0xFFFFFFFF);
}

void DX11CommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) {
    if (!m_ctx || !buffer.IsValid()) return;

    auto* dx11Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx11Buffer || !dx11Buffer->buffer) {
        UZ_LOG(RHI, Warning, "SetVertexBuffer — buffer not found in pool");
        return;
    }

    UINT stride = dx11Buffer->desc.stride;
    UINT d3dOffset = static_cast<UINT>(offset);
    m_ctx->IASetVertexBuffers(slot, 1, &dx11Buffer->buffer, &stride, &d3dOffset);
}

void DX11CommandList::SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) {
    if (!m_ctx || !buffer.IsValid()) return;

    auto* dx11Buffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!dx11Buffer || !dx11Buffer->buffer) {
        UZ_LOG(RHI, Warning, "SetIndexBuffer — buffer not found in pool");
        return;
    }

    m_ctx->IASetIndexBuffer(dx11Buffer->buffer, ToD3D11IndexFormat(format), offset);
}

void DX11CommandList::SetViewport(const RHIViewport& viewport) {
    if (!m_ctx) return;

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = viewport.x;
    vp.TopLeftY = viewport.y;
    vp.Width = viewport.width;
    vp.Height = viewport.height;
    vp.MinDepth = viewport.minDepth;
    vp.MaxDepth = viewport.maxDepth;
    m_ctx->RSSetViewports(1, &vp);
}

void DX11CommandList::SetScissor(const RHIScissorRect& scissor) {
    if (!m_ctx) return;

    D3D11_RECT rect = {};
    rect.left = static_cast<LONG>(scissor.left);
    rect.top = static_cast<LONG>(scissor.top);
    rect.right = static_cast<LONG>(scissor.right);
    rect.bottom = static_cast<LONG>(scissor.bottom);
    m_ctx->RSSetScissorRects(1, &rect);
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
    // DX11 has no native root constants — emulate via a scratch constant buffer,
    // just like SetRootCBV. Upload the data and bind to the correct HLSL register.
    if (!m_ctx || !data || count == 0) return;

    const uint32_t totalValues = offsetIn32BitValues + count;
    const uint32_t alignedSize = (totalValues * 4 + 15u) & ~15u;
    ID3D11Buffer* scratch = GetOrCreateScratchCB(rootParamIndex, alignedSize);
    if (!scratch) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(scratch, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "SetRootConstants: Map failed for slot {}, hr=0x{:08X}",
               rootParamIndex, static_cast<uint32_t>(hr));
        return;
    }
    auto* dst = static_cast<uint32_t*>(mapped.pData);
    std::memcpy(dst + offsetIn32BitValues, data, count * sizeof(uint32_t));
    m_ctx->Unmap(scratch, 0);

    const uint32_t hlslRegister = m_device->GetRootLayout().params[rootParamIndex].shaderRegister;
    if (m_computePipelineBound) {
        m_ctx->CSSetConstantBuffers(hlslRegister, 1, &scratch);
    } else {
        m_ctx->VSSetConstantBuffers(hlslRegister, 1, &scratch);
        m_ctx->PSSetConstantBuffers(hlslRegister, 1, &scratch);
    }
}

void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
    // DX11 has no GPU virtual addresses. This must be mapped to a CB slot binding
    // by higher-level code that knows the actual buffer object.
    (void)rootParamIndex;
    (void)gpuAddress;
    UZ_LOG(RHI, Trace, "SetRootCBV: no-op on DX11 (rootParam={}, addr=0x{:X})",
           rootParamIndex, gpuAddress);
}

void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle /*buffer*/,
                                  uint64_t /*offsetBytes*/, const void* cpuData, uint32_t dataSize) {
    // Map/WRITE_DISCARD into a DYNAMIC scratch buffer, then bind to VS+PS stages.
    if (!m_ctx || !cpuData || dataSize == 0) return;

    // Align to 16 bytes (D3D11 CB requirement)
    const uint32_t alignedSize = (dataSize + 15u) & ~15u;
    ID3D11Buffer* scratch = GetOrCreateScratchCB(rootParamIndex, alignedSize);
    if (!scratch) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(scratch, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "SetRootCBV: Map failed for slot {}, hr=0x{:08X}",
               rootParamIndex, static_cast<uint32_t>(hr));
        return;
    }
    std::memcpy(mapped.pData, cpuData, dataSize);
    m_ctx->Unmap(scratch, 0);

    // Translate root param index to HLSL register via the root layout.
    // Root param 3 maps to b4 (shadow), not b3 — using the index directly was a bug.
    const uint32_t hlslRegister = m_device->GetRootLayout().params[rootParamIndex].shaderRegister;
    if (m_computePipelineBound) {
        m_ctx->CSSetConstantBuffers(hlslRegister, 1, &scratch);
    } else {
        m_ctx->VSSetConstantBuffers(hlslRegister, 1, &scratch);
        m_ctx->PSSetConstantBuffers(hlslRegister, 1, &scratch);
    }
}

ID3D11Buffer* DX11CommandList::GetOrCreateScratchCB(uint32_t slot, uint32_t requiredSize) {
    if (slot >= kMaxCBSlots) {
        UZ_LOG(RHI, Error, "SetRootCBV: slot {} exceeds max {}", slot, kMaxCBSlots);
        return nullptr;
    }

    // Reuse existing buffer if large enough
    if (m_scratchCBs[slot] && m_scratchCBSizes[slot] >= requiredSize) {
        return m_scratchCBs[slot];
    }

    // Release old buffer if it exists but is too small
    if (m_scratchCBs[slot]) {
        m_scratchCBs[slot]->Release();
        m_scratchCBs[slot] = nullptr;
        m_scratchCBSizes[slot] = 0;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = requiredSize;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = m_device->GetDevice()->CreateBuffer(&bd, nullptr, &m_scratchCBs[slot]);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "GetOrCreateScratchCB: CreateBuffer failed, slot={}, size={}, hr=0x{:08X}",
               slot, requiredSize, static_cast<uint32_t>(hr));
        return nullptr;
    }
    m_scratchCBSizes[slot] = requiredSize;
    return m_scratchCBs[slot];
}

void DX11CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    // DX11 SM5: translate descriptor index into slot-based SRV/UAV/sampler binding.
    // Check root layout to determine if this is an SRV_Table or UAV_Table.
    const auto& rootLayout = m_device->GetRootLayout();
    if (rootParamIndex < rootLayout.paramCount &&
        rootLayout.params[rootParamIndex].type == RHIRootParamDesc::Type::UAV_Table) {
        // UAV binding — descriptorIndex is a pool index from GetTextureUAVIndex/GetBufferUAVIndex.
        // Bind the UAV at register u0 for compute dispatch.
        ID3D11UnorderedAccessView* uav = m_device->FindTextureUAVByPoolIndex(descriptorIndex);
        if (!uav) {
            uav = m_device->FindBufferUAVByPoolIndex(descriptorIndex);
        }
        if (uav) {
            const uint32_t hlslRegister = rootLayout.params[rootParamIndex].shaderRegister;
            m_ctx->CSSetUnorderedAccessViews(hlslRegister, 1, &uav, nullptr);
        }
        // No warning — caller may pass non-UAV texture indices when sky cubemap
        // capture uses the compute path on backends that don't need UAV for this.
        return;
    }

    // SRV binding — descriptorIndex is a logical SRV index from GetTextureSRVIndex.
    // Use shaderRegister from the root layout as the HLSL texture slot (t<N>), so that
    // t0-t13 in the deferred lighting pass all bind to their correct registers.
    ID3D11ShaderResourceView* srv = m_device->FindTextureSRVByIndex(descriptorIndex);
    if (srv) {
        const uint32_t hlslSlot = (rootParamIndex < rootLayout.paramCount)
            ? rootLayout.params[rootParamIndex].shaderRegister
            : 0u;
        if (m_computePipelineBound) {
            m_ctx->CSSetShaderResources(hlslSlot, 1, &srv);
        } else {
            m_ctx->PSSetShaderResources(hlslSlot, 1, &srv);
        }
    } else {
        UZ_LOG(RHI, Warning, "SetDescriptorTable: no SRV found for index {} (rootParam={})",
               descriptorIndex, rootParamIndex);
    }
}

void DX11CommandList::SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    if (!m_ctx) return;

    if (!texture.IsValid()) {
        ID3D11ShaderResourceView* nullSrv = nullptr;
        if (m_computePipelineBound)
            m_ctx->CSSetShaderResources(slot, 1, &nullSrv);
        else
            m_ctx->PSSetShaderResources(slot, 1, &nullSrv);
        return;
    }

    auto* dx11Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx11Tex || !dx11Tex->srv) {
        UZ_LOG(RHI, Warning, "SetFragmentTexture: texture has no SRV at slot {}", slot);
        return;
    }

    if (m_computePipelineBound)
        m_ctx->CSSetShaderResources(slot, 1, &dx11Tex->srv);
    else
        m_ctx->PSSetShaderResources(slot, 1, &dx11Tex->srv);
}

void DX11CommandList::SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) {
    if (!m_ctx) return;

    if (!sampler.IsValid()) {
        ID3D11SamplerState* nullSampler = nullptr;
        if (m_computePipelineBound)
            m_ctx->CSSetSamplers(slot, 1, &nullSampler);
        else
            m_ctx->PSSetSamplers(slot, 1, &nullSampler);
        return;
    }

    auto* dx11Sampler = m_device->GetSamplerPool().Get(sampler.index, sampler.generation);
    if (!dx11Sampler || !dx11Sampler->sampler) {
        UZ_LOG(RHI, Warning, "SetFragmentSampler: sampler not found at slot {}", slot);
        return;
    }

    if (m_computePipelineBound)
        m_ctx->CSSetSamplers(slot, 1, &dx11Sampler->sampler);
    else
        m_ctx->PSSetSamplers(slot, 1, &dx11Sampler->sampler);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw and Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t vertexOffset,
                                   uint32_t firstInstance) {
    if (!m_ctx) return;
    m_ctx->DrawIndexedInstanced(indexCount, instanceCount, firstIndex,
                                         vertexOffset, firstInstance);
}

void DX11CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
    if (!m_ctx) return;
    m_ctx->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX11CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                uint32_t groupCountZ) {
    if (!m_ctx) return;
    m_ctx->Dispatch(groupCountX, groupCountY, groupCountZ);

    // Unbind all UAVs after dispatch to prevent SRV-UAV conflicts.
    // DX11 forces SRV to NULL if the same resource is still bound as UAV.
    // Without this, the next pass reading the compute output as SRV gets zeros.
    ID3D11UnorderedAccessView* nullUAVs[8] = {};
    m_ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);
}

void DX11CommandList::DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                                           uint32_t drawCount, uint32_t argsStride) {
    if (!m_ctx) return;

    if (!argsBuffer.IsValid()) {
        UZ_LOG(RHI, Warning, "DrawIndexedIndirect — invalid args buffer handle");
        return;
    }

    DX11Buffer* buf = m_device->GetBufferPool().Get(argsBuffer.index, argsBuffer.generation);
    if (!buf || !buf->buffer) {
        UZ_LOG(RHI, Warning, "DrawIndexedIndirect — could not resolve args buffer");
        return;
    }

    if (drawCount == 0) return;

    // DX11 DrawIndexedInstancedIndirect processes exactly one draw command per call.
    // The args buffer must contain D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS structs
    // (IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
    //  StartInstanceLocation — 5 uint32_t = 20 bytes each).
    // For multi-draw, we loop and advance by argsStride per iteration.
    // If argsStride is 0, default to the standard argument struct size (20 bytes).
    const uint32_t stride = (argsStride != 0) ? argsStride : 20u;
    for (uint32_t i = 0; i < drawCount; ++i) {
        const uint32_t offset = static_cast<uint32_t>(argsOffset) + i * stride;
        m_ctx->DrawIndexedInstancedIndirect(buf->buffer, offset);
    }
}

void DX11CommandList::DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) {
    if (!m_ctx) return;

    if (!argsBuffer.IsValid()) {
        UZ_LOG(RHI, Warning, "DispatchIndirect — invalid args buffer handle");
        return;
    }

    DX11Buffer* buf = m_device->GetBufferPool().Get(argsBuffer.index, argsBuffer.generation);
    if (!buf || !buf->buffer) {
        UZ_LOG(RHI, Warning, "DispatchIndirect — could not resolve args buffer");
        return;
    }

    // The args buffer must contain a D3D11_DISPATCH_INDIRECT_ARGS struct at argsOffset
    // (ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ — 3 uint32_t = 12 bytes).
    m_ctx->DispatchIndirect(buf->buffer, static_cast<uint32_t>(argsOffset));
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
    if (!m_ctx || !texture.IsValid()) return;

    auto* dx11Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx11Tex || !dx11Tex->rtv) {
        UZ_LOG(RHI, Warning, "ClearRenderTarget — texture has no RTV");
        return;
    }

    m_ctx->ClearRenderTargetView(dx11Tex->rtv, color);
}

void DX11CommandList::ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) {
    if (!m_ctx || !texture.IsValid()) return;

    auto* dx11Tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!dx11Tex || !dx11Tex->dsv) {
        UZ_LOG(RHI, Warning, "ClearDepthStencil — texture has no DSV");
        return;
    }

    m_ctx->ClearDepthStencilView(
        dx11Tex->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, stencil);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                                  RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) {
    if (!m_ctx || !src.IsValid() || !dst.IsValid()) return;

    auto* srcBuf = m_device->GetBufferPool().Get(src.index, src.generation);
    auto* dstBuf = m_device->GetBufferPool().Get(dst.index, dst.generation);
    if (!srcBuf || !srcBuf->buffer || !dstBuf || !dstBuf->buffer) {
        UZ_LOG(RHI, Warning, "CopyBuffer — could not resolve buffer handles");
        return;
    }

    // DX11 path: when source is a STAGING buffer (Upload+CopySrc) and destination
    // is a DEFAULT buffer, use Map→UpdateSubresource instead of CopySubresourceRegion.
    // CopySubresourceRegion from STAGING works per spec, but UpdateSubresource is
    // more reliable across driver implementations for buffer-to-buffer copies.
    if (srcBuf->desc.memory == RHIMemoryType::Upload &&
        dstBuf->desc.memory == RHIMemoryType::Default) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_ctx->Map(srcBuf->buffer, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr)) {
            const auto* srcData = static_cast<const uint8_t*>(mapped.pData) + srcOffset;
            D3D11_BOX dstBox = {};
            dstBox.left = static_cast<UINT>(dstOffset);
            dstBox.right = static_cast<UINT>(dstOffset + size);
            dstBox.top = 0;
            dstBox.bottom = 1;
            dstBox.front = 0;
            dstBox.back = 1;
            m_ctx->UpdateSubresource(dstBuf->buffer, 0, &dstBox, srcData, 0, 0);
            m_ctx->Unmap(srcBuf->buffer, 0);
            return;
        }
        UZ_LOG(RHI, Warning, "CopyBuffer: Map failed for staging→default, "
               "falling back to CopySubresourceRegion (hr=0x{:08X})",
               static_cast<uint32_t>(hr));
    }

    D3D11_BOX srcBox = {};
    srcBox.left = static_cast<UINT>(srcOffset);
    srcBox.right = static_cast<UINT>(srcOffset + size);
    srcBox.top = 0;
    srcBox.bottom = 1;
    srcBox.front = 0;
    srcBox.back = 1;

    m_ctx->CopySubresourceRegion(
        dstBuf->buffer, 0, static_cast<UINT>(dstOffset), 0, 0,
        srcBuf->buffer, 0, &srcBox);
}

void DX11CommandList::CopyTexture(RHITextureHandle src, RHITextureHandle dst) {
    if (!m_ctx || !src.IsValid() || !dst.IsValid()) return;

    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !srcTex->GetResource() || !dstTex || !dstTex->GetResource()) {
        UZ_LOG(RHI, Warning, "CopyTexture — could not resolve texture handles");
        return;
    }

    m_ctx->CopyResource(dstTex->GetResource(), srcTex->GetResource());
}

void DX11CommandList::CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                            uint32_t srcRowPitch,
                                            RHITextureHandle dstTexture,
                                            uint32_t mipLevel, uint32_t arraySlice) {
    if (!m_ctx || !srcBuffer.IsValid() || !dstTexture.IsValid()) return;

    auto* buf = m_device->GetBufferPool().Get(srcBuffer.index, srcBuffer.generation);
    auto* tex = m_device->GetTexturePool().Get(dstTexture.index, dstTexture.generation);
    if (!buf || !buf->buffer || !tex || !tex->GetResource()) {
        UZ_LOG(RHI, Warning, "CopyBufferToTexture — could not resolve handles");
        return;
    }

    // Map the staging buffer, then UpdateSubresource to the texture.
    // For 3D textures the depth-row-pitch (depthPitch) is passed as 0, which lets
    // D3D11 infer it from the resource dimensions — correct for mip 0.
    const uint32_t subresource = D3D11CalcSubresource(mipLevel, arraySlice, tex->desc.mipLevels);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_ctx->Map(buf->buffer, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CopyBufferToTexture: failed to map staging buffer, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    const auto* srcData = static_cast<const uint8_t*>(mapped.pData) + srcOffset;
    m_ctx->UpdateSubresource(tex->GetResource(), subresource, nullptr,
                                      srcData, srcRowPitch, 0);
    m_ctx->Unmap(buf->buffer, 0);
}

void DX11CommandList::CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                           RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                           uint32_t dstRowPitch, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();
    if (!m_ctx || !srcTexture.IsValid() || !dstBuffer.IsValid()) return;

    auto* tex = m_device->GetTexturePool().Get(srcTexture.index, srcTexture.generation);
    auto* buf = m_device->GetBufferPool().Get(dstBuffer.index, dstBuffer.generation);
    if (!tex || !tex->GetResource() || !buf || !buf->buffer) {
        UZ_LOG(RHI, Warning, "CopyTextureToBuffer — could not resolve handles");
        return;
    }

    // DX11 cannot copy texture→buffer directly. Use a staging texture as
    // intermediate: copy src texture → staging texture, then map staging
    // and memcpy into readback buffer.
    // Dimensions are read from the typed D3D11 descriptor so we get the exact
    // DXGI_FORMAT that was created (avoiding a separate format-map function).
    auto* d3dDevice = m_device->GetDevice();

    // Subresource index — for 3D textures arraySlice is ignored (must be 0)
    const uint32_t srcSubresource = D3D11CalcSubresource(mipLevel, arraySlice, tex->desc.mipLevels);

    // Create a staging resource matching the source mip dimensions.
    // For 3D sources we need ID3D11Texture3D staging; for 2D we use ID3D11Texture2D.
    ID3D11Resource* staging = nullptr;
    HRESULT hr = S_OK;

    if (tex->desc.is3D) {
        D3D11_TEXTURE3D_DESC srcDesc = {};
        tex->texture3D->GetDesc(&srcDesc);

        const uint32_t mipWidth  = std::max(1u, srcDesc.Width  >> mipLevel);
        const uint32_t mipHeight = std::max(1u, srcDesc.Height >> mipLevel);
        const uint32_t mipDepth  = std::max(1u, srcDesc.Depth  >> mipLevel);

        D3D11_TEXTURE3D_DESC stagingDesc = {};
        stagingDesc.Width          = mipWidth;
        stagingDesc.Height         = mipHeight;
        stagingDesc.Depth          = mipDepth;
        stagingDesc.MipLevels      = 1;
        stagingDesc.Format         = srcDesc.Format;
        stagingDesc.Usage          = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture3D* staging3D = nullptr;
        hr = d3dDevice->CreateTexture3D(&stagingDesc, nullptr, &staging3D);
        staging = staging3D;
    } else {
        D3D11_TEXTURE2D_DESC srcDesc = {};
        tex->texture->GetDesc(&srcDesc);

        const uint32_t mipWidth  = std::max(1u, srcDesc.Width  >> mipLevel);
        const uint32_t mipHeight = std::max(1u, srcDesc.Height >> mipLevel);

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width            = mipWidth;
        stagingDesc.Height           = mipHeight;
        stagingDesc.MipLevels        = 1;
        stagingDesc.ArraySize        = 1;
        stagingDesc.Format           = srcDesc.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage            = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* staging2D = nullptr;
        hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &staging2D);
        staging = staging2D;
    }

    if (FAILED(hr) || !staging) {
        UZ_LOG(RHI, Error, "CopyTextureToBuffer: failed to create staging texture, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    // Copy the source subresource into the staging resource (executes immediately)
    m_ctx->CopySubresourceRegion(staging, 0, 0, 0, 0,
                                 tex->GetResource(), srcSubresource, nullptr);

    // Map the staging resource for CPU reading
    D3D11_MAPPED_SUBRESOURCE stagingMapped = {};
    hr = m_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &stagingMapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CopyTextureToBuffer: failed to map staging texture, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        staging->Release();
        return;
    }

    // Map the readback buffer for writing and copy row by row
    D3D11_MAPPED_SUBRESOURCE bufMapped = {};
    hr = m_ctx->Map(buf->buffer, 0, D3D11_MAP_WRITE, 0, &bufMapped);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CopyTextureToBuffer: failed to map readback buffer for write, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        m_ctx->Unmap(staging, 0);
        staging->Release();
        return;
    }

    // Derive mip-level dimensions from the RHI descriptor (same values used above
    // inside the typed-GetDesc branches, hoisted here for the copy loop).
    const uint32_t mipWidth  = std::max(1u, static_cast<uint32_t>(tex->desc.width)  >> mipLevel);
    const uint32_t mipHeight = std::max(1u, static_cast<uint32_t>(tex->desc.height) >> mipLevel);

    const uint32_t texelSize = BytesPerTexel(tex->desc.format);
    const uint32_t copyBytes = mipWidth * texelSize;
    const auto* srcPtr = static_cast<const uint8_t*>(stagingMapped.pData);
    auto* dstPtr = static_cast<uint8_t*>(bufMapped.pData) + dstOffset;

    for (uint32_t row = 0; row < mipHeight; ++row) {
        std::memcpy(dstPtr + row * dstRowPitch,
                    srcPtr + row * stagingMapped.RowPitch,
                    copyBytes);
    }

    m_ctx->Unmap(buf->buffer, 0);
    m_ctx->Unmap(staging, 0);
    staging->Release();
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void DX11CommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!m_ctx) return;

    // DX11 requires SRVs to be unbound before the same resource can be used as
    // RTV or DSV. Clear all PS/VS/CS SRV slots up front to prevent the runtime
    // error: "Resource being set to OM DepthStencil is still bound on input!"
    // This matches reference's D3D11 RHI behavior (FD3D11DynamicRHI::CommitRenderTargets).
    ID3D11ShaderResourceView* nullSRVs[16] = {};
    m_ctx->PSSetShaderResources(0, 16, nullSRVs);
    m_ctx->VSSetShaderResources(0, 16, nullSRVs);
    m_ctx->CSSetShaderResources(0, 16, nullSRVs);

    // Also unbind any compute UAVs that may conflict with render targets
    ID3D11UnorderedAccessView* nullUAVs[8] = {};
    m_ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);

    ID3D11RenderTargetView* rtvs[8] = {};
    uint32_t rtvCount = 0;

    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < 8; ++i) {
        const auto& att = desc.colorAttachments[i];
        if (!att.texture.IsValid()) continue;

        auto* tex = m_device->GetTexturePool().Get(att.texture.index, att.texture.generation);
        // 3D textures cannot be render targets; tex->texture is the 2D path used here
        if (!tex || !tex->texture) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has no texture", i);
            continue;
        }

        ID3D11RenderTargetView* rtv = tex->rtv;

        // Per-slice/mip RTV: create a temporary RTV for the requested array slice and mip level.
        // DX11 supports this natively via D3D11_RENDER_TARGET_VIEW_DESC with Texture2DArray.
        if (att.arraySlice != 0 || att.mipLevel != 0) {
            // Get the format from the existing resource
            D3D11_TEXTURE2D_DESC texDesc;
            tex->texture->GetDesc(&texDesc);

            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = texDesc.Format;
            if (texDesc.ArraySize > 1) {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice        = att.mipLevel;
                rtvDesc.Texture2DArray.FirstArraySlice  = att.arraySlice;
                rtvDesc.Texture2DArray.ArraySize        = 1;
            } else {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = att.mipLevel;
            }
            ID3D11RenderTargetView* tempRtv = nullptr;
            HRESULT hr = m_device->GetDevice()->CreateRenderTargetView(tex->texture, &rtvDesc, &tempRtv);
            if (SUCCEEDED(hr) && tempRtv) {
                rtv = tempRtv;
                // Store for cleanup in EndRenderPass
                if (m_tempRtvCount < 8) {
                    m_tempRtvs[m_tempRtvCount++] = tempRtv;
                }
            } else {
                UZ_LOG(RHI, Error, "BeginRenderPass — failed to create per-slice RTV "
                       "(attachment {} slice={} mip={} hr=0x{:08X})", i, att.arraySlice, att.mipLevel, static_cast<uint32_t>(hr));
                if (!tex->rtv) continue;
                rtv = tex->rtv;
            }
        }

        if (!rtv) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has no RTV", i);
            continue;
        }

        if (att.clear) {
            m_ctx->ClearRenderTargetView(rtv, att.clearColor);
        }

        rtvs[rtvCount++] = rtv;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (desc.hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* depthTex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index, desc.depthAttachment.texture.generation);
        if (depthTex && depthTex->dsv) {
            dsv = depthTex->dsv;
            if (desc.depthAttachment.clear) {
                m_ctx->ClearDepthStencilView(
                    dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                    desc.depthAttachment.clearDepth, desc.depthAttachment.clearStencil);
            }
        }
    }

    m_ctx->OMSetRenderTargets(rtvCount, rtvCount > 0 ? rtvs : nullptr, dsv);
}

void DX11CommandList::EndRenderPass() {
    if (!m_ctx) return;

    // Release temporary per-slice RTVs created in BeginRenderPass
    for (uint32_t i = 0; i < m_tempRtvCount; ++i) {
        if (m_tempRtvs[i]) {
            m_tempRtvs[i]->Release();
            m_tempRtvs[i] = nullptr;
        }
    }
    m_tempRtvCount = 0;

    // Unbind all render targets and SRVs to prevent DX11 resource hazards.
    // DX11 automatically forces SRV slots to NULL when the same resource is
    // bound as RTV/DSV. By explicitly unbinding here, we ensure the next pass
    // can freely bind these resources as SRVs without the runtime interfering.
    m_ctx->OMSetRenderTargets(0, nullptr, nullptr);

    // Clear SRV slots that may reference render targets or depth buffers.
    ID3D11ShaderResourceView* nullSrvs[8] = {};
    m_ctx->PSSetShaderResources(0, 8, nullSrvs);
}

} // namespace uz
