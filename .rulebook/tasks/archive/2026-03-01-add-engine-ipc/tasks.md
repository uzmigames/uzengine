## 1. SceneParams — thread-safe scene state

- [x] 1.1 Create `src/renderer/scene_params.h`:
  - `std::atomic<float> rotX/Y/Z` (rotation speed per axis)
  - `std::atomic<bool> paused`
  - `std::mutex bgMutex` + `float bgR/G/B`
  - `std::atomic<int> geometryType` (enum: Pyramid=0, Cube=1)
- [x] 1.2 Instantiate `SceneParams g_scene` in `main.cpp`, pass reference to Renderer and IPC poller
- [x] 1.3 Renderer reads SceneParams every frame (rotation, background color, pause)

## 2. IPC Registry — function dispatch table

- [x] 2.1 Create `src/common/ipc_registry.h/cpp`:
  - `using Handler = std::function<nlohmann::json(const nlohmann::json&)>`
  - `IpcRegistry::add(string name, Handler)` + `IpcRegistry::call(string, json) -> json`
- [x] 2.2 Add `nlohmann-json` to vcpkg.json + CMakeLists.txt
- [x] 2.3 Register handlers in `main.cpp` at startup:
  - `engine.ping`, `engine.getStatus`, `engine.screenshot`
  - `scene.setRotation`, `scene.setBackground`, `scene.setGeometry`, `scene.setPaused`, `scene.setParam`
  - `file.read`, `file.write`, `file.list`
  - `shader.reload` (stub for now)

## 3. IPC Poller — polling loop (IPC thread)

- [x] 3.1 Create `src/common/ipc_poller.h/cpp`:
  - Poll `build/preview/command.json` every 50ms via `GetFileTime`
  - If changed: read → parse json → `registry.call(function, args)` → write `response.json`
  - Status writer: update `status.json` every 1000ms
- [x] 3.2 Create `build/preview/` on startup via `CreateDirectoryA` (no-op if exists)
- [x] 3.3 Run IpcPoller in dedicated thread from `main.cpp`

## 4. Screenshot Handler

- [x] 4.1 Download `stb_image_write.h` → `external/stb/stb_image_write.h`
- [x] 4.2 Add to CMakeLists: `target_include_directories(... external/stb)`
- [x] 4.3 Renderer exposes `getLastFrameCopy()` thread-safe (calls existing getLatestFrame())
- [x] 4.4 Handler `engine.screenshot`: copy lastFrame.pixels → `stbi_write_png("build/preview/frame_<id>.png", ...)`

## 5. File Handlers

- [x] 5.1 Handler `file.read`: read file, return `{"content": "..."}` or `{"error": "not found"}`
- [x] 5.2 Handler `file.write`: write file (path relative to build/), return `{"ok": true}`
- [x] 5.3 Handler `file.list`: list directory via FindFirstFileA, return `{"files": [...]}`
- [x] 5.4 Sandbox: file handlers reject paths containing `..`

## 6. Acceptance Criteria

- [x] Writing `{"id":"1","type":"call","function":"engine.ping","args":{}}` to command.json → response.json appears in <100ms
- [x] `{"function":"engine.screenshot"}` → `build/preview/frame_.png` created and readable as PNG
- [x] `{"function":"scene.setRotation","args":{"y":3.0}}` → pyramid visibly speeds up
- [x] `{"function":"scene.setPaused","args":{"paused":true}}` → render freezes, stream stops
- [x] `{"function":"file.write","args":{"path":"test.txt","content":"hello"}}` → file created
- [x] status.json updated every ~1s with real fps value
- [x] Render loop shows no stutter (IPC runs on separate thread)

<!-- Build: Release, zero errors, zero warnings. All 12 handlers registered. -->
