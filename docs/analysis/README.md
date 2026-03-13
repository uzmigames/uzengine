# Godot 4 Architecture Analysis — POM/Heightmap Displacement

**Analysis Date**: 2026-03-13
**Godot Source**: `E:\godot` (4.x branch)
**UzEngine Target**: Parallax Occlusion Mapping (POM) feature for PBR renderer

---

## Documents in This Analysis

This folder contains three comprehensive documents analyzing Godot 4's parallax occlusion mapping (heightmap displacement) implementation:

### 1. godot-pom-heightmap-analysis.md (Main Reference)

**Audience**: Technical leads, render engineers
**Key Content**:
- Executive summary of Godot's two-tier algorithm (Deep Parallax vs. Simple Parallax)
- Adaptive layer calculation based on view angle
- Material properties and shader integration
- Comparison with UE5 and UzEngine
- Performance considerations
- Implementation roadmap

**Key Findings**:
- Linear raymarching (not binary search) for good performance/quality trade-off
- Adaptive layers via grazing angle fade eliminate need for explicit distance LOD
- No self-shadowing in Godot's POM — handled separately via AO texture
- C++ string codegen approach is elegant for standardized pipelines

### 2. godot-pom-hlsl-reference.md (Implementation Guide)

**Audience**: Shader engineers, implementation team
**Key Content**:
- Full production-quality HLSL implementations of both algorithms
- Material constant buffer structure and bitfield packing
- TBN space transformation with MikktSpace flip corrections
- Complete fragment shader integration example
- Performance baseline and known issues
- Test vectors for unit and visual regression testing

**Ready-to-Use**: Code can be directly adapted into UzEngine's pbr_pass.hlsl.

### 3. README.md (This File)

Navigation and quick-start guide for the analysis corpus.

---

## Quick Start by Role

### Render Engineers (Implementation)
1. Read godot-pom-hlsl-reference.md Sections 3-4 (algorithms)
2. Review Section 6 (shader integration)
3. Implement in `runtime/engine/renderer/passes/pbr_pass.hlsl`

### Material/Tools Programmers
1. Read godot-pom-heightmap-analysis.md Section 1 (properties)
2. Review godot-pom-hlsl-reference.md Section 8 (constant buffers)
3. Define MaterialParams struct and expose inspector UI

### Architects
1. Read godot-pom-heightmap-analysis.md Sections 1-2, 10-14
2. Review Section 12 (UE5 comparison)
3. Make decision: Godot's linear refinement vs. UE5's binary refinement

### Asset Pipeline
1. Read godot-pom-heightmap-analysis.md Sections 5-7
2. Document heightmap format and conventions
3. Implement validation in asset importer

---

## Key Architectural Decisions

### Algorithm Choice
- **Godot**: Linear search + 1-step linear interpolation refinement
- **UE5**: Linear search + N-step binary refinement
- **Recommendation**: Godot approach (8-32 texture lookups vs. 16-64)

### Self-Shadowing
- **Godot**: None (separate AO texture)
- **UE5**: Real-time via light-space raycasts (expensive)
- **Recommendation**: Godot approach (aligns with UzEngine's existing contact shadows)

### Shader Implementation
- **Godot**: C++ string codegen
- **UzEngine current**: Shader variants + DXC defines
- **Recommendation**: Stick with UzEngine's existing variant system

---

## Integration Checklist

- [ ] Add heightmap properties to MaterialParams struct
- [ ] Define HeightmapConstants CB and allocate register
- [ ] Create `shaders/include/parallax.hlsli` with algorithm implementations
- [ ] Modify pbr_pass.hlsl to call parallax before texture sampling
- [ ] Expose UI: heightmap_scale slider, min/max layers, flip checkboxes
- [ ] Write unit tests (layer adaptation, inversion, flips)
- [ ] Capture visual regression baseline from Godot
- [ ] Profile on target GPUs; verify < 5% frame time impact
- [ ] Document heightmap authoring guide

---

## Performance Targets

| Configuration | Est. Cost | Target Platform |
|---|---|---|
| Deep Parallax, 32 layers | 1.0–1.2 ms @ 1440p | High-end (RTX 4080) |
| Deep Parallax, 16 layers | 0.5–0.7 ms @ 1440p | Mid-range (RTX 3070) |
| Simple Parallax, 1 layer | 0.04 ms @ 1440p | Mobile/low-end |

---

## Source References

### Godot 4 Source
- Material properties: `/e/godot/scene/resources/material.h` (lines 384–703)
- POM algorithm: `/e/godot/scene/resources/material.cpp` (lines 1544–1614)
- Property setters: `/e/godot/scene/resources/material.cpp` (lines 2863–2905)

### UzEngine Integration Points
- Material system: `/e/UzEngine/runtime/engine/renderer/core/material.h`
- PBR pass: `/e/UzEngine/runtime/engine/renderer/passes/pbr_pass.cpp`
- Constant buffers: `/e/UzEngine/runtime/engine/renderer/core/constant_buffers.h`

---

## Implementation Effort

**Estimated**: 2-3 days

- Core HLSL functions: 0.5 day
- Shader integration: 0.5 day
- Material UI: 0.5 day
- Testing: 1 day

---

## Summary

Godot 4's POM implementation is production-ready and suitable for direct HLSL adaptation. The linear raymarching approach offers excellent performance/quality balance. The adaptive layer system elegantly handles LOD without explicit distance-based tuning. Both detailed analysis and ready-to-use HLSL code are provided in the accompanying documents.
