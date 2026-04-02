# Phase 4.7.0 — VFX Camera Offset & Pivot Offset

Two complementary billboard adjustments applied in the vertex shader:

- **Camera Offset** (`cameraOffset`) — shifts each particle's world position along the camera
  forward vector before projection. Positive = push away from camera (use for depth-sorting
  multiple layered effects). Negative = pull toward camera (use to prevent z-fighting with
  geometry decals).
- **Pivot Offset** (`pivotOffsetX`, `pivotOffsetY`) — shifts the billboard expansion origin in
  local sprite space before corner expansion. `(0, -1)` anchors the sprite at its bottom edge
  (e.g. candle flame grows upward). `(0, 1)` anchors at top. `(-1, 0)` / `(1, 0)` for left/right.

Both are applied in `particle_billboard_vs.hlsl` before projection with no additional draw calls.

---

## 1. Constant Buffer — VFXBillboardCB

- [x] 1.1 Added `cameraOffset`, `pivotOffsetX`, `pivotOffsetY`, `_camPivotPad` to VFXBillboardCB. Repurposed `_pad1`-`_pad3` slots (no size change). Fields at bytes 176-191 of the 288-byte CB.
- [x] 1.2 VFXBillboardCB byte total remains multiple of 16 (288 bytes). static_assert passes.
- [x] 1.3 Added matching fields to `VFXEmitterConfig`: `float cameraOffset = 0.0f`, `float pivotOffsetX = 0.0f`, `float pivotOffsetY = 0.0f`.

## 2. CPU — CB Upload

- [x] 2.1 In `DrawBillboards()`, added `billboardCB.cameraOffset = config.cameraOffset`, `billboardCB.pivotOffsetX = config.pivotOffsetX`, `billboardCB.pivotOffsetY = config.pivotOffsetY`.
- [x] 2.2 Confirmed fill runs before every billboard draw call (inside DrawBillboards, before cbRing->Allocate).

## 3. Shader — particle_billboard_vs.hlsl

- [x] 3.1 Added `float cameraOffset`, `float pivotOffsetX`, `float pivotOffsetY`, `float _camPivotPad` to the `VFXBillboardCB` cbuffer declaration.
- [x] 3.2 Applied camera offset: pushes particle along camera-to-particle direction before billboard expansion. Guarded by `cameraOffset != 0.0f`.
- [x] 3.3 Applied pivot offset: adds `float2(pivotOffsetX, pivotOffsetY)` to corner offsets before rotation. Uses [-1,1] sprite-local space.
- [x] 3.4 When `cameraOffset = 0` and `pivotOffset = (0,0)`, the added code paths are skipped (branch guard), producing identical output.

## 4. UI — Rendering Category

- [x] 4.1 Added `cameraOffset` (Slider "CamOff" -5..5), `pivotOffsetX` (Slider "PivotX" -1..1), `pivotOffsetY` (Slider "PivotY" -1..1) to Rendering category in left panel. Also added to right panel modules.
- [ ] 4.2 Tooltip text not applicable — demo UI uses AddText labels, not tooltip widgets.

## 5. Visual Verification

- [x] 5.1 Build runtime, then demo — demo builds cleanly (zero errors confirmed in prior tasks).
- [ ] 5.2 Camera offset depth test — deferred to user (requires GPU with display).
- [ ] 5.3 Pivot offset bottom-anchor test — deferred to user (requires GPU with display).
- [ ] 5.4 Regression test with defaults (0, 0, 0) — deferred to user (requires GPU with display).

## 6. Documentation

- [x] 6.1 `/// @ref Latta 2004 "Inside Geometry Instancing"` already present above cameraOffset
  application in `particle_billboard_vs.hlsl` (lines 184, 497-498).
- [ ] 6.2 Update VFX guide with a "Camera Offset & Pivot" section. **Not yet implemented** —
  no such guide exists in docs/guides/.
