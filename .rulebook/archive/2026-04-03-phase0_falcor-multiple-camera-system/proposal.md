# Proposal: phase0_falcor-multiple-camera-system

## Why
UzEngine supports exactly one camera, hardcoded into `PerFrameCB`. Falcor exposes `getCameras()` returning a vector of viewpoints that can be switched at runtime. A single-camera constraint blocks: security camera feeds rendered to texture, picture-in-picture overlays, split-screen multiplayer, cinematic cut-scene cameras, and the editor's free-cam debug view. The feature is also a prerequisite for reflection capture cameras and shadow-casting spot light cameras rendered off-screen.

## What Changes
- New `CameraManager` class: owns a `std::vector<Camera>` with named IDs, active camera index, push/pop stack for temporary overrides (e.g., editor free-cam)
- Camera ID system: each camera gets a stable `CameraId` (uint32 handle), valid for the lifetime of the scene
- `SetActiveCamera(CameraId)` and `GetActiveCamera() → Camera&` APIs; `PerFrameCB` is always populated from the active camera
- Per-camera render target option: each `Camera` may optionally own an `RhiTextureHandle` as its render target (for RTT cameras); if null, renders to the swapchain
- `scene_render_frame.cpp` updated to iterate render targets per camera when RTT cameras are present
- Editor free-cam: pressing F5 pushes a debug fly camera onto the stack; F5 again pops it, restoring the scene camera
- Lua API: `uz.camera.create(name)`, `uz.camera.switch(id)`, `uz.camera.list()`, `uz.camera.get_active()`, `uz.camera.destroy(id)`
- Affected files: new `camera_manager.h/.cpp`, `per_frame_params.cpp`, `scene_render_frame.cpp`, `application.cpp`, `scripting/api/camera_api.cpp`

## Impact
- Affected specs: `docs/specs/camera-system.md`, `docs/specs/lua-scripting.md`
- Affected code: `per_frame_params.cpp`, `scene_render_frame.cpp`, `application.cpp`, camera Lua binding
- Breaking change: NO — default single-camera path is unchanged; `CameraManager` starts with one camera at index 0
- User benefit: Security cameras, split-screen, cut-scene cameras, editor free-cam, reflection capture cameras all become possible; Lua scripts can dynamically create and switch cameras
