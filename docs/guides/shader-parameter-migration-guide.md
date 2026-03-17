<!-- doc-meta
title: Shader Parameter Migration Guide
category: guide
theme: rendering
status: active
created: 2026-03-16
updated: 2026-03-16
author: docs-operator
-->

# Shader Parameter Migration Guide

This guide explains how to migrate an existing constant buffer struct with manual
`SetRootCBV` + `SetFragmentTexture` calls to the new `ShaderParameterBinder` system.

---

## When to Migrate

Migrate a pass when any of the following apply:

- It has three or more separate resource binding calls (`SetFragmentTexture`, `SetFragmentSampler`).
- Its CB struct has a corresponding texture list that changes alongside it.
- You are adding SM5 resource binding to a pass that previously only ran on SM6.6 bindless.
- You want compile-time layout verification with `static_assert`.

Passes that bind only a constant buffer with no textures (e.g., a simple postprocess pass
with one CB) do not benefit much — migration is optional in that case.

---

## Before and After

### Before — manual CB + resource binding

```cpp
// ── Header ───────────────────────────────────────────────────────────────────
// constant_buffers.h (legacy)
struct alignas(256) MyPassCB {
    Mat4     viewProj;
    Vec4     cameraPos;
    float    exposure;
    float    _pad[3];
    // NOTE: textures listed separately, no connection to this struct
};

// ── Render pass ──────────────────────────────────────────────────────────────
void MyPass::Execute(IRHICommandList* cmd) {
    MyPassCB cbData{};
    cbData.viewProj  = camera.GetViewProj();
    cbData.cameraPos = Vec4(camera.GetPosition(), 1.0f);
    cbData.exposure  = autoExposure.GetCurrentEV();

    // Manual: allocate ring, copy, bind
    auto alloc = m_cbRing.Allocate(sizeof(MyPassCB));
    std::memcpy(alloc.cpuPtr, &cbData, sizeof(MyPassCB));
    cmd->SetRootCBV(kSlotMyPassCB, alloc.bufferHandle,
                    alloc.offsetBytes, alloc.cpuPtr, alloc.sizeBytes);

    // Manual texture binding — broken on SM5 if order changes
    if (!bindless) {
        cmd->SetFragmentTexture(5, m_envCubemap);
        cmd->SetFragmentTexture(7, m_brdfLut);
        cmd->SetFragmentSampler(0, m_linearSampler);
    }

    cmd->DrawFullscreenTriangle();
}
```

### After — ShaderParameterBinder

```cpp
// ── Header ───────────────────────────────────────────────────────────────────
// my_pass_params.h
BEGIN_SHADER_PARAMETER_STRUCT(MyPassParams)
    SHADER_PARAMETER(Mat4,  viewProj)
    SHADER_PARAMETER(Vec4,  cameraPos)
    SHADER_PARAMETER(float, exposure)
    SHADER_PARAMETER_ARRAY(float, _pad, 3)
SHADER_PARAMETER_SCALAR_END()
    SHADER_PARAMETER_TEXTURE(TextureCube, envCubemap,     5)
    SHADER_PARAMETER_TEXTURE(Texture2D,   brdfLut,        7)
    SHADER_PARAMETER_SAMPLER(             linearSampler,  0)
END_SHADER_PARAMETER_STRUCT()

// ── .cpp ─────────────────────────────────────────────────────────────────────
// my_pass_params.cpp
BEGIN_SHADER_PARAMETER_METADATA(MyPassParams)
    METADATA_SCALAR      (MyPassParams, viewProj,      Mat4)
    METADATA_SCALAR      (MyPassParams, cameraPos,     Vec4)
    METADATA_SCALAR      (MyPassParams, exposure,      float)
    METADATA_SCALAR_ARRAY(MyPassParams, _pad,          float, 3)
    METADATA_TEXTURE_CUBE(MyPassParams, envCubemap,    5)
    METADATA_TEXTURE2D   (MyPassParams, brdfLut,       7)
    METADATA_SAMPLER     (MyPassParams, linearSampler, 0)
END_SHADER_PARAMETER_METADATA(MyPassParams, 256, 2, 1, 0)

// ── Render pass ──────────────────────────────────────────────────────────────
void MyPass::Execute(IRHICommandList* cmd, bool bindless) {
    MyPassParams params{};
    params.viewProj      = camera.GetViewProj();
    params.cameraPos     = Vec4(camera.GetPosition(), 1.0f);
    params.exposure      = autoExposure.GetCurrentEV();
    params.envCubemap    = m_envCubemap;
    params.brdfLut       = m_brdfLut;
    params.linearSampler = m_linearSampler;

    auto alloc = ShaderParameterBinder::Bind(
        cmd, &params, MyPassParams::GetMetadata(),
        m_cbRing, kSlotMyPassCB, bindless);
}
```

---

## Migration Steps

### 1. Identify the scalar section size

Count how many bytes the CB struct uses. The scalar section size must be a multiple of
256. If the existing struct uses `alignas(256)`, that size is your starting `cbSize`.

```cpp
// How big is the existing CB?
static_assert(sizeof(MyPassCB) % 256 == 0, "verify existing size");
// e.g. sizeof(MyPassCB) == 256 → cbSize = 256
```

### 2. Create the parameter struct header

Create `my_pass_params.h` next to the existing pass source. Declare all scalar fields
in the same order as the old CB struct, then add `SHADER_PARAMETER_SCALAR_END()` and
the texture/sampler/UAV members.

Match the old struct field-for-field. Do not reorder scalars — that would break
compatibility with shaders that read from the same cbuffer layout.

### 3. Add a layout verification static_assert

Add a `static_assert` that confirms the resource section starts at the expected offset:

```cpp
// Verify byte identity with old CB
static_assert(offsetof(MyPassParams, envCubemap) == sizeof(MyPassCB),
              "MyPassParams scalar section must match MyPassCB layout");
```

If you are replacing a CB struct entirely (no legacy coexistence needed), use:

```cpp
static_assert(sizeof(MyPassParams) > sizeof(MyPassCB),
              "Resource handles must extend beyond the scalar section");
```

### 4. Define metadata in the .cpp file

Create `my_pass_params.cpp`. List every scalar member and every resource member in the
same order they appear in the struct. The member count must match the struct definition
exactly — missing or extra entries will produce wrong offsets at runtime.

The four counts in `END_SHADER_PARAMETER_METADATA` are:
1. `cbSize` — the 256-byte-aligned scalar section size.
2. `textureCount` — number of `Texture2D` + `TextureCube` members combined.
3. `samplerCount` — number of `Sampler` members.
4. `uavCount` — number of `UAV` members.

### 5. Replace manual binding in the render pass

Replace the three-step manual sequence with `ShaderParameterBinder::Bind()`:

```cpp
// Remove these:
auto alloc = m_cbRing.Allocate(sizeof(MyPassCB));
std::memcpy(alloc.cpuPtr, &cbData, sizeof(MyPassCB));
cmd->SetRootCBV(kSlotMyPassCB, ...);
if (!bindless) {
    cmd->SetFragmentTexture(5, m_envCubemap);
    cmd->SetFragmentTexture(7, m_brdfLut);
    cmd->SetFragmentSampler(0, m_linearSampler);
}

// Add this:
auto alloc = ShaderParameterBinder::Bind(
    cmd, &params, MyPassParams::GetMetadata(),
    m_cbRing, kSlotMyPassCB, bindless);
```

### 6. Add the new .cpp to the build

In the Zig build file, add `my_pass_params.cpp` to the source list alongside the pass
source. The metadata definition must be compiled into the same binary as the pass.

### 7. Remove the old CB struct (optional)

Once all passes that shared the old struct have been migrated, you can remove the
original `struct MyPassCB` from `constant_buffers.h`. Until then, leave it — the
`static_assert` in step 3 keeps the two in sync.

---

## Real Migration Example: ShadowDataParams

`ShadowDataParams` is a complete migration example in the codebase.

**Before** (legacy, still exists for coexistence):
```cpp
// constant_buffers.h
struct alignas(256) ShadowDataCB {
    Mat4     cascadeViewProj[8];      // 512 bytes
    float    cascadeSplits[8];        // 32 bytes
    // ... more scalars ...
    float    _pad2[18];               // padding to 768 bytes
};
```

**After** (`shadow_data_params.h`):
```cpp
BEGIN_SHADER_PARAMETER_STRUCT(ShadowDataParams)
    SHADER_PARAMETER_ARRAY(Mat4,     cascadeViewProj,       8)
    SHADER_PARAMETER_ARRAY(float,    cascadeSplits,         8)
    // ... mirrored scalar fields ...
    SHADER_PARAMETER_ARRAY(float,    _pad2,                18)
SHADER_PARAMETER_SCALAR_END()
    SHADER_PARAMETER_TEXTURE(Texture2D, shadowAtlas,        3)
    SHADER_PARAMETER_TEXTURE(Texture2D, staticShadowAtlas,  8)
    SHADER_PARAMETER_TEXTURE(Texture2D, dfsOutputTexture,   9)
END_SHADER_PARAMETER_STRUCT()

// Layout guard
static_assert(offsetof(ShadowDataParams, shadowAtlas) == 768,
              "ShadowDataParams resource section must start at byte 768");
```

**Metadata** (`shadow_data_params.cpp`):
```cpp
BEGIN_SHADER_PARAMETER_METADATA(ShadowDataParams)
    METADATA_SCALAR_ARRAY(ShadowDataParams, cascadeViewProj,      Mat4,  8)
    METADATA_SCALAR_ARRAY(ShadowDataParams, cascadeSplits,        float, 8)
    // ... mirrored entries ...
    METADATA_SCALAR_ARRAY(ShadowDataParams, _pad2,                float, 18)
    METADATA_TEXTURE2D   (ShadowDataParams, shadowAtlas,          3)
    METADATA_TEXTURE2D   (ShadowDataParams, staticShadowAtlas,    8)
    METADATA_TEXTURE2D   (ShadowDataParams, dfsOutputTexture,     9)
END_SHADER_PARAMETER_METADATA(ShadowDataParams, 768, 3, 0, 0)
```

---

## Pitfalls

### Scalars out of order in metadata

The metadata member list must match the struct field order exactly. If you reorder
entries in `BEGIN_SHADER_PARAMETER_METADATA`, the binder copies the correct bytes
but `GenerateHLSL()` and debug output will show wrong field names next to wrong offsets.

### cbSize not 256-byte aligned

`END_SHADER_PARAMETER_METADATA` contains:
```cpp
static_assert((CbSize) % 256 == 0, "cbSize must be 256-byte aligned");
```
This fires at compile time. Add `SHADER_PARAMETER_ARRAY(float, _pad, N)` to the scalar
section to reach the next 256-byte boundary. Calculate N as `(256 - rawSize % 256) / 4`.

### Resource members listed before SHADER_PARAMETER_SCALAR_END

The binder copies `cbSize` bytes from offset 0 of the struct. If a texture handle
(`RHITextureHandle`, 8 bytes) is placed before the padding, the layout changes and the
copy includes garbage bytes. Always place `SHADER_PARAMETER_SCALAR_END()` after the last
scalar field.

### Wrong resource counts in END_SHADER_PARAMETER_METADATA

The `textureCount`, `samplerCount`, `uavCount` arguments are informational — the binder
uses the `ShaderParamType` in each member entry, not these counts. However, the
`static_assert` in `END_SHADER_PARAMETER_METADATA` does not verify them. Keep them
accurate for `GenerateHLSL()` and debugging purposes.

---

## See Also

- [Shader Parameter Binding Guide](shader-parameter-binding-guide.md) — Full reference for the macro system and binder API.
- `runtime/engine/renderer/core/per_frame_params.h` — Migrated PerFrameParams example.
- `runtime/engine/renderer/core/shadow_data_params.h` — Migrated ShadowDataParams example.
- `runtime/engine/renderer/core/lighting_params.h` — Migrated LightingParams example.
- `docs/specs/shader-system.md` — Shader system specification.
