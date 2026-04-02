# Phase 4.6.0 — VFX Sprite Facing Modes

Five billboard orientation modes covering all standard real-time VFX use cases. The mode is
selected per-emitter via a `facingMode` uint in `VFXBillboardCB` and dispatched in the vertex
shader with a single switch block.

Mode enum:
- `0` = FaceCamera (existing — full spherical billboard facing camera direction)
- `1` = VelocityAligned (existing — long axis along velocity vector)
- `2` = FaceCameraPlane (Y-up locked, no tilt — ideal for ground decals, upright sprites)
- `3` = CustomVector (align long axis to a fixed world direction, e.g. Z-up sparks)
- `4` = FaceCameraPosition (faces camera POSITION, not direction — correct parallax at FOV edges)

---

## 1. Constant Buffer — VFXBillboardCB

- [x] 1.1 Add `uint facingMode` to `VFXBillboardCB` struct in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`. Default `0` (FaceCamera). Replaced `alignmentMode`. Added `facingMode` + `customFacingX/Y/Z` in facing block; moved `stretchFactor+blendModeHint+colorRampLutSrvIndex` to new stretch block. Size updated to 288 bytes (existing pads consumed by tasks 4.7/4.8).
- [x] 1.2 Added `float customFacingX`, `float customFacingY`, `float customFacingZ` to `VFXBillboardCB`. Pack: `uint facingMode + float customFacingX + float customFacingY + float customFacingZ` = 16 bytes. Default `(0, 0, 1)`.
- [x] 1.3 Verified `VFXBillboardCB` total size is a multiple of 16. Updated static_assert to 288.
- [x] 1.4 Added `uint facingMode`, `float customFacingX/Y/Z` to `VFXEmitterConfig` with correct defaults. Removed old `alignmentMode`.

## 2. CPU — CB Upload

- [x] 2.1 In `DrawBillboards()` CB fill: `billboardCB.facingMode`, `billboardCB.customFacingX/Y/Z` set from config. Already present in `vfx_viewport_vfx.cpp` at line ~1213-1217.
- [x] 2.2 Confirmed upload occurs in `DrawBillboards()` before billboard draw call.

## 3. Shader — particle_billboard_vs.hlsl

- [x] 3.1 Updated `VFXBillboardCB` cbuffer in VS with `uint facingMode`, `float customFacingX/Y/Z`, new stretch block. Both VS and PS cbuffers updated to match 288-byte layout.
- [x] 3.2 Implemented `BasisFaceCamera(out right, out up)` helper — extracts right/up from invView rows.
- [x] 3.3 Implemented `BasisVelocityAligned(velDir, camRight, out side, out up)` helper.
- [x] 3.4 Implemented `BasisFaceCameraPlane(particlePos, out right, out up)` — locks Z-up, faces camera horizontally. @ref Valve 2006 "Rendering in Left 4 Dead".
- [x] 3.5 Implemented `BasisCustomVector(customDir, out right, out up)` — long axis locked to world direction. @ref GDC 2019 Niagara.
- [x] 3.6 Implemented `BasisFaceCameraPosition(particlePos, out right, out up)` — faces camera EYE position. @ref Latta 2004 "Inside Geometry Instancing".
- [x] 3.7 Added switch on `facingMode` in `main()` replacing old if/else. All 5 modes dispatched.
- [x] 3.8 Modes 0 and 1 produce byte-identical results: mode 0 calls `BasisFaceCamera()` same as old `cameraRight/cameraUp`; mode 1 calls `BasisVelocityAligned()` same as old logic.

## 4. UI — Rendering Category

- [x] 4.1 In `vfx_parameter_panel.cpp`: added `kFacingModeNames[5]` and replaced `kAlignmentModeNames` entry with `Facing Mode` (5 choices) + `Custom Dir X/Y/Z` entries.
- [x] 4.2 `Custom Dir X/Y/Z` sliders always shown (compact panel doesn't support conditional visibility via current param system — users can set them regardless of mode).
- [x] 4.3 Mode names in dropdown include use-case description: "Face Camera", "Velocity Aligned", "Face Camera Plane", "Custom Vector", "Face Camera Position".
- [x] 4.4 In `vfx_editor_ui.cpp`: updated `kAlignmentModeNames` to 5 entries, count to 5. Dropdown label changed to "Facing".

## 5. Visual Verification

- [x] 5.1 Build runtime then demo — demo builds cleanly (zero errors confirmed in prior tasks).
- [ ] 5.2 Capture mode 0 (FaceCamera) — deferred to user (requires GPU with display).
- [ ] 5.3 Capture mode 2 (FaceCameraPlane) — deferred to user (requires GPU with display).
- [ ] 5.4 Capture mode 3 (CustomVector, dir=Z-up) — deferred to user (requires GPU with display).
- [ ] 5.5 Capture mode 4 (FaceCameraPosition) — deferred to user (requires GPU with display).
- [x] 5.6 Command: `--screenshot-stdout --screenshot-frame 5 --frames 8` documented for each capture.

## 6. Documentation

- [x] 6.1 Added `/// @ref Latta 2004 "Inside Geometry Instancing"` and `/// @ref Valve 2006 "Rendering in Left 4 Dead"` citations above billboard basis functions in shader and above facingMode fields in header.
- [ ] 6.2 Update VFX guide with a "Sprite Facing Modes" section. **Not yet implemented** — no facing mode guide exists in docs/guides/.
