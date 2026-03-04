/*
 * rhi_dx12_internal.h -- Internal shared header for the DX12 RHI backend
 *
 * This header is included ONLY by rhi_dx12.c, rhi_dx12_init.c, and
 * rhi_dx12_commands.c.  It is NOT part of the public RHI API.
 *
 * It declares the shared state (`s_dx12`), slot types, constants,
 * and conversion helpers used across all three translation units.
 */

#ifndef RHI_DX12_INTERNAL_H
#define RHI_DX12_INTERNAL_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef COBJMACROS
#define COBJMACROS
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
 * ======================================================================= */

typedef struct Dx12TextureSlot {
    ID3D12Resource *resource;
    bool            is_swapchain;
    bool            occupied;
} Dx12TextureSlot;

typedef struct Dx12BufferSlot {
    ID3D12Resource *resource;
    void           *mapped_ptr;
    uint64_t        size;
    uint32_t        usage;
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
 * Backend state (defined in rhi_dx12.c, shared across TUs)
 * ======================================================================= */

typedef struct Dx12State {
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
} Dx12State;

extern Dx12State s_dx12;

/* =======================================================================
 * Slot allocators (defined in rhi_dx12.c)
 * ======================================================================= */

uint64_t dx12_alloc_texture_slot(void);
uint64_t dx12_alloc_buffer_slot(void);
uint64_t dx12_alloc_pipeline_slot(void);
uint64_t dx12_alloc_fence_slot(void);
uint64_t dx12_alloc_cmd_list_slot(void);

/* =======================================================================
 * Conversion helpers (defined in rhi_dx12.c)
 * ======================================================================= */

DXGI_FORMAT               to_dxgi_format(RhiFormat fmt);
D3D12_RESOURCE_STATES     to_dx12_state(RhiResourceState s);
D3D12_PRIMITIVE_TOPOLOGY_TYPE to_dx12_topology_type(RhiTopology t);
D3D_PRIMITIVE_TOPOLOGY    to_dx12_topology(RhiTopology t);
D3D12_BLEND               to_dx12_blend(RhiBlendFactor f);
D3D12_BLEND_OP            to_dx12_blend_op(RhiBlendOp op);
D3D12_COMPARISON_FUNC     to_dx12_compare(RhiCompareFunc f);
D3D12_FILL_MODE           to_dx12_fill(RhiFillMode m);
D3D12_CULL_MODE           to_dx12_cull(RhiCullMode m);
DXGI_FORMAT               to_dx12_vertex_format(RhiVertexFormat f);

/* =======================================================================
 * Internal helpers (defined in rhi_dx12.c)
 * ======================================================================= */

void dx12_wait_for_gpu(void);
void dx12_acquire_backbuffers(void);
void dx12_release_backbuffers(void);

/* =======================================================================
 * Lifecycle functions (defined in rhi_dx12_init.c)
 * ======================================================================= */

bool dx12_init(void *window_handle, uint32_t width, uint32_t height);
void dx12_shutdown(void);
void dx12_resize(uint32_t width, uint32_t height);

/* =======================================================================
 * Resource functions (defined in rhi_dx12.c)
 * ======================================================================= */

RhiBuffer   dx12_buffer_create(const RhiBufferDesc *desc);
void        dx12_buffer_destroy(RhiBuffer buf);
void       *dx12_buffer_map(RhiBuffer buf);
void        dx12_buffer_unmap(RhiBuffer buf);
void        dx12_buffer_upload(RhiBuffer buf, const void *data, size_t size);

RhiTexture  dx12_texture_create(const RhiTextureDesc *desc);
void        dx12_texture_destroy(RhiTexture tex);
void        dx12_texture_upload(RhiTexture tex, const void *data, size_t size);

RhiPipeline dx12_pipeline_create(const RhiPipelineDesc *desc);
RhiPipeline dx12_compute_pipeline_create(const RhiComputePipelineDesc *desc);
void        dx12_pipeline_destroy(RhiPipeline pipeline);

/* =======================================================================
 * Command recording (defined in rhi_dx12_commands.c)
 * ======================================================================= */

RhiCmdList dx12_cmd_begin(void);
void       dx12_cmd_end(RhiCmdList cmd);
void       dx12_cmd_submit(RhiCmdList cmd);
void       dx12_cmd_set_pipeline(RhiCmdList cmd, RhiPipeline pipeline);
void       dx12_cmd_set_render_targets(RhiCmdList cmd, const RhiTexture *rts,
                                        uint32_t count, RhiTexture depth);
void       dx12_cmd_set_viewport(RhiCmdList cmd, float x, float y,
                                  float w, float h, float min_d, float max_d);
void       dx12_cmd_set_scissor(RhiCmdList cmd, int32_t x, int32_t y,
                                 uint32_t w, uint32_t h);
void       dx12_cmd_set_vertex_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                       RhiBuffer buf, uint64_t offset,
                                       uint32_t stride);
void       dx12_cmd_set_index_buffer(RhiCmdList cmd, RhiBuffer buf,
                                      uint64_t offset, bool is_32bit);
void       dx12_cmd_set_constant_buffer(RhiCmdList cmd, uint32_t slot_idx,
                                         RhiBuffer buf);
void       dx12_cmd_set_texture(RhiCmdList cmd, uint32_t slot_idx,
                                 RhiTexture tex);
void       dx12_cmd_draw(RhiCmdList cmd, uint32_t vertex_count,
                          uint32_t instance_count, uint32_t first_vertex,
                          uint32_t first_instance);
void       dx12_cmd_draw_indexed(RhiCmdList cmd, uint32_t index_count,
                                  uint32_t instance_count, uint32_t first_index,
                                  int32_t vertex_offset,
                                  uint32_t first_instance);
void       dx12_cmd_dispatch(RhiCmdList cmd, uint32_t x, uint32_t y,
                              uint32_t z);
void       dx12_cmd_clear_render_target(RhiCmdList cmd, RhiTexture rt,
                                         float r, float g, float b, float a);
void       dx12_cmd_clear_depth_stencil(RhiCmdList cmd, RhiTexture ds,
                                         float depth, uint8_t stencil);
void       dx12_cmd_barrier_texture(RhiCmdList cmd, RhiTexture tex,
                                     RhiResourceState src_state,
                                     RhiResourceState dst_state);
void       dx12_cmd_barrier_buffer(RhiCmdList cmd, RhiBuffer buf,
                                    RhiResourceState src_state,
                                    RhiResourceState dst_state);
void       dx12_cmd_set_root_constants(RhiCmdList cmd, uint32_t slot,
                                        const void *data, uint32_t count_32bit,
                                        uint32_t offset_32bit);
void       dx12_cmd_copy_texture_to_buffer(RhiCmdList cmd, RhiTexture src,
                                            RhiBuffer dst,
                                            const RhiTextureFootprint *footprint);

/* =======================================================================
 * Swapchain (defined in rhi_dx12_commands.c)
 * ======================================================================= */

RhiTexture dx12_swapchain_backbuffer(void);
void       dx12_swapchain_present(uint32_t sync_interval);

/* =======================================================================
 * Fences (defined in rhi_dx12.c)
 * ======================================================================= */

RhiFence dx12_fence_create(void);
void     dx12_fence_destroy(RhiFence fence);
void     dx12_fence_signal(RhiFence fence, uint64_t value);
bool     dx12_fence_wait(RhiFence fence, uint64_t value, uint64_t timeout_ns);

/* =======================================================================
 * Debug (defined in rhi_dx12_commands.c)
 * ======================================================================= */

void dx12_set_debug_name(uint64_t handle, const char *name);
void dx12_begin_event(RhiCmdList cmd, const char *name);
void dx12_end_event(RhiCmdList cmd);

#endif /* _WIN32 */
#endif /* RHI_DX12_INTERNAL_H */
