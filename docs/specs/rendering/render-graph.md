# Spec: Render Graph

> **Status**: Planned (Phase 2)
> **Owner**: `Renderer` module (src/engine/renderer/)
> **Purpose**: Frame resource abstraction, pass dependencies, automatic D3D12 barrier insertion, transient resource aliasing
> **Thread Safety**: Single-threaded (game thread only), compiled on-frame
> **Key Benefit**: Composable post-processes, automatic memory reuse, explicit data flow validation

---

## Overview

The render graph is a frame-local DAG (directed acyclic graph) that tracks all GPU resources and rendering passes, enabling:

1. **Explicit dependencies** — Each pass declares what it reads and writes
2. **Automatic barriers** — D3D12 state transitions inserted at resource lifetime boundaries
3. **Resource aliasing** — Transient textures/buffers with non-overlapping lifetimes share GPU memory
4. **Composable post-processes** — Add/remove passes without rewriting shader code
5. **Frame resource pooling** — Reuse allocations across frames within budget constraints

Key properties:

- **Compiled per frame** — DAG structure varies with scene (e.g., shadow maps only if lights cast shadows)
- **Debug visualization** — IPC command to inspect intermediate textures/buffers
- **Integration with renderer** — Renderer sets up core passes (GBuffer, lighting, post-processes)
- **Not a full render engine** — Just resource + dependency management; pass execution is callback-based

---

## Core Architecture

```
RenderGraph (per frame)
│
├── Resources (virtual handles)
│   ├── RgTexture: format, width, height, mips, usage flags
│   └── RgBuffer: element type, count, stride, usage flags
│
├── Passes (ordered by topological sort)
│   ├── RgPass: name, read/write sets, execute callback
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
    └── Pre-allocate D3D12 resources
```

### Design Goals

**Simplicity**: Render graphs can become complex (Frostbite, UE5). We optimize for:
- Clear data flow (no mysterious resource state bugs)
- Easy to add post-processes (declare reads/writes, add pass)
- No implicit resource creation

**Performance**:
- Compile phase runs once per frame (~1-2ms for 20-30 passes)
- Execution phase directly dispatches D3D12 commands (no indirection)
- Resource aliasing reduces VRAM pressure (critical for editor with many textures)

**AI-Native**:
- Render graph definition can be code-generated (MCP tool generates post-process chains)
- All resources and passes explicitly named (debuggable)
- IPC inspection of intermediate buffers

---

## Core Types

### RgHandle

Virtual handle to a render graph resource (texture or buffer).

```cpp
struct RgHandle {
    uint32_t index;     // 0 = invalid
    uint32_t generation;  // For reuse detection
};
```

Used internally only; external API returns `RgTextureHandle` or `RgBufferHandle`.

### RgTextureHandle & RgTexture

```cpp
struct RgTextureHandle {
    uint32_t index;
};

struct RgTexture {
    // Identity
    StringId name;              // "gbuffer_albedo", "bloom_temp", etc.

    // Format & Dimensions
    DXGI_FORMAT format;          // DXGI_FORMAT_R8G8B8A8_UNORM, etc.
    uint32_t width, height;      // In pixels
    uint32_t mip_levels;         // 1 for most; >1 for downsampling chains

    // Transience
    bool is_transient;           // True = aliased within frame
    uint64_t created_by_pass;    // Pass index that first writes
    uint64_t last_used_by_pass;  // Pass index that last reads (for lifetime)

    // D3D12 State
    D3D12_RESOURCE_STATES initial_state;
    ID3D12Resource* d3d_resource;  // Allocated during compile phase

    // Allocation
    uint64_t heap_offset;        // For aliased resources in shared heap
};
```

### RgBufferHandle & RgBuffer

```cpp
struct RgBufferHandle {
    uint32_t index;
};

struct RgBuffer {
    // Identity
    StringId name;              // "indirect_dispatch_args", etc.

    // Layout
    uint32_t element_size;      // sizeof(T)
    uint32_t element_count;     // For StructuredBuffer
    uint32_t total_bytes;       // = element_size * element_count

    // Transience
    bool is_transient;
    uint64_t created_by_pass;
    uint64_t last_used_by_pass;

    // D3D12 State
    D3D12_RESOURCE_STATES initial_state;
    ID3D12Resource* d3d_resource;

    // Allocation
    uint64_t heap_offset;
};
```

### RgPass

Represents a single rendering pass with explicit read/write resource sets.

```cpp
struct RgPass {
    // Identity
    StringId name;              // "gbuffer", "lighting", "ssao", etc.
    uint64_t pass_id;           // Unique index in graph

    // Reads & Writes
    std::vector<RgTextureHandle> reads_textures;
    std::vector<RgBufferHandle> reads_buffers;
    std::vector<RgTextureHandle> writes_textures;
    std::vector<RgBufferHandle> writes_buffers;

    // Execution
    using ExecuteCallback = std::function<void(RgPassContext&)>;
    ExecuteCallback execute;    // Called with shader resources bound

    // Metadata
    enum class Type { Graphics, Compute, Copy };
    Type type;

    // Debugging
    bool debug_visualize;       // Blit output to screen after pass
};
```

### RgPassContext

Passed to each `execute` callback with bound resources and utilities.

```cpp
struct RgPassContext {
    // Command list (pre-transitioned to required states)
    ID3D12GraphicsCommandList* cmd_list;

    // Access to resources within the graph
    ID3D12Resource* get_texture(RgTextureHandle h);
    ID3D12Resource* get_buffer(RgBufferHandle h);
    D3D12_GPU_DESCRIPTOR_HANDLE get_texture_srv(RgTextureHandle h);
    D3D12_GPU_DESCRIPTOR_HANDLE get_buffer_srv(RgBufferHandle h);

    // Render target / UAV binding helpers
    void set_render_target(RgTextureHandle rt, RgTextureHandle depth = {});
    void bind_uav(uint32_t slot, RgTextureHandle uav);
    void bind_uav(uint32_t slot, RgBufferHandle uav);

    // Metadata
    const RgPass& pass;
    const RenderGraph& graph;
};
```

### RenderGraph

The container managing all resources and passes for one frame.

```cpp
class RenderGraph {
public:
    // Resource Creation (building phase, before compile)
    RgTextureHandle create_texture(
        StringId name,
        DXGI_FORMAT format,
        uint32_t width, uint32_t height,
        uint32_t mip_levels = 1,
        bool is_transient = true
    );

    RgBufferHandle create_buffer(
        StringId name,
        uint32_t element_size,
        uint32_t element_count,
        bool is_transient = true
    );

    // Pass Registration (building phase)
    RgPass& add_pass(
        StringId name,
        RgPass::Type type = RgPass::Type::Graphics
    );
    RgPass& add_graphics_pass(StringId name);
    RgPass& add_compute_pass(StringId name);
    RgPass& add_copy_pass(StringId name);

    // Dependency Declaration (building phase)
    void add_read(RgPass& pass, RgTextureHandle texture);
    void add_read(RgPass& pass, RgBufferHandle buffer);
    void add_write(RgPass& pass, RgTextureHandle texture);
    void add_write(RgPass& pass, RgBufferHandle buffer);

    // Compile (called once per frame after building)
    struct CompileResult {
        bool success;
        std::string error_message;
    };
    CompileResult compile(ID3D12Device* device, uint64_t gpu_budget_bytes = 512 * 1024 * 1024);

    // Execution (called once per frame after compile)
    void execute(ID3D12GraphicsCommandList* cmd_list);

    // Debugging
    void enable_debug_visualization(RgTextureHandle h);
    void enable_debug_visualization(RgBufferHandle h);
    std::vector<StringId> get_resource_names() const;
    const RgTexture& get_texture(RgTextureHandle h) const;
    const RgBuffer& get_buffer(RgBufferHandle h) const;
};
```

---

## Compile Phase

The compile phase runs once per frame after all passes are registered.

### Step 1: DAG Validation

```cpp
struct CompileContext {
    std::vector<RgPass> passes;
    std::vector<RgTexture> textures;
    std::vector<RgBuffer> buffers;
};

bool validate_dag(const CompileContext& ctx) {
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

```cpp
std::vector<uint64_t> topological_sort(const CompileContext& ctx) {
    // Kahn's algorithm: sort passes by dependency order
    // Passes with no dependencies run first
    // A pass can run after all its dependencies have been satisfied

    // Result: execution_order[i] = pass index to run at step i
    return execution_order;
}
```

### Step 3: Compute Resource Lifetimes

```cpp
struct ResourceLifetime {
    uint64_t first_write_pass;   // When resource is first produced
    uint64_t last_read_pass;     // When resource is last consumed
};

std::vector<ResourceLifetime> compute_lifetimes(
    const CompileContext& ctx,
    const std::vector<uint64_t>& execution_order
) {
    // For each resource, find:
    // - first pass that writes to it (start of lifetime)
    // - last pass that reads from it (end of lifetime)

    // Two resources can alias if their lifetimes don't overlap:
    // resource_a.last_read < resource_b.first_write OR vice versa

    return lifetimes;
}
```

### Step 4: Resource Aliasing

```cpp
struct AliasGroup {
    std::vector<uint32_t> resource_indices;  // Textures or buffers with same size
    uint64_t heap_offset;
};

std::vector<AliasGroup> compute_alias_groups(
    const std::vector<ResourceLifetime>& lifetimes,
    const std::vector<RgTexture>& textures,
    uint64_t total_budget
) {
    // Group transient resources by:
    // 1. Type (all textures or all buffers)
    // 2. Size (must be exactly the same to alias)
    // 3. Non-overlapping lifetimes

    // Assign heap offsets to minimize peak VRAM usage
    // Validate total against gpu_budget

    return alias_groups;
}
```

### Step 5: D3D12 Resource Allocation

```cpp
void allocate_resources(
    CompileContext& ctx,
    const std::vector<AliasGroup>& alias_groups,
    ID3D12Device* device
) {
    // For each alias group, create a single D3D12 heap
    // Use D3D12_HEAP_TYPE_DEFAULT for GPU resources

    // For each resource in the group, create a D3D12_PLACED_RESOURCE
    // pointing to a heap offset

    // Persistent (non-transient) resources use separate heaps
    // (they survive frame boundaries)
}
```

### Example Compile Output

```cpp
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

Execution runs once per frame after `compile()` succeeds.

```cpp
void RenderGraph::execute(ID3D12GraphicsCommandList* cmd_list) {
    for (uint64_t pass_idx : execution_order) {
        const RgPass& pass = passes[pass_idx];

        // Step 1: Insert state transitions
        for (RgTextureHandle h : pass.reads_textures) {
            insert_barrier(cmd_list, get_texture(h),
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        for (RgTextureHandle h : pass.writes_textures) {
            insert_barrier(cmd_list, get_texture(h),
                          D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        // ... similar for buffers

        // Step 2: Execute pass callback
        RgPassContext ctx{cmd_list, *this, pass};
        pass.execute(ctx);

        // Step 3: Optional debug visualization
        if (pass.debug_visualize) {
            blit_resource_to_screen(cmd_list, pass.writes_textures[0]);
        }
    }
}
```

---

## Integration with Renderer

The `Renderer` class sets up the render graph each frame.

```cpp
class Renderer {
private:
    RenderGraph graph;

public:
    void render_frame(GameContext& game_ctx) {
        // 1. Create resources
        RgTextureHandle gbuffer_albedo = graph.create_texture(
            "gbuffer_albedo"_sid,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            viewport_width, viewport_height
        );
        RgTextureHandle gbuffer_normal = graph.create_texture(
            "gbuffer_normal"_sid,
            DXGI_FORMAT_R10G10B10A2_UNORM,
            viewport_width, viewport_height
        );
        RgTextureHandle gbuffer_depth = graph.create_texture(
            "gbuffer_depth"_sid,
            DXGI_FORMAT_D32_FLOAT,
            viewport_width, viewport_height
        );

        // 2. Add GBuffer pass
        RgPass& gbuffer_pass = graph.add_graphics_pass("gbuffer");
        gbuffer_pass.writes_textures = {gbuffer_albedo, gbuffer_normal, gbuffer_depth};
        gbuffer_pass.execute = [this, gbuffer_albedo, gbuffer_normal, gbuffer_depth]
            (RgPassContext& ctx) {
            render_gbuffer(ctx, gbuffer_albedo, gbuffer_normal, gbuffer_depth);
        };

        // 3. Add Lighting pass
        RgPass& lighting_pass = graph.add_graphics_pass("lighting");
        lighting_pass.reads_textures = {gbuffer_albedo, gbuffer_normal, gbuffer_depth};
        RgTextureHandle hdr_radiance = graph.create_texture(
            "hdr_radiance"_sid,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            viewport_width, viewport_height
        );
        lighting_pass.writes_textures = {hdr_radiance};
        lighting_pass.execute = [this, gbuffer_albedo, gbuffer_normal, gbuffer_depth, hdr_radiance]
            (RgPassContext& ctx) {
            render_deferred_lighting(ctx, gbuffer_albedo, gbuffer_normal, gbuffer_depth, hdr_radiance);
        };

        // 4. Add post-processes based on scene configuration
        if (post_process_config.ssao_enabled) {
            RgPass& ssao_pass = graph.add_compute_pass("ssao");
            ssao_pass.reads_textures = {gbuffer_depth, gbuffer_normal};
            RgTextureHandle ssao_mask = graph.create_texture(
                "ssao_mask"_sid,
                DXGI_FORMAT_R8_UNORM,
                viewport_width / 2, viewport_height / 2  // Half-res
            );
            ssao_pass.writes_textures = {ssao_mask};
            ssao_pass.execute = [this, gbuffer_depth, gbuffer_normal, ssao_mask]
                (RgPassContext& ctx) {
                compute_hbao(ctx, gbuffer_depth, gbuffer_normal, ssao_mask);
            };
        }

        if (post_process_config.ssr_enabled) {
            // Similar pattern for SSR
        }

        if (post_process_config.taa_enabled) {
            // Similar pattern for TAA
        }

        // 5. Compile the DAG
        auto compile_result = graph.compile(device, 512 * 1024 * 1024);  // 512 MB budget
        if (!compile_result.success) {
            ERROR("Render graph compilation failed: {}", compile_result.error_message);
            return;
        }

        // 6. Execute all passes
        graph.execute(cmd_list);
    }
};
```

---

## Resource Usage Examples

### Example 1: Bloom Post-Process

```cpp
// Input: hdr_radiance texture
// Output: bloom effect applied to backbuffer

// Downsampling chain
RgTextureHandle bloom_down_4x = graph.create_texture(
    "bloom_down_4x"_sid,
    DXGI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4
);

RgPass& bloom_downsample = graph.add_compute_pass("bloom_downsample");
bloom_downsample.reads_textures = {hdr_radiance};
bloom_downsample.writes_textures = {bloom_down_4x};
bloom_downsample.execute = [this, hdr_radiance, bloom_down_4x](RgPassContext& ctx) {
    // Dispatch compute shader with 4x downsampling
    // Write to bloom_down_4x
};

// Horizontal blur
RgTextureHandle bloom_h_blur = graph.create_texture(
    "bloom_h_blur"_sid,
    DXGI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4
);

RgPass& bloom_h = graph.add_compute_pass("bloom_blur_h");
bloom_h.reads_textures = {bloom_down_4x};
bloom_h.writes_textures = {bloom_h_blur};
bloom_h.execute = [this, bloom_down_4x, bloom_h_blur](RgPassContext& ctx) {
    compute_blur_h(ctx, bloom_down_4x, bloom_h_blur);
};

// Vertical blur
RgTextureHandle bloom_final = graph.create_texture(
    "bloom_final"_sid,
    DXGI_FORMAT_R11G11B10_FLOAT,
    viewport_width / 4, viewport_height / 4
);

RgPass& bloom_v = graph.add_compute_pass("bloom_blur_v");
bloom_v.reads_textures = {bloom_h_blur};
bloom_v.writes_textures = {bloom_final};
bloom_v.execute = [this, bloom_h_blur, bloom_final](RgPassContext& ctx) {
    compute_blur_v(ctx, bloom_h_blur, bloom_final);
};

// Composite: hdr_radiance + bloom_final → taa_output
// (happens in TAAPass or separate composite pass)
```

### Example 2: Shadow Depth Maps

```cpp
// For directional light with cascaded shadow maps:
//
// Allocate shadow atlas (single large texture with multiple cascades)
RgTextureHandle shadow_atlas = graph.create_texture(
    "shadow_atlas"_sid,
    DXGI_FORMAT_R32_TYPELESS,  // Depth format
    4096, 4096,               // Large atlas
    1,
    true  // Transient (recreated each frame)
);

// Add shadow rendering pass
RgPass& shadow_pass = graph.add_graphics_pass("shadow_depth");
shadow_pass.writes_textures = {shadow_atlas};
shadow_pass.execute = [this, shadow_atlas](RgPassContext& ctx) {
    // Render all shadow-casting meshes to atlas
    // 3 cascades, each to different region of atlas
    // Uses depth format for PCF filtering
};

// Later, lighting pass reads shadow_atlas
```

---

## Debug & Visualization

### IPC Command: Inspect Intermediate Render Target

```cpp
// Editor command:
// debug.show_render_target("ssao_mask")
//
// Renders specified resource to a debug visualization panel in VSCode

struct DebugRenderTargetRequest {
    std::string resource_name;  // "gbuffer_normal", "bloom_final", etc.
};

void handle_debug_show_render_target(const DebugRenderTargetRequest& req) {
    // Find resource by name
    RgTextureHandle h = graph.get_texture_handle_by_name(req.resource_name);

    if (!h.is_valid()) {
        return;  // Resource not found this frame
    }

    // Create a temporary blit pass to copy to a readable texture
    // Stream as JPEG back to editor via IPC

    uint8_t* rgba_data = read_texture_to_cpu(graph.get_texture(h));
    std::vector<uint8_t> jpeg = encode_jpeg(rgba_data, width, height);
    ipc_send_debug_texture(jpeg);
}
```

### Graph Visualization

```cpp
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
      "format": "DXGI_FORMAT_R8G8B8A8_UNORM",
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

Example: Post-process chain with multiple temporary buffers (all 1920x1080 R16G16B16A16_FLOAT):

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
- Callback dispatch: Minimal (function pointers, no overhead)
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

```cpp
// Compile phase validation
TEST(RenderGraphCompile, ValidatesDAG) {
    RenderGraph graph;

    // Add circular dependency
    RgPass& pass_a = graph.add_graphics_pass("pass_a");
    RgPass& pass_b = graph.add_graphics_pass("pass_b");
    RgTextureHandle tex = graph.create_texture("tex", DXGI_FORMAT_R8G8B8A8_UNORM, 512, 512);

    pass_a.writes_textures = {tex};
    pass_b.reads_textures = {tex};
    pass_b.writes_textures = {tex};  // Circular!

    auto result = graph.compile(device, 512 * 1024 * 1024);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, HasSubstr("circular"));
}

TEST(RenderGraphCompile, ComputesCorrectExecutionOrder) {
    RenderGraph graph;

    RgPass& pass_a = graph.add_graphics_pass("pass_a");
    RgPass& pass_b = graph.add_graphics_pass("pass_b");
    RgPass& pass_c = graph.add_graphics_pass("pass_c");

    RgTextureHandle tex_ab = graph.create_texture("tex_ab", DXGI_FORMAT_R8G8B8A8_UNORM, 512, 512);
    RgTextureHandle tex_bc = graph.create_texture("tex_bc", DXGI_FORMAT_R8G8B8A8_UNORM, 512, 512);

    pass_a.writes_textures = {tex_ab};
    pass_b.reads_textures = {tex_ab};
    pass_b.writes_textures = {tex_bc};
    pass_c.reads_textures = {tex_bc};

    auto result = graph.compile(device, 512 * 1024 * 1024);
    EXPECT_TRUE(result.success);

    // Verify execution order is a → b → c
    const auto& order = graph.execution_order();
    EXPECT_EQ(order[0], 0);  // pass_a
    EXPECT_EQ(order[1], 1);  // pass_b
    EXPECT_EQ(order[2], 2);  // pass_c
}

TEST(RenderGraphCompile, AliasesNonOverlappingResources) {
    RenderGraph graph;

    RgPass& pass_a = graph.add_graphics_pass("pass_a");
    RgPass& pass_b = graph.add_graphics_pass("pass_b");

    // Two 10 MB textures with non-overlapping lifetimes
    RgTextureHandle tex_a = graph.create_texture("temp_a", DXGI_FORMAT_R8G8B8A8_UNORM, 1024, 1024);
    RgTextureHandle tex_b = graph.create_texture("temp_b", DXGI_FORMAT_R8G8B8A8_UNORM, 1024, 1024);

    pass_a.writes_textures = {tex_a};
    pass_b.writes_textures = {tex_b};  // Runs after, so no overlap

    auto result = graph.compile(device, 512 * 1024 * 1024);
    EXPECT_TRUE(result.success);

    // Verify they're aliased (same heap offset)
    const auto& tex_a_info = graph.get_texture(tex_a);
    const auto& tex_b_info = graph.get_texture(tex_b);
    EXPECT_EQ(tex_a_info.heap_offset, tex_b_info.heap_offset);
}

TEST(RenderGraphCompile, RejectsExceedingBudget) {
    RenderGraph graph;

    // Create resources totaling > 256 MB
    for (int i = 0; i < 30; i++) {
        graph.create_texture(
            fmt::format("large_tex_{}", i),
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            2048, 2048
        );
    }

    RgPass& pass = graph.add_graphics_pass("test");
    // Add all textures to writes (forces allocation)

    auto result = graph.compile(device, 256 * 1024 * 1024);  // 256 MB budget
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, HasSubstr("budget"));
}
```

### Integration Tests

```cpp
// End-to-end render graph execution
TEST(RenderGraphExecute, RenderSimpleBloomChain) {
    RenderGraph graph;

    // Set up minimal bloom chain
    RgTextureHandle input = create_test_hdr_texture(graph, "input", 512, 512);
    RgTextureHandle downsampled = graph.create_texture("downsampled",
        DXGI_FORMAT_R11G11B10_FLOAT, 256, 256);
    RgTextureHandle blurred = graph.create_texture("blurred",
        DXGI_FORMAT_R11G11B10_FLOAT, 256, 256);

    // Downsample pass
    RgPass& down_pass = graph.add_compute_pass("downsample");
    down_pass.reads_textures = {input};
    down_pass.writes_textures = {downsampled};
    down_pass.execute = [](RgPassContext& ctx) {
        // Dispatch downsample shader
    };

    // Blur pass
    RgPass& blur_pass = graph.add_compute_pass("blur");
    blur_pass.reads_textures = {downsampled};
    blur_pass.writes_textures = {blurred};
    blur_pass.execute = [](RgPassContext& ctx) {
        // Dispatch blur shader
    };

    // Compile and execute
    auto result = graph.compile(device, 512 * 1024 * 1024);
    EXPECT_TRUE(result.success);

    graph.execute(cmd_list);

    // Verify blurred texture has valid data
    auto readback_data = read_texture_to_cpu(graph.get_texture(blurred));
    EXPECT_FALSE(readback_data.empty());
}
```

---

## Phase Roadmap

### Phase 2 (Planned)

- Core render graph types and compile phase
- DAG validation and topological sort
- Resource aliasing for transient textures/buffers
- D3D12 integration (barriers, resource allocation)
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
