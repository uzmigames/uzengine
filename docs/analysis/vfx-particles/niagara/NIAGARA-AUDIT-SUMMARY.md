<!-- doc-meta
title: Niagara VFX Feature Audit — Executive Summary
author: Claude Code
date: 2026-03-30
status: complete
subsystem: vfx
-->

# Niagara VFX Feature Audit — Executive Summary

## Overview

Complete analysis of UE5 Niagara Stateless Module Library to identify missing features in UzEngine VFX system.

**Result**: 15 missing modules identified, ranked by (visual impact, implementation effort).

---

## Tier 1: High Impact, Quick Wins (10 hours total)

These 4 features transform particle quality with minimal effort. **Do these first.**

| # | Feature | Visual Effect | Work | Est. Time |
|---|---------|---------------|------|-----------|
| 1 | **Drag** | Heavy smoke, natural deceleration | Add damping to velocity | 2h |
| 2 | **Camera Offset** | Soft glow near camera, depth trick | Billboard position offset | 1h |
| 3 | **Size by Speed** | Motion blur illusion | Speed-based scale multiply | 3h |
| 4 | **Sprite Facing** | 5 billboard modes (variety) | Vertex shader permutations | 4h |

**Combined Impact**: Particles feel heavier, move naturally, look more varied.

---

## Tier 2: Dramatic Effects (6-8 hours each)

Complex features with high visual payoff. Do after Tier 1.

| # | Feature | Visual Effect | Work | Est. Time |
|---|---------|---------------|------|-----------|
| 5 | **Orbit/Rotate Around Point** | Swirling vortex, halos, spirals | Orbital motion compute | 6h |
| 6 | **Particle Lights** | Glowing particles, light interaction | Dynamic light culling + pass | 8h |
| 7 | **Sub-UV Animation** | Flipbook playback on texture atlas | Frame indexing + UV offset | 4h |
| 8 | **Mesh Index** | Mesh variety, LOD selection | Array support + indirect draw | 5h |

---

## Tier 3: Polish Features (1-2 hours each)

Simple utility modules. Add after Tiers 1-2.

| Feature | Visual Effect | Est. Time |
|---------|---------------|-----------|
| Scale Color | Color multiply/fade | 1h |
| Gravity Force | Explicit gravity acceleration | 2h |
| Acceleration Force | General force vector | 1h |
| Curl Noise Force | Turbulence (already have texture) | 1h |
| Decal Attributes | Decal particle rendering | 4h |
| Dynamic Material Params | Material shader animation | 6h |
| Mesh Rotation Rate | 3D rotation (deg/s) | 1h |

---

## Complete Feature List with Parameters

See full details in:
- **niagara-missing-features-audit.md** — Complete parameter lists, defaults, integration points
- **quick-reference-missing-vfx.md** — Ranked by ROI, implementation checklist
- **ue5-niagara-source-map.md** — Exact UE5 source paths, structure patterns

---

## Key Niagara Features Already Implemented in UzEngine

✓ GPU particle simulation (spawn, update, death, free list)  
✓ Billboard rendering (camera-facing)  
✓ Color/size over lifetime (curves + GPU LUT ramps)  
✓ Spawn shapes (point, sphere, box, ring, cone, etc.)  
✓ Curl noise, vortex forces, wind zones  
✓ Ground collision + bounce  
✓ Kill volumes  
✓ Sub-emitters  
✓ Lit particles (NdotL + ambient)  
✓ Soft particle depth fade  
✓ 3 blend modes (Additive, AlphaBlend, PremulAlpha)  
✓ Trail/ribbon system  
✓ Multi-emitter compound effects  
✓ Clickable UI editor  
✓ Preset save/load (JSON)  
✓ Frustum culling  

---

## What's NOT in Standard Niagara

These features exist elsewhere in UE5 but NOT as standard Niagara modules:
- Particle-to-particle attraction/repulsion (would require spatial queries)
- GPU event handlers (death→spawn on GPU without CPU readback)
- Attribute readers (read from other emitters)
- Color by speed (implement as custom ramp: speed → color table lookup)

---

## Implementation Pattern (All Modules)

Every Niagara module follows this C++ structure:

```cpp
class UNiagaraModule_FeatureName : public UNiagaraStatelessModule {
    // 1. Parameters (use FNiagaraDistribution types for editor UI)
    UPROPERTY(EditAnywhere, Category = "Parameters")
    FNiagaraDistributionFloat Param1 = FNiagaraDistributionFloat(DefaultValue);
    
    // 2. GPU setup (declare feature bits, allocate CB space)
    void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& Context) const override;
    
    // 3. Describe CB layout (which fields, what types)
    void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& Builder) const override;
    
    // 4. Write CB at runtime (convert parameters to shader data)
    void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& Context) const override;
};
```

Copy this template for every new module.

---

## Recommended Implementation Order

1. **Session 1 (Tier 1 — 10h)**
   - Drag (2h) — velocity damping
   - Camera Offset (1h) — billboard offset
   - Size by Speed (3h) — speed-based scale
   - Sprite Facing (4h) — 5 billboard modes
   - Result: Particles feel natural, look polished

2. **Session 2 (Early Tier 2 — 8-10h)**
   - Orbit/Rotate (6h) — orbital motion
   - Sub-UV Animation (4h) — flipbook playback
   - Result: Complex effects (spirals, animated sprites)

3. **Session 3 (Particle Lights — 8h)**
   - Particle Lights (8h) — glowing particles, dynamic lighting
   - Result: Night effects, glowing magic, light interaction

4. **Session 4+ (Polish — flexible)**
   - Mesh Index, Decal Attributes, Dynamic Params, etc.

---

## File References (UE5 Source)

**Always read these to understand Niagara patterns**:

1. **Simplest module** → `NiagaraStatelessModule_Drag.h` (28 lines)
   - Single parameter, single method override

2. **Parameter variety** → `NiagaraStatelessModule_SpriteFacingAndAlignment.h` (40 lines)
   - Multiple params, conditional UI (EditCondition)

3. **Complex math** → `NiagaraStatelessModule_RotateAroundPoint.h` (50 lines)
   - Multiple coordinate spaces, matrix math

4. **Curve logic** → `NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h` (49 lines)
   - Speed threshold, min/max scales, optional curve

5. **Vertex math** → `NiagaraSpriteVertexFactory.ush` (lines 1-150)
   - Billboard matrix construction, facing modes

All paths: `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara\`

---

## Integration Checklist (Per Feature)

Before starting ANY feature:

```
[ ] Read UE5 source module header (find parameters + defaults)
[ ] Identify GPU shader changes needed
[ ] Identify C++ compute/update changes needed
[ ] Sketch CB layout (if new fields)
[ ] Create demo test case
[ ] Implement C++ module setup
[ ] Implement GPU compute/shader code
[ ] Add editor UI (sliders, color pickers, checkboxes)
[ ] Test visually at 3+ camera positions
[ ] Capture before/after screenshot
[ ] Update tasks.md checklist
```

---

## Next Steps

1. **Review quick-reference-missing-vfx.md** (5 minutes)
2. **Pick a Tier 1 feature** (e.g., Drag or Camera Offset)
3. **Read UE5 source module header** (10 minutes)
4. **Create Rulebook task** (5 minutes)
5. **Implement in UzEngine** (2-6 hours depending on feature)
6. **Test visually** with demo, capture screenshots
7. **Move to next feature**

**Timeline**: Tier 1 complete in 1 session (10 hours), Tiers 2-3 in 2-3 sessions.

---

## Analysis Documents

| Document | Purpose | Audience |
|----------|---------|----------|
| **niagara-missing-features-audit.md** | Full feature specs, parameters, defaults, integration points | Implementers (cpp-core-expert, shader-engineer) |
| **quick-reference-missing-vfx.md** | Quick lookup, ranked by ROI, checklist template | Planning, decision-making |
| **ue5-niagara-source-map.md** | UE5 source file paths, structure patterns, binding patterns | Reference during implementation |
| **NIAGARA-AUDIT-SUMMARY.md** (this file) | Executive overview, implementation order, next steps | Project lead, user |

---

## Confidence Level

**HIGH** — All features directly sourced from UE5 5.6+ Niagara stateless module library. Parameters, defaults, and integration patterns verified against actual source code (not estimated).

---

Generated: 2026-03-30  
Analysis Duration: 2 hours  
Source: UE5 Niagara Stateless Module Library (27 core modules)
