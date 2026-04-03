## Session 1: CameraManager Core + PerFrameCB Integration

- [x] 1.1 Created `camera_manager.h` — `CameraId` (uint32_t), `CameraManager` class with `std::vector<CameraEntry>`, active index, push/pop stack, `CreateCamera/DestroyCamera/SetActiveCamera/GetActiveCamera/GetCamera/PushCamera/PopCamera`.
- [x] 1.2 Implemented `camera_manager.cpp` — `CreateCamera` assigns sequential IDs, `SetActiveCamera` with bounds check + logging, `PushCamera/PopCamera` with stack. Constructor creates default "Main" camera (id=0).
- [x] 1.3 Replaced `std::unique_ptr<Camera> m_camera` with `std::unique_ptr<CameraManager> m_cameraManager` in SceneRenderer. All 21 `m_camera` references across 10 frame files updated to `m_cameraManager->GetActiveCamera()`. `GetCamera()` delegates to active camera. `GetCameraManager()` exposed publicly.
- [x] 1.4 CameraManager owned by SceneRenderer (not Application). Initialized in `SceneRenderer::Init()`, destroyed in `Shutdown()`. Application accesses via `GetRenderer()->GetCameraManager()`.
- [x] 1.5 Build clean (zig build), 0 errors, 205 FPS on pbr_showcase. Existing rendering 100% unchanged — drop-in replacement.

## Session 2: Editor Free-Cam + Lua API + Demo

- [ ] 2.1 Editor free-cam: F5 keypress calls `CameraManager::PushCamera` with a newly created fly-cam; second F5 calls `PopCamera`. Fly-cam uses WASD+mouse delta input independent of scene camera.
- [ ] 2.2 Per-camera render target: add `RhiTextureHandle renderTarget` (nullable) to `Camera`; update `scene_render_frame.cpp` to render RTT cameras to their target before swapchain cameras
- [ ] 2.3 Lua bindings in `scripting/api/camera_api.cpp` — `uz.camera.create(name) → id`, `uz.camera.switch(id)`, `uz.camera.list() → table`, `uz.camera.get_active() → id`, `uz.camera.destroy(id)`; register in `LuaVM::Init()`
- [ ] 2.4 Demo: extend an existing demo with a Lua script that creates a second camera, switches between them every 3 seconds, verifies both viewpoints render correctly
- [ ] 2.5 Build + verify: demo runs, camera switches visible, `--screenshot-stdout` captures correct active camera view
