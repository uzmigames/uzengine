# DX11 Stale SRV/UAV Bindings Between Render Graph Passes

**Category**: rhi
**Tags**: dx11, srv, uav, stale-binding, render-graph, rhi, ibl, black-output

## Description

DX11's immediate context preserves shader resource bindings across draw/dispatch calls. When the render graph executes multiple passes (GBuffer → Shadow → Lighting → Compute), SRV/UAV slots from previous passes leak into subsequent ones. This causes: (1) textures bound as SRV in pass N still bound when pass N+1 tries to use the same resource as RTV/DSV — D3D11 Debug Layer error "Resource still bound on input". (2) Compute dispatches inherit stale SRV bindings from graphics passes — dimension mismatches (TEXTURE2D vs TEXTURECUBE). (3) IBL compute producing black output because stale auto-exposure UAV corrupts irradiance dispatch.

## Example

// WRONG: No SRV/UAV clearing between passes
cmd->BeginRenderPass(passDesc); // OMSetRenderTargets — but slot t2 still has depth SRV!
// D3D11 Debug: "Resource being set to OM DepthStencil is still bound on input!"

// CORRECT: Clear all SRV/UAV slots at pass boundaries (UE5 pattern)
// In Begin():
ID3D11ShaderResourceView* nullSRVs[16] = {};
m_ctx->PSSetShaderResources(0, 16, nullSRVs);
m_ctx->CSSetShaderResources(0, 16, nullSRVs);

// In BeginRenderPass() before OMSetRenderTargets:
m_ctx->PSSetShaderResources(0, 16, nullSRVs);

// In SetPipeline() when switching to compute:
m_ctx->PSSetShaderResources(0, 16, nullSRVs);
ID3D11UnorderedAccessView* nullUAVs[8] = {};
m_ctx->CSSetUnorderedAccessViews(0, 8, nullUAVs, nullptr);

## When to Use

Any DX11 renderer with multiple render passes per frame, especially when using a render graph that manages barriers but doesn't explicitly unbind resources between passes.

## When NOT to Use

DX12 and Vulkan handle this via explicit resource barriers and descriptor sets — no stale binding issue.
