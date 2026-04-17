<!-- doc-meta
title: VFX Material System Integration Spec
author: Claude Opus 4.6
date: 2026-04-02
tags: [vfx, material, architecture, spec]
-->

# VFX Material System Integration Spec

## Problem

The VFX system defines its own `VFXMaterial` struct (`engine/vfx/material/vfx_material.h`)
that is completely isolated from the engine's unified material system (`MaterialInterface`,
`Material`, `MaterialInstance`). This means:

1. VFX particles cannot reuse PBR materials created for meshes
2. No material instancing — variations require full VFXMaterial duplication
3. No material graph support — VFX shading is hardcoded in the billboard PS
4. No hot-reload — changing a material doesn't affect particles
5. Artists must configure VFX materials separately from mesh materials

## Solution: VFXMaterial inherits MaterialInterface

Make `VFXMaterial` a subclass of `MaterialInterface`, gaining the full parameter
system, texture slot management, and material instance support for free.

### Architecture

```
MaterialInterface (engine/renderer/material/material_interface.h)
├── Material            — shared PBR asset (albedo, normal, roughness, etc.)
├── MaterialInstance    — per-object overrides (sparse, parent chain)
└── VFXMaterial (NEW)   — particle-specific extensions + MaterialInterface base
    ├── Inherits: blend mode, textures, shading model, domain
    ├── Adds: flipbook params, soft fade, emissive, color ramp, lit mode
    └── VFXMaterialInstance (NEW) — per-emitter overrides
```

### VFXMaterial Changes

```cpp
// BEFORE: isolated struct
struct VFXMaterial {
    std::string spriteTexturePath;
    uint32_t spriteTextureSrvIndex;
    uint32_t blendMode;
    float emissiveIntensity;
    // ... flat fields ...
};

// AFTER: inherits MaterialInterface
class VFXMaterial : public MaterialInterface {
public:
    // ── MaterialInterface overrides ──
    float GetScalarParam(const std::string& name, float def) const override;
    void GetVectorParam(const std::string& name, ...) const override;
    const std::string& GetTexturePath(const std::string& name) const override;
    uint32_t GetTextureSrvIndex(const std::string& name) const override;
    EBlendMode GetBlendMode() const override;
    EMaterialDomain GetMaterialDomain() const override { return EMaterialDomain::Particle; }
    EShadingModel GetShadingModel() const override;

    // ── VFX-specific parameters (not in MaterialInterface) ──
    uint32_t flipbookRows = 1;
    uint32_t flipbookCols = 1;
    uint32_t flipbookTotalFrames = 1;
    float    flipbookFPS = 0.0f;
    float    softFadeDistance = 100.0f;  // cm
    float    alphaErosion = 0.0f;
    uint32_t colorRampLutSrvIndex = UINT32_MAX;

    // ── Texture loading ──
    bool LoadTexture(IRHIDevice* device, IRHICommandList* cmdList, bool isSRGB = true);
};
```

### EMaterialDomain Extension

Add `Particle` to the existing enum:

```cpp
enum class EMaterialDomain : uint8_t {
    Surface,      // Standard mesh geometry (G-buffer / forward)
    PostProcess,  // Screen-space post-processing
    Decal,        // Deferred decal projection
    Particle,     // VFX particle billboard / mesh / trail (NEW)
    UI,           // UI elements
};
```

### Renderer Module Changes

| Module | File | Change |
|--------|------|--------|
| SpriteRenderModule | `sprite_render_module.h` | `MaterialInterface*` slot instead of `VFXMaterial*` |
| MeshRenderModule | `mesh_render_module.h` | `MaterialInterface*` slot |
| TrailSystem | `trail_system.h` | `MaterialInterface*` slot |
| ParticleEmitter | `particle_emitter.h` | `MaterialInterface*` material pointer |
| Billboard PS | `particle_billboard_ps.hlsl` | Read textures from material SRV slots |
| Trail PS | `trail_ps.hlsl` | Read textures from material SRV slots |

### Per-Emitter Material Instance

Each emitter can create a `MaterialInstance` that overrides parameters from its
base `VFXMaterial`:

```cpp
// In particle_emitter.h
MaterialInterface* material = nullptr;          // Shared base (VFXMaterial or Material)
std::unique_ptr<MaterialInstance> materialOverride; // Per-emitter overrides (optional)

// Usage in render pass:
const MaterialInterface* effectiveMaterial = emitter.materialOverride
    ? emitter.materialOverride.get()
    : emitter.material;
```

### Migration Path

1. Add `Particle` to `EMaterialDomain`
2. Make `VFXMaterial` inherit `MaterialInterface`
3. Implement the virtual getters (map VFX fields to MaterialInterface API)
4. Change renderer modules to accept `MaterialInterface*`
5. Existing VFXMaterial usage works unchanged (fields are still public)
6. New code can use MaterialInstance for per-emitter overrides

### Comparison with Industry

| Feature | Unreal Niagara | Godot GPUParticles3D | UzEngine (after) |
|---------|---------------|---------------------|-------------------|
| Material type | UMaterialInterface* | ShaderMaterial | MaterialInterface* |
| Instance overrides | MaterialInstanceDynamic | Resource instancing | MaterialInstance |
| Shared with meshes | Yes | Yes | Yes |
| Material domain | Niagara domain | N/A (shader-based) | EMaterialDomain::Particle |
| Hot-reload | Yes | Yes | Yes (via MaterialManager) |
