# DX12 Register Binding Convention

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/rhi/dx12/dx12_resource_creation.cpp` — `CreateBindlessRootSignature()`
> **Depends on**: RHI (§03), Shader Compatibility (§07)

## Overview

This document specifies how registers are allocated in the DX12 root signature and how shaders bind to them. The root signature is the GPU's interface contract — any mismatch between C++ root parameter binding and HLSL register declarations causes runtime rendering errors.

**Key principle**: Root parameter **index** ≠ register **number**. Parameter[0] may bind to register `b0`, but parameter[3] binds to `b4` (skipping `b3`). Always use explicit register numbers in HLSL, never rely on positional correspondence.

## Root Signature Layout

The global root signature has **4 root parameters** (SM6.6+) or **5 root parameters** (SM5.x fallback).

### Root Parameter[0]: Bindless Descriptor Indices (4× uint32 constants)

| Property | Value |
|----------|-------|
| Parameter Index | 0 |
| Type | 32-bit root constants |
| Register | `b0` |
| Count | 4 values (128 bits total) |
| Shader Visibility | All (VS, PS, CS) |
| Usage | Bindless resource indices for texture/buffer lookup |

**C++ binding**:
```cpp
rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
rootParams[0].Constants.ShaderRegister = 0;   // b0
rootParams[0].Constants.Num32BitValues = 4;
```

**HLSL usage** (SM6.6 bindless only):
```hlsl
// These values are passed as root constants, not a cbuffer
uint vertexBufferIndex = push.vertexBufferIndex; // accessed directly
```

### Root Parameter[1]: Per-Frame Constant Buffer

| Property | Value |
|----------|-------|
| Parameter Index | 1 |
| Type | Root CBV (direct GPU VA pointer) |
| Register | `b1` |
| Shader Visibility | All |
| Usage | Frame-global data (camera, time, directional light, ambient) |

**C++ binding**:
```cpp
rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
rootParams[1].Descriptor.ShaderRegister = 1;  // b1
rootParams[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
```

**HLSL declaration**:
```hlsl
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
};
```

**Updated per frame**: Once per render frame with shared data for all objects.

### Root Parameter[2]: Per-Object Constant Buffer

| Property | Value |
|----------|-------|
| Parameter Index | 2 |
| Type | Root CBV |
| Register | `b2` |
| Shader Visibility | All |
| Usage | Per-draw object data (model matrix, material indices) |

**C++ binding**:
```cpp
rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
rootParams[2].Descriptor.ShaderRegister = 2;  // b2
rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
```

**HLSL declaration**:
```hlsl
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint albedoIndex;
    uint normalIndex;
    uint roughnessIndex;
    uint metallicIndex;
    uint aoIndex;
};
```

**Updated per draw**: Once per DrawIndexed() call with object-specific transforms and material indices.

### Root Parameter[3]: Shadow Data Constant Buffer

| Property | Value |
|----------|-------|
| Parameter Index | 3 |
| Type | Root CBV |
| Register | `b4` (note: skips b3) |
| Shader Visibility | All |
| Usage | Cascaded shadow map matrices and parameters |

**C++ binding**:
```cpp
rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
rootParams[3].Descriptor.ShaderRegister = 4;  // b4 — NOT b3!
rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
```

**HLSL declaration**:
```hlsl
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[4];
    float4 cascadeSplits;
    uint   shadowAtlasIndex;
    uint   cascadeCount;
    float  normalBias;
    float  depthBias;
    float  shadowMapSize;
};
```

**Updated per frame**: Once per frame when shadow-casting lights are present.

### Root Parameter[4]: Texture/Buffer Descriptor Table (SM5 only)

| Property | Value |
|----------|-------|
| Parameter Index | 4 |
| Type | Descriptor table (SRV range) |
| Register Range | `t0` — unbounded |
| Shader Visibility | All |
| Used Only In | SM5.x fallback (!UZ_BINDLESS) |

**C++ binding** (SM5 fallback only):
```cpp
D3D12_DESCRIPTOR_RANGE1 srvRange = {};
srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
srvRange.NumDescriptors = UINT_MAX;       // unbounded array
srvRange.BaseShaderRegister = 0;          // t0
srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
rootParams[4].DescriptorTable.pDescriptorRanges = &srvRange;
```

**HLSL declaration** (SM5 only):
```hlsl
#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture    : register(t0);
Texture2D<float4> normalTexture    : register(t1);
Texture2D<float4> roughnessTexture : register(t2);
Texture2D<float4> metallicTexture  : register(t3);
Texture2D<float4> aoTexture        : register(t4);
Texture2D<float> shadowAtlasTexture : register(t5);
#endif
```

## Static Samplers

Two static samplers are created at root signature creation time and bound to all shaders.

| Index | Register | Filter | Address Mode | Purpose |
|-------|----------|--------|--------------|---------|
| 0 | `s0` | Linear (MIN_MAG_MIP_LINEAR) | Wrap (U, V, W) | Standard texture sampling |
| 1 | `s1` | Comparison MIN_MAG_LINEAR_MIP_POINT | Border (black) | Shadow depth comparison (PCF) |

**HLSL declaration**:
```hlsl
SamplerState linearSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);
```

**Static samplers are created once** and never change per-draw. They execute faster than descriptor-heap samplers.

## Shader Model Adaptation

The root signature adapts to the negotiated shader model:

### SM6.6+ (Bindless)

- **Root parameters**: 4 (indices 0–3)
- **Feature**: `D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`
- **Texture binding**: Via ResourceDescriptorHeap[index] in shader code
- **Advantage**: No descriptor table switching; single heap access by integer index

**Root signature flags**:
```cpp
flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
```

### SM5.x (Input Assembler + Descriptor Table)

- **Root parameters**: 5 (indices 0–4, parameter[4] is SRV descriptor table)
- **Feature**: `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT`
- **Texture binding**: Via register-bound textures (t0–t5 in parameter[4])
- **Advantage**: Broader compatibility; required for older GPUs without bindless support

**Root signature flags**:
```cpp
flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
```

## Common Patterns

### Accessing Bindless Textures (SM6.6)

```hlsl
#ifdef UZ_BINDLESS
uint albedoIdx = albedoIndex;  // from PerObjectCB at b2
Texture2D<float4> albedoTex = ResourceDescriptorHeap[albedoIdx];
float4 albedoSample = albedoTex.Sample(linearSampler, uv);
#else
float4 albedoSample = albedoTexture.Sample(linearSampler, uv);  // SM5
#endif
```

### Accessing Register-Bound Textures (SM5)

```hlsl
#ifndef UZ_BINDLESS
Texture2D<float4> albedoTexture : register(t0);
SamplerState linearSampler : register(s0);
// Then in shader:
float4 color = albedoTexture.Sample(linearSampler, uv);
#endif
```

### Shadow Sampling with Comparison

```hlsl
// Both paths use the same comparison sampler s1
SamplerComparisonState shadowSampler : register(s1);

float shadowDepth = ...;
float shadow = shadowAtlas.SampleCmpLevelZero(shadowSampler, uv, shadowDepth);
```

## Rules

1. **ALWAYS use explicit register numbers in HLSL** — never rely on parameter order. `register(b4)` is correct; assuming parameter[3] → `b3` is not.

2. **ALWAYS keep constant buffer layouts in sync** — the C++ struct in `PerFrameCB` must match the HLSL `cbuffer PerFrameCB` byte-for-byte, including padding.

3. **NEVER skip shadow data CB** — even if shadows are unused, `ShadowDataCB` is bound at `b4` by all passes. Shaders must declare it or compilation fails.

4. **ALWAYS declare both SM6.6 and SM5 paths** — use `#ifdef UZ_BINDLESS` for dual-path shaders. Never assume one or the other.

5. **NEVER change root signature parameters** — they are global and affect all render passes. Validation errors occur if a shader expects `b2` but gets `b3`.

6. **ALWAYS update `ShadowDataCB` before shadow-dependent passes** — cascadeViewProj, cascadeSplits, and other fields must be valid or shadows become corrupted.

7. **NEVER bind additional CBVs beyond b0–b4** — exceeds the fixed root signature layout. Use structured buffers (SRV) or descriptor tables instead.

## Engine Passes Using This Convention

All engine render passes follow this binding convention:

- **Forward PBR pass** (`ForwardPBRPass`) — binds all four CBVs, samples textures via bindless or SM5 tables
- **G-Buffer pass** (`GBufferPass`) — same layout, writes to render target instead of final color
- **Deferred lighting pass** (`DeferredLightingPass`) — same layout, reads G-Buffer SRVs
- **Shadow pass** (`ShadowPass`) — binds parameter[0–3] only (no textures in shadow depth pass)
- **Editor grid pass** (`GridPass`) — separate root signature (editor-only, not discussed here)

## Troubleshooting

**Error: "Shader declares `register(b3)` but root signature has no parameter at b3"**
- **Cause**: Shader uses wrong register number for root parameter[3]
- **Fix**: Change to `register(b4)` — parameter[3] binds to `b4`, not `b3`

**Error: "Texture samples from descriptor table but shader model is SM6.6"**
- **Cause**: SM6.6 shader using `#ifndef UZ_BINDLESS` path
- **Fix**: Define `UZ_BINDLESS` during SM6.6 compilation; ensure shader uses ResourceDescriptorHeap[index]

**Error: "Constant buffer size mismatch"**
- **Cause**: C++ struct and HLSL `cbuffer` layouts don't match
- **Fix**: Verify padding and field order. Use `sizeof()` in C++ and `mul(1, 4)` in HLSL to print byte counts

## Related Documentation

- [Shader Compatibility (§20-shader-compatibility.md)](20-shader-compatibility.md) — SM6.6 vs SM5 dual-path design
- [Forward PBR Pass (§09-forward-pbr-pass.md)](09-forward-pbr-pass.md) — practical binding example
- [Shared Include Structure (§14-shader-includes.md)](14-shader-includes.md) — .hlsli file conventions
- [Shadow System (§06-shadow-system.md)](06-shadow-system.md) — ShadowDataCB contents and cascade layout
