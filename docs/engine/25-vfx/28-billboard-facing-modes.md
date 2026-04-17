# VFX billboard facing modes

Seven facing modes on the billboard particle vertex shader. The author sets
`facingMode` on the emitter; the shader dispatches into the matching basis-
vector helper to produce the `(right, up)` axes that expand each particle
into a quad.

Reference: UE5 `ENiagaraBillboardType`.

## Modes

| ID | Name              | Long axis behaviour                                      |
|----|-------------------|----------------------------------------------------------|
| 0  | FaceCamera        | Full-spherical — always faces the view direction.        |
| 1  | VelocityAligned   | Long axis follows particle velocity (streaks, sparks).   |
| 2  | FaceCameraPlane   | Rotates around world-Z; no pitch tilt.                   |
| 3  | CustomVector      | Long axis locked to `customFacingXYZ` (fixed world dir). |
| 4  | FaceCameraPosition| Faces camera EYE position (parallax-correct at wide FOV).|
| 5  | PlaneAxis         | Rotates within the plane whose normal is `customFacingXYZ`. |
| 6  | WorldZAligned     | Long axis locked to world Z; short axis horizontal.      |

Modes 0–4 were shipped in prior phases. **Modes 5 and 6 were added in
phase 8.11.0.**

## Mode 5 — PlaneAxis

```
planeNormal = normalize(customFacingXYZ)   (defaults to (0,0,1))
toCamera    = cameraPos - particlePos
inPlane     = toCamera - (toCamera · n) · n
right       = normalize(n × inPlane)
up          = right × n            (lies in the plane)
```

Typical uses:

- `customFacingXYZ = (0,0,1)` — ground-locked sprites (footprints, puddles).
- `customFacingXYZ = (0,1,0)` — wall-facing decals.
- Any unit normal — foliage / banner constrained to a surface.

When the camera lies on the plane normal axis (inPlane is zero), the shader
falls back to a stable in-plane axis derived from world-X (or world-Y if
degenerate).

## Mode 6 — WorldZAligned

```
up   = (0,0,1)
horiz = normalize((camera.xy - particle.xy))    (fallback (1,0) overhead)
right = (-horiz.y, horiz.x, 0)
```

Long axis always world up. Short axis lies in the horizontal plane and
faces the camera. Cheaper than PlaneAxis because there is no dynamic
normal projection.

Typical uses: rain streaks, laser beams, flares, vertical energy pillars.

## CB fields

Both new modes reuse the existing `customFacingX/Y/Z` fields (mode 5 as
plane normal, mode 6 ignores them). No new CB fields — the CPU only has to
set `facingMode` and, for mode 5, the plane normal.

## Invariants

Every mode guarantees:

1. `right` and `up` are unit-length (tests enforce within 1e-4).
2. `right ⊥ up` (tests enforce within 1e-5).
3. Degenerate camera placements fall back to a stable axis so the sprite
   never collapses to zero area.

## Tests

`runtime/tests/engine/vfx/test_billboard_facing_modes.cpp` mirrors the
HLSL math in C++ and covers: mode-5 right ⊥ planeNormal, up in plane,
right ⊥ up, fallback on overhead camera; mode-6 up = (0,0,1), right in
horizontal plane, right ⊥ horizontal camera direction, right ⊥ up,
overhead-camera X fallback, unit-length invariant across both modes.
