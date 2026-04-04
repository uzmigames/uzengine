# Material System: Decal Material Support Architecture

<!-- doc-meta
title: Material System — Decal Material Support Architecture
category: analysis
engine: uzengine
theme: rendering
created: 2026-03-31
updated: 2026-03-31
status: active
author: engine-architect
-->

## Overview

This document proposes an architecture for integrating decal materials into
UzEngine's existing material system. Currently `DecalComponent` stores raw texture
paths and raw SRV indices directly on the component struct. The goal is to let
`DecalComponent` hold a `MaterialInterface*` with `materialDomain = EMaterialDomain::Decal`,
and have the decal render pass query that interface for textures the same way the
GBuffer pass already does for surface materials.

## Context

The decal test scene (`sample/content/scenes/decal_test.uzscene`) exposes the
friction between the current hardcoded `DecalComponent` and the mature
`MaterialInterface` hierarchy that already exists in the renderer. Decal artists
cannot author decals as `.uzmat` files, cannot override individual channels per
instance (e.g., a wet-stain variant of the same bullet-hole decal), and the
render pass has no way to use the `EDecalBlendMode` stored on a material to
decide which DBuffer targets to write.

Related task phase: `feature/falcor-parity` branch, decal system groundwork.

---

## Section 1: Current State of UzEngine Materials

### 1.1 Type hierarchy

```
MaterialInterface          (material_interface.h — abstract base)
  |-- Material             (material_asset.h — full PBR asset with fixed fields)
  |-- MaterialInstance     (material_instance.h — sparse override chain)
```

`MaterialInterface` exposes:
- `GetScalarParam(name, default)` — named float query
- `GetVectorParam(name, ...)` — named float3 query
- `GetTexturePath(name)` — VFS-relative path string
- `GetTextureSrvIndex(name)` — bindless GPU SRV index (uint32)
- Typed convenience getters — `GetRoughnessMultiplier()`, etc.
- Domain/blend/shading model queries — `GetMaterialDomain()`, `GetBlendMode()`, `GetShadingModel()`

### 1.2 Enum coverage

`material_types.h` already declares the full set:

```
EMaterialDomain: Surface=0, Decal=1, PostProcess=2, LightFunction=3, UI=4, Volume=5
EShadingModel:  Unlit=0, DefaultLit=1, Subsurface=2, ..., ThinTranslucent=9
EBlendMode:     Opaque=0, Masked=1, Translucent=2, Additive=3, Modulate=4
```

`EMaterialDomain::Decal` is already defined. It is not enforced anywhere — the
GBuffer pipeline accepts any material regardless of domain, and no code path
special-cases `Decal`.

### 1.3 Material pipeline

`MaterialPipeline` compiles `MaterialGraph` objects into GBuffer PSOs. The
pipeline is hardcoded to target the four GBuffer render targets (albedo RGBA8,
normal+material RGBA8, world-pos RGBA32F, emissive RGBA16F). No DBuffer target
layout exists.

### 1.4 Texture binding flow (current)

For surface materials:

```
Material::albedoPath  ──► SceneRenderer::UploadMaterialTextures()
                          ──► IRHIDevice::LoadTexture()
                          ──► Material::albedoSrvIndex  (GPU SRV heap index)
                          ──► DecalObjectCB::albedoSrv  (uploaded to CB ring)
                          ──► decal_ps.hlsl reads via ResourceDescriptorHeap[albedoSrv]
```

For decals, the current path stores `albedoPath`, `normalPath`, `roughnessPath`,
`metallicPath` directly in `DecalComponent`. The scene loader reads those paths.
The render gather step in `frame_decals.cpp` leaves `albedoSrvIndex = UINT32_MAX`
because no code resolves the decal paths to SRV indices. This is an open bug —
decal textures are not actually bound to the shader.

### 1.5 DecalComponent — current problems

`DecalComponent` stores:
- `albedoTextureId` — raw SRV index (UINT32_MAX = not loaded)
- `normalTextureId` — raw SRV index
- `materialTextureId` — raw SRV index (packed roughness/metallic/AO)
- `albedoPath`, `normalPath`, `roughnessPath`, `metallicPath` — separate string fields
- `roughness`, `metallic` — scalar fallback values when textures are absent

Problems:
1. Decal textures are never resolved to SRV indices in the current render gather.
2. No `EDecalBlendMode` field — all decals write all three DBuffer targets regardless of which channels are authored.
3. Artists cannot create a `.uzmat` file that is typed as a decal material and reuse the existing material pipeline.
4. `MaterialInstance` override chain cannot be applied to decals (no way to author a "wet variant" of a bullet-hole decal).

---

## Section 2: UE5 Reference — How Decal Materials Work

### 2.1 Material domain

In UE5, `UMaterial` has `EMaterialDomain MaterialDomain`. When set to
`MD_DeferredDecal`, the material editor shows an additional `EDecalBlendMode`
dropdown. The blend mode declares which GBuffer channels the decal writes:

| UE5 EDecalBlendMode | Writes BaseColor | Writes Normal | Writes Roughness/Specular/Metallic |
|---|---|---|---|
| DBM_Translucent | Yes | No | No |
| DBM_Stain | Yes | No | No |
| DBM_Normal | No | Yes | No |
| DBM_Emissive | No | No | No |
| DBM_DBuffer_ColorNormalRoughness | Yes | Yes | Yes |
| DBM_DBuffer_Color | Yes | No | No |
| DBM_DBuffer_ColorNormal | Yes | Yes | No |
| DBM_DBuffer_ColorRoughness | Yes | No | Yes |
| DBM_DBuffer_Normal | No | Yes | No |
| DBM_DBuffer_NormalRoughness | No | Yes | Yes |
| DBM_DBuffer_Roughness | No | No | Yes |
| DBM_AmbientOcclusion | No | No | No |

### 2.2 DecalComponent → material relationship

`UDecalComponent` (DecalComponent.h) holds a single `TObjectPtr<UMaterialInterface> DecalMaterial`.
This is the only texture/parameter source. The component has no individual texture
path fields. Instead:

- `SetDecalMaterial(UMaterialInterface*)` sets the material.
- `GetDecalMaterial()` returns it.
- The render proxy (`FDeferredDecalProxy`) carries a reference to the material interface.
- `DecalRendering::ComputeDecalBlendDesc()` reads the material's connected property
  flags (`HasBaseColorConnected()`, `HasNormalConnected()`, etc.) to build
  `FDecalBlendDesc`, which controls the render target write mask and blend state.

### 2.3 Decal blend descriptor

`FDecalBlendDesc` is a packed 32-bit union carrying:
- `BlendMode` (8 bits) — the standard blend mode
- `RenderStageMask` (8 bits) — bitmask of which render stages this decal participates in
- `bWriteBaseColor`, `bWriteNormal`, `bWriteRoughnessSpecularMetallic`, `bWriteEmissive`, `bWriteAmbientOcclusion` (1 bit each)

This descriptor is computed once per material and stored on the proxy. The render
pass uses it to:
1. Select the correct blend state (per-RT write masks).
2. Determine whether the decal belongs in the BeforeBasePass (DBuffer) or
   BeforeLighting (GBuffer) stage.
3. Drive shader compilation defines (`WRITE_BASE_COLOR`, etc.).

### 2.4 Material parameters consumed by the decal shader

The decal pixel shader reads from the material the same way a surface pixel shader
does. In UE5 this is transparent because both use the same material expression
graph mechanism. The shader receives material parameters via `FMaterialRenderProxy`
which is queried identically regardless of whether the material domain is Surface or
DeferredDecal.

For UzEngine without a full material graph, the equivalent is: the decal render
pass calls `GetTextureSrvIndex("Albedo")`, `GetTextureSrvIndex("Normal")`,
`GetScalarParam("RoughnessMultiplier", ...)`, etc. on the `MaterialInterface*`
stored on the component — exactly the same calls the GBuffer pass makes today.

---

## Section 3: Falcor Reference — Material Domain Concept

Falcor's `MaterialType` enum (MaterialTypes.slang) defines types as BSDF
categories: `Standard`, `Hair`, `Cloth`, etc. There is no `Decal` type because
Falcor is a path tracer / research renderer, not a rasterized deferred pipeline.
Falcor does not implement screen-space deferred decals. Its material system is
orthogonal to the decal problem.

Conclusion: Falcor is not a useful reference for decal material architecture. The
UE5 reference is definitive.

---

## Section 4: Architecture Proposal

### 4.1 EDecalBlendMode enum (new)

A new enum in `material_types.h` declares the write channels for decal materials.
This mirrors UE5's `EDecalBlendMode` but uses UzEngine naming conventions.

```
EDecalBlendMode (uint8_t):
  ColorNormalRoughness  = 0   // Write albedo + normal + roughness/metallic — most common
  ColorNormal           = 1   // Write albedo + normal only
  ColorRoughness        = 2   // Write albedo + roughness/metallic only
  Color                 = 3   // Write albedo only
  Normal                = 4   // Write normal only
  NormalRoughness       = 5   // Write normal + roughness/metallic only
  Roughness             = 6   // Write roughness/metallic only
  Emissive              = 7   // Write emissive only (forward pass, no DBuffer)
  AmbientOcclusion      = 8   // Write AO only (separate compute pass)
```

This enum lives on `Material` as `EDecalBlendMode decalBlendMode`. It is only
meaningful when `materialDomain == EMaterialDomain::Decal`. For Surface materials
it is ignored.

### 4.2 DecalWriteMask helper (new)

A derived bitmask computed from `EDecalBlendMode` at material evaluation time
controls per-RT blend states and shader defines:

```
DecalWriteMask {
  bool writeAlbedo;
  bool writeNormal;
  bool writeRoughness;   // includes metallic packed in same target
  bool writeEmissive;
  bool writeAO;
};

DecalWriteMask ComputeDecalWriteMask(EDecalBlendMode mode);
```

This struct is cached on `DecalRenderData` after gathering so the render pass
does not need to re-query the material per draw call.

### 4.3 Material::decalBlendMode field (addition to material_asset.h)

```cpp
/// Decal blend mode — controls which DBuffer channels this decal writes.
/// Only meaningful when materialDomain == EMaterialDomain::Decal.
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer channel selection
EDecalBlendMode decalBlendMode = EDecalBlendMode::ColorNormalRoughness;
```

This field is serialized in `.uzmat` under `"decal_blend_mode"`.

### 4.4 MaterialInterface::GetDecalBlendMode() (addition to material_interface.h)

```cpp
/// Returns the decal blend mode for materials with domain == Decal.
/// Returns EDecalBlendMode::ColorNormalRoughness for all other domains.
[[nodiscard]] virtual EDecalBlendMode GetDecalBlendMode() const {
    return EDecalBlendMode::ColorNormalRoughness;
}
```

`Material` overrides this to return its `decalBlendMode` field.
`MaterialInstance` overrides it with a `hasDecalBlendMode_` flag pattern
identical to every other scalar override.

### 4.5 DecalComponent — material pointer (migration from raw paths)

The component adds a `MaterialInterface*` pointer alongside the existing raw
fields. During Phase 1 both coexist. During Phase 2 the raw fields are removed.

Phase 1 layout:

```
struct DecalComponent {
    // NEW: optional material reference
    MaterialInterface* material = nullptr;   ///< If set, overrides all raw texture paths below.

    // EXISTING: raw paths (kept for backward compat in Phase 1)
    std::string albedoPath;
    std::string normalPath;
    std::string roughnessPath;
    std::string metallicPath;

    // EXISTING: scalar fallbacks
    float opacity      = 1.0f;
    float normalBlend  = 1.0f;
    float roughness    = 0.5f;
    float metallic     = 0.0f;

    // EXISTING: geometry and sorting
    float extentsX = 25.0f;
    float extentsY = 25.0f;
    float extentsZ = 5.0f;
    int32_t priority   = 0;
    float fadeDistance = 5.0f;
    float angleFadeStart = 60.0f;
    bool enabled       = true;
};
```

Gather logic: when `material != nullptr`, source all texture SRV indices and
scalar parameters from `material->GetTextureSrvIndex(...)` and `material->GetScalarParam(...)`.
When `material == nullptr`, fall back to the legacy raw-path resolution path.

### 4.6 DecalObjectCB — adding decalBlendModeMask (change to decal_pass.h)

The CB ring data sent to the decal pixel shader needs to know which channels to
write so the shader can skip sampling textures for channels not being written.

```cpp
struct DecalObjectCB {
    Mat4     world;
    Mat4     svPosToDecal;
    Mat4     viewProj;
    float    opacity;
    float    normalBlend;
    float    roughness;
    float    metallic;
    float    fadeDistance;
    float    angleFade;
    uint32_t albedoSrv;
    uint32_t normalSrv;
    uint32_t materialSrv;
    uint32_t depthSrv;
    uint32_t normalGbufSrv;
    uint32_t decalBlendModeMask;   // NEW: bitmask (bit0=albedo, bit1=normal, bit2=roughness)
    float    _pad0 = 0.0f;
};
```

The shader reads `decalBlendModeMask` and conditionally skips texture sampling
for channels with their bit clear. This eliminates wasted texture fetches for
normal-only or color-only decals.

### 4.7 Data flow — material path vs raw path

```
Scene file (.uzscene)
  "decal": { "material": "materials/decals/bullet_hole.uzmat" }
                 |
                 v
  SceneLoader resolves path → MaterialRegistry::Load("bullet_hole.uzmat")
                 |
                 v
  Material* m (domain=Decal, blendMode=ColorNormal, albedoPath="...", normalPath="...")
                 |
                 v
  DecalComponent::material = m
                 |
                 v
  frame_decals.cpp: SceneRenderer::RegisterDecalPasses()
    for each active decal:
      if decal.component.material:
        albedoSrv  = material->GetTextureSrvIndex("Albedo")
        normalSrv  = material->GetTextureSrvIndex("Normal")
        opacity    = material->GetOpacity()
        normalBlend = material->GetNormalIntensity()
        roughness  = material->GetRoughnessMultiplier()
        metallic   = material->GetMetallicMultiplier()
        writeMask  = ComputeDecalWriteMask(material->GetDecalBlendMode())
      else:
        (legacy path — resolve albedoPath → albedoSrv via texture map)
        writeMask  = DecalWriteMask{true, true, true, false, false}  // all channels
                 |
                 v
  DecalObjectCB filled with resolved SRV indices + writeMask
                 |
                 v
  decal_ps.hlsl: sample only channels with bit set in decalBlendModeMask
                 |
                 v
  DBuffer A (albedo+opacity), DBuffer B (normal+blend), DBuffer C (roughness/metallic/blend)
                 |
                 v
  DecalApply compute: lerp DBuffer into GBuffer albedo + normal
```

### 4.8 Decal PSO variants

UE5 compiles separate PSOs per decal blend mode to use RT write masks.
UzEngine Phase 1 takes the simpler approach: one PSO, conditional sampling in
the shader. Phase 2 can introduce per-write-mask PSO variants to reduce overdraw.

Phase 1 shader pseudo-code:

```hlsl
// decal_ps.hlsl (new conditional structure)
DecalOutput main(DecalVSOutput input) {
    float4 albedo   = (decalCB.decalBlendModeMask & 1u) ?
                      SampleAlbedo(input, decalCB.albedoSrv) : float4(0,0,0,0);
    float4 normal   = (decalCB.decalBlendModeMask & 2u) ?
                      SampleNormal(input, decalCB.normalSrv) : float4(0.5,0.5,1.0,0);
    float4 material = (decalCB.decalBlendModeMask & 4u) ?
                      SampleMaterial(input, decalCB.materialSrv) : float4(0,0,0,0);

    // Write to DBuffer targets with alpha controlled by writeMask
    out.dbufferA = albedo   * float4(1,1,1, (decalCB.decalBlendModeMask & 1u) ? 1.0 : 0.0);
    out.dbufferB = normal   * float4(1,1,1, (decalCB.decalBlendModeMask & 2u) ? 1.0 : 0.0);
    out.dbufferC = material * float4(1,1,1, (decalCB.decalBlendModeMask & 4u) ? 1.0 : 0.0);
}
```

Setting the alpha channel of each DBuffer target to zero for channels not being
written causes the DBuffer apply step's lerp to have zero weight on those channels,
which is functionally equivalent to RT write mask suppression without needing PSO
variants. This is a correct and standard technique.

### 4.9 .uzmat file format extension

A decal material `.uzmat` file:

```ini
[Material]
materialDomain = Decal
blendMode = Translucent
decalBlendMode = ColorNormal

[Textures]
Albedo = textures/decals/bullet_hole_albedo.png
Normal = textures/decals/bullet_hole_n.png

[Params]
Opacity = 0.9
NormalIntensity = 0.7
```

The serializer reads `decalBlendMode` from the `[Material]` section and sets
`Material::decalBlendMode`. No other `.uzmat` parsing changes are needed.

### 4.10 MaterialRegistry (new concept, Phase 1 lightweight form)

Currently `Material` objects live on the stack inside scene-loaded entity
components. For decals to share a material across multiple `DecalComponent`
instances, a material needs a stable lifetime managed by an owner.

Phase 1 minimal approach: the `SceneLoader` holds `std::vector<std::unique_ptr<Material>>`
for all materials it instantiates. `DecalComponent::material` points into that
storage (raw pointer, no ownership). The loader's lifetime exceeds the scene
lifetime. This avoids a full asset registry in Phase 1 while providing the
shared-lifetime guarantee.

Phase 2 adds a proper `MaterialRegistry` with ref-counted handles.

---

## Section 5: Files That Change

### Phase 1 changes

| File | Change | Scope |
|---|---|---|
| `runtime/engine/renderer/material/material_types.h` | Add `EDecalBlendMode` enum and `ComputeDecalWriteMask()` | Small addition |
| `runtime/engine/renderer/material/material_interface.h` | Add `GetDecalBlendMode()` virtual with default | 5 lines |
| `runtime/engine/renderer/material/material_asset.h` | Add `decalBlendMode` field + `GetDecalBlendMode()` override + serialization in `GetScalarParam` | 15 lines |
| `runtime/engine/renderer/material/material_instance.h` | Add `hasDecalBlendMode_` + override pattern | 15 lines |
| `runtime/engine/ecs/environment/decal_component.h` | Add `MaterialInterface* material = nullptr` field | 3 lines |
| `runtime/engine/ecs/environment/decal_component.cpp` | Serialize/deserialize `"material"` path, resolve via loader | 20 lines |
| `runtime/engine/renderer/passes/geometry/decal_pass.h` | Add `decalBlendModeMask` to `DecalObjectCB` (replaces `_pad0`) | 2 lines |
| `runtime/shaders/runtime/decals/decal_ps.hlsl` | Conditional channel sampling based on `decalBlendModeMask` | ~30 lines |
| `runtime/engine/renderer/frame/frame_decals.cpp` | Resolve material textures via `MaterialInterface*` when set | ~40 lines |
| `docs/analysis/rendering/material-system-decal-support.md` | This document | New |

### Phase 2 changes (after Phase 1 is stable)

| File | Change |
|---|---|
| `runtime/engine/renderer/material/material_registry.h/.cpp` | New: ref-counted material asset registry |
| `runtime/engine/ecs/environment/decal_component.h` | Remove raw path fields, keep only `MaterialInterface*` |
| `runtime/engine/ecs/environment/decal_component.cpp` | Remove legacy path serialization |
| `runtime/engine/renderer/passes/geometry/decal_pass.h` | Add `DecalPSOVariant` per write-mask for RT optimization |
| `runtime/engine/renderer/core/pipeline/pipeline_decal.cpp` | Compile per-variant PSOs |
| `runtime/shaders/runtime/decals/decal_ps.hlsl` | Replace conditional sampling with compile-time defines per variant |

---

## Section 6: What Must NOT Change

The following constraints MUST be preserved throughout this work:

1. `MaterialInterface` MUST remain a pure abstract base. No concrete fields.
2. `GetTextureSrvIndex(name)` MUST remain the authoritative GPU binding path. The
   decal render pass MUST use this call, not any new decal-specific accessor.
3. `EMaterialDomain::Decal` value (1) MUST remain stable — it may be stored in
   GPU buffers or scene files.
4. `DecalObjectCB` padding MUST keep the struct 16-byte aligned. Adding
   `decalBlendModeMask` (uint32) replaces `_pad0 = 0.0f` exactly — no size
   change.
5. The DBuffer A/B/C layout (albedo+opacity, normal+blend, roughness+metallic+AO+blend)
   MUST NOT change. The decal blend mask controls write weights, not the layout.
6. The `MaterialInstance` override chain MUST work for decal materials.
   A `MaterialInstance` with `materialDomain = Decal` and `parent = &bulletHoleMaterial`
   MUST be a valid configuration.

---

## Section 7: What Is Out of Scope (Phase 1)

These items are architecturally correct but deferred to Phase 2 or later:

- **Per-write-mask PSO variants.** Phase 1 uses conditional shader sampling.
  Phase 2 compiles one PSO per `EDecalBlendMode` value for RT write mask
  efficiency.
- **Emissive decals.** `EDecalBlendMode::Emissive` requires a separate forward
  pass. Phase 1 documents the enum value but does not render it.
- **AO decals.** `EDecalBlendMode::AmbientOcclusion` requires writing to a
  separate AO texture. Phase 1 documents the enum value but does not render it.
- **MaterialRegistry with ref counting.** Phase 1 uses raw pointers into
  scene-loader owned storage.
- **Live hot-reload of decal materials.** Requires MaterialRegistry to notify
  dependent `DecalComponent` instances.
- **DecalMaterialInstance Lua API.** Phase 1 focuses on C++. Lua bindings follow
  after the C++ path is stable per the lua-parity rule.

---

## Section 8: Migration Guide for Existing Scenes

Scenes currently using the raw-path format in `DecalComponent`:

```json
"decal": {
    "albedo": "textures/decals/bullet_hole_albedo.png",
    "normal": "textures/decals/bullet_hole_n.png",
    "roughness": "textures/decals/bullet_hole_orm.png",
    "opacity": 0.9
}
```

These continue to work without modification in Phase 1. The scene loader checks
for the `"material"` key first; if absent, it falls back to the legacy raw-path
keys. The render gather in `frame_decals.cpp` checks `component.material != nullptr`
and falls back to the old SRV resolution path when it is null.

To upgrade a scene to material-based decals:

1. Create a `.uzmat` file with `materialDomain = Decal`.
2. Reference it in the scene: `"material": "materials/decals/bullet_hole.uzmat"`.
3. Remove the raw path keys from the scene file.
4. The legacy fields on `DecalComponent` remain zero-initialized and harmless.

In Phase 2, once all shipped scenes have been migrated, the legacy fields are
removed from `DecalComponent` and the fallback path is deleted.

---

## Section 9: Validation Criteria

A correct Phase 1 implementation satisfies all of the following:

1. A `Material` with `materialDomain = Decal` and `decalBlendMode = ColorNormal`
   can be loaded from a `.uzmat` file and assigned to `DecalComponent::material`.
2. `frame_decals.cpp` resolves `material->GetTextureSrvIndex("Albedo")` and
   `material->GetTextureSrvIndex("Normal")` to non-zero SRV indices when the
   textures are loaded.
3. `DecalObjectCB::decalBlendModeMask` is filled correctly: `0b011` (bits 0+1)
   for `ColorNormal`, `0b111` for `ColorNormalRoughness`.
4. The decal pixel shader writes zero alpha to `dbufferC` for a `ColorNormal`
   decal, leaving the GBuffer roughness/metallic channels unmodified after the
   apply pass.
5. A `MaterialInstance` with parent pointing to the bullet-hole `Material`
   overrides `GetOpacity()` correctly (the `hasOpacity_` flag pattern).
6. Scenes using the legacy raw-path format continue to render identically to
   before (no visual regression).
7. `--pass-dump` output shows DBuffer A and B non-zero for a decal in-view,
   and DBuffer C all-zero for a `ColorNormal` decal.

---

## Key Takeaways

- `EMaterialDomain::Decal` and the full `MaterialInterface` hierarchy are already
  in place. No new base types are needed.
- The critical addition is `EDecalBlendMode` on `Material` and `DecalComponent::material`.
- `DecalObjectCB` gains `decalBlendModeMask` (replacing existing padding) to drive
  conditional channel writes in the pixel shader. The struct size does not change.
- Phase 1 maintains full backward compatibility via a `material != nullptr` guard
  in the gather step.
- Falcor has no decal material concept and is not a reference for this feature.
  UE5's `FDecalBlendDesc` / `EDecalBlendMode` is the authoritative reference.
- The DBuffer alpha-channel-as-write-weight approach is correct without PSO
  variants, keeping Phase 1 implementation cost low.

## References

- Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer technique
- Pesce 2014 "Rendering Decals in Screen Space"
- `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/DecalRenderingCommon.h` — `FDecalBlendDesc`, `EDecalRenderStage`
- `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/DecalRenderingCommon.cpp` — `ComputeDecalBlendDesc()`
- `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Classes/Components/DecalComponent.h` — `DecalMaterial` field
- `e:/UzEngine/runtime/engine/renderer/material/material_types.h` — existing `EMaterialDomain`
- `e:/UzEngine/runtime/engine/ecs/environment/decal_component.h` — current raw-path component
- `e:/UzEngine/runtime/engine/renderer/passes/geometry/decal_pass.h` — `DecalObjectCB`
- `e:/UzEngine/runtime/engine/renderer/frame/frame_decals.cpp` — gather step with UINT32_MAX placeholders

## Changelog

| Date | Change |
|---|---|
| 2026-03-31 | Initial creation after research of UzEngine material system, UE5 DecalRenderingCommon, and Falcor MaterialType |
