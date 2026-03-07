/*
 * rhi_dx12_commands.c -- DX12 RHI backend: command recording, barriers,
 *                        swapchain, and debug utilities
 *
 * Split from rhi_dx12.c per §FILE-SIZE-LIMIT (CS-21).
 * See rhi_dx12_internal.h for shared state and declarations.
 */

#ifdef _WIN32

#include "rhi_dx12_internal.h"
#include "engine/core/diag.h"

/* =======================================================================
 * Command recording
 * ======================================================================= */

RhiCmdList dx12_cmd_begin(void) {
    RHI_ASSERT(s_dx12.initialized);

    uint64_t slot = dx12_alloc_cmd_list_slot();
    if (slot == RHI_NULL) return RHI_NULL;

    HRESULT hr;

    /* Create allocator if this slot has never been used */
    if (!s_dx12.cmd_lists[slot].allocator) {
        hr = ID3D12Device_CreateCommandAllocator(
            s_dx12.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator,
            (void **)&s_dx12.cmd_lists[slot].allocator);
        if (FAILED(hr)) {
            s_dx12.cmd_lists[slot].occupied = false;
            return RHI_NULL;
        }
    } else {
        ID3D12CommandAllocator_Reset(s_dx12.cmd_lists[slot].allocator);
    }

    /* Create command list if needed, otherwise reset it */
    if (!s_dx12.cmd_lists[slot].list) {
        hr = ID3D12Device_CreateCommandList(
            s_dx12.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            s_dx12.cmd_lists[slot].allocator, NULL,
            &IID_ID3D12GraphicsCommandList,
            (void **)&s_dx12.cmd_lists[slot].list);
        if (FAILED(hr)) {
            s_dx12.cmd_lists[slot].occupied = false;
            return RHI_NULL;
        }
    } else {
        ID3D12GraphicsCommandList_Reset(
            s_dx12.cmd_lists[slot].list,
            s_dx12.cmd_lists[slot].allocator, NULL);
    }

    /* Set the shader-visible descriptor heaps */
    ID3D12DescriptorHeap *heaps[2] = {
        s_dx12.cbv_srv_uav_heap,
        s_dx12.sampler_heap
    };
    ID3D12GraphicsCommandList_SetDescriptorHeaps(
        s_dx12.cmd_lists[slot].list, 2, heaps);

    s_dx12.cmd_lists[slot].recording = true;
    return (RhiCmdList)slot;
}

void dx12_cmd_end(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    uint32_t slot = (uint32_t)cmd;
    RHI_ASSERT(slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[slot].occupied);
    RHI_ASSERT(s_dx12.cmd_lists[slot].recording);

    ID3D12GraphicsCommandList_Close(s_dx12.cmd_lists[slot].list);
    s_dx12.cmd_lists[slot].recording = false;
}

void dx12_cmd_submit(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    uint32_t slot = (uint32_t)cmd;
    RHI_ASSERT(slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[slot].occupied);
    RHI_ASSERT(!s_dx12.cmd_lists[slot].recording); /* Must be ended first */

    ID3D12CommandList *lists[] = {
        (ID3D12CommandList *)s_dx12.cmd_lists[slot].list
    };
    ID3D12CommandQueue_ExecuteCommandLists(s_dx12.cmd_queue, 1, lists);

    /* Return the slot so it can be reused */
    s_dx12.cmd_lists[slot].occupied = false;
}

void dx12_cmd_set_pipeline(RhiCmdList cmd, RhiPipeline pipeline) {
    if (cmd == RHI_NULL || pipeline == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t pso_slot = (uint32_t)pipeline;
    RHI_ASSERT(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);
    RHI_ASSERT(pso_slot < DX12_MAX_PIPELINES && s_dx12.pipelines[pso_slot].occupied);

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    ID3D12GraphicsCommandList_SetPipelineState(cl, s_dx12.pipelines[pso_slot].pso);

    bool is_compute = s_dx12.pipelines[pso_slot].is_compute;
    s_dx12.cmd_lists[cmd_slot].is_compute_bound = is_compute;

    if (is_compute) {
        ID3D12GraphicsCommandList_SetComputeRootSignature(cl, s_dx12.pipelines[pso_slot].root_sig);
    } else {
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(cl, s_dx12.pipelines[pso_slot].root_sig);
    }
}

void dx12_cmd_set_render_targets(RhiCmdList cmd, const RhiTexture *rts,
                                        uint32_t count, RhiTexture depth) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    RHI_ASSERT(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[8];
    for (uint32_t i = 0; i < count && i < 8; i++) {
        uint32_t tex_slot = (uint32_t)rts[i];
        RHI_ASSERT(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.rtv_heap, &rtv_handles[i]);
        rtv_handles[i].ptr += (SIZE_T)tex_slot * s_dx12.rtv_desc_size;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    D3D12_CPU_DESCRIPTOR_HANDLE *p_dsv = NULL;
    if (depth != RHI_NULL) {
        uint32_t ds_slot = (uint32_t)depth;
        RHI_ASSERT(ds_slot < DX12_MAX_RESOURCES && s_dx12.textures[ds_slot].occupied);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.dsv_heap, &dsv_handle);
        dsv_handle.ptr += (SIZE_T)ds_slot * s_dx12.dsv_desc_size;
        p_dsv = &dsv_handle;
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(
        cl, count, count > 0 ? rtv_handles : NULL, FALSE, p_dsv);
}

void dx12_cmd_set_viewport(RhiCmdList cmd, float x, float y,
                                  float w, float h, float min_d, float max_d) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;

    D3D12_VIEWPORT vp;
    vp.TopLeftX = x;
    vp.TopLeftY = y;
    vp.Width    = w;
    vp.Height   = h;
    vp.MinDepth = min_d;
    vp.MaxDepth = max_d;
    ID3D12GraphicsCommandList_RSSetViewports(cl, 1, &vp);
}

void dx12_cmd_set_scissor(RhiCmdList cmd, int32_t x, int32_t y,
                                 uint32_t w, uint32_t h) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;

    D3D12_RECT rect;
    rect.left   = (LONG)x;
    rect.top    = (LONG)y;
    rect.right  = (LONG)(x + (int32_t)w);
    rect.bottom = (LONG)(y + (int32_t)h);
    ID3D12GraphicsCommandList_RSSetScissorRects(cl, 1, &rect);
}

void dx12_cmd_set_vertex_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                       RhiBuffer buf, uint64_t offset,
                                       uint32_t stride) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    RHI_ASSERT(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_VERTEX_BUFFER_VIEW view;
    view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource) + offset;
    view.SizeInBytes    = (UINT)(s_dx12.buffers[buf_slot].size - offset);
    view.StrideInBytes  = stride;

    ID3D12GraphicsCommandList_IASetVertexBuffers(
        s_dx12.cmd_lists[cmd_slot].list, slot_idx, 1, &view);
}

void dx12_cmd_set_index_buffer(RhiCmdList cmd, RhiBuffer buf,
                                      uint64_t offset, bool is_32bit) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    RHI_ASSERT(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_INDEX_BUFFER_VIEW view;
    view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource) + offset;
    view.SizeInBytes    = (UINT)(s_dx12.buffers[buf_slot].size - offset);
    view.Format         = is_32bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    ID3D12GraphicsCommandList_IASetIndexBuffer(
        s_dx12.cmd_lists[cmd_slot].list, &view);
}

void dx12_cmd_set_constant_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                         RhiBuffer buf) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    RHI_ASSERT(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    RHI_ASSERT(slot_idx < 4 && "Root CBV slots 0-3 (b0-b3)");
    D3D12_GPU_VIRTUAL_ADDRESS addr = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource);
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
        s_dx12.cmd_lists[cmd_slot].list, slot_idx, addr);
}

void dx12_cmd_set_texture(RhiCmdList cmd, uint32_t slot_idx,
                                 RhiTexture tex) {
    if (cmd == RHI_NULL || tex == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)tex;
    RHI_ASSERT(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);

    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(
        s_dx12.cbv_srv_uav_heap, &gpu_handle);
    gpu_handle.ptr += (UINT64)tex_slot * s_dx12.cbv_srv_uav_desc_size;

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    if (s_dx12.cmd_lists[cmd_slot].is_compute_bound) {
        /* Compute root sig: param 1 = t0, param 2 = t1 */
        assert(slot_idx < 2 && "Compute pipeline supports t0..t1");
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
            cl, 1 + slot_idx, gpu_handle);
    } else {
        /* Graphics root sig: param 4 = t0 .. param 13 = t9 */
        assert(slot_idx < 10 && "Only slots 0-9 supported (t0..t9)");
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
            cl, 4 + slot_idx, gpu_handle);
    }
}

void dx12_cmd_set_uav(RhiCmdList cmd, uint32_t slot_idx,
                             RhiTexture tex, uint32_t mip_level) {
    if (cmd == RHI_NULL || tex == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)tex;
    assert(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);
    assert(s_dx12.textures[tex_slot].has_uav && "Texture needs RHI_TEXTURE_USAGE_UNORDERED");
    assert(mip_level < s_dx12.textures[tex_slot].uav_mip_count && "mip_level out of range");
    assert(slot_idx < 2 && "Compute pipeline supports u0..u1");

    /* Per-mip UAV descriptor at heap[uav_heap_base + mip_level] */
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(
        s_dx12.cbv_srv_uav_heap, &gpu_handle);
    gpu_handle.ptr += (UINT64)(s_dx12.textures[tex_slot].uav_heap_base + mip_level)
                    * s_dx12.cbv_srv_uav_desc_size;

    /* Compute root sig: param 3 = u0, param 4 = u1 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
        s_dx12.cmd_lists[cmd_slot].list, 3 + slot_idx, gpu_handle);
}

static D3D12_PRIMITIVE_TOPOLOGY rhi_topology_to_dx12(RhiTopology t) {
    switch (t) {
        case RHI_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHI_TOPOLOGY_LINE_LIST:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHI_TOPOLOGY_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHI_TOPOLOGY_POINT_LIST:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                          return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

void dx12_cmd_set_topology(RhiCmdList cmd, RhiTopology topology) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    s_dx12.cmd_lists[cmd_slot].topology = rhi_topology_to_dx12(topology);
}

void dx12_cmd_draw(RhiCmdList cmd, uint32_t vertex_count,
                           uint32_t instance_count, uint32_t first_vertex,
                           uint32_t first_instance) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    D3D12_PRIMITIVE_TOPOLOGY topo = s_dx12.cmd_lists[cmd_slot].topology;
    if (topo == 0) topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(cl, topo);
    ID3D12GraphicsCommandList_DrawInstanced(
        cl, vertex_count, instance_count, first_vertex, first_instance);
}

void dx12_cmd_draw_indexed(RhiCmdList cmd, uint32_t index_count,
                                   uint32_t instance_count,
                                   uint32_t first_index,
                                   int32_t vertex_offset,
                                   uint32_t first_instance) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    D3D12_PRIMITIVE_TOPOLOGY topo = s_dx12.cmd_lists[cmd_slot].topology;
    if (topo == 0) topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(cl, topo);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(
        cl, index_count, instance_count, first_index,
        vertex_offset, first_instance);
}

void dx12_cmd_dispatch(RhiCmdList cmd, uint32_t x, uint32_t y,
                               uint32_t z) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;

    /* TODO Phase 2: Requires compute pipeline to be set */
    ID3D12GraphicsCommandList_Dispatch(
        s_dx12.cmd_lists[cmd_slot].list, x, y, z);
}

/* =======================================================================
 * Clear commands
 * ======================================================================= */

void dx12_cmd_clear_render_target(RhiCmdList cmd, RhiTexture rt,
                                         float r, float g, float b, float a) {
    if (cmd == RHI_NULL || rt == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)rt;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
        s_dx12.rtv_heap, &rtv);
    rtv.ptr += (SIZE_T)tex_slot * s_dx12.rtv_desc_size;

    float color[4] = { r, g, b, a };
    ID3D12GraphicsCommandList_ClearRenderTargetView(
        s_dx12.cmd_lists[cmd_slot].list, rtv, color, 0, NULL);
}

void dx12_cmd_clear_depth_stencil(RhiCmdList cmd, RhiTexture ds,
                                         float depth, uint8_t stencil) {
    if (cmd == RHI_NULL || ds == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)ds;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
        s_dx12.dsv_heap, &dsv);
    dsv.ptr += (SIZE_T)tex_slot * s_dx12.dsv_desc_size;

    ID3D12GraphicsCommandList_ClearDepthStencilView(
        s_dx12.cmd_lists[cmd_slot].list, dsv,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        depth, stencil, 0, NULL);
}

void dx12_cmd_set_stencil_ref(RhiCmdList cmd, uint8_t ref) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    RHI_ASSERT(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);

    ID3D12GraphicsCommandList_OMSetStencilRef(
        s_dx12.cmd_lists[cmd_slot].list, (UINT)ref);
}

/* =======================================================================
 * Barriers
 * ======================================================================= */

void dx12_cmd_barrier_texture(RhiCmdList cmd, RhiTexture tex,
                                     RhiResourceState src_state,
                                     RhiResourceState dst_state) {
    if (cmd == RHI_NULL || tex == RHI_NULL) return;
    if (src_state == dst_state) return; /* No-op */

    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)tex;
    RHI_ASSERT(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);

    D3D12_RESOURCE_BARRIER barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = s_dx12.textures[tex_slot].resource;
    barrier.Transition.StateBefore = to_dx12_state(src_state);
    barrier.Transition.StateAfter  = to_dx12_state(dst_state);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    ID3D12GraphicsCommandList_ResourceBarrier(
        s_dx12.cmd_lists[cmd_slot].list, 1, &barrier);
}

void dx12_cmd_barrier_buffer(RhiCmdList cmd, RhiBuffer buf,
                                    RhiResourceState src_state,
                                    RhiResourceState dst_state) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    if (src_state == dst_state) return;

    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    RHI_ASSERT(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_RESOURCE_BARRIER barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = s_dx12.buffers[buf_slot].resource;
    barrier.Transition.StateBefore = to_dx12_state(src_state);
    barrier.Transition.StateAfter  = to_dx12_state(dst_state);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    ID3D12GraphicsCommandList_ResourceBarrier(
        s_dx12.cmd_lists[cmd_slot].list, 1, &barrier);
}

/* =======================================================================
 * Root constants (inline 32-bit values)
 * ======================================================================= */

void dx12_cmd_set_root_constants(RhiCmdList cmd, uint32_t slot,
                                  const void *data, uint32_t count_32bit,
                                  uint32_t offset_32bit) {
    if (cmd == RHI_NULL || !data || count_32bit == 0) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    RHI_ASSERT(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    if (s_dx12.cmd_lists[cmd_slot].is_compute_bound) {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(
            cl, slot, count_32bit, data, offset_32bit);
    } else {
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(
            cl, slot, count_32bit, data, offset_32bit);
    }
}

/* =======================================================================
 * Copy commands
 * ======================================================================= */

void dx12_cmd_copy_texture_to_buffer(RhiCmdList cmd, RhiTexture src,
                                      RhiBuffer dst,
                                      const RhiTextureFootprint *footprint) {
    if (cmd == RHI_NULL || src == RHI_NULL || dst == RHI_NULL || !footprint) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)src;
    uint32_t buf_slot = (uint32_t)dst;
    RHI_ASSERT(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);
    RHI_ASSERT(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_TEXTURE_COPY_LOCATION src_loc;
    memset(&src_loc, 0, sizeof(src_loc));
    src_loc.pResource        = s_dx12.textures[tex_slot].resource;
    src_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst_loc;
    memset(&dst_loc, 0, sizeof(dst_loc));
    dst_loc.pResource                          = s_dx12.buffers[buf_slot].resource;
    dst_loc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_loc.PlacedFootprint.Offset             = footprint->offset;
    dst_loc.PlacedFootprint.Footprint.Format   = to_dxgi_format(footprint->format);
    dst_loc.PlacedFootprint.Footprint.Width    = footprint->width;
    dst_loc.PlacedFootprint.Footprint.Height   = footprint->height;
    dst_loc.PlacedFootprint.Footprint.Depth    = footprint->depth ? footprint->depth : 1;
    dst_loc.PlacedFootprint.Footprint.RowPitch = footprint->row_pitch;

    ID3D12GraphicsCommandList_CopyTextureRegion(
        s_dx12.cmd_lists[cmd_slot].list,
        &dst_loc, 0, 0, 0, &src_loc, NULL);
}

/* =======================================================================
 * Swapchain
 * ======================================================================= */

RhiTexture dx12_swapchain_backbuffer(void) {
    RHI_ASSERT(s_dx12.initialized);
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);
    return (RhiTexture)s_dx12.backbuffer_indices[s_dx12.current_backbuffer];
}

void dx12_swapchain_present(uint32_t sync_interval) {
    RHI_ASSERT(s_dx12.initialized);
    HRESULT hr = IDXGISwapChain3_Present(s_dx12.swap_chain, sync_interval, 0);

    /* Check for device removal (TDR / GPU crash) */
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HRESULT reason = ID3D12Device_GetDeviceRemovedReason(s_dx12.device);
        DIAG_FMT("FATAL: Device Removed! Reason=0x%08X", (unsigned)reason);
        dx12_write_dred_report(reason);
        return; /* Don't signal fence — device is gone */
    }

    /* Signal fence for this frame and advance */
    s_dx12.frame_fence_value++;
    ID3D12CommandQueue_Signal(s_dx12.cmd_queue, s_dx12.frame_fence,
                             s_dx12.frame_fence_value);

    /* Wait for the oldest frame in flight (simple approach for Phase 1) */
    if (ID3D12Fence_GetCompletedValue(s_dx12.frame_fence)
        < s_dx12.frame_fence_value - (DX12_BACKBUFFER_COUNT - 1)) {
        uint64_t wait_val = s_dx12.frame_fence_value - (DX12_BACKBUFFER_COUNT - 1);
        ID3D12Fence_SetEventOnCompletion(s_dx12.frame_fence, wait_val,
                                         s_dx12.frame_fence_event);
        WaitForSingleObject(s_dx12.frame_fence_event, INFINITE);
    }
}

/* =======================================================================
 * Debug
 * ======================================================================= */

void dx12_set_debug_name(uint64_t handle, const char *name) {
    if (handle == RHI_NULL || !name) return;

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);

    uint32_t slot = (uint32_t)handle;
    if (slot < DX12_MAX_RESOURCES && s_dx12.textures[slot].occupied
        && s_dx12.textures[slot].resource) {
        ID3D12Resource_SetName(s_dx12.textures[slot].resource, wname);
    } else if (slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied
               && s_dx12.buffers[slot].resource) {
        ID3D12Resource_SetName(s_dx12.buffers[slot].resource, wname);
    }
}

void dx12_begin_event(RhiCmdList cmd, const char *name) {
    if (cmd == RHI_NULL || !name) return;
    (void)cmd;
    (void)name;
    /* TODO Phase 2: Use PIX events (PIXBeginEvent) for GPU profiling. */
}

void dx12_end_event(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    (void)cmd;
    /* TODO Phase 2: PIXEndEvent */
}

/* =======================================================================
 * DRED — Device Removed Extended Data report
 * ======================================================================= */

void dx12_write_dred_report(HRESULT removal_reason) {
    /* Build the output path next to the executable */
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char *last_slash = exe_path;
    for (char *p = exe_path; *p; ++p) {
        if (*p == '\\' || *p == '/') last_slash = p;
    }
    last_slash[1] = '\0';

    char crash_dir[MAX_PATH] = {};
    snprintf(crash_dir, sizeof(crash_dir), "%s.uzengine/crash_reports", exe_path);
    CreateDirectoryA(crash_dir, NULL);

    char dred_path[MAX_PATH] = {};
    snprintf(dred_path, sizeof(dred_path), "%s/dred_report.json", crash_dir);

    FILE *f = NULL;
    fopen_s(&f, dred_path, "w");
    if (!f) {
        DIAG("Failed to open dred_report.json for writing");
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"removal_reason\": \"0x%08X\",\n", (unsigned)removal_reason);

    /* §4.5: Active RenderGraph pass at time of crash */
    {
        extern const char* rg_get_active_pass_name(void);
        const char* active_pass = rg_get_active_pass_name();
        if (active_pass && active_pass[0]) {
            fprintf(f, "  \"active_render_pass\": \"%s\",\n", active_pass);
            DIAG_FMT("DRED: Active render pass at crash: %s", active_pass);
        } else {
            fprintf(f, "  \"active_render_pass\": null,\n");
        }
    }

    /* Try to query DRED interface */
    ID3D12DeviceRemovedExtendedData *dred = NULL;
    HRESULT hr = ID3D12Device_QueryInterface(
        s_dx12.device,
        &IID_ID3D12DeviceRemovedExtendedData,
        (void **)&dred);

    if (SUCCEEDED(hr) && dred) {
        /* Auto-breadcrumbs */
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
        hr = ID3D12DeviceRemovedExtendedData_GetAutoBreadcrumbsOutput(
            dred, &breadcrumbs);

        if (SUCCEEDED(hr) && breadcrumbs.pHeadAutoBreadcrumbNode) {
            fprintf(f, "  \"auto_breadcrumbs\": [\n");
            const D3D12_AUTO_BREADCRUMB_NODE *node =
                breadcrumbs.pHeadAutoBreadcrumbNode;
            int bc_count = 0;
            while (node) {
                if (bc_count > 0) fprintf(f, ",\n");

                /* Convert command list name */
                char cmd_name[256] = "unknown";
                if (node->pCommandListDebugNameW) {
                    WideCharToMultiByte(CP_UTF8, 0,
                        node->pCommandListDebugNameW, -1,
                        cmd_name, sizeof(cmd_name), NULL, NULL);
                }

                fprintf(f, "    {\"command_list\": \"%s\"", cmd_name);
                fprintf(f, ", \"breadcrumb_count\": %u", node->BreadcrumbCount);

                /* Last completed breadcrumb index */
                if (node->pLastBreadcrumbValue) {
                    fprintf(f, ", \"last_completed\": %u",
                            *node->pLastBreadcrumbValue);
                }

                fprintf(f, "}");
                node = node->pNext;
                bc_count++;
                if (bc_count > 32) break; /* Safety limit */
            }
            fprintf(f, "\n  ],\n");

            DIAG_FMT("DRED: %d auto-breadcrumb node(s) captured", bc_count);
        } else {
            fprintf(f, "  \"auto_breadcrumbs\": null,\n");
            DIAG("DRED: No auto-breadcrumbs available");
        }

        /* Page fault output */
        D3D12_DRED_PAGE_FAULT_OUTPUT page_fault = {};
        hr = ID3D12DeviceRemovedExtendedData_GetPageFaultAllocationOutput(
            dred, &page_fault);

        if (SUCCEEDED(hr)) {
            fprintf(f, "  \"page_fault\": {\n");
            fprintf(f, "    \"virtual_address\": \"0x%llX\",\n",
                    (unsigned long long)page_fault.PageFaultVA);

            /* List allocations near the faulting address */
            fprintf(f, "    \"allocations\": [\n");
            const D3D12_DRED_ALLOCATION_NODE *alloc =
                page_fault.pHeadExistingAllocationNode;
            int alloc_count = 0;
            while (alloc && alloc_count < 16) {
                if (alloc_count > 0) fprintf(f, ",\n");
                char alloc_name[256] = "unnamed";
                if (alloc->ObjectNameW) {
                    WideCharToMultiByte(CP_UTF8, 0,
                        alloc->ObjectNameW, -1,
                        alloc_name, sizeof(alloc_name), NULL, NULL);
                }
                fprintf(f, "      {\"name\": \"%s\", \"type\": %d}",
                        alloc_name, (int)alloc->AllocationType);
                alloc = alloc->pNext;
                alloc_count++;
            }
            fprintf(f, "\n    ]\n");
            fprintf(f, "  }\n");

            DIAG_FMT("DRED: Page fault at VA=0x%llX, %d allocation(s)",
                      (unsigned long long)page_fault.PageFaultVA, alloc_count);
        } else {
            fprintf(f, "  \"page_fault\": null\n");
        }

        ID3D12DeviceRemovedExtendedData_Release(dred);
    } else {
        fprintf(f, "  \"auto_breadcrumbs\": null,\n");
        fprintf(f, "  \"page_fault\": null\n");
        DIAG("DRED interface not available — no GPU telemetry captured");
    }

    fprintf(f, "}\n");
    fclose(f);

    DIAG_FMT("DRED report written to: %s", dred_path);
}

#endif /* _WIN32 */

