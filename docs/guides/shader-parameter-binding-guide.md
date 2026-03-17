<!-- doc-meta
title: Shader Parameter Binding Guide
category: guide
theme: rendering
status: active
updated: 2026-03-16
author: docs-operator
-->

# Shader Parameter Binding Guide

This guide explains UzEngine's UE5-style shader parameter system: how to declare
parameter structs with macros, define their metadata, and use `ShaderParameterBinder`
to automatically upload constant buffer data and bind GPU resources.

---

## Overview

The old approach required one RHI call per resource — change parameter order in a
shader and all the binding code breaks. The new system declares parameter structs
with macros, generates metadata once at startup, and binds everything with a single
`ShaderParameterBinder::Bind()` call.

The system has four components:

| Component | File | Purpose |
|-----------|------|---------|
| Core types | `shader_parameters.h` | `ShaderParamType`, `ShaderParamMember`, `ShaderParameterMetadata` |
| Macros | `shader_parameter_macros.h` | Struct declaration + metadata definition |
| Binder | `shader_parameter_binder.h/.cpp` | CB upload + resource binding |
| HLSL generation | `shader_parameter_hlsl.h/.cpp` | Generate HLSL for validation |

All files live in `runtime/engine/renderer/core/`.

---

## Struct Layout Contract

Every parameter struct is split into two contiguous sections:

```
[ 0 ──── cbSize )        Scalar section  — copied to the constant buffer
[ cbSize ──── end ]      Resource section — bound as SRV/sampler/UAV, NOT in CB
```

Rules:
- All `SHADER_PARAMETER` and `SHADER_PARAMETER_ARRAY` members **must come first**.
- All `SHADER_PARAMETER_TEXTURE`, `SHADER_PARAMETER_SAMPLER`, and `SHADER_PARAMETER_UAV` members **must come after** `SHADER_PARAMETER_SCALAR_END()`.
- `cbSize` must be a multiple of 256 (D3D12 constant buffer alignment requirement).
- Users are responsible for padding the scalar section to 16-byte boundaries (HLSL `float4` alignment within cbuffers).

---

## Step 1 — Declare the Struct in a Header

Use the macros to open the struct, list scalar members, mark the boundary, list resource members, and close the struct.

```cpp
// my_pass_params.h
#pragma once
#include "engine/renderer/core/shader_parameter_macros.h"
#include "math/mat4.h"
#include "math/vector4.h"

namespace uz {

BEGIN_SHADER_PARAMETER_STRUCT(MyPassParams)
    // ── Scalar section (goes into CB) ────────────────────────────────────
    SHADER_PARAMETER(Mat4,     viewProj)
    SHADER_PARAMETER(Vec4,     cameraPos)
    SHADER_PARAMETER(float,    exposure)
    SHADER_PARAMETER_ARRAY(float, _pad, 3)  // pad to 16-byte boundary
SHADER_PARAMETER_SCALAR_END()
    // ── Resource section (bound as SRV/sampler, NOT in CB) ───────────────
    SHADER_PARAMETER_TEXTURE(TextureCube, envCubemap, 5)
    SHADER_PARAMETER_TEXTURE(Texture2D,   brdfLut,    7)
    SHADER_PARAMETER_SAMPLER(linearSampler, 0)
END_SHADER_PARAMETER_STRUCT()

} // namespace uz
```

### Macro Reference

| Macro | Section | Description |
|-------|---------|-------------|
| `BEGIN_SHADER_PARAMETER_STRUCT(Name)` | — | Opens a `struct alignas(16) Name`. |
| `SHADER_PARAMETER(Type, Name)` | Scalar | Declares `Type Name {}` — copied to CB. |
| `SHADER_PARAMETER_ARRAY(Type, Name, Count)` | Scalar | Declares `Type Name[Count] {}` — copied to CB. |
| `SHADER_PARAMETER_SCALAR_END()` | Boundary | Documentation marker only — no code generated. |
| `SHADER_PARAMETER_TEXTURE(HlslType, Name, Slot)` | Resource | Declares `RHITextureHandle Name` — bound at tSlot on SM5. |
| `SHADER_PARAMETER_SAMPLER(Name, Slot)` | Resource | Declares `RHISamplerHandle Name` — bound at sSlot. |
| `SHADER_PARAMETER_UAV(Name, Slot)` | Resource | Declares `RHITextureHandle Name` — bound at uSlot. |
| `END_SHADER_PARAMETER_STRUCT()` | — | Closes the struct, declares `static const ShaderParameterMetadata& GetMetadata()`. |

---

## Step 2 — Define Metadata in a .cpp File

`GetMetadata()` is declared in the header by `END_SHADER_PARAMETER_STRUCT()` but must
be defined in a `.cpp` file using the metadata macros. The metadata tells the binder
exactly which bytes belong to the CB and which are resource slots.

```cpp
// my_pass_params.cpp
#include "my_pass_params.h"
#include "engine/renderer/core/shader_parameter_macros.h"
#include <cstddef>  // offsetof

namespace uz {

BEGIN_SHADER_PARAMETER_METADATA(MyPassParams)
    // Scalars — list every member in the scalar section
    METADATA_SCALAR      (MyPassParams, viewProj,      Mat4)
    METADATA_SCALAR      (MyPassParams, cameraPos,     Vec4)
    METADATA_SCALAR      (MyPassParams, exposure,      float)
    METADATA_SCALAR_ARRAY(MyPassParams, _pad,          float, 3)
    // Resources — list every texture, sampler, and UAV
    METADATA_TEXTURE_CUBE(MyPassParams, envCubemap, 5)
    METADATA_TEXTURE2D   (MyPassParams, brdfLut,    7)
    METADATA_SAMPLER     (MyPassParams, linearSampler, 0)
END_SHADER_PARAMETER_METADATA(
    MyPassParams,
    256,  // cbSize — must be 256-byte aligned; scalar section is 96 bytes, rounded up
    2,    // textureCount (envCubemap + brdfLut)
    1,    // samplerCount
    0     // uavCount
)

} // namespace uz
```

`END_SHADER_PARAMETER_METADATA` contains a `static_assert` that verifies `cbSize % 256 == 0`
at compile time.

### Metadata Macro Reference

| Macro | Use for |
|-------|---------|
| `METADATA_SCALAR(Struct, Name, Type)` | Single scalar field. |
| `METADATA_SCALAR_ARRAY(Struct, Name, Type, Count)` | Array of scalars or padding. |
| `METADATA_TEXTURE2D(Struct, Name, Slot)` | `Texture2D` SRV at `register(tSlot)`. |
| `METADATA_TEXTURE_CUBE(Struct, Name, Slot)` | `TextureCube` SRV at `register(tSlot)`. |
| `METADATA_SAMPLER(Struct, Name, Slot)` | `SamplerState` at `register(sSlot)`. |
| `METADATA_UAV(Struct, Name, Slot)` | UAV at `register(uSlot)`. |

> **Note**: The `BEGIN_SHADER_PARAMETER_METADATA` / `END_SHADER_PARAMETER_METADATA`
> macros define `MyPassParams::GetMetadata()`. Do not write the function by hand.

---

## Step 3 — Use ShaderParameterBinder in a Render Pass

```cpp
#include "my_pass_params.h"
#include "engine/renderer/core/shader_parameter_binder.h"

void MyPass::Execute(IRHICommandList* cmd, bool bindless) {
    MyPassParams params{};
    params.viewProj      = camera.GetViewProj();
    params.cameraPos     = Vec4(camera.GetPosition(), 1.0f);
    params.exposure      = autoExposure.GetCurrentEV();
    params.envCubemap    = ibl.irradianceCubemap;
    params.brdfLut       = ibl.brdfLut;
    params.linearSampler = globalSamplers.linear;

    // Convenience: BindCB + BindResources in one call.
    // Returns the CBAllocation in case you need the GPU address.
    auto alloc = ShaderParameterBinder::Bind(
        cmd,
        &params,
        MyPassParams::GetMetadata(),
        m_cbRing,          // CBRingAllocator&
        kSlotMyPassCB,     // root parameter slot index
        bindless           // true = SM6.6; false = SM5 explicit binding
    );

    if (!alloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "MyPass: CB ring full");
        return;
    }

    cmd->DrawFullscreenTriangle();
}
```

You can also call the two steps separately when you need the CBAllocation before
resource binding:

```cpp
// Step A: upload scalar data and bind CBV
auto alloc = ShaderParameterBinder::BindCB(
    cmd, &params, MyPassParams::GetMetadata(), m_cbRing, kSlotMyPassCB);

// ... set other root parameters ...

// Step B: bind textures/samplers at their HLSL register slots
ShaderParameterBinder::BindResources(
    cmd, &params, MyPassParams::GetMetadata(), bindless);
```

---

## SM5 vs SM6.6 Binding Differences

The `bindless` flag controls how textures are bound:

| Mode | Textures | Samplers | How shaders access textures |
|------|----------|----------|-----------------------------|
| SM5 (`bindless=false`) | `SetFragmentTexture(slot, handle)` per member | `SetFragmentSampler(slot, handle)` per member | `Texture2D albedoMap : register(t0)` |
| SM6.6 (`bindless=true`) | No explicit binding — skipped | `SetFragmentSampler(slot, handle)` per member | `ResourceDescriptorHeap[srvIndex]` |

In SM5 mode, the `resourceSlot` in metadata determines the HLSL register index (`tN`,
`sN`, `uN`). In SM6.6 mode, SRV indices are typically stored as scalar fields in the CB
(e.g., `irradianceSrvIndex`) and read from the CB by the shader directly.

UAV binding is intentionally excluded from `BindResources` — UAV binding requires the
compute root signature path which varies per backend and is handled by each compute pass.

---

## Existing Parameter Structs

Three structs have already been migrated from manual CB definitions:

### PerFrameParams (`per_frame_params.h`)

Mirrors `PerFrameCB` (512-byte scalar section) plus IBL texture handles.

```
cbSize: 512 bytes
Textures: irradianceCubemap (t5), prefilteredSpecular (t6), brdfLut (t7)
```

Verified with:
```cpp
static_assert(offsetof(PerFrameParams, irradianceCubemap) == 512, ...);
```

### ShadowDataParams (`shadow_data_params.h`)

Mirrors `ShadowDataCB` (768-byte scalar section) plus shadow atlas handles.

```
cbSize: 768 bytes
Textures: shadowAtlas (t3), staticShadowAtlas (t8), dfsOutputTexture (t9)
```

Verified with:
```cpp
static_assert(offsetof(ShadowDataParams, shadowAtlas) == 768, ...);
```

### LightingParams (`lighting_params.h`)

Mirrors `LightingCB` (3328-byte scalar section). No resource handles currently.

```
cbSize: 3328 bytes
Textures: none
```

Verified with:
```cpp
static_assert(sizeof(LightingParams) == sizeof(LightingCB), ...);
```

---

## HLSL Generation for Validation

`GenerateHLSL()` converts metadata into an HLSL `cbuffer` + texture/sampler declarations.
This is used to verify that the C++ struct layout agrees with the hand-written HLSL.

```cpp
#include "engine/renderer/core/shader_parameter_hlsl.h"

// Generate HLSL for MyPassParams, bound to cbuffer register b2
std::string hlsl = uz::GenerateHLSL(
    MyPassParams::GetMetadata(),
    "MyPassCB",   // cbuffer name in HLSL
    2             // register slot (b2)
);
// hlsl now contains:
// cbuffer MyPassCB : register(b2) {
//     float4x4 viewProj;   // offset: 0, size: 64
//     float4 cameraPos;    // offset: 64, size: 16
//     float exposure;      // offset: 80, size: 4
//     float _pad[3];       // offset: 84, size: 12
// };
// TextureCube envCubemap : register(t5);
// Texture2D brdfLut : register(t7);
// SamplerState linearSampler : register(s0);
```

> **Note**: HLSL type inference from byte size is a heuristic (4 bytes → `float`, 64 bytes
> → `float4x4`, etc.). The generated output is for documentation and manual comparison
> against shader source — it is not compiled or injected into shaders.

---

## Alignment Rules and Padding

HLSL cbuffers pack scalars into 16-byte rows. A single `float` at offset 80 occupies
bytes 80–83; the next `float4` must start at offset 96 (not 84). Failing to pad correctly
causes the C++ struct to disagree with the HLSL cbuffer layout, producing silent garbage values.

**Correct padding pattern:**

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(ExampleParams)
    SHADER_PARAMETER(Vec4,  lightDir)         // 16 bytes, offset 0  — OK, 16-byte boundary
    SHADER_PARAMETER(float, intensity)        // 4 bytes,  offset 16 — OK, 16-byte boundary
    SHADER_PARAMETER_ARRAY(float, _pad0, 3)   // 12 bytes — pads to offset 32
    SHADER_PARAMETER(Vec4,  lightColor)       // 16 bytes, offset 32 — OK
SHADER_PARAMETER_SCALAR_END()
END_SHADER_PARAMETER_STRUCT()
// Total scalar: 48 bytes → cbSize = 256 (next 256-byte multiple)
```

The 256-byte alignment is enforced at compile time by `END_SHADER_PARAMETER_METADATA`.

---

## See Also

- `runtime/engine/renderer/core/shader_parameters.h` — Core types.
- `runtime/engine/renderer/core/shader_parameter_macros.h` — Macro definitions.
- `runtime/engine/renderer/core/shader_parameter_binder.h` — Binder API.
- `runtime/engine/renderer/core/shader_parameter_hlsl.h` — HLSL generation.
- [Shader Parameter Migration Guide](shader-parameter-migration-guide.md) — Step-by-step migration from manual CBs.
- `docs/specs/shader-system.md` — Shader system specification.
