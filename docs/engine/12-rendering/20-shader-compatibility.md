---
title: Cross-Backend Shader Compatibility
status: active
last-updated: 2026-03-13
---

# Cross-Backend Shader Compatibility

> **Status**: Complete (Milestone 1.2)
> **Scope**: 7 runtime shaders validated for 3 compilation targets
> **Key Decision**: Single HLSL source with `#ifdef UZ_BINDLESS` dual-path design

## Overview

The UzEngine shader system compiles all 7 production shaders to three distinct GPU targets, enabling deployment across modern (SM6.6 bindless) and legacy (SM5.0 register-bound) architectures. This document defines the compilation matrix, resource binding models, and implementation patterns.

### Validation Matrix

| Shader | Stage | DXIL SM6.6 | DXBC SM5.0 | SPIR-V SM6.0 |
|--------|-------|-----------|-----------|--------------|
| mesh_vs.hlsl | Vertex | ✅ | ✅ | ✅ |
| mesh_ps.hlsl | Pixel | ✅ | ✅ | ✅ |
| gbuffer_ps.hlsl | Pixel | ✅ | ✅ | ✅ |
| deferred_lighting_ps.hlsl | Pixel | ✅ | ✅ | ✅ |
| shadow_depth_vs.hlsl | Vertex | ✅ | ✅ | ✅ |
| fullscreen_vs.hlsl | Vertex | ✅ | ✅ | ✅ |
| gbuffer_debug_ps.hlsl | Pixel | ✅ | ✅ | ✅ |

All shaders compile without warnings or errors on all three targets.

## Compilation Targets

### DXIL SM6.6 (DirectX 12 — Primary Target)

**When used**: DX12 runtime on modern NVIDIA/AMD/Intel GPUs (2016+)

**Resource binding**: Bindless descriptor heap via `ResourceDescriptorHeap[index]`
- All texture SRVs are heap indices (uint) stored in constant buffers
- No descriptor table switches — every texture fetch uses heap indexing
- Maximum 1M textures per heap (platform limit)

**Compiler**: DXC (DirectX Compiler)

```bash
dxc -D UZ_BINDLESS=1 -T vs_6_6 shader_vs.hlsl
dxc -D UZ_BINDLESS=1 -T ps_6_6 shader_ps.hlsl
```

**Key features**:
- `ResourceDescriptorHeap[index]` intrinsic for dynamic SRV access
- `ConstantBuffer<T>` template syntax (requires SM5.1+)
- Dynamic loop unrolling, wave operations, mesh shaders support
- Smallest compiled bytecode size due to optimizations

### DXBC SM5.0 (DirectX 11 — Fallback Target)

**When used**: DX11 runtime or older DX12 hardware (pre-2016)

**Resource binding**: Register-bound textures in fixed slots (t0–t5, s0–s1)
- Each texture has a dedicated register (`Texture2D<T> : register(tN)`)
- Samplers also register-bound (`SamplerState : register(sN)`)
- Limited to ~16 texture bindings per shader due to register constraints

**Compiler**: FXC (Legacy DirectX Compiler)

```bash
fxc -T vs_5_0 shader_vs.hlsl
fxc -T ps_5_0 shader_ps.hlsl
```

**Key features**:
- `cbuffer` blocks instead of `ConstantBuffer<T>`
- No dynamic resource indexing — texture count fixed at compile time
- No wave operations, conservative rasterization
- Larger bytecode due to fewer optimizations

### SPIR-V SM6.0 (Vulkan — Cross-Platform Target)

**When used**: Vulkan runtime on all platforms via DXVK (on DX12) or native Vulkan

**Resource binding**: Register-bound textures (same as SM5.0)
- Uses the SM5.0 path (`#ifndef UZ_BINDLESS`)
- Shader staging via DXC with `-spirv` flag
- Vulkan translator converts SPIR-V to backend-specific code

**Compiler**: DXC with SPIR-V target

```bash
dxc -spirv -T vs_6_0 shader_vs.hlsl
dxc -spirv -T ps_6_0 shader_ps.hlsl
```

**Key features**:
- SPIR-V intermediate representation (human-readable with spirv-cross)
- Equivalent resource binding to SM5.0
- Portable across Linux, macOS, mobile Vulkan
- Requires SPIR-V Tools for validation

## Resource Binding Model

### Constant Buffers

Three constant buffers are declared in all shaders:

#### PerFrameCB (register b1)

```cpp
/// Global frame parameters updated once per frame.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;      ///< View-projection matrix
    float4   cameraPos;               ///< Camera position (xyz), unused (w)
    float4   timeParams;              ///< Time (x), deltaTime (y), frameCount (z), unused (w)
    float4   lightDir;                ///< Light direction TOWARD light (xyz), unused (w)
    float4   lightColor;              ///< Light color * intensity (xyz), unused (w)
    float4   ambientColor;            ///< Ambient radiance (xyz), unused (w)
};
```

**Size**: 256 bytes (16 float4 = 64 bytes)

**Padding requirement**: All CB sizes are padded to 256B multiples via `static_assert` to support GPU alignment.

#### PerObjectCB (register b2)

```cpp
/// Per-object transformation and material index data.
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;         ///< Object-to-world matrix
    row_major float4x4 normalMatrix;  ///< Normal transformation matrix
    uint     albedoIndex;             ///< SRV heap index (0 = white default)
    uint     normalIndex;             ///< SRV heap index (0 = flat normal)
    uint     roughnessIndex;          ///< SRV heap index (0 = 0.5 default)
    uint     metallicIndex;           ///< SRV heap index (0 = 0.0 default)
    uint     aoIndex;                 ///< SRV heap index (0 = 1.0 default)
};
```

**Size**: 256 bytes

**Texture indices**: Ring-allocated from GPU descriptor heap. Index 0 is reserved for "use default value" — pixel shader should check `if (albedoIndex > 0)` before sampling.

#### ShadowDataCB (register b4)

```cpp
/// Cascaded shadow map parameters for up to 4 cascades.
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[4];  ///< Light-space VP per cascade
    float4   cascadeSplits;                  ///< Far distances (x=C0, y=C1, z=C2, w=C3)
    uint     shadowAtlasIndex;               ///< SRV heap index for shadow atlas
    uint     cascadeCount;                   ///< Active cascades (1–4)
    float    normalBias;                     ///< Normal-space shadow bias
    float    depthBias;                      ///< Depth comparison offset
    float    shadowMapSize;                  ///< Per-cascade resolution in texels
};
```

**Size**: 512 bytes (4 matrices + 6 floats/ints = 64 + 32 + 16 = 512B)

**Only used in**: mesh_ps.hlsl and deferred_lighting_ps.hlsl (shadow evaluation)

### Texture Bindings

#### SM6.6 Bindless Path (`#ifdef UZ_BINDLESS`)

Textures are **not** declared at file scope. Instead, they are fetched dynamically from the `ResourceDescriptorHeap`:

```hlsl
#ifdef UZ_BINDLESS
// In constant buffer:
uint albedoIndex;  // SRV heap index

// In shader:
if (albedoIndex > 0) {
    Texture2D<float4> tex = ResourceDescriptorHeap[albedoIndex];
    float3 albedo = tex.Sample(linearSampler, uv).rgb;
}
#endif
```

**Advantages**:
- No descriptor table switches
- Unlimited texture bindings (up to heap size)
- Dynamic material assignment without shader recompilation

#### SM5.0 Register-Bound Path (`#ifndef UZ_BINDLESS`)

Textures are declared with fixed registers:

```hlsl
#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float>  shadowAtlasTexture : register(t5);
#endif
```

**Constraints**:
- Fixed at 6 texture registers (t0–t5)
- Shadow atlas is always at t5
- Samplers always at s0 (linear) and s1 (comparison for shadow)
- Constants buffer must pre-bind actual textures before draw call

### Sampler Bindings

Both paths use the same sampler declarations:

```hlsl
/// Linear filtering sampler for texture lookups.
SamplerState linearSampler : register(s0);

/// Comparison sampler for PCF shadow sampling.
SamplerComparisonState shadowSampler : register(s1);
```

Both samplers are **register-bound in all targets** because samplers cannot use heap indexing (platform limitation).

## Implementation Pattern: The #ifdef UZ_BINDLESS Guard

### General Structure

Every shader file follows this pattern:

```hlsl
// ── Declarations that differ by target ──

#ifdef UZ_BINDLESS
// SM6.6-only declarations (ResourceDescriptorHeap, ConstantBuffer<T>)
#else
// SM5.0-only declarations (register-bound textures, cbuffer)
#endif

// ── Shared code (same for all targets) ──
// Samplers, math functions, entry points

// ── Conditional resource access ──

#ifdef UZ_BINDLESS
    // Use ResourceDescriptorHeap[index]
#else
    // Use register-bound textures (t0–t5)
#endif
```

### Example: mesh_ps.hlsl

**Header declarations (SM5.0 path is active by default)**:

```hlsl
#ifdef UZ_BINDLESS
// SM6.6 bindless path — no texture declarations here
#endif

#ifndef UZ_BINDLESS
// SM5.0 path — register-bound textures
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
// ... etc
#endif

// Samplers always declared (same for both paths)
SamplerState linearSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);
```

**Usage within functions**:

```hlsl
float SampleShadowPCF(float2 atlasUV, float depth, float texelSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float shadow = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, atlasUV + offset, depth);
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 9.0f;
}
```

## Per-Shader Compilation Details

### mesh_vs.hlsl (Vertex Shader)

**Purpose**: Transform vertex positions to clip space, interpolate normals/tangents/texcoords.

**Constant buffers**: PerFrameCB (b1), PerObjectCB (b2)

**Texture bindings**: None (vertex shader doesn't sample textures)

**Compilation notes**:
- Straightforward compilation on all targets
- No bindless features required
- DXBC and SPIR-V identical

### mesh_ps.hlsl (Pixel Shader)

**Purpose**: Cook-Torrance PBR with cascaded shadow maps.

**Constant buffers**: PerFrameCB (b1), PerObjectCB (b2), ShadowDataCB (b4)

**Texture bindings (SM6.6)**: 5 PBR textures + shadow atlas via bindless heap indices

**Texture bindings (SM5.0)**: albedoTexture (t0), normalTexture (t1), roughnessTexture (t2), metallicTexture (t3), aoTexture (t4), shadowAtlasTexture (t5)

**Complex sections**:
- 3×3 PCF shadow sampling (requires manual loop unrolling for compatibility)
- Cascade selection and blending logic (identical across targets)
- TBN normal mapping (requires `tangentWS.w` for handedness on all targets)

**Compilation notes**:
- **DXIL**: Smallest bytecode (~4.2 KB), fastest execution
- **DXBC**: Larger (~5.8 KB), longer on GPUs from 2011–2015
- **SPIR-V**: Portable binary, ~4.8 KB after translation

### gbuffer_ps.hlsl (Geometry Pass)

**Purpose**: Render to G-Buffer MRTs (albedo, normal, metallic-roughness).

**Constant buffers**: PerFrameCB (b1), PerObjectCB (b2)

**Texture bindings**: Same as mesh_ps.hlsl (5 PBR textures)

**Output**: 4 MRTs (`SV_Target0` – `SV_Target3`)

**Compilation notes**:
- MRT declarations are identical across all targets
- No wave-dependent operations
- SPIR-V translation straightforward

### deferred_lighting_ps.hlsl (Lighting Pass)

**Purpose**: Read G-Buffer, evaluate Cook-Torrance, apply shadows.

**Constant buffers**: PerFrameCB (b1), ShadowDataCB (b4)

**Texture bindings (SM6.6)**: G-Buffer MRTs + shadow atlas (3 SRVs) via bindless

**Texture bindings (SM5.0)**: gBufferAlbedo (t0), gBufferNormal (t1), gBufferMetalRough (t2), shadowAtlas (t3)

**Input**: Screen-space coordinates (full-screen pass)

**Output**: Final linear color to backbuffer

**Compilation notes**:
- Fullscreen triangle topology (no vertices needed)
- SV_VertexID required on SM5.0 for vertex generation
- Shadow PCF identical to mesh_ps.hlsl

### shadow_depth_vs.hlsl (Shadow Vertex Shader)

**Purpose**: Transform vertices to light-space, output depth for shadow atlas.

**Constant buffers**: PerFrameCB (b1), PerObjectCB (b2), ShadowDataCB (b4)

**Texture bindings**: None

**Output**: SV_Position (depth-only)

**Compilation notes**:
- Simple transformation shader
- No special bindless requirements
- Identical across all three targets

### fullscreen_vs.hlsl (Fullscreen Triangle Shader)

**Purpose**: Generate a fullscreen triangle without vertex buffer.

**Constant buffers**: None

**Texture bindings**: None

**Method**: Generates vertices from SV_VertexID:
- ID 0 → (-1, -1)
- ID 1 → (3, -1)
- ID 2 → (-1, 3)

Creates a triangle that covers the full screen after perspective divide.

**Compilation notes**:
- Identical on all targets
- No platform-specific code needed

### gbuffer_debug_ps.hlsl (Debug Visualization)

**Purpose**: Visualize G-Buffer contents (albedo, normal, metallic, roughness, depth).

**Constant buffers**: PerFrameCB (b1)

**Texture bindings**: G-Buffer MRTs (same as deferred_lighting_ps.hlsl)

**Output**: Single RT with selected G-Buffer component

**Compilation notes**:
- Development-only shader (not shipped)
- Switch statement on visualized layer
- Identical compilation across targets

## Constant Buffer Alignment

All constant buffers must be **256-byte aligned** for GPU efficiency:

```cpp
// C++ side (engine code)
static_assert(sizeof(PerFrameCB) == 256, "PerFrameCB must be 256 bytes");
static_assert(sizeof(PerObjectCB) == 256, "PerObjectCB must be 256 bytes");
static_assert(sizeof(ShadowDataCB) == 512, "ShadowDataCB must be 512 bytes");

// HLSL side (shader code)
static_assert(sizeof(PerFrameCB) == 256);
static_assert(sizeof(PerObjectCB) == 256);
static_assert(sizeof(ShadowDataCB) == 512);
```

Padding with unused float4s or uint arrays ensures alignment. This constraint is non-negotiable: misaligned CBs cause GPU read errors or silent corruption.

## Compilation Workflow

### Build System Integration

The shader compilation is handled by the Zig build system (`runtime/build.zig`):

```bash
# Compile all shaders for all targets
zig build
```

**Process**:
1. DXC compiles `.hlsl` files with `-D UZ_BINDLESS=1` → DXIL bytecode
2. FXC compiles `.hlsl` files (no define) → DXBC bytecode
3. DXC with `-spirv` compiles → SPIR-V IR
4. All bytecodes embedded in the runtime executable

### Runtime Selection

The runtime selects which compiled bytecode to use at initialization:

```cpp
// Pseudocode
if (rhi_backend == RHI_DX12) {
    // Use DXIL bytecode (pre-compiled with UZ_BINDLESS=1)
    pipeline = device->CreatePipeline(dxil_bytecode);
} else if (rhi_backend == RHI_DX11) {
    // Use DXBC bytecode (pre-compiled without UZ_BINDLESS)
    pipeline = device->CreatePipeline(dxbc_bytecode);
} else if (rhi_backend == RHI_VULKAN) {
    // Use SPIR-V bytecode (pre-compiled with -spirv)
    pipeline = device->CreatePipeline(spir_v_bytecode);
}
```

## Testing & Validation

### Compilation Tests

All shaders are compiled in CI to catch:
- Syntax errors
- Type mismatches
- Register conflicts
- Bytecode generation failures

### Runtime Tests

Each shader is exercised in:
- **Clear screen demo** (mesh_vs + mesh_ps)
- **Shadow pass demo** (shadow_depth_vs)
- **Deferred pipeline demo** (gbuffer_ps + deferred_lighting_ps)
- **Debug visualization demo** (gbuffer_debug_ps)

### Coverage

As of Milestone 1.2:
- ✅ 7 shaders compiled (2 VS, 5 PS)
- ✅ 3 targets verified (DXIL, DXBC, SPIR-V)
- ✅ 21 compilation variants (7 shaders × 3 targets)
- ✅ 100% shader coverage in render demos

## Best Practices for New Shaders

When implementing new shaders, follow these rules:

### 1. Use `#ifdef UZ_BINDLESS` for Texture Access

```hlsl
#ifdef UZ_BINDLESS
if (textureIndex > 0) {
    Texture2D<float4> tex = ResourceDescriptorHeap[textureIndex];
    result = tex.Sample(sampler, uv);
}
#else
    // Register-bound path
    result = myTexture.Sample(sampler, uv);
#endif
```

### 2. Register-Bind Samplers (Both Paths)

Samplers **cannot** be placed in the descriptor heap. Always use register binding:

```hlsl
SamplerState linearSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);
```

### 3. Declare Constants Explicitly

Use `cbuffer` for SM5.0 compatibility:

```hlsl
cbuffer MyConstantBuffer : register(b3) {
    float4 myParam;     // 16 bytes
    float2 otherParam;  // 8 bytes + 8 padding
    // Total: 32 bytes (pad to 256B in C++ side)
};
```

**Do NOT** use `ConstantBuffer<T>` template syntax (SM5.1+ only).

### 4. Avoid SM6-Only Features

**Do NOT use**:
- Wave operations (`WaveReadFirstLane`, etc.)
- Conservative rasterization
- Mesh shaders
- Sampler feedback

**Safe to use**:
- Standard HLSL 5.0 constructs
- Loops, branches, recursion
- Structs and arrays
- Atomics (if atomic UAVs available in SM5.0)

### 5. Test on All Three Targets

Before merging shader code:

```bash
# Compile with bindless (DXIL SM6.6)
dxc -D UZ_BINDLESS=1 -T ps_6_6 my_shader.hlsl -Fo output_dxil.o

# Compile without bindless (DXBC SM5.0)
fxc -T ps_5_0 my_shader.hlsl -Fo output_dxbc.o

# Compile to SPIR-V (Vulkan)
dxc -spirv -T ps_6_0 my_shader.hlsl -Fo output_spirv.spv
```

All three must compile without warnings.

## Architecture Decision: Why Single Source?

### Alternative: Separate Shader Sources

**Rejected** because:
- Maintenance nightmare: every change requires 3x implementations
- Inconsistent behavior between backends
- Testing surface area triples (3 code paths)
- Duplication violates DRY principle

### Chosen: Conditional Compilation via `#ifdef UZ_BINDLESS`

**Benefits**:
- Single source of truth
- Identical logic across all platforms
- Easy to audit behavioral differences
- Conditional sections clearly marked

**Trade-offs**:
- Slightly larger shader source (~5% overhead from guards)
- Must avoid SM6-only features
- Compiler must be invoked 3 times per shader

The trade-off is worth it: easier maintenance + better testing + reduced bugs.

## Troubleshooting

### "ERROR: cannot find binding (t0)"

**Cause**: Register-bound texture not declared in SM5.0 path.

**Fix**: Add texture declaration in `#ifndef UZ_BINDLESS` block:

```hlsl
#ifndef UZ_BINDLESS
Texture2D<float4> myTexture : register(t0);
#endif
```

### "ERROR: ConstantBuffer<T> not supported in SM5"

**Cause**: Using SM5.1+ template syntax (`ConstantBuffer<MyStruct>`) instead of `cbuffer`.

**Fix**: Use traditional `cbuffer` syntax:

```hlsl
cbuffer MyBuffer : register(b0) {
    // fields
};
```

### "WARN: 128 out of 16 texture registers used"

**Cause**: Shader declares more than 16 textures with register binding (SM5.0 limit).

**Fix**: Either:
1. Reduce texture count in SM5.0 path (use defaults for non-critical textures)
2. Abandon SM5.0 support (if targeting DX12-only)
3. Use a texture indirection buffer (StructuredBuffer of indices)

### SPIR-V validation fails with "block size mismatch"

**Cause**: Constant buffer sizes don't match between C++ and HLSL.

**Fix**: Verify `static_assert` statements in both C++ and HLSL match.

## References

### Compiler Documentation

- **DXC**: https://github.com/microsoft/DirectXShaderCompiler
- **FXC**: Microsoft DirectX SDK documentation
- **SPIR-V Tools**: https://github.com/KhronosGroup/SPIRV-Tools

### Standards & Specifications

- **HLSL 5.0**: Microsoft HLSL specification (legacy, stable)
- **HLSL 6.x**: Modern HLSL with new features (used for SM6.6)
- **SPIR-V**: Khronos intermediate representation (1.2+)

### Related Documentation

- [§05 Shader Pipeline](../05-shader-pipeline/README.md) — VFS, caching, hot reload
- [§03 RHI](../03-rhi/) — GPU backend specifics
- [09-forward-pbr-pass.md](09-forward-pbr-pass.md) — Render pass implementation
- [10-cook-torrance-brdf.md](10-cook-torrance-brdf.md) — BRDF shader functions

## Appendix: Full Compilation Matrix

Complete command-line reference for all 7 shaders:

### Vertex Shaders

```bash
# mesh_vs.hlsl
dxc -D UZ_BINDLESS=1 -T vs_6_6 mesh_vs.hlsl -Fo mesh_vs_dxil.o
fxc -T vs_5_0 mesh_vs.hlsl -Fo mesh_vs_dxbc.o
dxc -spirv -T vs_6_0 mesh_vs.hlsl -Fo mesh_vs_spirv.spv

# shadow_depth_vs.hlsl
dxc -D UZ_BINDLESS=1 -T vs_6_6 shadow_depth_vs.hlsl -Fo shadow_depth_vs_dxil.o
fxc -T vs_5_0 shadow_depth_vs.hlsl -Fo shadow_depth_vs_dxbc.o
dxc -spirv -T vs_6_0 shadow_depth_vs.hlsl -Fo shadow_depth_vs_spirv.spv

# fullscreen_vs.hlsl
dxc -D UZ_BINDLESS=1 -T vs_6_6 fullscreen_vs.hlsl -Fo fullscreen_vs_dxil.o
fxc -T vs_5_0 fullscreen_vs.hlsl -Fo fullscreen_vs_dxbc.o
dxc -spirv -T vs_6_0 fullscreen_vs.hlsl -Fo fullscreen_vs_spirv.spv
```

### Pixel Shaders

```bash
# mesh_ps.hlsl
dxc -D UZ_BINDLESS=1 -T ps_6_6 mesh_ps.hlsl -Fo mesh_ps_dxil.o
fxc -T ps_5_0 mesh_ps.hlsl -Fo mesh_ps_dxbc.o
dxc -spirv -T ps_6_0 mesh_ps.hlsl -Fo mesh_ps_spirv.spv

# gbuffer_ps.hlsl
dxc -D UZ_BINDLESS=1 -T ps_6_6 gbuffer_ps.hlsl -Fo gbuffer_ps_dxil.o
fxc -T ps_5_0 gbuffer_ps.hlsl -Fo gbuffer_ps_dxbc.o
dxc -spirv -T ps_6_0 gbuffer_ps.hlsl -Fo gbuffer_ps_spirv.spv

# deferred_lighting_ps.hlsl
dxc -D UZ_BINDLESS=1 -T ps_6_6 deferred_lighting_ps.hlsl -Fo deferred_lighting_ps_dxil.o
fxc -T ps_5_0 deferred_lighting_ps.hlsl -Fo deferred_lighting_ps_dxbc.o
dxc -spirv -T ps_6_0 deferred_lighting_ps.hlsl -Fo deferred_lighting_ps_spirv.spv

# gbuffer_debug_ps.hlsl
dxc -D UZ_BINDLESS=1 -T ps_6_6 gbuffer_debug_ps.hlsl -Fo gbuffer_debug_ps_dxil.o
fxc -T ps_5_0 gbuffer_debug_ps.hlsl -Fo gbuffer_debug_ps_dxbc.o
dxc -spirv -T ps_6_0 gbuffer_debug_ps.hlsl -Fo gbuffer_debug_ps_spirv.spv
```

All output `.o` and `.spv` files are embedded in the runtime binary at compile time.
