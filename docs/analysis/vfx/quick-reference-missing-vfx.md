<!-- doc-meta
title: Quick Reference — Missing VFX Features
author: Claude Code
date: 2026-03-30
status: reference
subsystem: vfx
-->

# Quick Reference: Top 15 Missing Niagara Features

## Ranked by (Visual Impact, Ease of Implementation)

### Priority 0: Do First (1-2 hours, huge impact)

| Feature | Visual Effect | Params | Est. Time |
|---------|---------------|--------|-----------|
| **Drag** | Slow, heavy smoke/dust | `dragCoeff` (1.0) | 2h |
| **Camera Offset** | Soft glow near camera | `offsetDistance` (0) | 1h |
| **Scale Color** | Color multiply/fade | `scaleColor` (white) | 1h |

---

### Priority 1: Next Batch (3-6 hours, high payoff)

| Feature | Visual Effect | Key Params | Est. Time |
|---------|---------------|-----------|-----------|
| **Size by Speed** | Motion blur illusion | threshold, min/max scale | 3h |
| **Sprite Facing** | 5 billboard modes | facingMode (0-4), alignment | 4h |
| **Gravity Force** | Explicit gravity | gravityMagnitude | 2h |

---

### Priority 2: Dramatic Effects (6+ hours, feature-complete)

| Feature | Visual Effect | Key Params | Est. Time |
|---------|---------------|-----------|-----------|
| **Orbit** | Swirling vortex, halos | rate (deg/s), radius, center | 6h |
| **Sub-UV Animation** | Flipbook playback | numFrames, mode (loop/linear/random) | 4h |
| **Mesh Index** | Mesh variety | meshIndex (int), weights | 5h |
| **Particle Lights** | Glowing particles, light | radius, falloff, intensity | 8h |

---

### Priority 3: Polish (2-4 hours each)

| Feature | Visual Effect | Params |
|---------|---------------|--------|
| **Acceleration Force** | Constant push | force (vec3) |
| **Curl Noise Force** | Turbulence | scale, strength (already have texture) |
| **Decal Attributes** | Decal particles | orientation, size, fade |
| **Dynamic Material Params** | Material animation | per-material bindings |
| **Mesh Rotation Rate** | 3D rotation | rotationRate (vec3/s) |

---

## Implementation Checklist Template

For each feature, before starting:

```
Feature: __________________
Visual Goal: __________________

☐ Read UE5 source module header
☐ Identify all UPROPERTY parameters + defaults
☐ List GPU shader changes needed
☐ List C++ compute/update changes needed
☐ Design CB layout (if new fields)
☐ Create demo test case
☐ Implement C++ module setup
☐ Implement GPU compute/shader code
☐ Add editor UI (sliders, color pickers)
☐ Test visually at 3+ camera positions
☐ Record before/after screenshot
☐ Update tasks.md
```

---

## Code Integration Points

### Compute Shader Updates
- `runtime/shaders/vfx/particle_update_cs.hlsl` — add force application
- Affects: Drag, Gravity, Acceleration, Curl Noise, Orbit

### Vertex Shader Updates
- `runtime/shaders/vfx/particle_billboard_ps.hlsl` — billboard modes
- `runtime/shaders/vfx/particle_mesh_vs.hlsl` — mesh scale, rotation
- Affects: Sprite Facing, Camera Offset, Size by Speed, Mesh Index

### Renderer Updates
- `runtime/engine/vfx/particle_renderer.h/.cpp` — add features to feature mask
- Affects: Particle Lights (new pass), Sub-UV (new binding), Decal (new renderer type)

### Editor UI
- `runtime/engine/vfx/vfx_editor.h` — add module UI forms
- Each feature needs sliders, checkboxes, color pickers in the editor panel

---

## Fastest ROI (Do These First)

1. **Drag** (2h) — changes particle "feel" immediately
2. **Camera Offset** (1h) — closeup visual softness for free
3. **Size by Speed** (3h) — motion blur effect without new geometry
4. **Sprite Facing Modes** (4h) — fixes billboard variety problem

**Combined: 10 hours, transforms particle quality**

