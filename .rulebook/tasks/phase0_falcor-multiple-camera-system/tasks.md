## Session 1: CameraManager Core + PerFrameCB Integration

- [ ] 1.1 Create `camera_manager.h` — `CameraId` type alias (uint32_t), `CameraManager` class with `std::vector<Camera>` storage, active index, push/pop stack; `CreateCamera(name) → CameraId`, `DestroyCamera(CameraId)`, `SetActiveCamera(CameraId)`, `GetActiveCamera() → Camera&`, `GetCamera(CameraId) → Camera*`, `GetAllCameras() → span<Camera>`
- [ ] 1.2 Implement `camera_manager.cpp` — `CreateCamera` assigns next available ID, `SetActiveCamera` bounds-checks and logs the switch via `UZ_LOG(Camera, Info, ...)`, `PushCamera(CameraId)` saves active onto stack, `PopCamera()` restores previous active
- [ ] 1.3 Update `per_frame_params.cpp` — replace direct `Camera` reference with `CameraManager::GetActiveCamera()` so `PerFrameCB` always reflects the currently active camera
- [ ] 1.4 Register `CameraManager` singleton in `application.cpp` — initialize with one default camera at scene load, expose via `GetCameraManager()`
- [ ] 1.5 Build + verify: `zig build` compiles cleanly, existing single-camera rendering unchanged

## Session 2: Editor Free-Cam + Lua API + Demo

- [ ] 2.1 Editor free-cam: F5 keypress calls `CameraManager::PushCamera` with a newly created fly-cam; second F5 calls `PopCamera`. Fly-cam uses WASD+mouse delta input independent of scene camera.
- [ ] 2.2 Per-camera render target: add `RhiTextureHandle renderTarget` (nullable) to `Camera`; update `scene_render_frame.cpp` to render RTT cameras to their target before swapchain cameras
- [ ] 2.3 Lua bindings in `scripting/api/camera_api.cpp` — `uz.camera.create(name) → id`, `uz.camera.switch(id)`, `uz.camera.list() → table`, `uz.camera.get_active() → id`, `uz.camera.destroy(id)`; register in `LuaVM::Init()`
- [ ] 2.4 Demo: extend an existing demo with a Lua script that creates a second camera, switches between them every 3 seconds, verifies both viewpoints render correctly
- [ ] 2.5 Build + verify: demo runs, camera switches visible, `--screenshot-stdout` captures correct active camera view
