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

- [ ] 1.1 Add `uint facingMode` to `VFXBillboardCB` struct in `runtime/engine/vfx/vfx_viewport_vfx.h`. Default `0` (FaceCamera). Comment: `// 0=FaceCamera 1=VelocityAligned 2=FaceCameraPlane 3=CustomVector 4=FaceCameraPosition`.
- [ ] 1.2 Add `float customFacingX`, `float customFacingY`, `float customFacingZ` to `VFXBillboardCB` for mode 3. Pack together after `facingMode` to keep 16-byte alignment (`uint + float3 = 16 bytes`). Default `(0, 0, 1)` (world Z-up).
- [ ] 1.3 Verify `VFXBillboardCB` total size remains a multiple of 16. Update byte-size comment.
- [ ] 1.4 Add `uint facingMode`, `float customFacingX/Y/Z` to `VFXEmitterConfig` with the same defaults.

## 2. CPU — CB Upload

- [ ] 2.1 In the function that fills `VFXBillboardCB` from `VFXEmitterConfig`, add:
  ```cpp
  cb.facingMode     = config.facingMode;
  cb.customFacingX  = config.customFacingX;
  cb.customFacingY  = config.customFacingY;
  cb.customFacingZ  = config.customFacingZ;
  ```
- [ ] 2.2 Confirm upload occurs before the billboard draw call, not before the update dispatch.

## 3. Shader — particle_billboard_vs.hlsl

- [ ] 3.1 Add `uint facingMode`, `float3 customFacingDir` (reconstructed from `customFacingX/Y/Z`) to the `VFXBillboardCB` cbuffer declaration.
- [ ] 3.2 Extract existing FaceCamera basis construction into a helper `float3x3 BasisFaceCamera(float3 camForward, float3 camUp)`.
- [ ] 3.3 Extract existing VelocityAligned basis into `float3x3 BasisVelocityAligned(float3 vel, float3 camForward)`.
- [ ] 3.4 Implement `float3x3 BasisFaceCameraPlane(float3 camPos, float3 particlePos)`:
  ```hlsl
  // Faces toward camera position projected onto XZ plane — Y axis always world up.
  // @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
  float3 toCamera = normalize(camPos - particlePos);
  toCamera.y = 0.0f;
  toCamera = normalize(toCamera);
  float3 right = cross(float3(0,1,0), toCamera); // right in world XZ
  float3 up    = float3(0, 1, 0);
  ```
- [ ] 3.5 Implement `float3x3 BasisCustomVector(float3 customDir, float3 camForward)`:
  ```hlsl
  // Long axis = customDir, right = cross(customDir, camForward), then re-orthogonalize.
  float3 up    = normalize(customDir);
  float3 right = normalize(cross(up, camForward));
  float3 fwd   = cross(right, up);
  ```
- [ ] 3.6 Implement `float3x3 BasisFaceCameraPosition(float3 camPos, float3 particlePos, float3 worldUp)`:
  ```hlsl
  // Faces particle center toward camera POSITION (not direction), giving correct parallax.
  // @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
  float3 toCamera = normalize(camPos - particlePos);
  float3 right    = normalize(cross(worldUp, toCamera));
  float3 up       = cross(toCamera, right);
  ```
- [ ] 3.7 Add a switch on `facingMode` in `main()` that selects the correct basis and applies it to corner offsets:
  ```hlsl
  float3x3 basis;
  switch (facingMode)
  {
      case 1:  basis = BasisVelocityAligned(vel, camForward); break;
      case 2:  basis = BasisFaceCameraPlane(camPos, worldPos); break;
      case 3:  basis = BasisCustomVector(normalize(customFacingDir), camForward); break;
      case 4:  basis = BasisFaceCameraPosition(camPos, worldPos, float3(0,0,1)); break;
      default: basis = BasisFaceCamera(camForward, camUp); break;
  }
  ```
- [ ] 3.8 Ensure modes 0 and 1 produce byte-identical results to the pre-task shader (regression).

## 4. UI — Rendering Category

- [ ] 4.1 In the VFX inspector UI (search for `"Rendering"` ImGui category or where blend mode is set), add a combo dropdown:
  ```cpp
  const char* facingItems[] = { "Face Camera", "Velocity Aligned", "Face Camera Plane", "Custom Vector", "Face Camera Position" };
  ImGui::Combo("Facing Mode", (int*)&config.facingMode, facingItems, 5);
  ```
- [ ] 4.2 Show `customFacingX/Y/Z` float3 input only when `facingMode == 3`:
  ```cpp
  if (config.facingMode == 3)
      ImGui::InputFloat3("Custom Direction", &config.customFacingX);
  ```
- [ ] 4.3 Add a tooltip per mode explaining its use case.

## 5. Visual Verification

- [ ] 5.1 Build runtime then demo.
- [ ] 5.2 Capture mode 0 (FaceCamera) — particles must face camera from all angles (existing behavior, no regression).
- [ ] 5.3 Capture mode 2 (FaceCameraPlane) — upright sprites that never tilt sideways as camera orbits vertically.
- [ ] 5.4 Capture mode 3 (CustomVector, dir=Z-up) — flat horizontal discs (like rain splashes).
- [ ] 5.5 Capture mode 4 (FaceCameraPosition) — at high FOV, edge particles should visibly rotate toward camera position rather than direction.
- [ ] 5.6 Use `--screenshot-stdout --screenshot-frame 5 --frames 8` for each capture.

## 6. Documentation

- [ ] 6.1 Add `/// @ref Latta 2004 "Inside Geometry Instancing"` and `/// @ref Valve 2006 "Rendering in Left 4 Dead"` citations above the billboard basis functions.
- [ ] 6.2 Update VFX guide with a "Sprite Facing Modes" section, listing each mode, when to use it, and its visual behavior.
