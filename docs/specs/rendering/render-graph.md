# Spec: Render Graph

> **Status**: Planned (Phase 2)
> **Owner**: `Renderer` module (src/engine/renderer/)
> **Purpose**: Frame resource abstraction, pass dependencies, automatic barrier insertion (via RHI), transient resource aliasing
> **Thread Safety**: Single-threaded (game thread only), compiled on-frame
> **Key Benefit**: Composable post-processes, automatic memory reuse, explicit data flow validation

---

## Overview

The render graph is a frame-local DAG (directed acyclic graph) that tracks all GPU resources and rendering passes, enabling:

1. **Explicit dependencies** — Each pass declares what it reads and writes
2. **Automatic barriers** — Resource state transitions inserted at lifetime boundaries via `rhi()->cmd_barrier_texture()` / `rhi()->cmd_barrier_buffer()`
3. **Resource aliasing** — Transient textures/buffers with non-overlapping lifetimes share GPU memory
4. **Composable post-processes** — Add/remove passes without rewriting shader code
5. **Frame resource pooling** — Reuse allocations across frames within budget constraints

Key properties:

- **Compiled per frame** — DAG structure varies with scene (e.g., shadow maps only if lights cast shadows)
- **Debug visualization** — IPC command to inspect intermediate textures/buffers
- **Integration with renderer** — Renderer sets up core passes (GBuffer, lighting, post-processes)
- **Not a full render engine** — Just resource + dependency management; pass execution is callback-based
- **Backend-agnostic** — Uses only RHI handles (`RhiTexture`, `RhiBuffer`, `RhiCmdList`) and `rhi()->*` calls; never includes DX12/Vulkan/Metal headers

---

## Core Architecture

```
RenderGraph (per frame, allocated from frame_arena)
│
├── Resources (virtual handles — resolved to RhiTexture/RhiBuffer at compile time)
│   ├── RgTexture: format (RhiFormat), width, height, mips, usage flags
│   └── RgBuffer: element type, count, stride, usage flags
│
├── Passes (ordered by topological sort)
│   ├── RgPass: name, read/write sets, execute callback (C function pointer)
│   ├── GBufferPass
│   ├── LightingPass
│   ├── SSAOPass
│   ├── SSRPass
│   ├── TAAPass
│   ├── BloomPass
│   └── TonemapPass
│
└── Compile Pipeline
    ├── Build DAG from pass read/write sets
    ├── Detect missing or circular dependencies
    ├── Topological sort → execution order
    ├── Compute resource lifetimes (first write → last read)
    ├── Alias overlapping transient resources
    └── Allocate physical GPU resources via rhi()->texture_create() / rhi()->buffer_create()
```

### Design Goals

**Simplicity**: Render graphs can become complex (Frostbite, UE5). We optimize for:
- Clear data flow (no mysterious resource state bugs)
- Easy to add post-processes (declare reads/writes, add pass)
- No implicit resource creation

**Performance**:
- Compile phase runs once per frame (~1-2ms for 20-30 passes)
- Execution phase directly dispatches GPU commands via RHI (no extra indirection)
- Resource aliasing reduces VRAM pressure (critical for editor with many textures)

**AI-Native**:
- Render graph definition can be code-generated (MCP tool generates post-process chains)
- All resources and passes explicitly named (debuggable)
- IPC inspection of intermediate buffers

---

## Core Types

### RgHandle

Virtual handle to a render graph resource (texture or buffer).

```c
typedef struct RgHandle {
    uint32_t index;       // 0 = invalid
    uint32_t generation;  // For reuse detection
} RgHandle;
```

Used internally only; external API returns `RgTextureHandle` or `RgBufferHandle`.

### RgTextureHandle & RgTexture

```c
typedef struct RgTextureHandle {
    uint32_t index;
} RgTextureHandle;

typedef struct RgTexture {
    // Identity
    StringId name;              // "gbuffer_albedo", "bloom_temp", etc.

    // Format & Dimensions
    RhiFormat  format;          // RHI_FORMAT_RGBA8_UNORM, etc.
    uint32_t   width, height;   // In pixels
    uint32_t   mip_levels;      // 1 for most; >1 for downsampling chains

    // Transience
    bool     is_transient;           // True = aliased within frame
    uint64_t created_by_pass;        // Pass index that first writes
    uint64_t last_used_by_pass;      // Pass index that last reads (for lifetime)

    // RHI State
    RhiTexture rhi_handle;           // Allocated during compile; RHI_NULL_HANDLE if not yet allocated
    uint32_t   initial_state;        // RhiResourceState enum value (e.g., RHI_STATE_RENDER_TARGET)

    // Allocation
    uint64_t heap_offset;            // For aliased resources in shared heap
} RgTexture;
```

### RgBufferHandle & RgBuffer

```c
typedef struct RgBufferHandle {
    uint32_t index;
} RgBufferHandle;

typedef struct RgBuffer {
    // Identity
    StringId name;              // "indirect_dispatch_args", etc.

    // Layout
    uint32_t element_size;      // sizeof(element)
    uint32_t element_count;     // For StructuredBuffer
    uint32_t total_bytes;       // = element_size * element_count

    // Transience
    bool     is_transient;
    uint64_t created_by_pass;
    uint64_t last_used_by_pass;

    // RHI State
    RhiBuffer rhi_handle;            // Allocated during compile; RHI_NULL_HANDLE if not yet allocated
    uint32_t  initial_state;         // RhiResourceState enum value

    // Allocation
    uint64_t heap_offset;
} RgBuffer;
```

### RgPassType

```c
typedef enum RgPassType {
    RG_PASS_GRAPHICS = 0,
    RG_PASS_COMPUTE  = 1,
    RG_PASS_COPY     = 2,
} RgPassType;
```

### RgPass

Represents a single rendering pass with explicit read/write resource sets.

```c
// Forward declarations
typedef struct RgPassContext RgPassContext;

// Execute callback — C function pointer, not std::function
typedef void (*rg_execute_fn)(RgPassContext *ctx, void *user_data);

typedef struct RgPass {
    // Identity
    StringId name;              // "gbuffer", "lighting", "ssao", etc.
    uint64_t pass_id;           // Unique index in graph

    // Reads & Writes (fixed-size arrays with counts)
    RgTextureHandle read_textures[16];
    uint32_t        read_texture_count;
    RgTextureHandle write_textures[16];
    uint32_t        write_texture_count;
    RgBufferHandle  read_buffers[8];
    uint32_t        read_buffer_count;
    RgBufferHandle  write_buffers[8];
    uint32_t        write_buffer_count;

    // Execution
    rg_execute_fn execute;      // Called with resolved RHI resources
    void         *user_data;    // Passed to execute callback

    // Metadata
    RgPassType type;

    // Debugging
    bool debug_visualize;       // Blit output to screen after pass
} RgPass;
```

### RgPassContext

Passed to each `execute` callback with bound resources and utilities.

```c
typedef struct RgPassContext {
    // Command list (pre-transitioned to required states)
    RhiCmdList          cmd;    // RHI command list (backend-agnostic)

    // Back-pointers
    struct RenderGraph  *graph; // Owning graph
    const RgPass        *pass;  // The pass being executed
} RgPassContext;

// Helper functions (free functions, not methods):
RhiTexture rg_ctx_get_texture(const RgPassContext *ctx, RgTextureHandle h);
RhiBuffer  rg_ctx_get_buffer(const RgPassContext *ctx, RgBufferHandle h);

// Convenience wrappers (call through to rhi()->* internally)
void rg_ctx_set_render_target(RgPassContext *ctx, RgTextureHandle rt, RgTextureHandle depth);
void rg_ctx_bind_texture(RgPassContext *ctx, uint32_t slot, RgTextureHandle tex);
void rg_ctx_bind_buffer(RgPassContext *ctx, uint32_t slot, RgBufferHandle buf);
```

### RenderGraph

The container managing all resources and passes for one frame.

```c
typedef struct RenderGraph RenderGraph;

// --- Lifecycle ---
RenderGraph *rg_create(void);
void         rg_destroy(RenderGraph *graph);

// --- Resource Creation (building phase, before compile) ---
RgTextureHandle rg_add_texture(
    RenderGraph *graph,
    const char  *name,
    RhiFormat    format,
    uint32_t     width,
    uint32_t     height,
    uint32_t     mip_levels,    // 1 for most targets
    bool         is_transient   // true = aliased within frame
);

RgBufferHandle rg_add_buffer(
    RenderGraph *graph,
    const char  *name,
    uint32_t     element_size,
    uint32_t     element_count,
    bool         is_transient
);

// --- Pass Registration (building phase) ---
RgPass *rg_add_pass(RenderGraph *graph, const char *name, RgPassType type);

// Convenience wrappers:
RgPass *rg_add_graphics_pass(RenderGraph *graph, const char *name);
RgPass *rg_add_compute_pass(RenderGraph *graph, const char *name);
RgPass *rg_add_copy_pass(RenderGraph *graph, const char *name);

// --- Dependency Declaration (building phase) ---
void rg_pass_read_texture(RgPass *pass, RgTextureHandle texture);
void rg_pass_read_buffer(RgPass *pass, RgBufferHandle buffer);
void rg_pass_write_texture(RgPass *pass, RgTextureHandle texture);
void rg_pass_write_buffer(RgPass *pass, RgBufferHandle buffer);

// --- Compile (called once per frame after building) ---
typedef struct rg_compile_result_t {
    bool  success;
    char  error_message[256];   // Empty string on success
} rg_compile_result_t;

rg_compile_result_t rg_compile(RenderGraph *graph, uint64_t gpu_budget_bytes);

// --- Execution (called once per frame after compile) ---
void rg_execute(RenderGraph *graph, RhiCmdList cmd);

// --- Debugging ---
void rg_enable_debug_visualization_texture(RenderGraph *graph, RgTextureHandle h);
void rg_enable_debug_visualization_buffer(RenderGraph *graph, RgBufferHandle h);

const RgTexture *rg_get_texture(const RenderGraph *graph, RgTextureHandle h);
const RgBuffer  *rg_get_buffer(const RenderGraph *graph, RgBufferHandle h);
```

---

## Compile Phase

The compile phase runs once per frame after all passes are registered.

### Step 1: DAG Validation

```c
typedef struct rg_compile_context_t {
    RgPass    *passes;
    uint32_t   pass_count;
    RgTexture *textures;
    uint32_t   texture_count;
    RgBuffer  *buffers;
    uint32_t   buffer_count;
} rg_compile_context_t;

bool rg_validate_dag(const rg_compile_context_t *ctx) {
    // Check 1: No circular dependencies
    // Build adjacency list from read/write sets
    // DFS to detect cycles

    // Check 2: No writes to external resources
    // All writes must be to resources created in this graph

    // Check 3: Pass ordering constraint
    // A pass cannot write after it has been read by another

    // Check 4: No missing reads/writes
    // Resources read must be written first

    return all_checks_passed;
}
```

### Step 2: Topological Sort

```c
// Kahn's algorithm: sort passes by dependency order
// Passes with no dependencies run first
// A pass can run after all its dependencies have been satisfied
//
// Result: execution_order[i] = pass index to run at step i

void rg_topological_sort(
    const rg_compile_context_t *ctx,
    uint64_t *out_execution_order,    // Caller-allocated array of ctx->pass_count elements
    uint32_t *out_order_count
);
```

### Step 3: Compute Resource Lifetimes

```c
typedef struct RgResourceLifetime {
    uint64_t first_write_pass;   // When resource is first produced
    uint64_t last_read_pass;     // When resource is last consumed
} RgResourceLifetime;

// For each resource, find:
// - first pass that writes to it (start of lifetime)
// - last pass that reads from it (end of lifetime)
//
// Two resources can alias if their lifetimes don't overlap:
// resource_a.last_read < resource_b.first_write OR vice versa

void rg_compute_lifetimes(
    const rg_compile_context_t *ctx,
    const uint64_t             *execution_order,
    uint32_t                    order_count,
    RgResourceLifetime         *out_lifetimes    // One per resource
);
```

### Step 4: Resource Aliasing

```c
typedef struct RgAliasGroup {
    uint32_t resource_indices[16];   // Textures or buffers with same size
    uint32_t resource_count;
    uint64_t heap_offset;
} RgAliasGroup;

// Group transient resources by:
// 1. Type (all textures or all buffers)
// 2. Size (must be exactly the same to alias)
// 3. Non-overlapping lifetimes
//
// Assign heap offsets to minimize peak VRAM usage
// Validate total against gpu_budget

uint32_t rg_compute_alias_groups(
    const RgResourceLifetime *lifetimes,
    const RgTexture          *textures,
    uint32_t                  texture_count,
    uint64_t                  total_budget,
    RgAliasGroup             *out_groups,        // Caller-allocated
    uint32_t                  max_groups
);
```

### Step 5: GPU Resource Allocation (via RHI)

```c
void rg_allocate_resources(
    rg_compile_context_t     *ctx,
    const RgAliasGroup       *alias_groups,
    uint32_t                  group_count
) {
    // For each alias group, allocate physical GPU resources via RHI:
    //   rhi()->texture_create(&desc)  or  rhi()->buffer_create(&desc)
    //
    // Aliased resources within a group share the same backing allocation
    // (heap_offset determines placement within a shared heap)
    //
    // Persistent (non-transient) resources use separate allocations
    // (they survive frame boundaries)
}
```

### Example Compile Output

```c
// After compile, a typical DAG might execute:
//
// 0. GBufferPass
//    Writes: gbuffer_albedo, gbuffer_normal, gbuffer_depth
//
// 1. LightingPass
//    Reads: gbuffer_albedo, gbuffer_normal, gbuffer_depth
//    Writes: hdr_radiance
//
// 2. SSAOPass (parallel: reads only gbuffer_depth, gbuffer_normal)
//    Reads: gbuffer_depth, gbuffer_normal
//    Writes: ssao_mask
//
// 3. SSRPass (can run in parallel or after SSAO)
//    Reads: hdr_radiance, gbuffer_depth, gbuffer_normal
//    Writes: reflection_buffer
//
// 4. TAAPass
//    Reads: hdr_radiance, reflection_buffer, depth_history, velocity
//    Writes: taa_output
//
// 5. BloomPass
//    Reads: taa_output
//    Writes: bloom_blur
//
// 6. TonemapPass
//    Reads: taa_output, bloom_blur
//    Writes: backbuffer
//
// Resource Alias Example:
// - ssao_temp_buffer (100 MB, lives pass 2 only)
// - ssr_temp_buffer (100 MB, lives pass 3 only)
// → aliased to same 100 MB heap (saves 100 MB VRAM)
```

---

## Execution Phase

Execution runs once per frame after `rg_compile()` succeeds.

```c
void rg_execute(RenderGraph *graph, RhiCmdList cmd) {
    for (uint32_t i = 0; i < graph->execution_order_count; i++) {
        uint64_t pass_idx = graph->execution_order[i];
        const RgPass *pass = &graph->passes[pass_idx];

        // Step 1: Insert state transitions via RHI
        for (uint32_t t = 0; t < pass->read_texture_count; t++) {
            RgTextureHandle h = pass->read_textures[t];
            const RgTexture *tex = rg_get_texture(graph, h);
            rhi()->cmd_barrier_texture(cmd, tex->rhi_handle,
                tex->initial_state, RHI_STATE_SHADER_READ);
        }
        for (uint32_t t = 0; t < pass->write_texture_count; t++) {
            RgTextureHandle h = pass->write_textures[t];
            const RgTexture *tex = rg_get_texture(graph, h);
            rhi()->cmd_barrier_texture(cmd, tex->rhi_handle,
                tex->initial_state, RHI_STATE_RENDER_TARGET);
        }
        // ... similar for buffers via rhi()->cmd_barrier_buffer()

        // Step 2: Execute pass callback
        RgPassContext ctx = { .cmd = cmd, .graph = graph, .pass = pass };
        pass->execute(&ctx, pass->user_data);

        // Step 3: Optional debug visualization
        if (pass->debug_visualize && pass->write_texture_count > 0) {
            rg_blit_resource_to_screen(graph, cmd, pass->write_textures[0]);
        }
    }
}
```

---

## Integration with Renderer

The renderer sets up the render graph each frame.

```c
void renderer_render_frame(RendererState *renderer, GameContext *game_ctx) {
    RenderGraph *graph = rg_create();

    // 1. Create resources
    RgTextureHandle gbuffer_albedo = rg_add_texture(graph,
        "gbuffer_albedo", RHI_FORMAT_RGBA8_UNORM,
        renderer->viewport_width, renderer->viewport_height, 1, true);

    RgTextureHandle gbuffer_normal = rg_add_texture(graph,
        "gbuffer_normal", RHI_FORMAT_RGB10A2_UNORM,
        renderer->viewport_width, renderer->viewport_height, 1, true);

    RgTextureHandle gbuffer_depth = rg_add_texture(graph,
        "gbuffer_depth", RHI_FORMAT_D32_FLOAT,
        renderer->viewport_width, renderer->viewport_height, 1, true);

    // 2. Add GBuffer pass
    RgPass *gbuffer_pass = rg_add_graphics_pass(graph, "gbuffer");
    rg_pass_write_texture(gbuffer_pass, gbuffer_albedo);
    rg_pass_write_texture(gbuffer_pass, gbuffer_normal);
    rg_pass_write_texture(gbuffer_pass, gbuffer_depth);

    GBufferPassData gbuffer_data = {
        .albedo = gbuffer_albedo,
        .normal = gbuffer_normal,
        .depth  = gbuffer_depth,
        .scene  = game_ctx->scene,
    };
    gbuffer_pass->execute   = gbuffer_pass_execute;
    gbuffer_pass->user_data = &gbuffer_data;

    // 3. Add Lighting pass
    RgTextureHandle hdr_radiance = rg_add_texture(graph,
        "hdr_radiance", RHI_FORMAT_RGBA16_FLOAT,
        renderer->viewport_width, renderer->viewport_height, 1, true);

    RgPass *lighting_pass = rg_add_graphics_pass(graph, "lighting");
    rg_pass_read_texture(lighting_pass, gbuffer_albedo);
    rg_pass_read_texture(lighting_pass, gbuffer_normal);
    rg_pass_read_texture(lighting_pass, gbuffer_depth);
    rg_pass_write_texture(lighting_pass, hdr_radiance);

    LightingPassData lighting_data = {
        .albedo     = gbuffer_albedo,
        .normal     = gbuffer_normal,
        .depth      = gbuffer_depth,
        .hdr_output = hdr_radiance,
        .scene      = game_ctx->scene,
    };
    lighting_pass->execute   = lighting_pass_execute;
    lighting_pass->user_data = &lighting_data;

    // 4. Add post-processes based on scene configuration
    if (renderer->post_process_config.ssao_enabled) {
        RgTextureHandle ssao_mask = rg_add_texture(graph,
            "ssao_mask", RHI_FORMAT_R8_UNORM,
            renderer->viewport_width / 2, renderer->viewport_height / 2,
            1, true);  // Half-res

        RgPass *ssao_pass = rg_add_compute_pass(graph, "ssao");
        rg_pass_read_texture(ssao_pass, gbuffer_depth);
        rg_pass_read_texture(ssao_pass, gbuffer_normal);
        rg_pass_write_texture(ssao_pass, ssao_mask);

        SSAOPassData ssao_data = {
            .depth      = gbuffer_depth,
            .normal     = gbuffer_normal,
            .ssao_mask  = ssao_mask,
        };
        ssao_pass->execute   = ssao_pass_execute;
        ssao_pass->user_data = &ssao_data;
    }

    if (renderer->post_process_config.ssr_enabled) {
        // Similar pattern for SSR
    }

    if (renderer->post_process_config.taa_enabled) {
        // Similar pattern for TAA
    }

    // 5. Compile the DAG
    rg_compile_result_t compile_result = rg_compile(graph, 512 * 1024 * 1024);  // 512 MB budget
    if (!compile_result.success) {
        LOG_ERROR("Render graph compilation failed: %s", compile_result.error_message);
        rg_destroy(graph);
        return;
    }

    // 6. Execute all passes
    RhiCmdList cmd = rhi()->cmd_begin();
    rg_execute(graph, cmd);
    rhi()->cmd_submit(cmd);
    rhi()->swapchain_present(1);

    rg_destroy(graph);
}
```

---

## Resource Usage Examples

### Example 1: Bloom Post-Process

```c
// Input: hdr_radiance texture
// Output: bloom effect applied to backbuffer

// Downsampling chain
RgTextureHandle bloom_down_4x = rg_add_texture(graph,
    "bloom_down_4x", RHI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4, 1, true);

RgPass *bloom_downsample = rg_add_compute_pass(graph, "bloom_downsample");
rg_pass_read_texture(bloom_downsample, hdr_radiance);
rg_pass_write_texture(bloom_downsample, bloom_down_4x);
bloom_downsample->execute   = bloom_downsample_execute;
bloom_downsample->user_data = &(BloomDownsampleData){
    .input = hdr_radiance, .output = bloom_down_4x
};

// Horizontal blur
RgTextureHandle bloom_h_blur = rg_add_texture(graph,
    "bloom_h_blur", RHI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4, 1, true);

RgPass *bloom_h = rg_add_compute_pass(graph, "bloom_blur_h");
rg_pass_read_texture(bloom_h, bloom_down_4x);
rg_pass_write_texture(bloom_h, bloom_h_blur);
bloom_h->execute   = bloom_blur_h_execute;
bloom_h->user_data = &(BloomBlurData){
    .input = bloom_down_4x, .output = bloom_h_blur
};

// Vertical blur
RgTextureHandle bloom_final = rg_add_texture(graph,
    "bloom_final", RHI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4, 1, true);

RgPass *bloom_v = rg_add_compute_pass(graph, "bloom_blur_v");
rg_pass_read_texture(bloom_v, bloom_h_blur);
rg_pass_write_texture(bloom_v, bloom_final);
bloom_v->execute   = bloom_blur_v_execute;
bloom_v->user_data = &(BloomBlurData){
    .input = bloom_h_blur, .output = bloom_final
};

// Composite: hdr_radiance + bloom_final → taa_output
// (happens in TAAPass or separate composite pass)
```

### Example 2: Shadow Depth Maps

```c
// For directional light with cascaded shadow maps:
//
// Allocate shadow atlas (single large texture with multiple cascades)
RgTextureHandle shadow_atlas = rg_add_texture(graph,
    "shadow_atlas", RHI_FORMAT_D32_FLOAT,
    4096, 4096,               // Large atlas
    1,
    true                      // Transient (recreated each frame)
);

// Add shadow rendering pass
RgPass *shadow_pass = rg_add_graphics_pass(graph, "shadow_depth");
rg_pass_write_texture(shadow_pass, shadow_atlas);

ShadowPassData shadow_data = {
    .atlas = shadow_atlas,
    .scene = scene,
};
shadow_pass->execute   = shadow_pass_execute;
shadow_pass->user_data = &shadow_data;

// Later, lighting pass reads shadow_atlas
```

---

## Debug & Visualization

### IPC Command: Inspect Intermediate Render Target

```c
// Editor command:
// debug.show_render_target("ssao_mask")
//
// Renders specified resource to a debug visualization panel in VSCode

typedef struct DebugRenderTargetRequest {
    const char *resource_name;  // "gbuffer_normal", "bloom_final", etc.
} DebugRenderTargetRequest;

void handle_debug_show_render_target(
    RenderGraph *graph,
    const DebugRenderTargetRequest *req
) {
    // Find resource by name
    RgTextureHandle h = rg_find_texture_by_name(graph, req->resource_name);

    if (h.index == 0) {
        return;  // Resource not found this frame
    }

    // Create a temporary blit pass to copy to a readable texture
    // Stream as JPEG back to editor via IPC

    const RgTexture *tex = rg_get_texture(graph, h);
    uint8_t *rgba_data = rg_read_texture_to_cpu(graph, tex->rhi_handle);
    uint8_t *jpeg = NULL;
    size_t   jpeg_size = 0;
    encode_jpeg(rgba_data, tex->width, tex->height, &jpeg, &jpeg_size);
    ipc_send_debug_texture(jpeg, jpeg_size);
}
```

### Graph Visualization

```c
// For debugging complex render graphs, IPC command:
// debug.dump_render_graph()
//
// Returns JSON describing the DAG:
{
  "passes": [
    {
      "id": 0,
      "name": "gbuffer",
      "type": "graphics",
      "reads": [],
      "writes": ["gbuffer_albedo", "gbuffer_normal", "gbuffer_depth"]
    },
    {
      "id": 1,
      "name": "lighting",
      "type": "graphics",
      "reads": ["gbuffer_albedo", "gbuffer_normal", "gbuffer_depth"],
      "writes": ["hdr_radiance"]
    }
  ],
  "resources": [
    {
      "name": "gbuffer_albedo",
      "type": "texture",
      "format": "RHI_FORMAT_RGBA8_UNORM",
      "width": 1920,
      "height": 1080,
      "lifetime": {
        "first_write_pass": 0,
        "last_read_pass": 1
      },
      "is_transient": true
    }
  ]
}
```

---

## Performance Considerations

### Compile Time

- **Small graphs** (5–10 passes): < 0.5 ms
- **Medium graphs** (20 passes): 1–2 ms
- **Large graphs** (50+ passes): 3–5 ms

Compile time is negligible vs. GPU frame time. Optimization only if profiling shows it's needed.

### Memory Aliasing Savings

Example: Post-process chain with multiple temporary buffers (all 1920x1080 RHI_FORMAT_RGBA16_FLOAT):

```
Without aliasing:
- ssao_temp:         10 MB
- ssr_temp:          10 MB
- bloom_temp_1:      10 MB
- bloom_temp_2:      10 MB
Total:               40 MB (wasted overlap)

With aliasing (non-overlapping lifetimes):
- Shared heap:       10 MB (reused 4 times)
Total:               10 MB (75% savings)
```

For GPU budgets on lower-end hardware (Intel Iris), this is critical.

### Execution Overhead

- Barrier insertion: ~0.01 ms per barrier (negligible if batched)
- Callback dispatch: Minimal (C function pointers, no overhead)
- No runtime dependency resolution (compile phase handles this)

---

## Integration with Related Systems

### Shader System

Each render pass binds shaders from the [Shader System](shader-system.md). The render graph is format-agnostic; shader bindings happen inside the pass callback.

### Editor Gizmos

Editor gizmos (grid, light wireframes, selected node outlines) are rendered as a separate pass at the end of the render graph.

### Screenshot & JPEG Encoding

When taking a screenshot, a special blit pass is inserted to copy the backbuffer to a readback texture, then encoded as JPEG for IPC transport.

### Related Docs

- [Renderer Architecture](../engine/lifecycle-and-concurrency.md) — Thread ownership and frame lifecycle
- [Shader System](shader-system.md) — PSO creation, hot-reload, variant system
- [Screen-Space Effects](screen-space-effects.md) — SSAO, SSR, TAA implementation details
- [Point & Spot Light Shadows](point-spot-light-shadows.md) — Shadow map rendering as render graph passes

---

## Test Coverage

### Unit Tests

```c
// Compile phase validation
void test_render_graph_validates_dag(void) {
    RenderGraph *graph = rg_create();

    // Add circular dependency
    RgPass *pass_a = rg_add_graphics_pass(graph, "pass_a");
    RgPass *pass_b = rg_add_graphics_pass(graph, "pass_b");
    RgTextureHandle tex = rg_add_texture(graph, "tex",
        RHI_FORMAT_RGBA8_UNORM, 512, 512, 1, true);

    rg_pass_write_texture(pass_a, tex);
    rg_pass_read_texture(pass_b, tex);
    rg_pass_write_texture(pass_b, tex);  // Circular!

    rg_compile_result_t result = rg_compile(graph, 512 * 1024 * 1024);
    ASSERT_FALSE(result.success);
    ASSERT_STR_CONTAINS(result.error_message, "circular");

    rg_destroy(graph);
}

void test_render_graph_computes_correct_execution_order(void) {
    RenderGraph *graph = rg_create();

    RgPass *pass_a = rg_add_graphics_pass(graph, "pass_a");
    RgPass *pass_b = rg_add_graphics_pass(graph, "pass_b");
    RgPass *pass_c = rg_add_graphics_pass(graph, "pass_c");

    RgTextureHandle tex_ab = rg_add_texture(graph, "tex_ab",
        RHI_FORMAT_RGBA8_UNORM, 512, 512, 1, true);
    RgTextureHandle tex_bc = rg_add_texture(graph, "tex_bc",
        RHI_FORMAT_RGBA8_UNORM, 512, 512, 1, true);

    rg_pass_write_texture(pass_a, tex_ab);
    rg_pass_read_texture(pass_b, tex_ab);
    rg_pass_write_texture(pass_b, tex_bc);
    rg_pass_read_texture(pass_c, tex_bc);

    rg_compile_result_t result = rg_compile(graph, 512 * 1024 * 1024);
    ASSERT_TRUE(result.success);

    // Verify execution order is a → b → c
    const uint64_t *order = rg_get_execution_order(graph);
    ASSERT_EQ(order[0], 0);  // pass_a
    ASSERT_EQ(order[1], 1);  // pass_b
    ASSERT_EQ(order[2], 2);  // pass_c

    rg_destroy(graph);
}

void test_render_graph_aliases_non_overlapping_resources(void) {
    RenderGraph *graph = rg_create();

    RgPass *pass_a = rg_add_graphics_pass(graph, "pass_a");
    RgPass *pass_b = rg_add_graphics_pass(graph, "pass_b");

    // Two 10 MB textures with non-overlapping lifetimes
    RgTextureHandle tex_a = rg_add_texture(graph, "temp_a",
        RHI_FORMAT_RGBA8_UNORM, 1024, 1024, 1, true);
    RgTextureHandle tex_b = rg_add_texture(graph, "temp_b",
        RHI_FORMAT_RGBA8_UNORM, 1024, 1024, 1, true);

    rg_pass_write_texture(pass_a, tex_a);
    rg_pass_write_texture(pass_b, tex_b);  // Runs after, so no overlap

    rg_compile_result_t result = rg_compile(graph, 512 * 1024 * 1024);
    ASSERT_TRUE(result.success);

    // Verify they're aliased (same heap offset)
    const RgTexture *tex_a_info = rg_get_texture(graph, tex_a);
    const RgTexture *tex_b_info = rg_get_texture(graph, tex_b);
    ASSERT_EQ(tex_a_info->heap_offset, tex_b_info->heap_offset);

    rg_destroy(graph);
}

void test_render_graph_rejects_exceeding_budget(void) {
    RenderGraph *graph = rg_create();

    // Create resources totaling > 256 MB
    char name_buf[64];
    for (int i = 0; i < 30; i++) {
        snprintf(name_buf, sizeof(name_buf), "large_tex_%d", i);
        rg_add_texture(graph, name_buf,
            RHI_FORMAT_RGBA16_FLOAT, 2048, 2048, 1, true);
    }

    RgPass *pass = rg_add_graphics_pass(graph, "test");
    // Add all textures to writes (forces allocation)

    rg_compile_result_t result = rg_compile(graph, 256 * 1024 * 1024);  // 256 MB budget
    ASSERT_FALSE(result.success);
    ASSERT_STR_CONTAINS(result.error_message, "budget");

    rg_destroy(graph);
}
```

### Integration Tests

```c
// End-to-end render graph execution
void test_render_graph_execute_simple_bloom_chain(void) {
    RenderGraph *graph = rg_create();

    // Set up minimal bloom chain
    RgTextureHandle input = rg_add_texture(graph, "input",
        RHI_FORMAT_RGBA16_FLOAT, 512, 512, 1, true);
    RgTextureHandle downsampled = rg_add_texture(graph, "downsampled",
        RHI_FORMAT_R11G11B10_FLOAT, 256, 256, 1, true);
    RgTextureHandle blurred = rg_add_texture(graph, "blurred",
        RHI_FORMAT_R11G11B10_FLOAT, 256, 256, 1, true);

    // Downsample pass
    RgPass *down_pass = rg_add_compute_pass(graph, "downsample");
    rg_pass_read_texture(down_pass, input);
    rg_pass_write_texture(down_pass, downsampled);
    down_pass->execute   = test_downsample_execute;
    down_pass->user_data = NULL;

    // Blur pass
    RgPass *blur_pass = rg_add_compute_pass(graph, "blur");
    rg_pass_read_texture(blur_pass, downsampled);
    rg_pass_write_texture(blur_pass, blurred);
    blur_pass->execute   = test_blur_execute;
    blur_pass->user_data = NULL;

    // Compile and execute
    rg_compile_result_t result = rg_compile(graph, 512 * 1024 * 1024);
    ASSERT_TRUE(result.success);

    RhiCmdList cmd = rhi()->cmd_begin();
    rg_execute(graph, cmd);
    rhi()->cmd_submit(cmd);

    // Verify blurred texture has valid data
    const RgTexture *blurred_tex = rg_get_texture(graph, blurred);
    ASSERT_NE(blurred_tex->rhi_handle, RHI_NULL_HANDLE);

    rg_destroy(graph);
}
```

---

## Phase Roadmap

### Phase 2 (Planned)

- Core render graph types and compile phase
- DAG validation and topological sort
- Resource aliasing for transient textures/buffers
- RHI integration (barriers via `rhi()->cmd_barrier_texture/buffer`, resource allocation via `rhi()->texture_create/buffer_create`)
- Basic post-process passes (bloom, SSAO, SSR, TAA)
- IPC debug visualization

### Phase 3+ (Future)

- GPU task scheduling (compute shader dispatch from render graph)
- Async texture streaming pipeline (integrated with render graph)
- Virtual memory swapping for large textures
- GPU-driven rendering (indirect dispatch from render graph)
- Performance profiling (per-pass GPU time capture)

---

## Related Docs

- [Renderer Architecture](../engine/lifecycle-and-concurrency.md) — Frame lifecycle and GPU synchronization
- [Shader System](shader-system.md) — Shader compilation, hot-reload, variant management
- [Screen-Space Effects](screen-space-effects.md) — SSAO, SSR, TAA technical details
- [Point & Spot Light Shadows](point-spot-light-shadows.md) — Shadow rendering as RenderGraph passes
- [Debug & Profiling](debug-profiling.md) — Tracy integration for per-pass GPU timing
- [RHI](rhi.md) — Handle types, vtable, and backend abstraction used by the Render Graph
