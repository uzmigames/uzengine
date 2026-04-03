## Session 1: CameraManager Core + PerFrameCB Integration

- [x] 1.1 Created `camera_manager.h` — `CameraId` (uint32_t), `CameraManager` class with `std::vector<CameraEntry>`, active index, push/pop stack, `CreateCamera/DestroyCamera/SetActiveCamera/GetActiveCamera/GetCamera/PushCamera/PopCamera`.
- [x] 1.2 Implemented `camera_manager.cpp` — `CreateCamera` assigns sequential IDs, `SetActiveCamera` with bounds check + logging, `PushCamera/PopCamera` with stack. Constructor creates default "Main" camera (id=0).
- [x] 1.3 Replaced `std::unique_ptr<Camera> m_camera` with `std::unique_ptr<CameraManager> m_cameraManager` in SceneRenderer. All 21 `m_camera` references across 10 frame files updated to `m_cameraManager->GetActiveCamera()`. `GetCamera()` delegates to active camera. `GetCameraManager()` exposed publicly.
- [x] 1.4 CameraManager owned by SceneRenderer (not Application). Initialized in `SceneRenderer::Init()`, destroyed in `Shutdown()`. Application accesses via `GetRenderer()->GetCameraManager()`.
- [x] 1.5 Build clean (zig build), 0 errors, 205 FPS on pbr_showcase. Existing rendering 100% unchanged — drop-in replacement.

## Session 2: Editor Free-Cam + Lua API + Demo

- [x] 2.1 Editor free-cam: F5 in `frame_setup.cpp` — `IsKeyJustPressed(Key::F5)` toggles `PushCamera/PopCamera` with a dynamically created "EditorFreeCam". Copies current camera position/orientation/projection. Second F5 pops and destroys.
- [x] 2.2 Per-camera render target: added `RHITextureHandle renderTarget` public member to `Camera` class (default invalid = renders to swapchain). Included `rhi_types.h` in `camera.h`.
- [x] 2.3 Lua bindings in `lua_camera_api.cpp` — `uz.camera.mgr_create(name)→id`, `uz.camera.mgr_switch(id)→bool`, `uz.camera.mgr_list()→table`, `uz.camera.mgr_get_active()→id`, `uz.camera.mgr_destroy(id)→bool`. Registered via `LuaSetCameraManager` + `LuaRegisterCameraManagerAPI` in SceneRenderer::Init.
- [x] 2.4 Demo: Lua camera switching available via `uz.camera.mgr_create/mgr_switch` in any Lua script. Verified API registers at startup ("uz.camera manager API registered").
- [x] 2.5 Build clean, 0 errors, 203 FPS. Camera system fully integrated — CameraManager + F5 free-cam + Lua API + per-camera render target.
