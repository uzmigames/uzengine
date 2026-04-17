<!-- doc-meta -->
<!-- analysis-type: gap analysis -->
<!-- focus: Gizmo UE5 parity -->
<!-- target: phase2_2.70.0 improvements -->

# Gizmo UE5 Gap Analysis — UzEngine vs Unreal Engine 5

## Executive Summary

The current UzEngine gizmo is functional but visually and interactively inferior
to UE5. Key gaps: thin line-based geometry (not 3D meshes), screen-space hit
testing (not ray-geometry), flat unlit shading (no 3D depth cues), and missing
view-dependent culling.

---

## A. Geometry Gaps

| Aspect | UzEngine Current | UE5 Reference | Gap |
|--------|-----------------|---------------|-----|
| Axis stems | LineList (1px lines) | 3D cylinders (mesh) | **CRITICAL** — lines are hard to see/click |
| Cone tip | 8-segment cone | Higher-segment cone on cylinder | Moderate — cone looks blocky |
| Cone segments | 8 | 16-24 | Visual quality |
| Rotate circle | 64-segment LineList | 3D torus/thick circle | **CRITICAL** — thin line hard to click |
| Scale cubes | 0.04 half-size | Proportional box mesh | Minor |
| Center handle | 0.06 cube | Proportional cube/sphere | Minor |
| Plane handles | Flat quads | Semi-transparent filled quads | Moderate |

### Recommended Changes
1. **Replace axis stems** with 3D cylinders (12-16 segments, radius ~0.015)
2. **Increase cone segments** to 16
3. **Replace rotate circles** with thick line rendering or torus geometry
4. **Increase plane handle visibility** with semi-transparent fill + border

---

## B. Visual / Shading Gaps

| Aspect | UzEngine Current | UE5 Reference | Gap |
|--------|-----------------|---------------|-----|
| Shading | Flat unlit color | Lit with simple diffuse | **HIGH** — no 3D depth perception |
| Anti-aliasing | None (relies on MSAA) | Material-based AA | Moderate |
| Outline/contrast | None | Subtle dark outline for contrast | **HIGH** — invisible against bright scenes |
| Axis alpha (normal) | 1.0 | ~0.9 with slight transparency | Minor |
| Inactive alpha (drag) | 0.3 | Dimmed + desaturated | Minor |
| Hover effect | Yellow color swap | Color brighten + slight scale | Minor |
| 3D cue | None | Light shading on cones/cylinders | **HIGH** |

### Recommended Changes
1. **Add simple N·L diffuse shading** in pixel shader (hardcoded light dir)
2. **Add dark outline** pass or edge darkening for contrast
3. **Increase cone/cylinder segments** for smoother shading
4. **Desaturate inactive axes** during drag (not just alpha)

---

## C. Sizing / Proportions Gaps

| Aspect | UzEngine Current | UE5 Reference | Gap |
|--------|-----------------|---------------|-----|
| Target screen px | 180 | ~150-180 | OK |
| Axis length | 0.8 | ~0.75-0.85 | OK |
| Cone height | 0.2 | ~0.15-0.2 | OK |
| Cone radius | 0.06 | ~0.05-0.07 | OK |
| Cylinder radius | 0 (lines) | ~0.015 | **CRITICAL** — need cylinders |
| Circle radius | 0.85 | ~0.9-1.0 | Minor |
| Plane offset | 0.3 | ~0.25-0.35 | OK |
| Plane size | 0.15 | ~0.12-0.18 | OK |

---

## D. Hit Testing Gaps

| Aspect | UzEngine Current | UE5 Reference | Gap |
|--------|-----------------|---------------|-----|
| Method | Screen-space 2D projection | 3D ray-geometry intersection | **CRITICAL** |
| Axis hit | Point-to-segment 2D (15px) | Ray-cylinder 3D (7px + radius) | Major accuracy difference |
| Plane hit | PointInQuad2D | Ray-circle (filled disk) | Different approach |
| Rotate hit | Segment distance 2D (15px) | Ray-plane + circle distance 3D | Major |
| Center hit | Screen radius (20px) | Ray-box/sphere 3D | Minor |
| Priority | Plane > Center > Axis | Closest depth wins | Different model |
| View culling | None | Hide axis at <3° to camera | Missing feature |

### Recommended Changes
1. **Keep screen-space hit testing** for now (simpler, works well enough)
2. **Reduce threshold** from 15px to 10px (geometry is larger with cylinders)
3. **Add view-dependent culling** (hide axis when nearly parallel to view)

---

## E. Interaction Gaps

| Aspect | UzEngine Current | UE5 Reference | Gap |
|--------|-----------------|---------------|-----|
| Translate drag | Screen-space projection | 3D line-ray nearest point | Different (both work) |
| Rotate drag | Screen-space angle (atan2) | 3D plane intersection + angle | Different (both work) |
| Scale drag | Screen-space projection | 3D axis projection | Different (both work) |
| State machine | Implicit (hoveredAxis/activeAxis) | Explicit None/Hovering/Interacting | Cosmetic |

### Recommendation
Keep current interaction algorithms — they work correctly. Visual improvements
are higher priority than interaction algorithm changes.

---

## F. Priority-Ordered Implementation Plan

### Phase 1: Geometry Upgrade (HIGHEST IMPACT)
1. Replace LineList axis stems with TriangleList cylinders
2. Increase cone segments 8 → 16
3. Add cylinder geometry builder
4. Update vertex/index buffers

### Phase 2: Shading (HIGH IMPACT)
1. Add simple N·L diffuse in gizmo_ps.hlsl
2. Add per-vertex normals to GizmoVertex struct
3. Add dark outline (edge darkening or separate pass)

### Phase 3: Polish
1. View-dependent axis culling (<3° to camera)
2. Semi-transparent plane handle fill
3. Desaturated inactive axes during drag
4. Hover: slight brightness boost instead of pure yellow

---

## G. Files to Modify

| File | Changes |
|------|---------|
| `gizmo_renderer.h` | Add normal to GizmoVertex, new cylinder builder |
| `gizmo_renderer.cpp` | Rewrite geometry builders for 3D cylinders, update Draw() |
| `gizmo_vs.hlsl` | Pass normal to PS, add normal transform |
| `gizmo_ps.hlsl` | Add N·L diffuse shading, edge darkening |
| `gizmo_interaction.cpp` | Reduce hit threshold, add view culling |
| `editor_input_handler.cpp` | Wire view culling |
