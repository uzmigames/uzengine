# RHI — Pipeline State Objects

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/core/rhi/rhi_types.h` (descriptors), `runtime/core/rhi/rhi.h` (IRHIDevice::CreateGraphicsPipeline)
> **Depends on**: RHI types, shader compilation

## Purpose

A Pipeline State Object (PSO) is an immutable bundle of all GPU state needed for rendering: shaders, rasterizer settings, blend mode, depth-stencil config, render target formats, and vertex layout. Creating a PSO can be expensive (driver compiles internal microcode), but using one is cheap — a single `SetPipeline()` call binds all state at once.

## Why Immutable PSOs

In legacy APIs (OpenGL, DX11), render state was set incrementally — each `glEnable`, `glBlendFunc`, `OMSetBlendState` was a separate call, and the driver deferred validation to draw time. DX12 and Vulkan require all state upfront in a PSO, which:

1. **Eliminates draw-time validation** — the driver validates once at creation, not every frame
2. **Enables driver optimization** — knowing all state upfront allows optimal microcode generation
3. **Prevents state leaks** — no forgotten `glDisable(GL_BLEND)` from a previous pass
4. **Enables caching** — identical state descriptions produce identical PSOs

## PSO Descriptor

```cpp
struct RHIGraphicsPipelineDesc {
    // ── Shaders ──────────────────────────────────────
    RHIShaderHandle vertexShader;       // Required
    RHIShaderHandle pixelShader;        // Optional (depth-only passes)

    // ── Topology ─────────────────────────────────────
    RHIPrimitiveTopology topology = TriangleList;

    // ── Rasterizer ───────────────────────────────────
    RHIRasterizerDesc rasterizer;
    //   .cullMode = Back
    //   .fillMode = Solid
    //   .frontCounterClockwise = false
    //   .depthBias = 0.0f
    //   .slopeScaledDepthBias = 0.0f

    // ── Depth-Stencil ────────────────────────────────
    RHIDepthStencilDesc depthStencil;
    //   .depthEnable = true
    //   .depthWrite = true
    //   .depthFunc = Less

    // ── Blend (per render target) ────────────────────
    RHIBlendDesc renderTargetBlend[8];
    //   .blendEnable = false (opaque by default)
    //   .srcColor = One, .dstColor = Zero, .colorOp = Add
    //   .srcAlpha = One, .dstAlpha = Zero, .alphaOp = Add

    // ── Render targets ───────────────────────────────
    RHIFormat renderTargetFormats[8];   // Unknown = unused slot
    uint32_t renderTargetCount = 1;
    RHIFormat depthStencilFormat = Unknown;  // Unknown = no depth

    // ── Vertex input (SM5 only) ──────────────────────
    const RHIInputElementDesc* inputElements = nullptr;  // nullptr for SM6.6
    uint32_t inputElementCount = 0;
    uint32_t vertexStride = 0;

    const char* debugName = nullptr;
};
```

## Common PSO Configurations

### Opaque Mesh (SM6.6 Bindless)

```cpp
RHIGraphicsPipelineDesc desc{};
desc.vertexShader            = meshVS;
desc.pixelShader             = meshPS;
desc.rasterizer.cullMode     = RHICullMode::Back;
desc.depthStencil.depthEnable = true;
desc.depthStencil.depthFunc  = RHICompareFunc::Less;
desc.renderTargetFormats[0]  = RHIFormat::RGBA8Unorm;
desc.renderTargetCount       = 1;
desc.depthStencilFormat      = RHIFormat::D32Float;
// No input elements — SM6.6 bindless vertex fetch
desc.debugName               = "OpaqueMesh";
```

### Alpha-Blended Transparent

```cpp
desc.renderTargetBlend[0].blendEnable = true;
desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;
desc.depthStencil.depthWrite          = false;  // No depth write for transparents
desc.debugName                        = "Transparent";
```

### Depth-Only Pre-pass

```cpp
desc.vertexShader           = depthVS;
desc.pixelShader            = {};  // No pixel shader (depth-only)
desc.renderTargetCount      = 0;   // No color outputs
desc.depthStencilFormat     = RHIFormat::D32Float;
desc.depthStencil.depthFunc = RHICompareFunc::Less;
desc.debugName              = "DepthPrepass";
```

### Wireframe Debug

```cpp
desc.rasterizer.fillMode    = RHIFillMode::Wireframe;
desc.rasterizer.cullMode    = RHICullMode::None;
desc.depthStencil.depthWrite = false;
desc.debugName              = "WireframeDebug";
```

## SM5 Vertex Input Layout

When running on SM5.x hardware (no bindless), vertex data goes through the Input Assembler. The pipeline needs an explicit input layout:

```cpp
RHIInputElementDesc elements[] = {
    { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0  },
    { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float, 0, 12 },
    { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,  0, 24 },
};

desc.inputElements     = elements;
desc.inputElementCount = 3;
desc.vertexStride      = 32;  // sizeof(Vertex)
```

The `RHIInputSemantic` enum maps to HLSL semantics:

| Enum | HLSL | Typical Format |
|------|------|---------------|
| `Position` | `POSITION` | float3 (RGB32Float) |
| `Normal` | `NORMAL` | float3 (RGB32Float) |
| `Texcoord` | `TEXCOORD` | float2 (RG32Float) |
| `Tangent` | `TANGENT` | float4 (RGBA32Float) |
| `Color` | `COLOR` | float4 (RGBA32Float) |

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| PSO representation | `FGraphicsPipelineState` (cached via `PSOPrecache`) | `RenderingDevice::PipelineID` | `RHIPipelineHandle` (generational handle) |
| Shader permutations | Massive — `FMaterialShaderMap` with hundreds of combinations | Moderate — CanvasShader vs SceneShader variants | Minimal for now — SM6.6 vs SM5 paths |
| PSO cache | Disk-serialized PSO cache (`PipelineFileCache`) | No disk cache (recreates on load) | Planned via content-hash (xxHash64) |
| Root signature | Per-material or shared | Global pipeline layout | **Single global bindless root signature** |

UzEngine's approach of a single global root signature simplifies everything: all pipelines share the same binding layout, eliminating root signature mismatches and reducing PSO combinations.

## Rules

1. **ALWAYS** set `debugName` on pipelines — it appears in GPU debuggers and helps identify stale PSOs
2. **NEVER** create PSOs inside the render loop — they're expensive. Create during load or in a background thread
3. **ALWAYS** match `renderTargetFormats` and `depthStencilFormat` to the actual framebuffer — mismatches cause D3D12 validation errors
4. **ALWAYS** use `inputElementCount = 0` for SM6.6 pipelines — the Input Assembler is bypassed
