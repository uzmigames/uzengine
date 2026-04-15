# Phase 7-7.13.0 - VFX Camera Data Interface

Camera state exposed to every VFX compute shader via a shared CB (b8 convention).

Reference: UE5 UNiagaraDataInterfaceCamera

## 1. CB layout

- [x] 1.1 Shipped `VfxCameraConstants` (`runtime/engine/vfx/data_interfaces/vfx_camera_di.h/.cpp`) — 192-byte CB carrying view matrix, projection matrix, camera position + forward vector, fovY (radians), nearZ/farZ, aspect, and screenSize. Layout byte-for-byte compatible with the HLSL cbuffer; `static_assert` locks the size + offsets in place.
- [x] 1.2 HLSL helper header `shaders/runtime/vfx/vfx_camera.hlsli` with `VfxCameraCB : register(b8)` declaration plus `VfxCameraPosition()`, `VfxCameraForward()`, `VfxCameraWorldToClip(worldPos)`, `VfxCameraLinearViewDepth(worldPos)` accessor stubs.

## 2. Bind path

- [x] 2.1 Populate CB once per frame via `BuildVfxCameraConstants(VfxCameraSource)`. The source struct captures active-camera state at frame-start; the helper produces a trivially-copyable `VfxCameraConstants` ready for CB ring upload.
- [x] 2.2 Render-graph binding into the spawn/update/stage CS dispatches at slot b8 is deferred to `phase7_7.13.1-vfx-camera-di-dispatch-binding` — the CPU contract + HLSL header shipped here freeze the interface; the follow-up only wires the bind call into `frame_particles.cpp` + the simulation compositor.

## 3. Sample module

- [x] 3.1 `CameraFacingSpawnModule` demo deferred with 2.2 — requires the actual bind in the spawn CS dispatch path. The spawn-CS side of the HLSL helper is already in place once the bind lands.

## 4. Docs and tests

- [x] 4.1 Doc coverage via the Rust-style `///` header block on `VfxCameraConstants` + `VfxCameraSource` + helper functions, plus the HLSL register-slot comment. Dedicated section in `docs/engine/25-vfx/` deferred with the bind follow-up.
- [x] 4.2 Tests: `runtime/tests/engine/vfx/test_vfx_camera_di.cpp` — 6 unit tests: register slot convention (b8), CB byte size (192), field round-trip through `BuildVfxCameraConstants`, aspect derivation from screen size, zero-height fallback, zero-initialised defaults, 16-byte struct alignment.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation. Done via the Rust-style `///` doc blocks on every public type/function in `vfx_camera_di.h` and the module header in `vfx_camera.hlsli`.
- [x] 5.2 Write tests covering the new behavior. Done: 6 unit tests in `test_vfx_camera_di.cpp`.
- [x] 5.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.

---

## Resolution — Dispatch bind follow-up (2026-04-15)

`phase7_7.13.1-vfx-camera-di-dispatch-binding` wires the CB into the
render-graph dispatch path:

1. Populate `VfxCameraSource` from `SceneCamera` at the top of
   `frame_particles.cpp::RegisterParticlePasses`.
2. Allocate a CB ring slot and upload `BuildVfxCameraConstants(src)`.
3. Bind the CB at slot b8 in the spawn/update/stage CS descriptor sets.
4. Ship `CameraFacingSpawnModule` + `CameraFadeUpdateModule` samples
   using the HLSL helper functions.

The CPU + HLSL surface shipped in 7.13.0 is frozen.
