/*
 * rhi_dx12.c -- DirectX 12 backend implementation
 *
 * This file is the ONLY place in the entire engine that includes d3d12.h,
 * dxgi1_6.h, or d3dcompiler.h.  All DX12 types are encapsulated here.
 *
 * Phase 1 notes:
 *   - Shaders are compiled from HLSL source via D3DCompile at pipeline
 *     creation time.  The RhiShaderDesc.spirv_data field temporarily
 *     contains raw HLSL source (not SPIR-V).
 *   - Phase 2 will add SPIR-V -> DXIL via DXC for the offline path.
 *
 * See docs/specs/rendering/rhi.md for the full specification.
 */

#ifdef _WIN32

/* Must define before including windows headers */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef COBJMACROS
#define COBJMACROS  /* Enable C-style COM macros (ID3D12Device_CreateCommittedResource, etc.) */
#endif

#include "rhi.h"
#include "rhi_dx12.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Link against required libraries */
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

/* =======================================================================
 * Constants
 * ======================================================================= */

#define DX12_BACKBUFFER_COUNT   3
#define DX12_MAX_RESOURCES      4096
#define DX12_MAX_BUFFERS        4096
#define DX12_MAX_PIPELINES      512
#define DX12_MAX_FENCES         64
#define DX12_MAX_CMD_LISTS      16

#define DX12_RTV_HEAP_SIZE      256
#define DX12_DSV_HEAP_SIZE      64
#define DX12_CBV_SRV_UAV_HEAP_SIZE 4096
#define DX12_SAMPLER_HEAP_SIZE  128

/* =======================================================================
 * Internal resource slot types
 *
 * All GPU resources are stored in flat arrays indexed by the uint64_t
 * handle value.  Slot 0 is always invalid (corresponds to RHI_NULL).
 * ======================================================================= */

typedef struct Dx12TextureSlot {
    ID3D12Resource *resource;
    bool            is_swapchain; /* true for backbuffers -- do NOT Release() */
    bool            occupied;
} Dx12TextureSlot;

typedef struct Dx12BufferSlot {
    ID3D12Resource *resource;
    void           *mapped_ptr;  /* non-NULL while mapped */
    uint64_t        size;
    uint32_t        usage;       /* RhiBufferUsage flags */
    bool            occupied;
} Dx12BufferSlot;

typedef struct Dx12PipelineSlot {
    ID3D12PipelineState *pso;
    ID3D12RootSignature *root_sig;
    bool                 occupied;
} Dx12PipelineSlot;

typedef struct Dx12FenceSlot {
    ID3D12Fence *fence;
    HANDLE       event;
    bool         occupied;
} Dx12FenceSlot;

typedef struct Dx12CmdListSlot {
    ID3D12GraphicsCommandList *list;
    ID3D12CommandAllocator    *allocator;
    bool                       recording;
    bool                       occupied;
} Dx12CmdListSlot;

/* =======================================================================
 * Backend state (file-scope statics -- NOT exposed in any header)
 * ======================================================================= */

static struct {
    /* Core device objects */
    IDXGIFactory6          *factory;
    ID3D12Device           *device;
    ID3D12CommandQueue     *cmd_queue;
    IDXGISwapChain3        *swap_chain;
    ID3D12Debug            *debug_controller;

    /* Descriptor heaps */
    ID3D12DescriptorHeap   *rtv_heap;
    ID3D12DescriptorHeap   *dsv_heap;
    ID3D12DescriptorHeap   *cbv_srv_uav_heap;
    ID3D12DescriptorHeap   *sampler_heap;
    uint32_t                rtv_desc_size;
    uint32_t                dsv_desc_size;
    uint32_t                cbv_srv_uav_desc_size;

    /* Backbuffer tracking */
    uint32_t                backbuffer_indices[DX12_BACKBUFFER_COUNT];
    uint32_t                current_backbuffer;

    /* Frame synchronization */
    ID3D12Fence            *frame_fence;
    HANDLE                  frame_fence_event;
    uint64_t                frame_fence_value;

    /* Resource slot arrays */
    Dx12TextureSlot         textures[DX12_MAX_RESOURCES];
    Dx12BufferSlot          buffers[DX12_MAX_BUFFERS];
    Dx12PipelineSlot        pipelines[DX12_MAX_PIPELINES];
    Dx12FenceSlot           fences[DX12_MAX_FENCES];
    Dx12CmdListSlot         cmd_lists[DX12_MAX_CMD_LISTS];

    /* Dimensions */
    uint32_t                width;
    uint32_t                height;

    bool                    initialized;
} s_dx12;

/* =======================================================================
 * Slot allocators
 *
 * Simple linear scan for the next free slot.  Slot 0 is reserved
 * (RHI_NULL).  This is O(N) worst-case but N is bounded and small for
 * Phase 1.  Phase 2 can add a free-list.
 * ======================================================================= */

static uint64_t dx12_alloc_texture_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_RESOURCES; i++) {
        if (!s_dx12.textures[i].occupied) {
            s_dx12.textures[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Texture slot pool exhausted");
    return RHI_NULL;
}

static uint64_t dx12_alloc_buffer_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_BUFFERS; i++) {
        if (!s_dx12.buffers[i].occupied) {
            s_dx12.buffers[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Buffer slot pool exhausted");
    return RHI_NULL;
}

static uint64_t dx12_alloc_pipeline_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_PIPELINES; i++) {
        if (!s_dx12.pipelines[i].occupied) {
            s_dx12.pipelines[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Pipeline slot pool exhausted");
    return RHI_NULL;
}

static uint64_t dx12_alloc_fence_slot(void) {
    for (uint32_t i = 1; i < DX12_MAX_FENCES; i++) {
        if (!s_dx12.fences[i].occupied) {
            s_dx12.fences[i].occupied = true;
            return (uint64_t)i;
        }
    }
    assert(0 && "Fence slot pool exhausted");
    return RHI_NULL;
}

static uint64_t dx12_alloc_cmd_list_slot(void) {
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

static DXGI_FORMAT to_dxgi_format(RhiFormat fmt) {
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

static D3D12_RESOURCE_STATES to_dx12_state(RhiResourceState s) {
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

static D3D12_PRIMITIVE_TOPOLOGY_TYPE to_dx12_topology_type(RhiTopology t) {
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

static D3D_PRIMITIVE_TOPOLOGY to_dx12_topology(RhiTopology t) {
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

static D3D12_BLEND to_dx12_blend(RhiBlendFactor f) {
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

static D3D12_BLEND_OP to_dx12_blend_op(RhiBlendOp op) {
    switch (op) {
    case RHI_BLEND_OP_ADD:          return D3D12_BLEND_OP_ADD;
    case RHI_BLEND_OP_SUBTRACT:     return D3D12_BLEND_OP_SUBTRACT;
    case RHI_BLEND_OP_REV_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
    case RHI_BLEND_OP_MIN:          return D3D12_BLEND_OP_MIN;
    case RHI_BLEND_OP_MAX:          return D3D12_BLEND_OP_MAX;
    default:                        return D3D12_BLEND_OP_ADD;
    }
}

static D3D12_COMPARISON_FUNC to_dx12_compare(RhiCompareFunc f) {
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

static D3D12_FILL_MODE to_dx12_fill(RhiFillMode m) {
    switch (m) {
    case RHI_FILL_SOLID:     return D3D12_FILL_MODE_SOLID;
    case RHI_FILL_WIREFRAME: return D3D12_FILL_MODE_WIREFRAME;
    default:                 return D3D12_FILL_MODE_SOLID;
    }
}

static D3D12_CULL_MODE to_dx12_cull(RhiCullMode m) {
    switch (m) {
    case RHI_CULL_NONE:  return D3D12_CULL_MODE_NONE;
    case RHI_CULL_FRONT: return D3D12_CULL_MODE_FRONT;
    case RHI_CULL_BACK:  return D3D12_CULL_MODE_BACK;
    default:             return D3D12_CULL_MODE_NONE;
    }
}

/* =======================================================================
 * Vertex format conversion
 * ======================================================================= */

static DXGI_FORMAT to_dx12_vertex_format(RhiVertexFormat f) {
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

static void dx12_wait_for_gpu(void) {
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

static void dx12_acquire_backbuffers(void) {
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

static void dx12_release_backbuffers(void) {
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
 * Lifecycle: init / shutdown / resize
 * ======================================================================= */

static bool dx12_init(void *window_handle, uint32_t width, uint32_t height) {
    HRESULT hr;

    assert(!s_dx12.initialized);
    memset(&s_dx12, 0, sizeof(s_dx12));
    s_dx12.width  = width;
    s_dx12.height = height;

    /* ------ Debug layer (debug builds only) ------ */
#if defined(_DEBUG) || defined(DEBUG)
    {
        ID3D12Debug *debug = NULL;
        if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug))) {
            ID3D12Debug_EnableDebugLayer(debug);
            s_dx12.debug_controller = debug;
        }
    }
#endif

    /* ------ DXGI Factory ------ */
    {
        UINT flags = 0;
#if defined(_DEBUG) || defined(DEBUG)
        flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
        hr = CreateDXGIFactory2(flags, &IID_IDXGIFactory6,
                                (void **)&s_dx12.factory);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateDXGIFactory2 failed\n");
            return false;
        }
    }

    /* ------ Adapter selection (prefer high-performance GPU) ------ */
    IDXGIAdapter1 *adapter = NULL;
    {
        for (UINT i = 0;
             IDXGIFactory6_EnumAdapterByGpuPreference(
                 s_dx12.factory, i,
                 DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 &IID_IDXGIAdapter1, (void **)&adapter) != DXGI_ERROR_NOT_FOUND;
             i++) {
            DXGI_ADAPTER_DESC1 desc;
            IDXGIAdapter1_GetDesc1(adapter, &desc);
            /* Skip software adapter */
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                IDXGIAdapter1_Release(adapter);
                adapter = NULL;
                continue;
            }
            /* Check D3D12 support */
            if (SUCCEEDED(D3D12CreateDevice((IUnknown *)adapter,
                                            D3D_FEATURE_LEVEL_12_0,
                                            &IID_ID3D12Device, NULL))) {
                break; /* Found a suitable adapter */
            }
            IDXGIAdapter1_Release(adapter);
            adapter = NULL;
        }
    }
    if (!adapter) {
        OutputDebugStringA("RHI DX12: No suitable DX12 adapter found\n");
        return false;
    }

    /* ------ Device ------ */
    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_12_0,
                           &IID_ID3D12Device, (void **)&s_dx12.device);
    IDXGIAdapter1_Release(adapter);
    if (FAILED(hr)) {
        OutputDebugStringA("RHI DX12: D3D12CreateDevice failed\n");
        return false;
    }

    /* ------ Command Queue (DIRECT) ------ */
    {
        D3D12_COMMAND_QUEUE_DESC qd;
        memset(&qd, 0, sizeof(qd));
        qd.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
        qd.NodeMask = 0;
        hr = ID3D12Device_CreateCommandQueue(s_dx12.device, &qd,
                                             &IID_ID3D12CommandQueue,
                                             (void **)&s_dx12.cmd_queue);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateCommandQueue failed\n");
            return false;
        }
    }

    /* ------ Swap Chain ------ */
    {
        DXGI_SWAP_CHAIN_DESC1 sd;
        memset(&sd, 0, sizeof(sd));
        sd.Width       = width;
        sd.Height      = height;
        sd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Stereo      = FALSE;
        sd.SampleDesc.Count   = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = DX12_BACKBUFFER_COUNT;
        sd.Scaling     = DXGI_SCALING_STRETCH;
        sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags       = 0;

        IDXGISwapChain1 *sc1 = NULL;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(
            (IDXGIFactory2 *)s_dx12.factory,
            (IUnknown *)s_dx12.cmd_queue,
            (HWND)window_handle,
            &sd, NULL, NULL, &sc1);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: CreateSwapChainForHwnd failed\n");
            return false;
        }

        hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3,
                                            (void **)&s_dx12.swap_chain);
        IDXGISwapChain1_Release(sc1);
        if (FAILED(hr)) {
            OutputDebugStringA("RHI DX12: QueryInterface for SwapChain3 failed\n");
            return false;
        }

        /* Disable Alt+Enter fullscreen toggle (engine manages its own) */
        IDXGIFactory2_MakeWindowAssociation((IDXGIFactory2 *)s_dx12.factory,
                                            (HWND)window_handle,
                                            DXGI_MWA_NO_ALT_ENTER);
    }

    /* ------ Descriptor Heaps ------ */
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd;
        memset(&hd, 0, sizeof(hd));

        /* RTV heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = DX12_RTV_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.rtv_heap);
        assert(SUCCEEDED(hr));

        /* DSV heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = DX12_DSV_HEAP_SIZE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.dsv_heap);
        assert(SUCCEEDED(hr));

        /* CBV/SRV/UAV heap (shader-visible) */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = DX12_CBV_SRV_UAV_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.cbv_srv_uav_heap);
        assert(SUCCEEDED(hr));

        /* Sampler heap */
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.NumDescriptors = DX12_SAMPLER_HEAP_SIZE;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = ID3D12Device_CreateDescriptorHeap(s_dx12.device, &hd,
                                               &IID_ID3D12DescriptorHeap,
                                               (void **)&s_dx12.sampler_heap);
        assert(SUCCEEDED(hr));

        /* Cache descriptor sizes */
        s_dx12.rtv_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        s_dx12.dsv_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        s_dx12.cbv_srv_uav_desc_size = ID3D12Device_GetDescriptorHandleIncrementSize(
            s_dx12.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    /* ------ Frame fence ------ */
    {
        hr = ID3D12Device_CreateFence(s_dx12.device, 0,
                                      D3D12_FENCE_FLAG_NONE,
                                      &IID_ID3D12Fence,
                                      (void **)&s_dx12.frame_fence);
        assert(SUCCEEDED(hr));
        s_dx12.frame_fence_value = 0;
        s_dx12.frame_fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
        assert(s_dx12.frame_fence_event != NULL);
    }

    /* ------ Acquire swapchain backbuffers ------ */
    dx12_acquire_backbuffers();
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);

    /* ------ PSO cache ------ */
    rhi_pso_cache_init();

    s_dx12.initialized = true;
    return true;
}

static void dx12_shutdown(void) {
    if (!s_dx12.initialized) return;

    dx12_wait_for_gpu();

    rhi_pso_cache_shutdown();

    /* Release command lists */
    for (uint32_t i = 1; i < DX12_MAX_CMD_LISTS; i++) {
        if (s_dx12.cmd_lists[i].occupied) {
            if (s_dx12.cmd_lists[i].list)
                ID3D12GraphicsCommandList_Release(s_dx12.cmd_lists[i].list);
            if (s_dx12.cmd_lists[i].allocator)
                ID3D12CommandAllocator_Release(s_dx12.cmd_lists[i].allocator);
        }
    }

    /* Release user fences */
    for (uint32_t i = 1; i < DX12_MAX_FENCES; i++) {
        if (s_dx12.fences[i].occupied) {
            if (s_dx12.fences[i].fence)
                ID3D12Fence_Release(s_dx12.fences[i].fence);
            if (s_dx12.fences[i].event)
                CloseHandle(s_dx12.fences[i].event);
        }
    }

    /* Release pipelines */
    for (uint32_t i = 1; i < DX12_MAX_PIPELINES; i++) {
        if (s_dx12.pipelines[i].occupied) {
            if (s_dx12.pipelines[i].pso)
                ID3D12PipelineState_Release(s_dx12.pipelines[i].pso);
            if (s_dx12.pipelines[i].root_sig)
                ID3D12RootSignature_Release(s_dx12.pipelines[i].root_sig);
        }
    }

    /* Release user buffers */
    for (uint32_t i = 1; i < DX12_MAX_BUFFERS; i++) {
        if (s_dx12.buffers[i].occupied && s_dx12.buffers[i].resource) {
            ID3D12Resource_Release(s_dx12.buffers[i].resource);
        }
    }

    /* Release user textures (non-swapchain) */
    for (uint32_t i = 1; i < DX12_MAX_RESOURCES; i++) {
        if (s_dx12.textures[i].occupied
            && !s_dx12.textures[i].is_swapchain
            && s_dx12.textures[i].resource) {
            ID3D12Resource_Release(s_dx12.textures[i].resource);
        }
    }

    /* Release backbuffers */
    dx12_release_backbuffers();

    /* Release frame fence */
    if (s_dx12.frame_fence)       ID3D12Fence_Release(s_dx12.frame_fence);
    if (s_dx12.frame_fence_event) CloseHandle(s_dx12.frame_fence_event);

    /* Release descriptor heaps */
    if (s_dx12.sampler_heap)      ID3D12DescriptorHeap_Release(s_dx12.sampler_heap);
    if (s_dx12.cbv_srv_uav_heap)  ID3D12DescriptorHeap_Release(s_dx12.cbv_srv_uav_heap);
    if (s_dx12.dsv_heap)          ID3D12DescriptorHeap_Release(s_dx12.dsv_heap);
    if (s_dx12.rtv_heap)          ID3D12DescriptorHeap_Release(s_dx12.rtv_heap);

    /* Release core objects */
    if (s_dx12.swap_chain)        IDXGISwapChain3_Release(s_dx12.swap_chain);
    if (s_dx12.cmd_queue)         ID3D12CommandQueue_Release(s_dx12.cmd_queue);
    if (s_dx12.device)            ID3D12Device_Release(s_dx12.device);
    if (s_dx12.factory)           IDXGIFactory6_Release(s_dx12.factory);
    if (s_dx12.debug_controller)  ID3D12Debug_Release(s_dx12.debug_controller);

    memset(&s_dx12, 0, sizeof(s_dx12));
}

static void dx12_resize(uint32_t width, uint32_t height) {
    if (!s_dx12.initialized) return;
    if (width == 0 || height == 0) return;

    dx12_wait_for_gpu();
    dx12_release_backbuffers();

    HRESULT hr = IDXGISwapChain3_ResizeBuffers(
        s_dx12.swap_chain,
        DX12_BACKBUFFER_COUNT,
        width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0);
    assert(SUCCEEDED(hr));
    (void)hr;

    s_dx12.width  = width;
    s_dx12.height = height;

    dx12_acquire_backbuffers();
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);
}

/* =======================================================================
 * Resources: Buffer
 * ======================================================================= */

static RhiBuffer dx12_buffer_create(const RhiBufferDesc *desc) {
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

static void dx12_buffer_destroy(RhiBuffer buf) {
    if (buf == RHI_NULL) return;
    uint32_t slot = (uint32_t)buf;
    assert(slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied);

    if (s_dx12.buffers[slot].resource) {
        ID3D12Resource_Release(s_dx12.buffers[slot].resource);
    }
    memset(&s_dx12.buffers[slot], 0, sizeof(Dx12BufferSlot));
}

static void *dx12_buffer_map(RhiBuffer buf) {
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

static void dx12_buffer_unmap(RhiBuffer buf) {
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

static void dx12_buffer_upload(RhiBuffer buf, const void *data, size_t size) {
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

static RhiTexture dx12_texture_create(const RhiTextureDesc *desc) {
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

static void dx12_texture_destroy(RhiTexture tex) {
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

static void dx12_texture_upload(RhiTexture tex, const void *data, size_t size) {
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

static RhiPipeline dx12_pipeline_create(const RhiPipelineDesc *desc) {
    assert(s_dx12.initialized && desc);

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
                OutputDebugStringA((const char *)ID3D10Blob_GetBufferPointer(err_blob));
                ID3D10Blob_Release(err_blob);
            }
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
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
                OutputDebugStringA((const char *)ID3D10Blob_GetBufferPointer(err_blob));
                ID3D10Blob_Release(err_blob);
            }
            if (vs_blob) ID3D10Blob_Release(vs_blob);
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
        if (err_blob) ID3D10Blob_Release(err_blob);
        err_blob = NULL;
    }

    /*
     * Phase 1: Create a simple root signature with:
     *   - 1 root CBV at b0 (per-frame constants)
     *   - 1 root CBV at b1 (per-object constants)
     *   - 1 descriptor table for SRVs (t0..tN)
     *   - 1 static sampler at s0
     *
     * TODO Phase 2: Root signature derived from shader reflection or
     * from the pipeline desc. Bindless resource support.
     */
    ID3D12RootSignature *root_sig = NULL;
    {
        D3D12_ROOT_PARAMETER root_params[3];
        memset(root_params, 0, sizeof(root_params));

        /* Param 0: CBV at b0 */
        root_params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[0].Descriptor.ShaderRegister = 0;
        root_params[0].Descriptor.RegisterSpace  = 0;
        root_params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 1: CBV at b1 */
        root_params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_params[1].Descriptor.ShaderRegister = 1;
        root_params[1].Descriptor.RegisterSpace  = 0;
        root_params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        /* Param 2: SRV descriptor table */
        D3D12_DESCRIPTOR_RANGE srv_range;
        memset(&srv_range, 0, sizeof(srv_range));
        srv_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors     = 16;
        srv_range.BaseShaderRegister = 0;
        srv_range.RegisterSpace      = 0;
        srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        root_params[2].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[2].DescriptorTable.NumDescriptorRanges = 1;
        root_params[2].DescriptorTable.pDescriptorRanges   = &srv_range;
        root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
        rs_desc.NumParameters     = 3;
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
                OutputDebugStringA((const char *)ID3D10Blob_GetBufferPointer(sig_err));
                ID3D10Blob_Release(sig_err);
            }
            if (vs_blob) ID3D10Blob_Release(vs_blob);
            if (ps_blob) ID3D10Blob_Release(ps_blob);
            s_dx12.pipelines[slot].occupied = false;
            return RHI_NULL;
        }
        if (sig_err) ID3D10Blob_Release(sig_err);

        hr = ID3D12Device_CreateRootSignature(
            s_dx12.device, 0,
            ID3D10Blob_GetBufferPointer(sig_blob),
            ID3D10Blob_GetBufferSize(sig_blob),
            &IID_ID3D12RootSignature, (void **)&root_sig);
        ID3D10Blob_Release(sig_blob);
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

    if (FAILED(hr)) {
        ID3D12RootSignature_Release(root_sig);
        s_dx12.pipelines[slot].occupied = false;
        return RHI_NULL;
    }

    s_dx12.pipelines[slot].pso      = pso;
    s_dx12.pipelines[slot].root_sig = root_sig;

    /* Insert into PSO cache */
    rhi_pso_cache_insert(&cache_key, (RhiPipeline)slot);

    return (RhiPipeline)slot;
}

static RhiPipeline dx12_compute_pipeline_create(const RhiComputePipelineDesc *desc) {
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

static void dx12_pipeline_destroy(RhiPipeline pipeline) {
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
 * Command recording
 * ======================================================================= */

static RhiCmdList dx12_cmd_begin(void) {
    assert(s_dx12.initialized);

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

static void dx12_cmd_end(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    uint32_t slot = (uint32_t)cmd;
    assert(slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[slot].occupied);
    assert(s_dx12.cmd_lists[slot].recording);

    ID3D12GraphicsCommandList_Close(s_dx12.cmd_lists[slot].list);
    s_dx12.cmd_lists[slot].recording = false;
}

static void dx12_cmd_submit(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    uint32_t slot = (uint32_t)cmd;
    assert(slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[slot].occupied);
    assert(!s_dx12.cmd_lists[slot].recording); /* Must be ended first */

    ID3D12CommandList *lists[] = {
        (ID3D12CommandList *)s_dx12.cmd_lists[slot].list
    };
    ID3D12CommandQueue_ExecuteCommandLists(s_dx12.cmd_queue, 1, lists);

    /* Return the slot so it can be reused */
    s_dx12.cmd_lists[slot].occupied = false;
}

static void dx12_cmd_set_pipeline(RhiCmdList cmd, RhiPipeline pipeline) {
    if (cmd == RHI_NULL || pipeline == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t pso_slot = (uint32_t)pipeline;
    assert(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);
    assert(pso_slot < DX12_MAX_PIPELINES && s_dx12.pipelines[pso_slot].occupied);

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    ID3D12GraphicsCommandList_SetPipelineState(cl, s_dx12.pipelines[pso_slot].pso);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(cl, s_dx12.pipelines[pso_slot].root_sig);
}

static void dx12_cmd_set_render_targets(RhiCmdList cmd, const RhiTexture *rts,
                                        uint32_t count, RhiTexture depth) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    assert(cmd_slot < DX12_MAX_CMD_LISTS && s_dx12.cmd_lists[cmd_slot].occupied);

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[8];
    for (uint32_t i = 0; i < count && i < 8; i++) {
        uint32_t tex_slot = (uint32_t)rts[i];
        assert(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.rtv_heap, &rtv_handles[i]);
        rtv_handles[i].ptr += (SIZE_T)tex_slot * s_dx12.rtv_desc_size;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    D3D12_CPU_DESCRIPTOR_HANDLE *p_dsv = NULL;
    if (depth != RHI_NULL) {
        uint32_t ds_slot = (uint32_t)depth;
        assert(ds_slot < DX12_MAX_RESOURCES && s_dx12.textures[ds_slot].occupied);
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(
            s_dx12.dsv_heap, &dsv_handle);
        dsv_handle.ptr += (SIZE_T)ds_slot * s_dx12.dsv_desc_size;
        p_dsv = &dsv_handle;
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(
        cl, count, count > 0 ? rtv_handles : NULL, FALSE, p_dsv);
}

static void dx12_cmd_set_viewport(RhiCmdList cmd, float x, float y,
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

static void dx12_cmd_set_scissor(RhiCmdList cmd, int32_t x, int32_t y,
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

static void dx12_cmd_set_vertex_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                       RhiBuffer buf, uint64_t offset,
                                       uint32_t stride) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    assert(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_VERTEX_BUFFER_VIEW view;
    view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource) + offset;
    view.SizeInBytes    = (UINT)(s_dx12.buffers[buf_slot].size - offset);
    view.StrideInBytes  = stride;

    ID3D12GraphicsCommandList_IASetVertexBuffers(
        s_dx12.cmd_lists[cmd_slot].list, slot_idx, 1, &view);
}

static void dx12_cmd_set_index_buffer(RhiCmdList cmd, RhiBuffer buf,
                                      uint64_t offset, bool is_32bit) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    assert(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    D3D12_INDEX_BUFFER_VIEW view;
    view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource) + offset;
    view.SizeInBytes    = (UINT)(s_dx12.buffers[buf_slot].size - offset);
    view.Format         = is_32bit ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

    ID3D12GraphicsCommandList_IASetIndexBuffer(
        s_dx12.cmd_lists[cmd_slot].list, &view);
}

static void dx12_cmd_set_constant_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                         RhiBuffer buf) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    assert(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

    /*
     * Root parameter indices 0 and 1 are CBVs at b0 and b1.
     * slot_idx maps directly: 0 -> root param 0, 1 -> root param 1.
     * Slots >= 2 are not supported in the Phase 1 root signature.
     */
    assert(slot_idx < 2);
    D3D12_GPU_VIRTUAL_ADDRESS addr = ID3D12Resource_GetGPUVirtualAddress(
        s_dx12.buffers[buf_slot].resource);
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
        s_dx12.cmd_lists[cmd_slot].list, slot_idx, addr);
}

static void dx12_cmd_set_texture(RhiCmdList cmd, uint32_t slot_idx,
                                 RhiTexture tex) {
    if (cmd == RHI_NULL || tex == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)tex;
    assert(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);

    /*
     * Root parameter 2 is the SRV descriptor table.
     * For Phase 1, we set the base of the table to point at the SRV
     * descriptor for this texture.  This only supports a single texture
     * at t0 per draw call.
     * TODO Phase 2: Proper descriptor table management for multiple textures.
     */
    (void)slot_idx; /* Phase 1: always use descriptor at tex_slot index */

    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(
        s_dx12.cbv_srv_uav_heap, &gpu_handle);
    gpu_handle.ptr += (UINT64)tex_slot * s_dx12.cbv_srv_uav_desc_size;

    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
        s_dx12.cmd_lists[cmd_slot].list, 2, gpu_handle);
}

static void dx12_cmd_draw(RhiCmdList cmd, uint32_t vertex_count,
                           uint32_t instance_count, uint32_t first_vertex,
                           uint32_t first_instance) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(
        cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(
        cl, vertex_count, instance_count, first_vertex, first_instance);
}

static void dx12_cmd_draw_indexed(RhiCmdList cmd, uint32_t index_count,
                                   uint32_t instance_count,
                                   uint32_t first_index,
                                   int32_t vertex_offset,
                                   uint32_t first_instance) {
    if (cmd == RHI_NULL) return;
    uint32_t cmd_slot = (uint32_t)cmd;

    ID3D12GraphicsCommandList *cl = s_dx12.cmd_lists[cmd_slot].list;
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(
        cl, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(
        cl, index_count, instance_count, first_index,
        vertex_offset, first_instance);
}

static void dx12_cmd_dispatch(RhiCmdList cmd, uint32_t x, uint32_t y,
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

static void dx12_cmd_clear_render_target(RhiCmdList cmd, RhiTexture rt,
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

static void dx12_cmd_clear_depth_stencil(RhiCmdList cmd, RhiTexture ds,
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

/* =======================================================================
 * Barriers
 * ======================================================================= */

static void dx12_cmd_barrier_texture(RhiCmdList cmd, RhiTexture tex,
                                     RhiResourceState src_state,
                                     RhiResourceState dst_state) {
    if (cmd == RHI_NULL || tex == RHI_NULL) return;
    if (src_state == dst_state) return; /* No-op */

    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t tex_slot = (uint32_t)tex;
    assert(tex_slot < DX12_MAX_RESOURCES && s_dx12.textures[tex_slot].occupied);

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

static void dx12_cmd_barrier_buffer(RhiCmdList cmd, RhiBuffer buf,
                                    RhiResourceState src_state,
                                    RhiResourceState dst_state) {
    if (cmd == RHI_NULL || buf == RHI_NULL) return;
    if (src_state == dst_state) return;

    uint32_t cmd_slot = (uint32_t)cmd;
    uint32_t buf_slot = (uint32_t)buf;
    assert(buf_slot < DX12_MAX_BUFFERS && s_dx12.buffers[buf_slot].occupied);

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
 * Swapchain
 * ======================================================================= */

static RhiTexture dx12_swapchain_backbuffer(void) {
    assert(s_dx12.initialized);
    s_dx12.current_backbuffer = IDXGISwapChain3_GetCurrentBackBufferIndex(
        s_dx12.swap_chain);
    return (RhiTexture)s_dx12.backbuffer_indices[s_dx12.current_backbuffer];
}

static void dx12_swapchain_present(uint32_t sync_interval) {
    assert(s_dx12.initialized);
    IDXGISwapChain3_Present(s_dx12.swap_chain, sync_interval, 0);

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
 * Sync: Fences
 * ======================================================================= */

static RhiFence dx12_fence_create(void) {
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

static void dx12_fence_destroy(RhiFence fence) {
    if (fence == RHI_NULL) return;
    uint32_t slot = (uint32_t)fence;
    assert(slot < DX12_MAX_FENCES && s_dx12.fences[slot].occupied);

    if (s_dx12.fences[slot].fence) ID3D12Fence_Release(s_dx12.fences[slot].fence);
    if (s_dx12.fences[slot].event) CloseHandle(s_dx12.fences[slot].event);
    memset(&s_dx12.fences[slot], 0, sizeof(Dx12FenceSlot));
}

static void dx12_fence_signal(RhiFence fence, uint64_t value) {
    if (fence == RHI_NULL) return;
    uint32_t slot = (uint32_t)fence;
    assert(slot < DX12_MAX_FENCES && s_dx12.fences[slot].occupied);

    ID3D12CommandQueue_Signal(s_dx12.cmd_queue,
                             s_dx12.fences[slot].fence, value);
}

static bool dx12_fence_wait(RhiFence fence, uint64_t value,
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
 * Debug
 * ======================================================================= */

static void dx12_set_debug_name(uint64_t handle, const char *name) {
    if (handle == RHI_NULL || !name) return;

    wchar_t wname[256];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);

    /*
     * Try to find the handle in textures first, then buffers.
     * This is a best-effort debug helper.
     */
    uint32_t slot = (uint32_t)handle;
    if (slot < DX12_MAX_RESOURCES && s_dx12.textures[slot].occupied
        && s_dx12.textures[slot].resource) {
        ID3D12Resource_SetName(s_dx12.textures[slot].resource, wname);
    } else if (slot < DX12_MAX_BUFFERS && s_dx12.buffers[slot].occupied
               && s_dx12.buffers[slot].resource) {
        ID3D12Resource_SetName(s_dx12.buffers[slot].resource, wname);
    }
}

static void dx12_begin_event(RhiCmdList cmd, const char *name) {
    if (cmd == RHI_NULL || !name) return;
    (void)cmd;
    (void)name;
    /*
     * TODO Phase 2: Use PIX events (PIXBeginEvent) for GPU profiling.
     * Requires WinPixEventRuntime. For Phase 1, this is a no-op.
     */
}

static void dx12_end_event(RhiCmdList cmd) {
    if (cmd == RHI_NULL) return;
    (void)cmd;
    /* TODO Phase 2: PIXEndEvent */
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

    /* Clear */
    .cmd_clear_render_target = dx12_cmd_clear_render_target,
    .cmd_clear_depth_stencil = dx12_cmd_clear_depth_stencil,

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
};

#endif /* _WIN32 */
