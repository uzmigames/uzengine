/*
 * rhi_dx12.c -- DirectX 12 backend: state, slot allocators, converters,
 *              resources (buffer / texture / pipeline), fences, and vtable.
 *
 * Split per FILE-SIZE-LIMIT (CS-21). Companion files:
 *   rhi_dx12_init.c      -- init / shutdown / resize
 *   rhi_dx12_commands.c  -- command recording, barriers, swapchain, debug
 *   rhi_dx12_internal.h  -- shared declarations
 *
 * See docs/specs/rendering/rhi.md for the full specification.
 */

#ifdef _WIN32

#include "rhi_dx12_internal.h"
#include "engine/core/diag.h"

/* Backend state (shared across TUs via rhi_dx12_internal.h) */
Dx12State s_dx12;
/* =======================================================================
 * Slot allocators
 *
 * Simple linear scan for the next free slot.  Slot 0 is reserved
 * (RHI_NULL).  This is O(N) worst-case but N is bounded and small for
 * Phase 1.  Phase 2 can add a free-list.
 * ======================================================================= */

uint64_t dx12_alloc_texture_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_RESOURCES; i++) {
        if (!s_dx12.textures[i].occupied) {
            s_dx12.textures[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Texture slot pool exhausted");
    return RHI_NULL;
}

uint64_t dx12_alloc_buffer_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_BUFFERS; i++) {
        if (!s_dx12.buffers[i].occupied) {
            s_dx12.buffers[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Buffer slot pool exhausted");
    return RHI_NULL;
}

uint64_t dx12_alloc_pipeline_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_PIPELINES; i++) {
        if (!s_dx12.pipelines[i].occupied) {
            s_dx12.pipelines[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Pipeline slot pool exhausted");
    return RHI_NULL;
}

uint64_t dx12_alloc_fence_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_FENCES; i++) {
        if (!s_dx12.fences[i].occupied) {
            s_dx12.fences[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Fence slot pool exhausted");
    return RHI_NULL;
}

uint64_t dx12_alloc_cmd_list_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_CMD_LISTS; i++) {
        if (!s_dx12.cmd_lists[i].occupied) {
            s_dx12.cmd_lists[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Command list slot pool exhausted");
    return RHI_NULL;
}

/* =======================================================================
 * Format conversion: RhiFormat -> DXGI_FORMAT
 * ======================================================================= */

DXGI_FORMAT to_dxgi_format(RhiFormat fmt) {
    switch (fmt) {
    case RHI_FORMAT_RGBA8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHI_FORMAT_RGBA8_SRGB:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHI_FORMAT_RGBA16_FLOAT:     return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHI_FORMAT_R32_FLOAT:        return DXGI_FORMAT_R32_FLOAT;
    case RHI_FORMAT_RG32_FLOAT:       return DXGI_FORMAT_R32G32_FLOAT;
    case RHI_FORMAT_RGBA32_FLOAT:     return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHI_FORMAT_R8_UNORM:         return DXGI_FORMAT_R8_UNORM;
    case RHI_FORMAT_R16_FLOAT:        return DXGI_FORMAT_R16_FLOAT;
    case RHI_FORMAT_RG16_FLOAT:       return DXGI_FORMAT_R16G16_FLOAT;
    case RHI_FORMAT_R11G11B10_FLOAT:  return DXGI_FORMAT_R11G11B10_FLOAT;
    case RHI_FORMAT_RGB10A2_UNORM:    return DXGI_FORMAT_R10G10B10A2_UNORM;
    case RHI_FORMAT_D32_FLOAT:        return DXGI_FORMAT_D32_FLOAT;
    case RHI_FORMAT_D24_UNORM_S8_UINT:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHI_FORMAT_BC7_UNORM:        return DXGI_FORMAT_BC7_UNORM;
    case RHI_FORMAT_BC7_SRGB:         return DXGI_FORMAT_BC7_UNORM_SRGB;
    case RHI_FORMAT_BC5_UNORM:        return DXGI_FORMAT_BC5_UNORM;
    case RHI_FORMAT_BC6H_UF16:        return DXGI_FORMAT_BC6H_UF16;
    case RHI_FORMAT_R32_UINT:         return DXGI_FORMAT_R32_UINT;
    case RHI_FORMAT_R16_UINT:         return DXGI_FORMAT_R16_UINT;
    default:                          return DXGI_FORMAT_UNKNOWN;
    }
}

/* =======================================================================
 * State conversion: RhiResourceState -> D3D12_RESOURCE_STATES
 * ======================================================================= */

D3D12_RESOURCE_STATES to_dx12_state(RhiResourceState s) {
    switch (s) {
    case RHI_STATE_UNDEFINED:
        return D3D12_RESOURCE_STATE_COMMON;
    case RHI_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case RHI_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHI_STATE_DEPTH_WRITE:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHI_STATE_DEPTH_READ:
        return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RHI_STATE_SHADER_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
             | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case RHI_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RHI_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHI_STATE_COPY_DST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHI_STATE_PRESENT:
        return D3D12_RESOURCE_STATE_PRESENT;
    case RHI_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case RHI_STATE_VERTEX_AND_CONSTANT_BUFFER:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case RHI_STATE_INDEX_BUFFER:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

/* =======================================================================
 * Topology conversion: RhiTopology -> D3D12_PRIMITIVE_TOPOLOGY_TYPE / D3D_PRIMITIVE_TOPOLOGY
 * ======================================================================= */

D3D12_PRIMITIVE_TOPOLOGY_TYPE to_dx12_topology_type(RhiTopology t) {
    switch (t) {
    case RHI_TOPOLOGY_TRIANGLE_LIST:
    case RHI_TOPOLOGY_TRIANGLE_STRIP:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case RHI_TOPOLOGY_LINE_LIST:
    case RHI_TOPOLOGY_LINE_STRIP:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case RHI_TOPOLOGY_POINT_LIST:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    default:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

D3D_PRIMITIVE_TOPOLOGY to_dx12_topology(RhiTopology t) {
    switch (t) {
    case RHI_TOPOLOGY_TRIANGLE_LIST:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case RHI_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RHI_TOPOLOGY_LINE_LIST:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHI_TOPOLOGY_LINE_STRIP:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHI_TOPOLOGY_POINT_LIST:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    default:                          return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

/* =======================================================================
 * Blend / Compare / Fill / Cull conversion
 * ======================================================================= */

D3D12_BLEND to_dx12_blend(RhiBlendFactor f) {
    switch (f) {
    case RHI_BLEND_ZERO:           return D3D12_BLEND_ZERO;
    case RHI_BLEND_ONE:            return D3D12_BLEND_ONE;
    case RHI_BLEND_SRC_ALPHA:      return D3D12_BLEND_SRC_ALPHA;
    case RHI_BLEND_INV_SRC_ALPHA:  return D3D12_BLEND_INV_SRC_ALPHA;
    case RHI_BLEND_DST_ALPHA:      return D3D12_BLEND_DEST_ALPHA;
    case RHI_BLEND_INV_DST_ALPHA:  return D3D12_BLEND_INV_DEST_ALPHA;
    case RHI_BLEND_SRC_COLOR:      return D3D12_BLEND_SRC_COLOR;
    case RHI_BLEND_INV_SRC_COLOR:  return D3D12_BLEND_INV_SRC_COLOR;
    case RHI_BLEND_DST_COLOR:      return D3D12_BLEND_DEST_COLOR;
    case RHI_BLEND_INV_DST_COLOR:  return D3D12_BLEND_INV_DEST_COLOR;
    default:                       return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP to_dx12_blend_op(RhiBlendOp op) {
    switch (op) {
    case RHI_BLEND_OP_ADD:          return D3D12_BLEND_OP_ADD;
    case RHI_BLEND_OP_SUBTRACT:     return D3D12_BLEND_OP_SUBTRACT;
    case RHI_BLEND_OP_REV_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
    case RHI_BLEND_OP_MIN:          return D3D12_BLEND_OP_MIN;
    case RHI_BLEND_OP_MAX:          return D3D12_BLEND_OP_MAX;
    default:                        return D3D12_BLEND_OP_ADD;
    }
}

D3D12_COMPARISON_FUNC to_dx12_compare(RhiCompareFunc f) {
    switch (f) {
    case RHI_COMPARE_NEVER:         return D3D12_COMPARISON_FUNC_NEVER;
    case RHI_COMPARE_LESS:          return D3D12_COMPARISON_FUNC_LESS;
    case RHI_COMPARE_EQUAL:         return D3D12_COMPARISON_FUNC_EQUAL;
    case RHI_COMPARE_LESS_EQUAL:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case RHI_COMPARE_GREATER:       return D3D12_COMPARISON_FUNC_GREATER;
    case RHI_COMPARE_NOT_EQUAL:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case RHI_COMPARE_GREATER_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case RHI_COMPARE_ALWAYS:        return D3D12_COMPARISON_FUNC_ALWAYS;
    default:                        return D3D12_COMPARISON_FUNC_LESS;
    }
}

D3D12_FILL_MODE to_dx12_fill(RhiFillMode m) {
    switch (m) {
    case RHI_FILL_SOLID:     return D3D12_FILL_MODE_SOLID;
    case RHI_FILL_WIREFRAME: return D3D12_FILL_MODE_WIREFRAME;
    default:                 return D3D12_FILL_MODE_SOLID;
    }
}

D3D12_CULL_MODE to_dx12_cull(RhiCullMode m) {
    switch (m) {
    case RHI_CULL_NONE:  return D3D12_CULL_MODE_NONE;
    case RHI_CULL_FRONT: return D3D12_CULL_MODE_FRONT;
    case RHI_CULL_BACK:  return D3D12_CULL_MODE_BACK;
    default:             return D3D12_CULL_MODE_NONE;
    }
}

D3D12_STENCIL_OP to_dx12_stencil_op(RhiStencilOp op) {
    switch (op) {
    case RHI_STENCIL_OP_KEEP:      return D3D12_STENCIL_OP_KEEP;
    case RHI_STENCIL_OP_ZERO:      return D3D12_STENCIL_OP_ZERO;
    case RHI_STENCIL_OP_REPLACE:   return D3D12_STENCIL_OP_REPLACE;
    case RHI_STENCIL_OP_INCR_SAT:  return D3D12_STENCIL_OP_INCR_SAT;
    case RHI_STENCIL_OP_DECR_SAT:  return D3D12_STENCIL_OP_DECR_SAT;
    case RHI_STENCIL_OP_INVERT:    return D3D12_STENCIL_OP_INVERT;
    default:                       return D3D12_STENCIL_OP_KEEP;
    }
}

/* =======================================================================
 * Vertex format conversion
 * ======================================================================= */

DXGI_FORMAT to_dx12_vertex_format(RhiVertexFormat f) {
    switch (f) {
    case RHI_VERTEX_FORMAT_FLOAT1:   return DXGI_FORMAT_R32_FLOAT;
    case RHI_VERTEX_FORMAT_FLOAT2:   return DXGI_FORMAT_R32G32_FLOAT;
    case RHI_VERTEX_FORMAT_FLOAT3:   return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHI_VERTEX_FORMAT_FLOAT4:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHI_VERTEX_FORMAT_UINT1:    return DXGI_FORMAT_R32_UINT;
    case RHI_VERTEX_FORMAT_UINT2:    return DXGI_FORMAT_R32G32_UINT;
    case RHI_VERTEX_FORMAT_UINT4:    return DXGI_FORMAT_R32G32B32A32_UINT;
    case RHI_VERTEX_FORMAT_SINT1:    return DXGI_FORMAT_R32_SINT;
    case RHI_VERTEX_FORMAT_SINT2:    return DXGI_FORMAT_R32G32_SINT;
    case RHI_VERTEX_FORMAT_SINT4:    return DXGI_FORMAT_R32G32B32A32_SINT;
    case RHI_VERTEX_FORMAT_UNORM8X4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:                         return DXGI_FORMAT_UNKNOWN;
    }
}

/* =======================================================================
 * Helper: wait for GPU to drain
 * ======================================================================= */

void dx12_wait_for_gpu(void) {
    if (!s_dx12.cmd_queue || !s_dx12.frame_fence) return;

    s_dx12.frame_fence_value++;
    ID3D12CommandQueue_Signal(s_dx12.cmd_queue, s_dx12.frame_fence,
                             s_dx12.frame_fence_value);

    if (ID3D12Fence_GetCompletedValue(s_dx12.frame_fence)
        < s_dx12.frame_fence_value) {
        ID3D12Fence_SetEventOnCompletion(s_dx12.frame_fence,
                                         s_dx12.frame_fence_value,
                                         s_dx12.frame_fence_event);
        WaitForSingleObject(s_dx12.frame_fence_event, INFINITE);
    }
}

/* =======================================================================
 * Helper: create swapchain backbuffer texture slots
 * ======================================================================= */

void dx12_acquire_backbuffers(void) {
    for (uint32_t i = 0; i < DX12_BACKBUFFER_COUNT; i++) {
        uint64_t slot = dx12_alloc_texture_slot();
        assert(slot != RHI_NULL);
        s_dx12.backbuffer_indices[i] = (uint32_t)slot;

        ID3D12Resource *bb = NULL;
        HRESULT hr = IDXGISwapChain3_GetBuffer(
            s_dx12.swap_chain, i, &IID_ID3D12Resource, (void **)&bb);
        assert(SUCCEEDED(hr));
        (void)hr;

        s_dx12.textures[slot].resource     = bb;
        s_dx12.textures[slot].is_swapchain = true;

        /* Create RTV for this backbuffer */
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.rtv_heap, &rtv_handle);
        rtv_handle.ptr += (SIZE_T)slot * s_dx12.rtv_desc_size;
        ID3D12Device_CreateRenderTargetView(s_dx12.device, bb, NULL,
                                            rtv_handle);
    }
}

void dx12_release_backbuffers(void) {
    for (uint32_t i = 0; i < DX12_BACKBUFFER_COUNT; i++) {
        uint32_t slot = s_dx12.backbuffer_indices[i];
        if (slot != 0 && s_dx12.textures[slot].resource) {
            ID3D12Resource_Release(s_dx12.textures[slot].resource);
            memset(&s_dx12.textures[slot], 0, sizeof(Dx12TextureSlot));
        }
        s_dx12.backbuffer_indices[i] = 0;
    }
}


/* =======================================================================
 * Resources: Buffer
 * ======================================================================= */

RhiBuffer dx12_buffer_create(const RhiBufferDesc *desc) {
    assert(s_dx12.initialized && desc);

    uint64_t slot = dx12_alloc_buffer_slot();
    if (slot == RHI_NULL) return RHI_NULL;

    D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;

    if (desc->usage & RHI_BUFFER_USAGE_UPLOAD) {
        heap_type     = D3D12_HEAP_TYPE_UPLOAD;
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (desc->usage & RHI_BUFFER_USAGE_READBACK) {
        heap_type     = D3D12_HEAP_TYPE_READBACK;
        initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof(hp));
    hp.Type = heap_type;

    /* Constant buffers must be 256-byte aligned */
    uint64_t aligned_size = desc->size;
    if (desc->usage & RHI_BUFFER_USAGE_CONSTANT) {
        aligned_size = (aligned_size + 255) & ~(uint64_t)255;
    }

    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Alignment          = 0;
    rd.Width              = aligned_size;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.SampleDesc.Quality = 0;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

    if (desc->usage & RHI_BUFFER_USAGE_STORAGE) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    ID3D12Resource *resource = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(
        s_dx12.device, &hp, D3D12_HEAP_FLAG_NONE,
        &rd, initial_state, NULL,
        &IID_ID3D12Resource, (void **)&resource);

    if (FAILED(hr)) {
        s_dx12.buffers[slot].occupied = false;
        return RHI_NULL;
    }

    s_dx12.buffers[slot].resource = resource;
    s_dx12.buffers[slot].size     = aligned_size;
    s_dx12.buffers[slot].usage    = desc->usage;

    if (desc->debug_name) {
        /* Convert to wide string for SetName */
        wchar_t wname[256];
        MultiByteToWideChar(CP_UTF8, 0, desc->debug_name, -1, wname, 256);
        ID3D12Resource_SetName(resource, wname);
    }

    return (RhiBuffer)slot;
}

void dx12_buffer_destroy(RhiBuffer buf) {
    if (buf == RHI_NULL) return;
    uint32_t slot = (uint32_t)buf;
    assert(slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied);

    if (s_dx12.buffers[slot].resource) {
        ID3D12Resource_Release(s_dx12.buffers[slot].resource);
    }
    memset(&s_dx12.buffers[slot], 0, sizeof(Dx12BufferSlot));
}

void *dx12_buffer_map(RhiBuffer buf) {
    if (buf == RHI_NULL) return NULL;
    uint32_t slot = (uint32_t)buf;
    assert(slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied);

    /* Only upload and readback buffers can be mapped */
    assert(s_dx12.buffers[slot].usage &
           (RHI_BUFFER_USAGE_UPLOAD | RHI_BUFFER_USAGE_READBACK));

    if (s_dx12.buffers[slot].mapped_ptr) {
        return s_dx12.buffers[slot].mapped_ptr; /* Already mapped */
    }

    void *ptr = NULL;
    D3D12_RANGE read_range;
    memset(&read_range, 0, sizeof(read_range));

    if (s_dx12.buffers[slot].usage & RHI_BUFFER_USAGE_UPLOAD) {
        /* Upload buffers: no CPU read range needed */
        read_range.Begin = 0;
        read_range.End   = 0;
    } else {
        /* Readback buffers: full read range */
        read_range.Begin = 0;
        read_range.End   = (SIZE_T)s_dx12.buffers[slot].size;
    }

    HRESULT hr = ID3D12Resource_Map(s_dx12.buffers[slot].resource,
                                    0, &read_range, &ptr);
    if (FAILED(hr)) return NULL;

    s_dx12.buffers[slot].mapped_ptr = ptr;
    return ptr;
}

void dx12_buffer_unmap(RhiBuffer buf) {
    if (buf == RHI_NULL) return;
    uint32_t slot = (uint32_t)buf;
    assert(slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied);

    if (!s_dx12.buffers[slot].mapped_ptr) return;

    D3D12_RANGE written_range;
    memset(&written_range, 0, sizeof(written_range));

    if (s_dx12.buffers[slot].usage & RHI_BUFFER_USAGE_UPLOAD) {
        written_range.Begin = 0;
        written_range.End   = (SIZE_T)s_dx12.buffers[slot].size;
    }
    /* Readback buffers: no CPU write range */

    ID3D12Resource_Unmap(s_dx12.buffers[slot].resource, 0, &written_range);
    s_dx12.buffers[slot].mapped_ptr = NULL;
}

void dx12_buffer_upload(RhiBuffer buf, const void *data, size_t size) {
    if (buf == RHI_NULL || !data || size == 0) return;
    uint32_t slot = (uint32_t)buf;
    assert(slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied);

    /*
     * For upload heap buffers, map and memcpy directly.
     * For default heap buffers, we need a staging buffer + copy command.
     * Phase 1: Only support direct upload to upload-heap buffers.
     * TODO Phase 2: Add staging buffer path for default-heap buffers.
     */
    if (s_dx12.buffers[slot].usage & RHI_BUFFER_USAGE_UPLOAD) {
        void *ptr = dx12_buffer_map(buf);
        if (ptr) {
            memcpy(ptr, data, size);
            dx12_buffer_unmap(buf);
        }
    } else {
        /* Default heap: create temp upload buffer, copy, wait */
        RhiBufferDesc staging_desc;
        memset(&staging_desc, 0, sizeof(staging_desc));
        staging_desc.size  = size;
        staging_desc.usage = RHI_BUFFER_USAGE_UPLOAD;
        RhiBuffer staging = dx12_buffer_create(&staging_desc);
        if (staging == RHI_NULL) return;

        void *ptr = dx12_buffer_map(staging);
        if (ptr) {
            memcpy(ptr, data, size);
            dx12_buffer_unmap(staging);
        }

        /* Create a one-shot command list for the copy */
        ID3D12CommandAllocator *alloc = NULL;
        ID3D12GraphicsCommandList *cmd = NULL;
        HRESULT hr;

        hr = ID3D12Device_CreateCommandAllocator(
            s_dx12.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&alloc);
        if (FAILED(hr)) { dx12_buffer_destroy(staging); return; }

        hr = ID3D12Device_CreateCommandList(
            s_dx12.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&cmd);
        if (FAILED(hr)) {
            ID3D12CommandAllocator_Release(alloc);
            dx12_buffer_destroy(staging);
            return;
        }

        uint32_t staging_slot = (uint32_t)staging;
        ID3D12GraphicsCommandList_CopyBufferRegion(
            cmd,
            s_dx12.buffers[slot].resource, 0,
            s_dx12.buffers[staging_slot].resource, 0,
            (UINT64)size);

        ID3D12GraphicsCommandList_Close(cmd);
        ID3D12CommandList *lists[] = { (ID3D12CommandList *)cmd };
        ID3D12CommandQueue_ExecuteCommandLists(s_dx12.cmd_queue, 1, lists);

        dx12_wait_for_gpu();

        ID3D12GraphicsCommandList_Release(cmd);
        ID3D12CommandAllocator_Release(alloc);
        dx12_buffer_destroy(staging);
    }
}

/* =======================================================================
 * Resources: Texture
 * ======================================================================= */

RhiTexture dx12_texture_create(const RhiTextureDesc *desc) {
    assert(s_dx12.initialized && desc);

    uint64_t slot = dx12_alloc_texture_slot();
    if (slot == RHI_NULL) return RHI_NULL;

    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.Format             = to_dxgi_format(desc->format);
    rd.Width              = desc->width;
    rd.Height             = desc->height;
    rd.MipLevels          = (UINT16)(desc->mip_levels ? desc->mip_levels : 1);
    rd.SampleDesc.Count   = 1;
    rd.SampleDesc.Quality = 0;

    switch (desc->dimension) {
    case RHI_TEXTURE_DIM_3D:
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        rd.DepthOrArraySize = (UINT16)(desc->depth ? desc->depth : 1);
        break;
    case RHI_TEXTURE_DIM_CUBE:
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.DepthOrArraySize = 6;
        break;
    case RHI_TEXTURE_DIM_2D_ARRAY:
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.DepthOrArraySize = (UINT16)(desc->array_size ? desc->array_size : 1);
        break;
    default: /* 2D */
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.DepthOrArraySize = 1;
        break;
    }

    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags  = D3D12_RESOURCE_FLAG_NONE;

    if (desc->usage & RHI_TEXTURE_USAGE_RENDER_TARGET) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (desc->usage & RHI_TEXTURE_USAGE_DEPTH_STENCIL) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (desc->usage & RHI_TEXTURE_USAGE_UNORDERED) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES hp;
    memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;

    /* Set up optimized clear value for RT / DS textures */
    D3D12_CLEAR_VALUE clear_val;
    D3D12_CLEAR_VALUE *p_clear = NULL;
    memset(&clear_val, 0, sizeof(clear_val));

    if (desc->usage & RHI_TEXTURE_USAGE_RENDER_TARGET) {
        clear_val.Format   = rd.Format;
        clear_val.Color[0] = 0.0f;
        clear_val.Color[1] = 0.0f;
        clear_val.Color[2] = 0.0f;
        clear_val.Color[3] = 1.0f;
        p_clear = &clear_val;
    } else if (desc->usage & RHI_TEXTURE_USAGE_DEPTH_STENCIL) {
        clear_val.Format               = rd.Format;
        clear_val.DepthStencil.Depth   = 1.0f;
        clear_val.DepthStencil.Stencil = 0;
        p_clear = &clear_val;
        initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    ID3D12Resource *resource = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(
        s_dx12.device, &hp, D3D12_HEAP_FLAG_NONE,
        &rd, initial_state, p_clear,
        &IID_ID3D12Resource, (void **)&resource);

    if (FAILED(hr)) {
        s_dx12.textures[slot].occupied = false;
        return RHI_NULL;
    }

    s_dx12.textures[slot].resource = resource;

    /* Create RTV if render target */
    if (desc->usage & RHI_TEXTURE_USAGE_RENDER_TARGET) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.rtv_heap, &rtv);
        rtv.ptr += (SIZE_T)slot * s_dx12.rtv_desc_size;
        ID3D12Device_CreateRenderTargetView(s_dx12.device, resource, NULL, rtv);
    }

    /* Create DSV if depth-stencil */
    if (desc->usage & RHI_TEXTURE_USAGE_DEPTH_STENCIL) {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.dsv_heap, &dsv);
        dsv.ptr += (SIZE_T)slot * s_dx12.dsv_desc_size;
        ID3D12Device_CreateDepthStencilView(s_dx12.device, resource, NULL, dsv);
    }

    /* Create SRV if shader-readable */
    if (desc->usage & RHI_TEXTURE_USAGE_SHADER_READ) {
        D3D12_CPU_DESCRIPTOR_HANDLE srv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.cbv_srv_uav_heap, &srv);
        srv.ptr += (SIZE_T)slot * s_dx12.cbv_srv_uav_desc_size;

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format                  = rd.Format;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        switch (desc->dimension) {
        case RHI_TEXTURE_DIM_CUBE:
            srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv_desc.TextureCube.MipLevels     = rd.MipLevels;
            break;
        case RHI_TEXTURE_DIM_3D:
            srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv_desc.Texture3D.MipLevels       = rd.MipLevels;
            break;
        case RHI_TEXTURE_DIM_2D_ARRAY:
            srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray.MipLevels  = rd.MipLevels;
            srv_desc.Texture2DArray.ArraySize  = rd.DepthOrArraySize;
            break;
        default:
            srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels       = rd.MipLevels;
            break;
        }

        ID3D12Device_CreateShaderResourceView(s_dx12.device, resource,
                                              &srv_desc, srv);
    }

    if (desc->debug_name) {
        wchar_t wname[256];
        MultiByteToWideChar(CP_UTF8, 0, desc->debug_name, -1, wname, 256);
        ID3D12Resource_SetName(resource, wname);
    }

    return (RhiTexture)slot;
}

void dx12_texture_destroy(RhiTexture tex) {
    if (tex == RHI_NULL) return;
    uint32_t slot = (uint32_t)tex;
    assert(slot < DX12_MAX_RESOURCES && s_dx12.textures[slot].occupied);

    if (s_dx12.textures[slot].is_swapchain) {
        /* Swapchain backbuffers are managed by dx12_release_backbuffers() */
        return;
    }

    if (s_dx12.textures[slot].resource) {
        ID3D12Resource_Release(s_dx12.textures[slot].resource);
    }
    memset(&s_dx12.textures[slot], 0, sizeof(Dx12TextureSlot));
}

void dx12_texture_upload(RhiTexture tex, const void *data, size_t size) {
    if (tex == RHI_NULL || !data || size == 0) return;
    uint32_t slot = (uint32_t)tex;
    assert(slot < DX12_MAX_RESOURCES && s_dx12.textures[slot].occupied);

    /*
     * Phase 1: Simple single-subresource upload via staging buffer.
     * Assumes tightly packed row data for subresource 0.
     * TODO Phase 2: Multi-mip, multi-array-slice upload.
     */
    D3D12_RESOURCE_DESC rd;
    memset(&rd, 0, sizeof(rd));
    ID3D12Resource_GetDesc(s_dx12.textures[slot].resource, &rd);

    /* Get required upload size */
    UINT64 required_size = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT num_rows = 0;
    UINT64 row_size = 0;
    ID3D12Device_GetCopyableFootprints(s_dx12.device, &rd, 0, 1, 0,
                                       &layout, &num_rows, &row_size,
                                       &required_size);

    /* Create staging upload buffer */
    RhiBufferDesc staging_desc;
    memset(&staging_desc, 0, sizeof(staging_desc));
    staging_desc.size  = required_size;
    staging_desc.usage = RHI_BUFFER_USAGE_UPLOAD;
    RhiBuffer staging = dx12_buffer_create(&staging_desc);
    if (staging == RHI_NULL) return;

    /* Map and copy row by row (handles pitch mismatch) */
    uint8_t *mapped = (uint8_t *)dx12_buffer_map(staging);
    if (mapped) {
        const uint8_t *src = (const uint8_t *)data;
        uint8_t *dst = mapped + layout.Offset;
        size_t src_row_pitch = (size_t)row_size;
        /* Clamp to provided data size */
        for (UINT row = 0; row < num_rows; row++) {
            size_t copy_size = src_row_pitch;
            size_t src_offset = (size_t)row * src_row_pitch;
            if (src_offset + copy_size > size) break;
            memcpy(dst + (size_t)row * layout.Footprint.RowPitch,
                   src + src_offset, copy_size);
        }
        dx12_buffer_unmap(staging);
    }

    /* Copy staging -> texture via command list */
    ID3D12CommandAllocator *alloc = NULL;
    ID3D12GraphicsCommandList *cmd = NULL;

    ID3D12Device_CreateCommandAllocator(
        s_dx12.device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(
        s_dx12.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&cmd);

    if (cmd && alloc) {
        uint32_t staging_slot = (uint32_t)staging;

        D3D12_TEXTURE_COPY_LOCATION dst_loc;
        memset(&dst_loc, 0, sizeof(dst_loc));
        dst_loc.pResource        = s_dx12.textures[slot].resource;
        dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src_loc;
        memset(&src_loc, 0, sizeof(src_loc));
        src_loc.pResource       = s_dx12.buffers[staging_slot].resource;
        src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = layout;

        /* Transition to COPY_DEST */
        D3D12_RESOURCE_BARRIER barrier;
        memset(&barrier, 0, sizeof(barrier));
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = s_dx12.textures[slot].resource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ID3D12GraphicsCommandList_ResourceBarrier(cmd, 1, &barrier);

        ID3D12GraphicsCommandList_CopyTextureRegion(cmd, &dst_loc,
                                                     0, 0, 0,
                                                     &src_loc, NULL);

        /* Transition back to COMMON */
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
        ID3D12GraphicsCommandList_ResourceBarrier(cmd, 1, &barrier);

        ID3D12GraphicsCommandList_Close(cmd);
        ID3D12CommandList *lists[] = { (ID3D12CommandList *)cmd };
        ID3D12CommandQueue_ExecuteCommandLists(s_dx12.cmd_queue, 1, lists);
        dx12_wait_for_gpu();
    }

    if (cmd)   ID3D12GraphicsCommandList_Release(cmd);
    if (alloc) ID3D12CommandAllocator_Release(alloc);
    dx12_buffer_destroy(staging);
}

/* =======================================================================
 * Pipelines
 * ======================================================================= */

RhiPipeline dx12_pipeline_create(const RhiPipelineDesc *desc) {
    assert(s_dx12.initialized && desc);
    DIAG("dx12_pipeline_create START");

    /* Check PSO cache first */
    RhiPsoCacheKey cache_key = rhi_make_pso_cache_key(desc);
    RhiPipeline cached = rhi_pso_cache_find(&cache_key);
    if (cached != RHI_NULL) {
        return cached;
    }

    uint64_t slot = dx12_alloc_pipeline_slot();
    if (slot == RHI_NULL) return RHI_NULL;

    HRESULT hr;

    /*
     * Phase 1: Compile shaders from HLSL source via D3DCompile.
     * The spirv_data field temporarily contains HLSL source text.
     * Phase 2 will replace this with SPIR-V -> DXIL via DXC.
     */
    ID3DBlob *vs_blob = NULL;
    ID3DBlob *ps_blob = NULL;
    ID3DBlob *err_blob = NULL;

    if (desc->vertex_shader.spirv_data && desc->vertex_shader.spirv_size > 0) {
        hr = D3DCompile(
            desc->vertex_shader.spirv_data,
            desc->vertex_shader.spirv_size,
            NULL, NULL, NULL,
            desc->vertex_shader.entry_point ? desc->vertex_shader.entry_point : "VSMain",
            "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &vs_blob, &err_blob);
        if (FAILED(hr)) {
            if (err_blob) {
                DIAG_FMT("VS compile error: %s", (const char *)ID3D10Blob_GetBufferPointer(err_blob));
                OutputDebugStringA((const char *)ID3D10Blob_GetBufferPointer(err_blob));
                ID3D10Blob_Release(err_blob);
            }
            DIAG("FATAL: VS D3DCompile failed");
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
        DIAG("VS compiled OK");
        if (err_blob) ID3D10Blob_Release(err_blob);
        err_blob = NULL;
    }

    if (desc->pixel_shader.spirv_data && desc->pixel_shader.spirv_size > 0) {
        hr = D3DCompile(
            desc->pixel_shader.spirv_data,
            desc->pixel_shader.spirv_size,
            NULL, NULL, NULL,
            desc->pixel_shader.entry_point ? desc->pixel_shader.entry_point : "PSMain",
            "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &ps_blob, &err_blob);
        if (FAILED(hr)) {
            if (err_blob) {
                DIAG_FMT("PS compile error: %s", (const char *)ID3D10Blob_GetBufferPointer(err_blob));
                OutputDebugStringA((const char *)ID3D10Blob_GetBufferPointer(err_blob));
                ID3D10Blob_Release(err_blob);
            }
            DIAG("FATAL: PS D3DCompile failed");
            if (vs_blob) ID3D10Blob_Release(vs_blob);
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
        DIAG("PS compiled OK");
        if (err_blob) ID3D10Blob_Release(err_blob);
        err_blob = NULL;
    }

    /*
     * Root signature with root constants matching the shader cbuffers:
     *   Param 0: 16 x 32-bit constants at b0 (Object: world matrix)
     *   Param 1:  8 x 32-bit constants at b1 (Material: albedo, metallic, rough, ao)
     *   Param 2: 16 x 32-bit constants at b2 (Light + Camera: dir, color, intensity, camPos, time)
     *   Param 3: 16 x 32-bit constants at b3 (Frame: viewProj matrix)
     *   Param 4: SRV descriptor table for t0 (Albedo)
     *   Param 5: SRV descriptor table for t1 (Normal)
     *   Param 6: SRV descriptor table for t2 (Metallic)
     *   Param 7: SRV descriptor table for t3 (Roughness)
     *   Param 8: SRV descriptor table for t4 (AO)
     *   Static sampler at s0
     */
    ID3D12RootSignature *root_sig = NULL;
    {
        D3D12_ROOT_PARAMETER root_params[9];
        memset(root_params, 0, sizeof(root_params));

        /* Param 0: b0 -- 16 DWORDs (World) */
        root_params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[0].Constants.ShaderRegister  = 0;
        root_params[0].Constants.RegisterSpace   = 0;
        root_params[0].Constants.Num32BitValues  = 16;
        root_params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 1: b1 -- 8 DWORDs (MaterialCB: albedo[3]+metallic+roughness+ao+pad[2]) */
        root_params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[1].Constants.ShaderRegister  = 1;
        root_params[1].Constants.RegisterSpace   = 0;
        root_params[1].Constants.Num32BitValues  = 8;
        root_params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 2: b2 -- 16 DWORDs (Light + Camera) */
        root_params[2].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[2].Constants.ShaderRegister  = 2;
        root_params[2].Constants.RegisterSpace   = 0;
        root_params[2].Constants.Num32BitValues  = 16;
        root_params[2].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 3: b3 -- 16 DWORDs (ViewProj) */
        root_params[3].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_params[3].Constants.ShaderRegister  = 3;
        root_params[3].Constants.RegisterSpace   = 0;
        root_params[3].Constants.Num32BitValues  = 16;
        root_params[3].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 4-8: Descriptor tables for t0..t4 */
        D3D12_DESCRIPTOR_RANGE srv_ranges[5];
        for (int i = 0; i < 5; i++) {
            memset(&srv_ranges[i], 0, sizeof(srv_ranges[i]));
            srv_ranges[i].RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srv_ranges[i].NumDescriptors     = 1;
            srv_ranges[i].BaseShaderRegister = (UINT)i;
            srv_ranges[i].RegisterSpace      = 0;
            srv_ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            root_params[4 + i].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            root_params[4 + i].DescriptorTable.NumDescriptorRanges = 1;
            root_params[4 + i].DescriptorTable.pDescriptorRanges   = &srv_ranges[i];
            root_params[4 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }

        D3D12_STATIC_SAMPLER_DESC static_sampler;
        memset(&static_sampler, 0, sizeof(static_sampler));
        static_sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        static_sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler.MipLODBias       = 0.0f;
        static_sampler.MaxAnisotropy    = 1;
        static_sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        static_sampler.BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        static_sampler.MinLOD           = 0.0f;
        static_sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        static_sampler.ShaderRegister   = 0;
        static_sampler.RegisterSpace    = 0;
        static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rs_desc;
        memset(&rs_desc, 0, sizeof(rs_desc));
        rs_desc.NumParameters     = 9;
        rs_desc.pParameters       = root_params;
        rs_desc.NumStaticSamplers = 1;
        rs_desc.pStaticSamplers   = &static_sampler;
        rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob *sig_blob = NULL;
        ID3DBlob *sig_err  = NULL;
        hr = D3D12SerializeRootSignature(&rs_desc,
                                         D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         &sig_blob, &sig_err);
        if (FAILED(hr)) {
            if (sig_err) {
                DIAG_FMT("Root sig serialize error: %s", (const char *)ID3D10Blob_GetBufferPointer(sig_err));
                ID3D10Blob_Release(sig_err);
            }
            if (vs_blob) ID3D10Blob_Release(vs_blob);
            if (ps_blob) ID3D10Blob_Release(ps_blob);
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
        DIAG("Root signature serialized OK");
        if (sig_err) ID3D10Blob_Release(sig_err);

        hr = ID3D12Device_CreateRootSignature(
            s_dx12.device, 0,
            ID3D10Blob_GetBufferPointer(sig_blob),
            ID3D10Blob_GetBufferSize(sig_blob),
            &IID_ID3D12RootSignature, (void **)&root_sig);
        ID3D10Blob_Release(sig_blob);
        DIAG_HR("CreateRootSignature", hr);
        if (FAILED(hr)) {
            if (vs_blob) ID3D10Blob_Release(vs_blob);
            if (ps_blob) ID3D10Blob_Release(ps_blob);
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
    }

    /* Build input layout from vertex layout desc */
    D3D12_INPUT_ELEMENT_DESC input_elems[RHI_MAX_VERTEX_ATTRIBS];
    memset(input_elems, 0, sizeof(input_elems));
    uint32_t input_count = desc->vertex_layout.attrib_count;
    if (input_count > RHI_MAX_VERTEX_ATTRIBS) {
        input_count = RHI_MAX_VERTEX_ATTRIBS;
    }
    for (uint32_t i = 0; i < input_count; i++) {
        const RhiVertexAttribute *attr = &desc->vertex_layout.attribs[i];
        input_elems[i].SemanticName      = attr->semantic_name;
        input_elems[i].SemanticIndex     = attr->semantic_index;
        input_elems[i].Format            = to_dx12_vertex_format(attr->format);
        input_elems[i].InputSlot         = attr->buffer_slot;
        input_elems[i].AlignedByteOffset = attr->offset;
        input_elems[i].InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        input_elems[i].InstanceDataStepRate = 0;
    }

    /* Build PSO descriptor */
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    memset(&pso_desc, 0, sizeof(pso_desc));

    pso_desc.pRootSignature = root_sig;

    if (vs_blob) {
        pso_desc.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs_blob);
        pso_desc.VS.BytecodeLength  = ID3D10Blob_GetBufferSize(vs_blob);
    }
    if (ps_blob) {
        pso_desc.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps_blob);
        pso_desc.PS.BytecodeLength  = ID3D10Blob_GetBufferSize(ps_blob);
    }

    pso_desc.InputLayout.pInputElementDescs = input_elems;
    pso_desc.InputLayout.NumElements        = input_count;

    /* Rasterizer state */
    pso_desc.RasterizerState.FillMode              = to_dx12_fill(desc->rasterizer.fill_mode);
    pso_desc.RasterizerState.CullMode              = to_dx12_cull(desc->rasterizer.cull_mode);
    pso_desc.RasterizerState.FrontCounterClockwise = desc->rasterizer.front_ccw ? TRUE : FALSE;
    pso_desc.RasterizerState.DepthBias             = desc->rasterizer.depth_bias;
    pso_desc.RasterizerState.DepthBiasClamp        = desc->rasterizer.depth_bias_clamp;
    pso_desc.RasterizerState.SlopeScaledDepthBias  = desc->rasterizer.slope_scaled_depth_bias;
    pso_desc.RasterizerState.DepthClipEnable       = TRUE;
    pso_desc.RasterizerState.MultisampleEnable     = FALSE;
    pso_desc.RasterizerState.AntialiasedLineEnable = FALSE;

    /* Blend state */
    pso_desc.BlendState.IndependentBlendEnable = desc->blend.independent_blend ? TRUE : FALSE;
    for (uint32_t i = 0; i < 8; i++) {
        const RhiRenderTargetBlend *src = &desc->blend.render_targets[i];
        D3D12_RENDER_TARGET_BLEND_DESC *dst = &pso_desc.BlendState.RenderTarget[i];
        dst->BlendEnable    = src->blend_enable ? TRUE : FALSE;
        dst->SrcBlend       = to_dx12_blend(src->src_blend);
        dst->DestBlend      = to_dx12_blend(src->dst_blend);
        dst->BlendOp        = to_dx12_blend_op(src->blend_op);
        dst->SrcBlendAlpha  = to_dx12_blend(src->src_blend_alpha);
        dst->DestBlendAlpha = to_dx12_blend(src->dst_blend_alpha);
        dst->BlendOpAlpha   = to_dx12_blend_op(src->blend_op_alpha);
        dst->RenderTargetWriteMask = src->write_mask ? src->write_mask : D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    /* Depth-stencil state */
    pso_desc.DepthStencilState.DepthEnable    = desc->depth_stencil.depth_enable ? TRUE : FALSE;
    pso_desc.DepthStencilState.DepthWriteMask = desc->depth_stencil.depth_write
        ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc      = to_dx12_compare(desc->depth_stencil.depth_func);

    /* Stencil state — apply sane defaults for zero-initialized structs */
    pso_desc.DepthStencilState.StencilEnable    = desc->depth_stencil.stencil_enable ? TRUE : FALSE;

    /* When masks are 0 (zero-init default), use D3D12 standard defaults */
    uint8_t rmask = desc->depth_stencil.stencil_read_mask;
    uint8_t wmask = desc->depth_stencil.stencil_write_mask;
    if (rmask == 0 && wmask == 0 && !desc->depth_stencil.stencil_enable) {
        rmask = 0xFF;
        wmask = 0xFF;
    }
    pso_desc.DepthStencilState.StencilReadMask  = rmask;
    pso_desc.DepthStencilState.StencilWriteMask = wmask;

    /* Default stencil ops: KEEP/KEEP/KEEP/ALWAYS when zero-initialized */
    D3D12_DEPTH_STENCILOP_DESC default_op;
    default_op.StencilFailOp      = D3D12_STENCIL_OP_KEEP;
    default_op.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    default_op.StencilPassOp      = D3D12_STENCIL_OP_KEEP;
    default_op.StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS;

    if (desc->depth_stencil.stencil_enable) {
        pso_desc.DepthStencilState.FrontFace.StencilFailOp      = to_dx12_stencil_op(desc->depth_stencil.front_face.fail_op);
        pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = to_dx12_stencil_op(desc->depth_stencil.front_face.depth_fail_op);
        pso_desc.DepthStencilState.FrontFace.StencilPassOp      = to_dx12_stencil_op(desc->depth_stencil.front_face.pass_op);
        pso_desc.DepthStencilState.FrontFace.StencilFunc        = to_dx12_compare(desc->depth_stencil.front_face.func);

        pso_desc.DepthStencilState.BackFace.StencilFailOp      = to_dx12_stencil_op(desc->depth_stencil.back_face.fail_op);
        pso_desc.DepthStencilState.BackFace.StencilDepthFailOp = to_dx12_stencil_op(desc->depth_stencil.back_face.depth_fail_op);
        pso_desc.DepthStencilState.BackFace.StencilPassOp      = to_dx12_stencil_op(desc->depth_stencil.back_face.pass_op);
        pso_desc.DepthStencilState.BackFace.StencilFunc        = to_dx12_compare(desc->depth_stencil.back_face.func);
    } else {
        pso_desc.DepthStencilState.FrontFace = default_op;
        pso_desc.DepthStencilState.BackFace  = default_op;
    }

    /* Sample mask */
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.SampleDesc.Count   = 1;
    pso_desc.SampleDesc.Quality = 0;

    /* Render targets */
    pso_desc.NumRenderTargets = desc->rt_count;
    for (uint32_t i = 0; i < desc->rt_count; i++) {
        pso_desc.RTVFormats[i] = to_dxgi_format(desc->rt_formats[i]);
    }
    pso_desc.DSVFormat = to_dxgi_format(desc->ds_format);

    /* Topology */
    pso_desc.PrimitiveTopologyType = to_dx12_topology_type(desc->topology);

    /* Create PSO */
    ID3D12PipelineState *pso = NULL;
    hr = ID3D12Device_CreateGraphicsPipelineState(
        s_dx12.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);

    /* Release shader blobs -- no longer needed */
    if (vs_blob) ID3D10Blob_Release(vs_blob);
    if (ps_blob) ID3D10Blob_Release(ps_blob);

    DIAG("CreateGraphicsPipelineState...");
    if (FAILED(hr)) {
        DIAG_HR("CreateGraphicsPipelineState", hr);
        ID3D12RootSignature_Release(root_sig);
        s_dx12.pipelines[slot].occupied = false;
        return RHI_NULL;
    }
    DIAG("PSO created OK");

    s_dx12.pipelines[slot].pso      = pso;
    s_dx12.pipelines[slot].root_sig = root_sig;

    /* Insert into PSO cache */
    rhi_pso_cache_insert(&cache_key, (RhiPipeline)slot);

    return (RhiPipeline)slot;
}

RhiPipeline dx12_compute_pipeline_create(const RhiComputePipelineDesc *desc) {
    assert(s_dx12.initialized && desc);

    /* TODO Phase 2: Implement compute pipeline creation.
     * Requires:
     *   - Compute-only root signature
     *   - D3D12_COMPUTE_PIPELINE_STATE_DESC
     *   - CS blob compilation (cs_5_0 / cs_6_0)
     */
    (void)desc;
    OutputDebugStringA("RHI DX12: compute_pipeline_create not yet implemented (Phase 2)\n");
    return RHI_NULL;
}

void dx12_pipeline_destroy(RhiPipeline pipeline) {
    if (pipeline == RHI_NULL) return;
    uint32_t slot = (uint32_t)pipeline;
    assert(slot < DX12_MAX_PIPELINES && s_dx12.pipelines[slot].occupied);

    if (s_dx12.pipelines[slot].pso)
        ID3D12PipelineState_Release(s_dx12.pipelines[slot].pso);
    if (s_dx12.pipelines[slot].root_sig)
        ID3D12RootSignature_Release(s_dx12.pipelines[slot].root_sig);
    memset(&s_dx12.pipelines[slot], 0, sizeof(Dx12PipelineSlot));
}


/* =======================================================================
 * Sync: Fences
 * ======================================================================= */

RhiFence dx12_fence_create(void) {
    assert(s_dx12.initialized);

    uint64_t slot = dx12_alloc_fence_slot();
    if (slot == RHI_NULL) return RHI_NULL;

    HRESULT hr = ID3D12Device_CreateFence(
        s_dx12.device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void **)&s_dx12.fences[slot].fence);
    if (FAILED(hr)) {
        s_dx12.fences[slot].occupied = false;
        return RHI_NULL;
    }

    s_dx12.fences[slot].event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!s_dx12.fences[slot].event) {
        ID3D12Fence_Release(s_dx12.fences[slot].fence);
        s_dx12.fences[slot].occupied = false;
        return RHI_NULL;
    }

    return (RhiFence)slot;
}

void dx12_fence_destroy(RhiFence fence) {
    if (fence == RHI_NULL) return;
    uint32_t slot = (uint32_t)fence;
    assert(slot < DX12_MAX_FENCES && s_dx12.fences[slot].occupied);

    if (s_dx12.fences[slot].fence) ID3D12Fence_Release(s_dx12.fences[slot].fence);
    if (s_dx12.fences[slot].event) CloseHandle(s_dx12.fences[slot].event);
    memset(&s_dx12.fences[slot], 0, sizeof(Dx12FenceSlot));
}

void dx12_fence_signal(RhiFence fence, uint64_t value) {
    if (fence == RHI_NULL) return;
    uint32_t slot = (uint32_t)fence;
    assert(slot < DX12_MAX_FENCES && s_dx12.fences[slot].occupied);

    ID3D12CommandQueue_Signal(s_dx12.cmd_queue,
                             s_dx12.fences[slot].fence, value);
}

bool dx12_fence_wait(RhiFence fence, uint64_t value,
                            uint64_t timeout_ns) {
    if (fence == RHI_NULL) return false;
    uint32_t slot = (uint32_t)fence;
    assert(slot < DX12_MAX_FENCES && s_dx12.fences[slot].occupied);

    if (ID3D12Fence_GetCompletedValue(s_dx12.fences[slot].fence) >= value) {
        return true; /* Already signaled */
    }

    ID3D12Fence_SetEventOnCompletion(s_dx12.fences[slot].fence, value,
                                     s_dx12.fences[slot].event);

    DWORD ms = (timeout_ns == UINT64_MAX) ? INFINITE
             : (DWORD)(timeout_ns / 1000000ULL);
    DWORD result = WaitForSingleObject(s_dx12.fences[slot].event, ms);
    return (result == WAIT_OBJECT_0);
}

/* =======================================================================
 * TEMPORARY bridge API (see rhi_dx12.h)
 * ======================================================================= */

ID3D12Device* rhi_dx12_get_device(void) {
    assert(s_dx12.initialized);
    return s_dx12.device;
}

ID3D12RootSignature* rhi_dx12_get_pipeline_root_sig(RhiPipeline pipeline) {
    if (pipeline == RHI_NULL) return NULL;
    uint32_t slot = (uint32_t)pipeline;
    assert(slot < DX12_MAX_PIPELINES && s_dx12.pipelines[slot].occupied);
    return s_dx12.pipelines[slot].root_sig;
}

ID3D12GraphicsCommandList* rhi_dx12_get_cmd_list(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return NULL;
    uint32_t slot = (uint32_t)cmd;
    assert(slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[slot].occupied);
    return s_dx12.cmd_lists[slot].list;
}

/* =======================================================================
 * GPU Memory Info (§6.3)
 * ======================================================================= */

bool dx12_gpu_memory_info(uint64_t *out_total, uint64_t *out_used,
                          uint64_t *out_budget) {
    if (!s_dx12.initialized || !s_dx12.factory) return false;

    IDXGIAdapter1 *adapter1 = NULL;
    HRESULT hr = IDXGIFactory6_EnumAdapters1(s_dx12.factory, 0, &adapter1);
    if (FAILED(hr) || !adapter1) return false;

    IDXGIAdapter3 *adapter3 = NULL;
    hr = IDXGIAdapter1_QueryInterface(
        adapter1, &IID_IDXGIAdapter3, (void **)&adapter3);
    IDXGIAdapter1_Release(adapter1);
    if (FAILED(hr) || !adapter3) return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO info;
    memset(&info, 0, sizeof(info));
    hr = IDXGIAdapter3_QueryVideoMemoryInfo(
        adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    IDXGIAdapter3_Release(adapter3);

    if (FAILED(hr)) return false;

    if (out_total)  *out_total  = info.Budget;
    if (out_used)   *out_used   = info.CurrentUsage;
    if (out_budget) *out_budget = info.Budget;
    return true;
}

/* =======================================================================
 * Backend vtable instance
 * ======================================================================= */

const rhi_backend_t rhi_dx12_backend = {
    /* Lifecycle */
    .init                    = dx12_init,
    .shutdown                = dx12_shutdown,
    .resize                  = dx12_resize,

    /* Resources: Buffer */
    .buffer_create           = dx12_buffer_create,
    .buffer_destroy          = dx12_buffer_destroy,
    .buffer_map              = dx12_buffer_map,
    .buffer_unmap            = dx12_buffer_unmap,
    .buffer_upload           = dx12_buffer_upload,

    /* Resources: Texture */
    .texture_create          = dx12_texture_create,
    .texture_destroy         = dx12_texture_destroy,
    .texture_upload          = dx12_texture_upload,

    /* Pipelines */
    .pipeline_create         = dx12_pipeline_create,
    .compute_pipeline_create = dx12_compute_pipeline_create,
    .pipeline_destroy        = dx12_pipeline_destroy,

    /* Command recording */
    .cmd_begin               = dx12_cmd_begin,
    .cmd_end                 = dx12_cmd_end,
    .cmd_submit              = dx12_cmd_submit,
    .cmd_set_pipeline        = dx12_cmd_set_pipeline,
    .cmd_set_topology        = dx12_cmd_set_topology,
    .cmd_set_render_targets  = dx12_cmd_set_render_targets,
    .cmd_set_viewport        = dx12_cmd_set_viewport,
    .cmd_set_scissor         = dx12_cmd_set_scissor,
    .cmd_set_vertex_buffer   = dx12_cmd_set_vertex_buffer,
    .cmd_set_index_buffer    = dx12_cmd_set_index_buffer,
    .cmd_set_constant_buffer = dx12_cmd_set_constant_buffer,
    .cmd_set_texture         = dx12_cmd_set_texture,
    .cmd_draw                = dx12_cmd_draw,
    .cmd_draw_indexed        = dx12_cmd_draw_indexed,
    .cmd_dispatch            = dx12_cmd_dispatch,

    /* Root constants */
    .cmd_set_root_constants  = dx12_cmd_set_root_constants,

    /* Copy */
    .cmd_copy_texture_to_buffer = dx12_cmd_copy_texture_to_buffer,

    /* Clear */
    .cmd_clear_render_target = dx12_cmd_clear_render_target,
    .cmd_clear_depth_stencil = dx12_cmd_clear_depth_stencil,

    /* Stencil */
    .cmd_set_stencil_ref     = dx12_cmd_set_stencil_ref,

    /* Barriers */
    .cmd_barrier_texture     = dx12_cmd_barrier_texture,
    .cmd_barrier_buffer      = dx12_cmd_barrier_buffer,

    /* Swapchain */
    .swapchain_backbuffer    = dx12_swapchain_backbuffer,
    .swapchain_present       = dx12_swapchain_present,

    /* Sync */
    .fence_create            = dx12_fence_create,
    .fence_destroy           = dx12_fence_destroy,
    .fence_signal            = dx12_fence_signal,
    .fence_wait              = dx12_fence_wait,

    /* Debug */
    .set_debug_name          = dx12_set_debug_name,
    .begin_event             = dx12_begin_event,
    .end_event               = dx12_end_event,

    /* GPU Timestamps — stubs (requires query heap infrastructure) */
    .timestamp_query_heap_create  = NULL,
    .timestamp_query_heap_destroy = NULL,
    .cmd_timestamp_write          = NULL,
    .timestamp_readback           = NULL,

    /* GPU Memory Info */
    .gpu_memory_info         = dx12_gpu_memory_info,
};

#endif /* _WIN32 */
