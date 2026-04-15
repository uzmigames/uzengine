# Phase 7-7.13.0 - VFX Camera Data Interface

Camera state exposed to every VFX compute shader via a shared CB (b8 convention).

Reference: UE5 UNiagaraDataInterfaceCamera

## 1. CB layout

- [ ] 1.1 Define VfxCameraCB (64 bytes: cameraPos, cameraForward, view/proj matrices, fovY, nearZ, farZ, screenSize).
- [ ] 1.2 HLSL helper header with accessor functions.

## 2. Bind path

- [ ] 2.1 Populate CB once per frame in frame_particles.cpp from the active SceneCamera.
- [ ] 2.2 Bind into spawn/update/stage CS dispatches on slot b8.

## 3. Sample module

- [ ] 3.1 CameraFacingSpawnModule - biases spawn positions toward camera view direction.

## 4. Docs and tests

- [ ] 4.1 Documentation on CB layout and binding convention.
- [ ] 4.2 Tests: CB value correctness across frames, matrix round-trip.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
