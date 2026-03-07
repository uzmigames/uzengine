/*
 * rhi.h -- Render Hardware Interface (public C API)
 *
 * This is the single header that ALL rendering code above the backend layer
 * includes.  It defines opaque handle types, resource descriptors, the
 * rhi_backend_t vtable, and the global rhi() accessor.
 *
 * NO backend-specific headers (d3d12.h, vulkan.h, Metal.h) are included here.
 * Backend implementations live in rhi_dx12.c, rhi_vulkan.c, rhi_metal.c.
 *
 * See docs/specs/rendering/rhi.md for the full specification.
 */

#ifndef UZ_RHI_H
#define UZ_RHI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Opaque GPU resource handles
 *
 * All resources are represented as uint64_t handles.  No raw pointers
 * cross the RHI boundary.  Internally each backend maps handles to
 * backend-specific objects via a dense slot array + generation counter.
 * ----------------------------------------------------------------------- */

typedef uint64_t RhiBuffer;
typedef uint64_t RhiTexture;
typedef uint64_t RhiPipeline;
typedef uint64_t RhiRenderPass;
typedef uint64_t RhiCmdList;
typedef uint64_t RhiFence;

#define RHI_NULL ((uint64_t)0)

/* -----------------------------------------------------------------------
 * Enums
 * ----------------------------------------------------------------------- */

typedef enum RhiBackend {
    RHI_BACKEND_DX12   = 0,
    RHI_BACKEND_VULKAN = 1,
    RHI_BACKEND_METAL  = 2,
} RhiBackend;

typedef enum RhiFormat {
    RHI_FORMAT_UNKNOWN = 0,
    RHI_FORMAT_RGBA8_UNORM,
    RHI_FORMAT_RGBA8_SRGB,
    RHI_FORMAT_RGBA16_FLOAT,
    RHI_FORMAT_R32_FLOAT,
    RHI_FORMAT_RG32_FLOAT,
    RHI_FORMAT_RGBA32_FLOAT,
    RHI_FORMAT_R8_UNORM,
    RHI_FORMAT_R16_FLOAT,
    RHI_FORMAT_RG16_FLOAT,
    RHI_FORMAT_R11G11B10_FLOAT,
    RHI_FORMAT_RGB10A2_UNORM,
    RHI_FORMAT_D32_FLOAT,
    RHI_FORMAT_D24_UNORM_S8_UINT,
    RHI_FORMAT_BC7_UNORM,
    RHI_FORMAT_BC7_SRGB,
    RHI_FORMAT_BC5_UNORM,
    RHI_FORMAT_BC6H_UF16,
    RHI_FORMAT_R32_UINT,
    RHI_FORMAT_R16_UINT,
    RHI_FORMAT_COUNT,
} RhiFormat;

typedef enum RhiResourceState {
    RHI_STATE_UNDEFINED                  = 0,
    RHI_STATE_COMMON                     = 1,
    RHI_STATE_RENDER_TARGET              = 2,
    RHI_STATE_DEPTH_WRITE                = 3,
    RHI_STATE_DEPTH_READ                 = 4,
    RHI_STATE_SHADER_READ                = 5,
    RHI_STATE_UNORDERED_ACCESS           = 6,
    RHI_STATE_COPY_SRC                   = 7,
    RHI_STATE_COPY_DST                   = 8,
    RHI_STATE_PRESENT                    = 9,
    RHI_STATE_INDIRECT_ARGUMENT          = 10,
    RHI_STATE_VERTEX_AND_CONSTANT_BUFFER = 11,
    RHI_STATE_INDEX_BUFFER               = 12,
} RhiResourceState;

typedef enum RhiTextureDimension {
    RHI_TEXTURE_DIM_2D       = 0,
    RHI_TEXTURE_DIM_3D       = 1,
    RHI_TEXTURE_DIM_CUBE     = 2,
    RHI_TEXTURE_DIM_2D_ARRAY = 3,
} RhiTextureDimension;

/* -----------------------------------------------------------------------
 * Usage flags (bitfield)
 * ----------------------------------------------------------------------- */

typedef enum RhiTextureUsage {
    RHI_TEXTURE_USAGE_SHADER_READ    = (1 << 0),
    RHI_TEXTURE_USAGE_RENDER_TARGET  = (1 << 1),
    RHI_TEXTURE_USAGE_DEPTH_STENCIL  = (1 << 2),
    RHI_TEXTURE_USAGE_UNORDERED      = (1 << 3),
    RHI_TEXTURE_USAGE_COPY_SRC       = (1 << 4),
    RHI_TEXTURE_USAGE_COPY_DST       = (1 << 5),
} RhiTextureUsage;

typedef enum RhiBufferUsage {
    RHI_BUFFER_USAGE_VERTEX    = (1 << 0),
    RHI_BUFFER_USAGE_INDEX     = (1 << 1),
    RHI_BUFFER_USAGE_CONSTANT  = (1 << 2),
    RHI_BUFFER_USAGE_STORAGE   = (1 << 3),
    RHI_BUFFER_USAGE_INDIRECT  = (1 << 4),
    RHI_BUFFER_USAGE_COPY_SRC  = (1 << 5),
    RHI_BUFFER_USAGE_COPY_DST  = (1 << 6),
    RHI_BUFFER_USAGE_UPLOAD    = (1 << 7),  /* CPU-writable staging / upload heap */
    RHI_BUFFER_USAGE_READBACK  = (1 << 8),  /* CPU-readable readback heap */
} RhiBufferUsage;

/* -----------------------------------------------------------------------
 * Resource descriptors
 * ----------------------------------------------------------------------- */

typedef struct RhiTextureDesc {
    uint32_t            width;
    uint32_t            height;
    uint32_t            depth;
    uint32_t            mip_levels;
    uint32_t            array_size;    /* 1 for regular textures, 6 for cube maps */
    RhiFormat           format;
    RhiTextureDimension dimension;     /* 2D, 3D, Cube, or 2D Array */
    uint32_t            usage;         /* RHI_TEXTURE_USAGE_* flags */
    const char         *debug_name;
} RhiTextureDesc;

typedef struct RhiBufferDesc {
    uint64_t    size;
    uint32_t    usage;          /* RHI_BUFFER_USAGE_* flags */
    const char *debug_name;
} RhiBufferDesc;

typedef struct RhiShaderDesc {
    const void *spirv_data;     /* SPIR-V bytecode (canonical IR).
                                 * Phase 1 DX12: temporarily contains raw HLSL source.
                                 * Phase 2+ will be true SPIR-V. */
    size_t      spirv_size;
    const char *entry_point;    /* "VSMain", "PSMain", "CSMain" */
} RhiShaderDesc;

/* -----------------------------------------------------------------------
 * Rasterizer / Blend / Depth-Stencil state (inline in pipeline desc)
 * ----------------------------------------------------------------------- */

typedef enum RhiFillMode {
    RHI_FILL_SOLID     = 0,
    RHI_FILL_WIREFRAME = 1,
} RhiFillMode;

typedef enum RhiCullMode {
    RHI_CULL_NONE  = 0,
    RHI_CULL_FRONT = 1,
    RHI_CULL_BACK  = 2,
} RhiCullMode;

typedef enum RhiBlendFactor {
    RHI_BLEND_ZERO             = 0,
    RHI_BLEND_ONE              = 1,
    RHI_BLEND_SRC_ALPHA        = 2,
    RHI_BLEND_INV_SRC_ALPHA    = 3,
    RHI_BLEND_DST_ALPHA        = 4,
    RHI_BLEND_INV_DST_ALPHA    = 5,
    RHI_BLEND_SRC_COLOR        = 6,
    RHI_BLEND_INV_SRC_COLOR    = 7,
    RHI_BLEND_DST_COLOR        = 8,
    RHI_BLEND_INV_DST_COLOR    = 9,
} RhiBlendFactor;

typedef enum RhiBlendOp {
    RHI_BLEND_OP_ADD          = 0,
    RHI_BLEND_OP_SUBTRACT     = 1,
    RHI_BLEND_OP_REV_SUBTRACT = 2,
    RHI_BLEND_OP_MIN          = 3,
    RHI_BLEND_OP_MAX          = 4,
} RhiBlendOp;

typedef enum RhiCompareFunc {
    RHI_COMPARE_NEVER         = 0,
    RHI_COMPARE_LESS          = 1,
    RHI_COMPARE_EQUAL         = 2,
    RHI_COMPARE_LESS_EQUAL    = 3,
    RHI_COMPARE_GREATER       = 4,
    RHI_COMPARE_NOT_EQUAL     = 5,
    RHI_COMPARE_GREATER_EQUAL = 6,
    RHI_COMPARE_ALWAYS        = 7,
} RhiCompareFunc;

typedef enum RhiTopology {
    RHI_TOPOLOGY_TRIANGLE_LIST  = 0,
    RHI_TOPOLOGY_TRIANGLE_STRIP = 1,
    RHI_TOPOLOGY_LINE_LIST      = 2,
    RHI_TOPOLOGY_LINE_STRIP     = 3,
    RHI_TOPOLOGY_POINT_LIST     = 4,
} RhiTopology;

typedef struct RhiRasterizerState {
    RhiFillMode fill_mode;
    RhiCullMode cull_mode;
    bool        front_ccw;       /* counter-clockwise front face */
    int32_t     depth_bias;
    float       depth_bias_clamp;
    float       slope_scaled_depth_bias;
} RhiRasterizerState;

typedef struct RhiRenderTargetBlend {
    bool           blend_enable;
    RhiBlendFactor src_blend;
    RhiBlendFactor dst_blend;
    RhiBlendOp     blend_op;
    RhiBlendFactor src_blend_alpha;
    RhiBlendFactor dst_blend_alpha;
    RhiBlendOp     blend_op_alpha;
    uint8_t        write_mask;   /* RGBA bitmask: 0xF = all channels */
} RhiRenderTargetBlend;

typedef struct RhiBlendState {
    bool                 independent_blend;
    RhiRenderTargetBlend render_targets[8];
} RhiBlendState;

typedef enum RhiStencilOp {
    RHI_STENCIL_OP_KEEP      = 0,
    RHI_STENCIL_OP_ZERO      = 1,
    RHI_STENCIL_OP_REPLACE   = 2,
    RHI_STENCIL_OP_INCR_SAT  = 3,
    RHI_STENCIL_OP_DECR_SAT  = 4,
    RHI_STENCIL_OP_INVERT    = 5,
} RhiStencilOp;

typedef struct RhiStencilOpDesc {
    RhiStencilOp    fail_op;       /* stencil test fails */
    RhiStencilOp    depth_fail_op; /* stencil passes, depth fails */
    RhiStencilOp    pass_op;       /* both pass */
    RhiCompareFunc  func;
} RhiStencilOpDesc;

typedef struct RhiDepthStencilState {
    bool              depth_enable;
    bool              depth_write;
    RhiCompareFunc    depth_func;
    bool              stencil_enable;
    uint8_t           stencil_read_mask;   /* default: 0xFF */
    uint8_t           stencil_write_mask;  /* default: 0xFF */
    RhiStencilOpDesc  front_face;
    RhiStencilOpDesc  back_face;
} RhiDepthStencilState;

/* -----------------------------------------------------------------------
 * Vertex input layout
 * ----------------------------------------------------------------------- */

typedef enum RhiVertexFormat {
    RHI_VERTEX_FORMAT_FLOAT1  = 0,
    RHI_VERTEX_FORMAT_FLOAT2  = 1,
    RHI_VERTEX_FORMAT_FLOAT3  = 2,
    RHI_VERTEX_FORMAT_FLOAT4  = 3,
    RHI_VERTEX_FORMAT_UINT1   = 4,
    RHI_VERTEX_FORMAT_UINT2   = 5,
    RHI_VERTEX_FORMAT_UINT4   = 6,
    RHI_VERTEX_FORMAT_SINT1   = 7,
    RHI_VERTEX_FORMAT_SINT2   = 8,
    RHI_VERTEX_FORMAT_SINT4   = 9,
    RHI_VERTEX_FORMAT_UNORM8X4 = 10,
} RhiVertexFormat;

#define RHI_MAX_VERTEX_ATTRIBS 16

typedef struct RhiVertexAttribute {
    const char      *semantic_name;  /* "POSITION", "NORMAL", "TEXCOORD", etc. */
    uint32_t         semantic_index; /* 0 for POSITION0, 1 for TEXCOORD1, etc. */
    RhiVertexFormat  format;
    uint32_t         offset;         /* byte offset within vertex */
    uint32_t         buffer_slot;    /* which vertex buffer slot (0-based) */
} RhiVertexAttribute;

typedef struct RhiVertexLayout {
    RhiVertexAttribute attribs[RHI_MAX_VERTEX_ATTRIBS];
    uint32_t           attrib_count;
} RhiVertexLayout;

/* -----------------------------------------------------------------------
 * Pipeline descriptors
 * ----------------------------------------------------------------------- */

typedef struct RhiPipelineDesc {
    RhiShaderDesc        vertex_shader;
    RhiShaderDesc        pixel_shader;
    RhiVertexLayout      vertex_layout;
    RhiFormat            rt_formats[8];
    uint32_t             rt_count;
    RhiFormat            ds_format;
    RhiRasterizerState   rasterizer;
    RhiBlendState        blend;
    RhiDepthStencilState depth_stencil;
    RhiTopology          topology;
    const char          *debug_name;
} RhiPipelineDesc;

/* Footprint for texture-to-buffer copy (readback) */
typedef struct RhiTextureFootprint {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t row_pitch;    /* bytes per row, 256-byte aligned on DX12 */
    uint64_t offset;       /* byte offset into the destination buffer */
    RhiFormat format;
} RhiTextureFootprint;

typedef struct RhiComputePipelineDesc {
    RhiShaderDesc compute_shader;
    const char   *debug_name;
} RhiComputePipelineDesc;

/* -----------------------------------------------------------------------
 * PSO cache key (used by rhi_common.c)
 * ----------------------------------------------------------------------- */

typedef struct RhiPsoCacheKey {
    uint64_t vs_spirv_hash;    /* xxHash64 of vertex shader data */
    uint64_t ps_spirv_hash;    /* xxHash64 of pixel shader data */
    uint32_t rt_formats_hash;  /* hash of RhiFormat[8] array */
    uint32_t state_hash;       /* hash of rasterizer + blend + depth-stencil state */
} RhiPsoCacheKey;

/* -----------------------------------------------------------------------
 * Backend vtable -- the entire RHI surface
 *
 * Engine code calls through rhi()->fn_name(...).  Backends populate this
 * struct with their implementation.  No virtual dispatch or inheritance
 * -- just a flat struct of function pointers.
 * ----------------------------------------------------------------------- */

typedef struct rhi_backend_t {
    /* --- Lifecycle ---------------------------------------------------- */
    bool (*init)(void *window_handle, uint32_t width, uint32_t height);
    void (*shutdown)(void);
    void (*resize)(uint32_t width, uint32_t height);

    /* --- Resources ---------------------------------------------------- */
    RhiBuffer  (*buffer_create)(const RhiBufferDesc *desc);
    void       (*buffer_destroy)(RhiBuffer buf);
    void      *(*buffer_map)(RhiBuffer buf);
    void       (*buffer_unmap)(RhiBuffer buf);
    void       (*buffer_upload)(RhiBuffer buf, const void *data, size_t size);

    RhiTexture (*texture_create)(const RhiTextureDesc *desc);
    void       (*texture_destroy)(RhiTexture tex);
    void       (*texture_upload)(RhiTexture tex, const void *data, size_t size);
    void       (*texture_upload_subresource)(RhiTexture tex, uint32_t subresource,
                                             const void *data, size_t size);

    /* --- Pipelines (backed by PSO cache) ------------------------------ */
    RhiPipeline (*pipeline_create)(const RhiPipelineDesc *desc);
    RhiPipeline (*compute_pipeline_create)(const RhiComputePipelineDesc *desc);
    void        (*pipeline_destroy)(RhiPipeline pipeline);

    /* --- Command recording -------------------------------------------- */
    RhiCmdList (*cmd_begin)(void);
    void       (*cmd_end)(RhiCmdList cmd);
    void       (*cmd_submit)(RhiCmdList cmd);

    void (*cmd_set_pipeline)(RhiCmdList cmd, RhiPipeline pipeline);
    void (*cmd_set_topology)(RhiCmdList cmd, RhiTopology topology);
    void (*cmd_set_render_targets)(RhiCmdList cmd, const RhiTexture *rts,
                                   uint32_t count, RhiTexture depth);
    void (*cmd_set_viewport)(RhiCmdList cmd, float x, float y, float w,
                             float h, float min_d, float max_d);
    void (*cmd_set_scissor)(RhiCmdList cmd, int32_t x, int32_t y,
                            uint32_t w, uint32_t h);
    void (*cmd_set_vertex_buffer)(RhiCmdList cmd, uint32_t slot,
                                  RhiBuffer buf, uint64_t offset,
                                  uint32_t stride);
    void (*cmd_set_index_buffer)(RhiCmdList cmd, RhiBuffer buf,
                                 uint64_t offset, bool is_32bit);
    void (*cmd_set_constant_buffer)(RhiCmdList cmd, uint32_t slot,
                                    RhiBuffer buf);
    void (*cmd_set_texture)(RhiCmdList cmd, uint32_t slot, RhiTexture tex);
    void (*cmd_draw)(RhiCmdList cmd, uint32_t vertex_count,
                     uint32_t instance_count, uint32_t first_vertex,
                     uint32_t first_instance);
    void (*cmd_draw_indexed)(RhiCmdList cmd, uint32_t index_count,
                             uint32_t instance_count, uint32_t first_index,
                             int32_t vertex_offset, uint32_t first_instance);
    void (*cmd_set_uav)(RhiCmdList cmd, uint32_t slot, RhiTexture tex, uint32_t mip_level);
    void (*cmd_dispatch)(RhiCmdList cmd, uint32_t x, uint32_t y, uint32_t z);

    /* --- Root constants (inline 32-bit values) ------------------------ */
    void (*cmd_set_root_constants)(RhiCmdList cmd, uint32_t slot,
                                   const void *data, uint32_t count_32bit,
                                   uint32_t offset_32bit);

    /* --- Copy commands ------------------------------------------------ */
    void (*cmd_copy_texture_to_buffer)(RhiCmdList cmd, RhiTexture src,
                                       RhiBuffer dst,
                                       const RhiTextureFootprint *footprint);

    /* --- Clear commands ----------------------------------------------- */
    void (*cmd_clear_render_target)(RhiCmdList cmd, RhiTexture rt,
                                    float r, float g, float b, float a);
    void (*cmd_clear_depth_stencil)(RhiCmdList cmd, RhiTexture ds,
                                    float depth, uint8_t stencil);

    /* --- Stencil -------------------------------------------------------- */
    void (*cmd_set_stencil_ref)(RhiCmdList cmd, uint8_t ref);

    /* --- Barriers ----------------------------------------------------- */
    void (*cmd_barrier_texture)(RhiCmdList cmd, RhiTexture tex,
                                RhiResourceState src_state,
                                RhiResourceState dst_state);
    void (*cmd_barrier_buffer)(RhiCmdList cmd, RhiBuffer buf,
                               RhiResourceState src_state,
                               RhiResourceState dst_state);

    /* --- Swapchain ---------------------------------------------------- */
    RhiTexture (*swapchain_backbuffer)(void);
    void       (*swapchain_present)(uint32_t sync_interval);

    /* --- Sync --------------------------------------------------------- */
    RhiFence (*fence_create)(void);
    void     (*fence_destroy)(RhiFence fence);
    void     (*fence_signal)(RhiFence fence, uint64_t value);
    bool     (*fence_wait)(RhiFence fence, uint64_t value,
                           uint64_t timeout_ns);

    /* --- Debug -------------------------------------------------------- */
    void (*set_debug_name)(uint64_t handle, const char *name);
    void (*begin_event)(RhiCmdList cmd, const char *name);
    void (*end_event)(RhiCmdList cmd);

    /* --- GPU Timestamps (§6.4) ---------------------------------------- */
    /* Create a GPU timestamp query heap with `count` query slots. */
    uint64_t (*timestamp_query_heap_create)(uint32_t count);
    /* Destroy a timestamp query heap. */
    void     (*timestamp_query_heap_destroy)(uint64_t heap);
    /* Record a GPU timestamp at slot `index` in the query heap. */
    void     (*cmd_timestamp_write)(RhiCmdList cmd, uint64_t heap,
                                    uint32_t index);
    /* Resolve timestamp queries to a CPU-readable buffer. Returns
     * the GPU timestamp frequency (ticks/second), or 0 on failure. */
    uint64_t (*timestamp_readback)(uint64_t heap, uint64_t *out_ticks,
                                   uint32_t first, uint32_t count);

    /* --- GPU Memory Info (§6.3) --------------------------------------- */
    /* Query GPU memory usage. Returns false if not supported. */
    bool (*gpu_memory_info)(uint64_t *out_total_bytes,
                            uint64_t *out_used_bytes,
                            uint64_t *out_budget_bytes);
} rhi_backend_t;

/* -----------------------------------------------------------------------
 * Global accessor -- set once at engine init, used everywhere
 * ----------------------------------------------------------------------- */

const rhi_backend_t *rhi(void);
void                 rhi_set_backend(const rhi_backend_t *backend);

/* -----------------------------------------------------------------------
 * PSO cache helpers (implemented in rhi_common.c)
 * ----------------------------------------------------------------------- */

void        rhi_pso_cache_init(void);
void        rhi_pso_cache_shutdown(void);
RhiPipeline rhi_pso_cache_find(const RhiPsoCacheKey *key);
void        rhi_pso_cache_insert(const RhiPsoCacheKey *key, RhiPipeline pso);
void        rhi_pso_cache_invalidate_by_shader(uint64_t spirv_hash);

/* Hash helpers */
uint64_t    rhi_hash_bytes(const void *data, size_t size);
uint32_t    rhi_hash_bytes32(const void *data, size_t size);
RhiPsoCacheKey rhi_make_pso_cache_key(const RhiPipelineDesc *desc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UZ_RHI_H */
