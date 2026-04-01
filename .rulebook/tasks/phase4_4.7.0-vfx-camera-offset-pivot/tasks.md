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

- [ ] 1.1 Add to `VFXBillboardCB` in `runtime/engine/vfx/vfx_viewport_vfx.h`:
  ```cpp
  float cameraOffset;   // world units along camera forward; default 0.0f
  float pivotOffsetX;   // sprite-local X shift of expansion origin; default 0.0f
  float pivotOffsetY;   // sprite-local Y shift of expansion origin; default 0.0f
  float _camPivotPad;   // padding to maintain 16-byte alignment
  ```
- [ ] 1.2 Verify `VFXBillboardCB` byte total is a multiple of 16 after addition. Update byte-size comment.
- [ ] 1.3 Add matching fields to `VFXEmitterConfig`: `float cameraOffset = 0.0f`, `float pivotOffsetX = 0.0f`, `float pivotOffsetY = 0.0f`.

## 2. CPU — CB Upload

- [ ] 2.1 In the `VFXBillboardCB` fill function, add:
  ```cpp
  cb.cameraOffset  = config.cameraOffset;
  cb.pivotOffsetX  = config.pivotOffsetX;
  cb.pivotOffsetY  = config.pivotOffsetY;
  cb._camPivotPad  = 0.0f;
  ```
- [ ] 2.2 Confirm the fill runs before every billboard draw call.

## 3. Shader — particle_billboard_vs.hlsl

- [ ] 3.1 Add `float cameraOffset`, `float pivotOffsetX`, `float pivotOffsetY`, `float _camPivotPad` to the `VFXBillboardCB` cbuffer declaration.
- [ ] 3.2 Apply camera offset to world position BEFORE billboard basis construction (so the basis is still correct relative to camera):
  ```hlsl
  // @ref Latta 2004 "Inside Geometry Instancing" — camera offset for depth layering
  float3 camForward = normalize(cameraPos - cameraTarget); // or read from CB
  worldPos += camForward * cameraOffset;
  ```
- [ ] 3.3 Apply pivot offset to the per-corner expansion offset BEFORE adding to world position. The corner offsets are in sprite-local space (right, up); pivot shifts the origin:
  ```hlsl
  // @ref Unity VFX Graph 2021 "Pivot Offset" — sprite pivot in local space
  float2 corner = cornerTable[vertexId]; // e.g. (-0.5, -0.5) to (0.5, 0.5)
  corner.x -= pivotOffsetX * 0.5f;
  corner.y -= pivotOffsetY * 0.5f;
  // then expand: worldPos += right * corner.x * size + up * corner.y * size
  ```
- [ ] 3.4 Confirm `cameraOffset = 0` and `pivotOffset = (0,0)` produce byte-identical output to the pre-task shader.

## 4. UI — Rendering Category

- [ ] 4.1 In the Rendering category of the VFX inspector, add after the existing facing mode controls:
  ```cpp
  ImGui::SeparatorText("Offsets");
  ImGui::SliderFloat("Camera Offset", &config.cameraOffset, -500.0f, 500.0f, "%.1f cm");
  ImGui::SliderFloat("Pivot X",       &config.pivotOffsetX, -1.0f,   1.0f,   "%.2f");
  ImGui::SliderFloat("Pivot Y",       &config.pivotOffsetY, -1.0f,   1.0f,   "%.2f");
  ```
- [ ] 4.2 Add tooltips:
  - Camera Offset: `"Pushes/pulls particle along camera forward before projection. Use to layer overlapping effects without z-fighting."`.
  - Pivot X/Y: `"Shifts the billboard expansion origin in sprite space. (0,-1) = bottom-anchored, (0,1) = top-anchored."`.

## 5. Visual Verification

- [ ] 5.1 Build runtime, then demo.
- [ ] 5.2 Camera offset test — set `cameraOffset = 200` on one emitter and `cameraOffset = -200` on another identical emitter; both must appear at different depths in the screenshot without geometry pop or z-fighting artifacts.
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 10 --frames 12
  ```
- [ ] 5.3 Pivot offset test — set `pivotOffsetY = -1.0` (bottom anchor); the sprite base must stay at the emitter origin while the top grows upward (flame-like).
- [ ] 5.4 Verify `cameraOffset = 0` + `pivotOffset = (0,0)` matches the pre-task reference screenshot pixel-for-pixel (or within 1 LSB float rounding).

## 6. Documentation

- [ ] 6.1 Add `/// @ref Latta 2004 "Inside Geometry Instancing"` above the cameraOffset application in the shader.
- [ ] 6.2 Update VFX guide with a "Camera Offset & Pivot" section including a diagram showing pivot anchoring at bottom/top/center and the depth-sorting use case for camera offset.
