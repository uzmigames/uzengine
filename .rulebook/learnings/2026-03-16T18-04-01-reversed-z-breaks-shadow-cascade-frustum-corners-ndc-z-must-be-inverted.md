# Reversed-Z breaks shadow cascade frustum corners — NDC Z must be inverted
**Source**: manual
**Date**: 2026-03-16
**Related Task**: fix-reversed-z-shadow-consistency
**Tags**: reversed-z, shadow, cascade, frustum, ndc, depth-convention, bug-fix, critical
## Bug

When migrating to reversed-Z depth (near=1.0, far=0.0), the `ComputeFrustumCorners()` function in `shadow_cascade.cpp` was still using standard-Z NDC corners: z=0 for near, z=1 for far. The `invViewProj` matrix (which undoes a reversed-Z projection) unprojected these wrong corners into a massively inflated frustum — bounding sphere radius ~114,000cm instead of ~1,274cm.

## Symptom

Shadows completely invisible. The shadow atlas was being written (geometry rendered correctly with 8 draw calls), but the ortho projection covered 228,000cm × 228,000cm for 2048×2048 texels = 111cm/texel. A 100cm sphere occupied less than 1 texel. Shadow sampling returned 1.0 (lit) everywhere because scene pixels mapped to the cleared region of the atlas.

## Root Cause

```cpp
// WRONG (standard-Z):
{-1, -1, 0}, // near  ← z=0 is FAR in reversed-Z!
{-1, -1, 1}, // far   ← z=1 is NEAR in reversed-Z!

// CORRECT (reversed-Z):
{-1, -1, 1}, // near  ← z=1 = near plane in reversed-Z
{-1, -1, 0}, // far   ← z=0 = far plane in reversed-Z
```

## Fix

One-line change in `shadow_cascade.cpp:78-87`: swap z=0↔z=1 in the 8 NDC corner definitions.

## Diagnosis Path

1. Buffer viz grid (--shadow-debug 1) showed Shadow tile all white (no shadows)
2. Raw Atlas tile (tile 2) showed geometry as a tiny sliver — confirmed atlas had data but cascade was oversized
3. Log showed texelSize=112cm (should be ~1cm) — cascade bounding sphere was 90x too large
4. Traced to `ComputeFrustumCorners` NDC Z values not matching reversed-Z convention

## Rule

When porting ANY code that uses NDC z values (0 or 1), verify it matches the depth convention:
- Standard-Z: near=0, far=1
- Reversed-Z: near=1, far=0

This applies to: frustum corner computation, depth linearization, sky depth output, clip space comparisons, Hi-Z pyramid.