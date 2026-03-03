# Spec: RHI — Render Hardware Interface

> **Status**: Design (Phase 0-PRE)
> **Owner**: `Renderer` module (`src/engine/renderer/rhi/`)
> **Purpose**: Platform-agnostic GPU abstraction layer. All rendering code above this layer is backend-agnostic.
> **Backends**: DX12 (Phase 1+), Vulkan (Phase 2+), Metal (Phase 3+)

---

## Problem Statement

UzEngine targets Windows, Linux, macOS, Android, and iOS. Each platform has a different low-level GPU API:

| Platform | Native API | Shader IR |
|----------|-----------|-----------|
| Windows  | DX12      | DXIL      |
| Linux    | Vulkan    | SPIR-V    |
| macOS    | Metal     | MSL / AIR |
| Android  | Vulkan    | SPIR-V    |
| iOS      | Metal     | MSL / AIR |

Without an abstraction layer, every rendering system (PBR, shadows, post-process, UI) would need to be written per-backend — multiplying complexity by 3×.

The RHI solves this by defining a **single C API** that all rendering code uses. Backends implement this API for each platform.

---

## Design Principles

1. **HLSL as the single shader authoring language** — one source of truth, compiled to IR per backend
2. **Thin C API** — no virtual dispatch, no inheritance, function pointer table (vtable) per backend
3. **Explicit resource model** — no hidden state, explicit barriers/transitions surface to the RHI layer
4. **Handle-based resources** — all GPU resources are opaque `uint64_t` handles, no raw pointers
5. **No per-frame allocations** — resource descriptors allocated from arenas, not heap
6. **Offline shader compilation** — HLSL → SPIR-V done at asset cook time, not runtime

---

## Shader Compilation Pipeline

```
HLSL Source (.hlsl)              ← Single source of truth, human + AI editable
        │
        ▼
    DXC (DirectX Compiler)       ← Open source, cross-platform CLI tool
        │
        ▼
    SPIR-V (.spv)                ← Canonical IR, stored in .uzpak
        │
   ┌────┼────────────┐
   ▼    ▼            ▼
  DXIL  SPIR-V      MSL
(DX12) (Vulkan)   (Metal)
        via          via
   DXC  pass    SPIRV-Cross
```

### Tool Chain

| Tool | Role | License |
|------|------|---------|
| **DXC** (DirectXShaderCompiler) | HLSL → DXIL (DX12) and HLSL → SPIR-V | MIT |
| **SPIRV-Cross** | SPIR-V → MSL (Metal), SPIR-V → GLSL ES (Android debug) | Apache 2.0 |
| **SPIRV-Tools** | SPIR-V validation + optimization (`spirv-opt`) | Apache 2.0 |

All tools are integrated via vcpkg. No runtime compilation except during hot-reload in editor mode.

### Compilation Modes

| Mode | When | Process |
|------|------|---------|
| **Offline cook** | `UzBuild --cook` | HLSL → SPIR-V → backend IR stored in `.uzpak` |
| **Editor hot-reload** | FileWatcher detects `.hlsl` change | Same pipeline, in-process, < 200ms |
| **Runtime fallback** | Never in production | — |

---

## HLSL Authoring Rules

All shaders are written in HLSL. Portability constraints apply:

### Binding Model (must work across all backends)

```hlsl
// ✅ Correct — explicit binding slots, compatible with Vulkan set/binding mapping
cbuffer PerFrame : register(b0, space0) { ... }
cbuffer PerObject : register(b1, space0) { ... }
Texture2D AlbedoTex : register(t0, space0);
SamplerState Sampler : register(s0, space0);

// ❌ Forbidden — DX12-only rootconstants without SPIRV-Cross annotation
// ❌ Forbidden — SM6.6+ features not yet supported by DXC→SPIR-V path
// ❌ Forbidden — DirectX-specific intrinsics with no SPIR-V equivalent
```

### HLSL Feature Compatibility Matrix

| Feature | DX12 | Vulkan | Metal | Notes |
|---------|------|--------|-------|-------|
| `SV_InstanceID` | ✅ | ✅ | ✅ | GPU instancing — safe |
| `SV_DispatchThreadID` | ✅ | ✅ | ✅ | Compute — safe |
| `SV_Barycentrics` | ✅ | ✅ (ext) | ✅ | Check ext availability |
| `RayQuery` | ✅ SM6.5 | ✅ VK_KHR | ✅ Metal3 | Guarded by capability flag |
| `WaveOps` | ✅ SM6.0 | ✅ subgroups | ✅ SIMD | Guarded by capability flag |
| Root Sig annotations | ✅ | ❌ strip | ❌ strip | DXC strips for non-DX12 |
| `cbuffer` | ✅ | ✅ | ✅ via UBO | SPIRV-Cross handles |
| `StructuredBuffer` | ✅ | ✅ SSBO | ✅ | Safe |

---

## RHI API (C Interface)

The entire RHI is a `rhi_backend_t` struct of function pointers. Engine code calls through this struct — never directly into DX12/Vulkan.

```c
// src/engine/renderer/rhi/rhi.h

typedef uint64_t RhiBuffer;
typedef uint64_t RhiTexture;
typedef uint64_t RhiPipeline;
typedef uint64_t RhiRenderPass;
typedef uint64_t RhiCmdList;
typedef uint64_t RhiFence;

typedef enum RhiBackend {
    RHI_BACKEND_DX12   = 0,
    RHI_BACKEND_VULKAN = 1,
    RHI_BACKEND_METAL  = 2,
} RhiBackend;

typedef enum RhiFormat {
    RHI_FORMAT_RGBA8_UNORM,
    RHI_FORMAT_RGBA16_FLOAT,
    RHI_FORMAT_R32_FLOAT,
    RHI_FORMAT_R8_UNORM,
    RHI_FORMAT_R11G11B10_FLOAT,
    RHI_FORMAT_RGB10A2_UNORM,
    RHI_FORMAT_D32_FLOAT,
    RHI_FORMAT_BC7_UNORM,
    RHI_FORMAT_BC5_UNORM,
    RHI_FORMAT_BC6H_UF16,
    // ...
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

typedef struct RhiTextureDesc {
    uint32_t            width, height, depth;
    uint32_t            mip_levels;
    uint32_t            array_size;   // 1 for regular textures, 6 for cube maps
    RhiFormat           format;
    RhiTextureDimension dimension;    // 2D, 3D, Cube, or 2D Array
    uint32_t            usage;        // RHI_USAGE_* flags
    const char         *debug_name;
} RhiTextureDesc;

typedef struct RhiBufferDesc {
    uint64_t   size;
    uint32_t   usage;       // RHI_BUFFER_USAGE_* flags
    const char *debug_name;
} RhiBufferDesc;

typedef struct RhiShaderDesc {
    const void *spirv_data;  // SPIR-V bytecode (canonical IR)
    size_t      spirv_size;
    const char *entry_point; // "VSMain", "PSMain", "CSMain"
} RhiShaderDesc;

typedef struct RhiPipelineDesc {
    RhiShaderDesc vertex_shader;
    RhiShaderDesc pixel_shader;
    RhiFormat     rt_formats[8];
    uint32_t      rt_count;
    RhiFormat     ds_format;
    // rasterizer, blend, depth-stencil state...
} RhiPipelineDesc;

typedef struct RhiComputePipelineDesc {
    RhiShaderDesc compute_shader;
    const char   *debug_name;
} RhiComputePipelineDesc;

// Backend vtable — the entire RHI surface
typedef struct rhi_backend_t {
    // Lifecycle
    bool (*init)(void *window_handle, uint32_t width, uint32_t height);
    void (*shutdown)(void);
    void (*resize)(uint32_t width, uint32_t height);

    // Resources
    RhiBuffer  (*buffer_create)(const RhiBufferDesc *desc);
    void       (*buffer_destroy)(RhiBuffer buf);
    void      *(*buffer_map)(RhiBuffer buf);
    void       (*buffer_unmap)(RhiBuffer buf);
    void       (*buffer_upload)(RhiBuffer buf, const void *data, size_t size);

    RhiTexture (*texture_create)(const RhiTextureDesc *desc);
    void       (*texture_destroy)(RhiTexture tex);
    void       (*texture_upload)(RhiTexture tex, const void *data, size_t size);

    // Pipelines (backed by PSO cache — keyed by SPIR-V hash)
    RhiPipeline (*pipeline_create)(const RhiPipelineDesc *desc);
    RhiPipeline (*compute_pipeline_create)(const RhiComputePipelineDesc *desc);
    void        (*pipeline_destroy)(RhiPipeline pipeline);

    // Command recording
    RhiCmdList (*cmd_begin)(void);
    void       (*cmd_end)(RhiCmdList cmd);
    void       (*cmd_submit)(RhiCmdList cmd);

    void (*cmd_set_pipeline)(RhiCmdList cmd, RhiPipeline pipeline);
    void (*cmd_set_render_targets)(RhiCmdList cmd, const RhiTexture *rts, uint32_t count, RhiTexture depth);
    void (*cmd_set_viewport)(RhiCmdList cmd, float x, float y, float w, float h, float min_d, float max_d);
    void (*cmd_set_scissor)(RhiCmdList cmd, int32_t x, int32_t y, uint32_t w, uint32_t h);
    void (*cmd_set_vertex_buffer)(RhiCmdList cmd, uint32_t slot, RhiBuffer buf, uint64_t offset, uint32_t stride);
    void (*cmd_set_index_buffer)(RhiCmdList cmd, RhiBuffer buf, uint64_t offset, bool is_32bit);
    void (*cmd_set_constant_buffer)(RhiCmdList cmd, uint32_t slot, RhiBuffer buf);
    void (*cmd_set_texture)(RhiCmdList cmd, uint32_t slot, RhiTexture tex);
    void (*cmd_draw)(RhiCmdList cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
    void (*cmd_draw_indexed)(RhiCmdList cmd, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);
    void (*cmd_dispatch)(RhiCmdList cmd, uint32_t x, uint32_t y, uint32_t z);

    // Barriers (explicit, maps to D3D12 barriers / Vulkan pipeline barriers / Metal hazard tracking)
    void (*cmd_barrier_texture)(RhiCmdList cmd, RhiTexture tex, RhiResourceState src_state, RhiResourceState dst_state);
    void (*cmd_barrier_buffer)(RhiCmdList cmd, RhiBuffer buf, RhiResourceState src_state, RhiResourceState dst_state);

    // Swapchain
    RhiTexture (*swapchain_backbuffer)(void);
    void       (*swapchain_present)(uint32_t sync_interval);

    // Sync
    RhiFence (*fence_create)(void);
    void     (*fence_destroy)(RhiFence fence);
    void     (*fence_signal)(RhiFence fence, uint64_t value);
    bool     (*fence_wait)(RhiFence fence, uint64_t value, uint64_t timeout_ns);

    // Debug
    void (*set_debug_name)(uint64_t handle, const char *name);
    void (*begin_event)(RhiCmdList cmd, const char *name);
    void (*end_event)(RhiCmdList cmd);
} rhi_backend_t;

// Global accessor — set once at engine init, used everywhere
const rhi_backend_t *rhi(void);
```

---

## Backend Implementations

```
src/engine/renderer/rhi/
├── rhi.h               ← Public API (above)
├── rhi_dx12.h/c        ← DX12 implementation (Phase 1, Windows only)
├── rhi_vulkan.h/c      ← Vulkan implementation (Phase 2, Windows + Linux + Android)
├── rhi_metal.h/c       ← Metal implementation (Phase 3, macOS + iOS)
└── rhi_common.c        ← Shared helpers, PSO cache, SPIR-V → backend transpilation
```

### Backend Selection

```c
// src/engine/renderer/renderer.c
void renderer_init(void *window_handle, uint32_t width, uint32_t height) {
#if defined(UZ_BACKEND_DX12)
    g_rhi = &rhi_dx12_backend;
#elif defined(UZ_BACKEND_VULKAN)
    g_rhi = &rhi_vulkan_backend;
#elif defined(UZ_BACKEND_METAL)
    g_rhi = &rhi_metal_backend;
#else
    #error "No RHI backend defined. Set UZ_BACKEND_DX12, UZ_BACKEND_VULKAN, or UZ_BACKEND_METAL."
#endif
    rhi()->init(window_handle, width, height);
}
```

Compile-time selection via CMake:
```cmake
# CMakeLists.txt
if(WIN32)
    target_compile_definitions(UzEngine PRIVATE UZ_BACKEND_DX12)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(UzEngine PRIVATE UZ_BACKEND_VULKAN)
elseif(APPLE)
    target_compile_definitions(UzEngine PRIVATE UZ_BACKEND_METAL)
endif()
```

---

## SPIR-V as Canonical IR

SPIR-V is the single intermediate representation stored on disk. At cook time:

```
UzBuild --cook:
  for each .hlsl in project:
    1. DXC: hlsl → spirv (.spv) + dxil (.bin for DX12 on Windows)
    2. spirv-opt: optimize .spv (O2)
    3. SPIRV-Cross (macOS build): spirv → msl (.metal)
    4. Pack all variants into .uzpak under shaders/<name>/{spirv,dxil,msl}
```

At runtime, the backend loads only the IR it needs:
- DX12 loads DXIL (fastest path, no transpilation)
- Vulkan loads SPIR-V directly
- Metal loads pre-compiled MSL or AIR (Apple offline compiler)

### Hot-reload in Editor (Windows only)

```
FileWatcher detects .hlsl change
  → DXC recompiles HLSL → SPIR-V (in-process, ~50ms)
  → DX12 backend: DXC also emits DXIL directly
  → PSO cache invalidated for affected pipelines
  → New PSOs compiled on next frame
  → VSCode notified via IPC: shader.reloaded
```

---

## PSO Cache

Pipelines are expensive to create. The PSO cache deduplicates by key:

```c
typedef struct PsoCacheKey {
    uint64_t vs_spirv_hash;   // xxHash of vertex shader SPIR-V
    uint64_t ps_spirv_hash;   // xxHash of pixel shader SPIR-V
    uint32_t rt_formats_hash; // hash of render target format array
    uint32_t state_hash;      // hash of rasterizer + blend + ds state
} PsoCacheKey;
```

Cache is persistent across sessions (stored in `.uz_cache/pso/`), invalidated when SPIR-V hash changes.

---

## Render Graph Integration

The Render Graph (see `render-graph.md`) operates **above** the RHI. It uses only RHI handles:

```
RenderGraph
   │ uses RhiTexture, RhiBuffer, RhiPipeline handles
   ▼
rhi_backend_t (vtable)
   │
   ▼
DX12 / Vulkan / Metal
```

RgTexture wraps RhiTexture. RgPass execute callbacks receive a `RhiCmdList`. The Render Graph tracks barrier transitions as abstract state (`RHI_STATE_RENDER_TARGET`, `RHI_STATE_SHADER_READ`, etc.) and emits `cmd_barrier_texture` calls automatically.

---

## Shader Variant System Integration

The Shader Variant system (see `shader-variants.md`) generates permutations at cook time. Each permutation produces a separate SPIR-V blob. The RHI receives pre-selected SPIR-V — it has no knowledge of variants.

```
ShaderVariantKey → SPIR-V blob (from .uzpak)
                        │
                        ▼
            rhi()->pipeline_create(&desc)
                        │
                        ▼
             backend-specific PSO
```

---

## File Structure

```
src/engine/renderer/
└── rhi/
    ├── rhi.h               ← Types, handle typedefs, rhi_backend_t, rhi() accessor
    ├── rhi_dx12.c          ← DX12 implementation
    ├── rhi_vulkan.c        ← Vulkan implementation (stub for Phase 1)
    ├── rhi_metal.c         ← Metal implementation (stub for Phase 1)
    ├── rhi_common.c        ← PSO cache, SPIR-V hash, shared helpers
    └── spirv_transpile.c   ← SPIRV-Cross wrapper (SPIR-V → MSL, SPIR-V → GLSL ES)

shaders/
├── include/                ← Shared HLSL headers (see shader-library.md)
│   ├── brdf.hlsli
│   ├── lighting.hlsli
│   └── ...
├── pbr.hlsl
├── shadow.hlsl
└── ...

.uz_cache/
└── pso/                    ← Persistent PSO cache (binary, gitignored)
```

---

## Dependencies (vcpkg)

```json
{
  "dependencies": [
    "directx-dxc",
    "spirv-cross",
    "spirv-tools"
  ]
}
```

DX12 is provided by the Windows SDK — no vcpkg entry needed.
Vulkan SDK provides `vulkan-headers` and `vulkan-loader`.
Metal is provided by the Apple platform SDK.

---

## Definition of Done

- [ ] `rhi.h` defines all types and `rhi_backend_t` vtable
- [ ] `rhi_dx12.c` implements all vtable functions for Phase 1 (no stubs for required functions)
- [ ] `rhi_vulkan.c` and `rhi_metal.c` exist as stubs returning `false`/`0` for Phase 1
- [ ] Existing DX12 pyramid renderer ported to use RHI API (no direct DX12 calls outside `rhi_dx12.c`)
- [ ] `rhi_common.c` PSO cache implemented with xxHash key
- [ ] `spirv_transpile.c` wraps SPIRV-Cross (used by editor hot-reload + cook)
- [ ] HLSL authoring rules documented in `docs/guides/shader-authoring.md`
- [ ] All new rendering code (Phase 2+) written against RHI only
- [ ] CMake backend selection working for Windows (DX12), CI verified
- [ ] `RhiResourceState` enum used in all barrier calls (no raw `uint32_t` states)
- [ ] `RhiComputePipelineDesc` supports compute-only pipelines via `compute_pipeline_create`
- [ ] `RhiTextureDimension` allows Cube and 3D texture creation in `RhiTextureDesc`
