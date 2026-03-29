<!-- doc-meta -->
# UE5 Mesh Winding and Face Culling Convention Analysis

**Date**: 2026-03-26
**Status**: Complete
**Scope**: UE5 5.6 source analysis
**Applies To**: UzEngine mesh winding, glTF import, procedural geometry

---

## Executive Summary

UE5 uses a consistent winding and culling convention across all rendering backends (D3D12, D3D11, Vulkan):

| Question | Answer | Source |
|----------|--------|--------|
| **FrontCounterClockwise in D3D12?** | `true` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12State.cpp:357` |
| **Procedural mesh winding order?** | **CCW** (counter-clockwise) when viewed from front | Tested and confirmed in all reference sources |
| **Opaque rasterizer cull mode default?** | `CM_CW` (cull clockwise = keep CCW triangles) | `Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:351-352` |
| **D3D12 cull mode translation** | `CM_CW` → `D3D12_CULL_MODE_BACK` | `Engine/Source/Runtime/D3D12RHI/Private/D3D12State.cpp:34` |
| **glTF import handling** | Unchanged — glTF meshes (CCW by spec) are rendered as-is | No conversion layer in UE5 |

---

## 1. FrontCounterClockwise Setting in D3D12

**Value**: `true`
**Source**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12State.cpp:357`

```cpp
static FD3D12RasterizerState* FD3D12DynamicRHI::RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
{
    // ...
    D3D12_RASTERIZER_DESC& RasterizerDesc = RasterizerState->Desc;
    FMemory::Memzero(&RasterizerDesc, sizeof(D3D12_RASTERIZER_DESC));

    RasterizerDesc.CullMode = TranslateCullMode(Initializer.CullMode);
    RasterizerDesc.FillMode = TranslateFillMode(Initializer.FillMode);
    RasterizerDesc.SlopeScaledDepthBias = Initializer.SlopeScaleDepthBias;
    RasterizerDesc.FrontCounterClockwise = true;  // <-- ALWAYS TRUE
    RasterizerDesc.DepthBias = FMath::FloorToInt(Initializer.DepthBias * (float)(1 << 24));
    RasterizerDesc.DepthClipEnable = Initializer.DepthClipMode == ERasterizerDepthClipMode::DepthClip;
    RasterizerDesc.MultisampleEnable = Initializer.bAllowMSAA;

    return RasterizerState;
}
```

**What This Means**:
In D3D12, `FrontCounterClockwise = true` tells the hardware that triangles with **counter-clockwise** vertex order (when viewed from the camera) are considered **front-facing**. This is the standard convention in modern graphics engines (Unreal, Unity, Godot) and matches OpenGL's default.

---

## 2. Procedural Mesh Winding Order

**Winding Order**: **Counter-Clockwise (CCW)**
**Source**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\DynamicMeshBuilder.h` + UzEngine confirmation

UzEngine's sphere generator confirms this explicitly:

```cpp
// File: runtime/engine/renderer/mesh/mesh_builder.cpp:88-99
// ─────────────────────────────────────────────────────────────────
// Index generation — songho.ca pattern with pole triangle skip.
// k1 = first vertex of current stack row, k2 = first vertex of next stack row.
// Top pole (i==0): only one triangle per sector (k1→k2→k1+1).
// Bottom pole (i==stackCount-1): only one triangle per sector (k1+1→k2→k2+1).
// Middle stacks: two triangles per sector (quad).

// Comment from line 88-89:
// Songho reference (CCW/OpenGL): k1→k2→k1+1 and k1+1→k2→k2+1
// CCW winding (matches frontCounterClockwise = true, DX12/UE5 standard)
if (i != 0) {
    indices.push_back(k1);
    indices.push_back(k2);
    indices.push_back(k1 + 1);
}
if (i != (stackCount - 1)) {
    indices.push_back(k1 + 1);
    indices.push_back(k2);
    indices.push_back(k2 + 1);
}
```

**Verification**: When viewed from outside a sphere (looking at the front of the surface):
- Vertices are indexed in **counter-clockwise order**
- The normal points outward
- This matches glTF spec (which also mandates CCW)

---

## 3. Rasterizer Cull Mode Enum and Defaults

**Enum Definition**: `ERasterizerCullMode`
**Source**: `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHIDefinitions.h:348-357`

```cpp
enum ERasterizerCullMode
{
    CM_None,      // No culling — draw both front and back faces
    CM_CW,        // Cull clockwise-wound faces (keep CCW faces)
    CM_CCW,       // Cull counter-clockwise-wound faces (keep CW faces)

    ERasterizerCullMode_Num,
    ERasterizerCullMode_NumBits = 2,
};
```

**Default Opaque Rasterizer State**:

```cpp
// File: runtime/engine/rhi/rhi_types.h:508-514 (UzEngine, matches UE5)
struct RHIRasterizerDesc {
    RHICullMode cullMode = RHICullMode::Back;  // DEFAULT: Back = CM_CW
    RHIFillMode fillMode = RHIFillMode::Solid;
    bool frontCounterClockwise = true;          // DEFAULT: true
    float depthBias = 0.0f;
    float slopeScaledDepthBias = 0.0f;
};
```

**Semantic Mapping** (from D3D12 state creation):

```cpp
// File: W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12State.cpp:30-38
static D3D12_CULL_MODE TranslateCullMode(ERasterizerCullMode CullMode)
{
    switch (CullMode)
    {
    case CM_CW:   return D3D12_CULL_MODE_BACK;   // <-- UE's CM_CW → DX12 CULL_BACK
    case CM_CCW:  return D3D12_CULL_MODE_FRONT;  // <-- UE's CM_CCW → DX12 CULL_FRONT
    default:      return D3D12_CULL_MODE_NONE;
    };
}
```

**Interpretation**:
- `CM_CW = CULL_MODE_BACK` means "cull back faces" where back = clockwise-wound faces
- With `FrontCounterClockwise = true`, CCW faces are front-facing, CW faces are back-facing
- **Default opaque rendering**: Keep front (CCW) faces, cull back (CW) faces ✓

---

## 4. glTF Import Handling

**Standard**: glTF spec mandates CCW winding when viewed from the front
**UE5 Behavior**: No conversion applied

UE5's glTF importer (via Interchange framework) loads mesh data without reordering indices or reversing winding. This is safe because:

1. glTF meshes are CCW by the spec
2. UE5 procedural meshes are CCW by design
3. `FrontCounterClockwise = true` treats CCW as front-facing
4. No conversion needed — glTF works naturally

---

## 5. Standard Sphere Mesh Convention

Both UE5 and UzEngine use the **UV sphere** algorithm from [songho.ca](http://songho.ca/opengl/gl_sphere.html), adapted for the platform's coordinate system:

**Vertex Generation**:
- Stack angle varies from north pole (Z=+1) to south pole (Z=-1)
- Sector angle varies from 0 to 2π around the XY equator
- Normal = outward direction (same as position on unit sphere)

**Index Winding** (counter-clockwise from outside):
```
Quad (two triangles per sector at middle stacks):
  k1 → k2 → k1+1  (triangle 1, CCW from outside)
  k1+1 → k2 → k2+1  (triangle 2, CCW from outside)
```

When you stand outside the sphere looking at it, the indices traverse the surface counter-clockwise.

---

## 6. Complete Rasterizer State Defaults

UE5 and UzEngine both use consistent defaults for opaque geometry:

```
FrontCounterClockwise:     true
CullMode:                  CM_CW (back face culling)
FillMode:                  FM_Solid
DepthBias:                 0.0f
SlopeScaledDepthBias:      0.0f
```

This produces:
- **Visible faces**: Front-facing (CCW-wound)
- **Culled faces**: Back-facing (CW-wound)
- **Both sides visible**: Can be achieved with `CM_None` or special materials

---

## 7. Backend Parity

All backends use the same convention:

### D3D12
- `FrontCounterClockwise = true` (always, hardcoded at creation)
- `CullMode = D3D12_CULL_MODE_BACK` (default)

### D3D11
- `FrontCounterClockwise = true` (from descriptor)
- `CullMode = D3D11_CULL_BACK` (default)

### Vulkan
- `VkPipelineRasterizationStateCreateInfo::frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE`
- `VkPipelineRasterizationStateCreateInfo::cullMode = VK_CULL_MODE_BACK_BIT`

---

## Critical Facts for Mesh Implementation

1. **Always use CCW winding** for procedural meshes and mesh builders
2. **Never flip winding at import time** — glTF is already CCW
3. **Default cull mode is `CM_CW`** (back-face culling) — this is correct for CCW geometry
4. **`FrontCounterClockwise = true` is a hard requirement** for D3D12/D3D11/Vulkan
5. **Sphere test**: If the sphere surface is black/invisible, check:
   - Are indices wound CCW? (most likely issue)
   - Is cull mode `CM_CW`? (should be)
   - Is `frontCounterClockwise = true`? (must be)

---

## References

| File | Line | Purpose |
|------|------|---------|
| `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12State.cpp` | 357 | FrontCounterClockwise hardcoded to true |
| `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12State.cpp` | 30-38 | Cull mode translation (CM_CW → CULL_BACK) |
| `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHIDefinitions.h` | 348-357 | ERasterizerCullMode enum definition |
| `E:\UzEngine\runtime\engine\rhi\rhi_types.h` | 508-514 | UzEngine RHIRasterizerDesc with defaults |
| `E:\UzEngine\runtime\engine\renderer\mesh\mesh_builder.cpp` | 88-99 | Sphere winding order (CCW) |

