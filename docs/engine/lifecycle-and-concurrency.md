# UzEngine — Lifecycle & Concurrency

> **Scope**: This document describes the complete lifecycle of the `UzEngine.exe` process and its concurrency model. It makes a clear distinction between two independent loops: the **Render Loop** (GPU, readback, JPEG streaming) and the **Game Loop** (input, scripts, physics, ECS). These are separate concerns that run at different rates and have different ownership rules.

---

## 1. Two Loops, One Process

The engine has two distinct update cycles that must never be conflated:

| | Render Loop | Game Loop |
|---|---|---|
| **Owned by** | Main thread | Game Logic thread (Phase 3) / inline on Main (Phase 0–2) |
| **Rate** | Locked to display / stream rate (60 fps target) | Fixed timestep (e.g. 60 Hz) or variable |
| **Inputs** | ECS draw data (read-only snapshot) | Input state, physics, script logic |
| **Outputs** | GPU commands → JPEG frame → WebSocket | Updated ECS transforms, game state |
| **GPU access** | Yes — exclusive | No — never touches GPU directly |
| **Must finish in** | ~16 ms (hard deadline) | Allowed to run longer (Phase 3 thread) |

The Render Loop reads a **stable snapshot** of game state. It never waits on game logic, and game logic never waits on the GPU.

---

## 2. Thread Map

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  UzEngine.exe                                                                   │
│                                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │  RENDER LOOP THREADS                                                    │   │
│  │                                                                         │   │
│  │  ┌─────────────────────┐   SPSC RingBuffer   ┌─────────────────────┐   │   │
│  │  │  MAIN THREAD        │ ─── FrameData ────► │  ENCODE THREAD      │   │   │
│  │  │  Win32 msg loop     │                     │  JPEG compress      │   │   │
│  │  │  DX12 render        │                     │  WS broadcast       │   │   │
│  │  │  GPU readback       │                     └──────────┬──────────┘   │   │
│  │  └─────────────────────┘                                │              │   │
│  └─────────────────────────────────────────────────────────┼──────────────┘   │
│                                                             │ binary JPEG      │
│                                                             ▼                  │
│                                                    IXWebSocket threads         │
│                                                    (owned by library)          │
│                                                             │                  │
│                                                             ▼                  │
│                                                    VSCode Extension            │
│                                                    (WebSocket client)          │
│                                                                                │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │  GAME LOOP THREADS                                                      │   │
│  │                                                                         │   │
│  │  ┌─────────────────────┐                                                │   │
│  │  │  GAME LOGIC THREAD  │  Input → Scripts → Physics → ECS update        │   │
│  │  │  (Phase 3)          │  Writes: CTransform, CRigidBody, etc.          │   │
│  │  │                     │  Reads: InputState, SceneGraph (via GameCtx)   │   │
│  │  └─────────────────────┘                                                │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │  ENGINE SERVICE THREADS                                                 │   │
│  │                                                                         │   │
│  │  ┌─────────────────────┐   ┌─────────────────────┐                     │   │
│  │  │  IPC POLLER         │   │  FILE WATCHER        │                     │   │
│  │  │  command.json 50ms  │   │  RDCW async I/O      │                     │   │
│  │  │  dispatch handlers  │   │  .hlsl/.uzscene/etc  │                     │   │
│  │  └─────────────────────┘   └─────────────────────┘  (Phase 1+)         │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Thread Ownership Table

| Thread | Group | Owns (exclusive write) | Reads from |
|--------|-------|------------------------|------------|
| **Main** | Render | DX12 device, command queue, swap chain, readback buffers | ECS world (read-only snapshot), `SceneParams` (atomic/mutex) |
| **Encode** | Render | `JpegEncoder`, temp JPEG buffer | `RingBuffer` (SPSC consumer) |
| **Game Logic** | Game | ECS component data (`CTransform`, scripts, physics state) | `InputState`, `SceneGraph` via `GameContext` |
| **IPC Poller** | Service | None (stateless) | `command.json`, `IpcRegistry` (read-only after setup) |
| **File Watcher** | Service | Internal event queue | RDCW directory handles |
| **IXWebSocket** | Library | Sockets, send buffers | Encoded JPEG from Encode thread |

---

## 3. Render Loop Lifecycle

The Render Loop runs on the **Main thread**. Its job is strictly: read stable game state → issue GPU commands → readback pixels → push to ring buffer. It must complete every frame within the budget and must never block on game logic.

### 3.1 Frame Sequence

```
RENDER LOOP (Main thread, ~16.67 ms budget at 60 fps):
│
├─ PeekMessage()
│   └─ WM_DESTROY → PostQuitMessage() → exits loop
│
├─ Frame Limiter (QPC spin-sleep)
│   ├─ QueryPerformanceCounter()
│   ├─ elapsed < kTargetFrameTime (1/60s)?
│   │   ├─ Sleep(remaining_ms - 1)   — yield CPU, avoid busy-waiting whole frame
│   │   └─ spin residual             — sub-millisecond precision
│   └─ record frame start time
│
├─ [Phase 1+] Drain scene command queue
│   └─ apply pending SceneCommands from IPC Poller (AddNode, SetProperty, ...)
│       — scene graph is ONLY mutated here, never from other threads
│
├─ [Phase 1+] Drain file watcher event queue
│   ├─ .hlsl change  → ShaderCompiler::queue_reload(path)
│   ├─ .lua change   → LuaScriptReloader::queue_reload(path)  (Phase 3)
│   ├─ .uzscene change → SceneManager::queue_reload(path)
│   └─ .cpp change   → HotReloadManager::trigger_build()
│
├─ [Phase 1+] ShaderCompiler::flush_pending()
│   ├─ compile_hlsl() → new DXIL bytecode
│   ├─ WaitForGpu()   — ensure GPU is not using old PSO
│   ├─ create new PSO from DXIL
│   └─ swap PSO atomically — old PSO released via ComPtr
│
├─ [Phase 3] LuaScriptReloader::flush_pending()  (Phase 3)
│   └─ For each changed .lua file:
│       ├─ Find all entities with CScriptRef.script_id == hash(path)
│       ├─ For each entity: call M:on_stop(L, self_ref, ctx)
│       ├─ luaL_loadfile(L, path) — reload module
│       ├─ lua_setglobal(L, module_key) — replace M in VM
│       └─ For each entity: call M:on_start(L, new_ref, ctx)
│           (total: ~60ms, no VM restart, no frame skip)
│
├─ [Phase 3] game_done.acquire()
│   └─ wait for Game Logic thread to finish its update
│       — ECS world is now stable, safe to read
│
├─ DX12 Render Pass
│   ├─ Reset command allocator + command list
│   ├─ ResourceBarrier: PRESENT → RENDER_TARGET
│   ├─ ClearRenderTargetView (background color — read SceneParams via mutex)
│   ├─ Set viewport + scissor
│   ├─ Set PSO, root signature
│   ├─ Read ECS world (CMeshRenderer, CTransform, CDirLight, CCamera) — READ ONLY
│   │   └─ build draw call list → DrawInstanced() / DrawIndexedInstanced()
│   ├─ FPS overlay (CPU-composited bitmap font)
│   ├─ ResourceBarrier: RENDER_TARGET → COPY_SOURCE
│   ├─ CopyTextureRegion → readback buffer[frame % 2]
│   ├─ ResourceBarrier: COPY_SOURCE → PRESENT
│   ├─ Close command list
│   └─ ExecuteCommandLists()
│
├─ Present(syncInterval=0, flags=0)
│
├─ Signal fence[frame % 2]
│   └─ Wait fence[(frame-1) % 2]
│       — ensures readback of previous frame is complete before we overwrite it
│
├─ getLatestFrame()
│   └─ map readback buffer → copy pixels to FrameData (CPU vector<uint8_t>)
│
├─ g_frameRing.push(std::move(frame))
│   └─ SPSC: Main = producer, Encode = consumer. Drop oldest if ring full.
│
├─ Update EngineMetrics (fps, frameCount) — atomic writes
│
└─ [Phase 3] game_ready.release()
    └─ signal Game Logic thread to start next update
```

### 3.2 Encode Thread Loop

Runs independently of the render frame rate. Drain frames as fast as the JPEG encoder allows.

```
ENCODE LOOP (Encode thread):
│
LOOP:
├─ g_frameRing.pop()
│   └─ empty? → sleep(1ms) + continue   — avoid busy-waiting
│
├─ JpegEncoder::encode(pixels, width, height, quality=85)
│   └─ libjpeg-turbo TurboJPEG API: RGBA → JPEG, no allocation
│
├─ WsServer::broadcast(jpegBlob)
│   └─ IXWebSocket enqueues send on its own threads
│
└─ repeat until g_encodeRunning == false
```

---

## 4. Game Loop Lifecycle

The Game Loop is concerned with **simulation**: reading input, running scripts, stepping physics, and updating ECS component data. It has no GPU access and no strict frame budget — it runs at a fixed timestep independent of the render rate.

### 4.1 Game Loop Placement

| Phase | Where game logic runs |
|-------|-----------------------|
| Phase 0–2 | Inline on Main thread, before render pass. No dedicated thread. |
| Phase 3 | Dedicated Game Logic thread, synchronized to render via semaphore. |

This is intentional: phases 0–2 don't need physics or complex scripts, so the threading overhead isn't worth it. Phase 3 introduces Jolt Physics and full scripting, where the dedicated thread prevents slow scripts from stalling the render.

### 4.2 Game Loop Sequence (Phase 3, dedicated thread)

```
GAME LOOP (Game Logic thread, Phase 3):
│
LOOP:
│
├─ game_ready.acquire()
│   └─ wait for Main thread to signal: "render is done reading ECS, you can write now"
│
├─ InputSystem::poll()
│   ├─ Raw Win32 input (keyboard, mouse, XInput gamepad)
│   ├─ Map to InputActions via active InputMappingContext
│   └─ write InputState (thread-local to game thread — no sharing needed)
│
├─ game_api.on_update(ctx, delta_time)
│   └─ game.dll code runs here (hot-reloadable)
│       ├─ Script logic:     read InputState, write ECS components
│       ├─ Spawn/despawn:    enqueue SceneCommands (thread-safe queue)
│       ├─ Audio triggers:   AnimNotify → miniaudio (Phase 3)
│       └─ Camera update:    SpringArm, lag, collision probe
│
├─ systems::update_transforms(ecs_world)
│   └─ propagate local → world transforms down the node hierarchy
│
├─ systems::step_physics(ecs_world, delta_time)   (Phase 3)
│   ├─ Jolt Physics::Update(dt, collision_steps)
│   └─ sync Jolt body positions → CTransform, CRigidBody
│
├─ systems::update_animations(ecs_world, delta_time)   (Phase 3)
│   ├─ sample animation clips (SLERP/LERP)
│   ├─ blend via state machine
│   └─ write bone palette to GPU upload buffer (mapped, no fence needed — double-buffered)
│
├─ game_done.release()
│   └─ signal Main thread: "ECS world is stable, safe to render"
│
└─ repeat until shutdown signal
```

### 4.3 Game Loop Sequence (Phase 0–2, inline on Main thread)

Before Phase 3, game update happens inline on Main, in a single tick before the render pass. No dedicated thread, no semaphore:

```
MAIN THREAD (Phase 0–2, simplified):
│
├─ Frame Limiter (QPC)
│
├─ delta = timer.tick()
│
├─ [Game update — inline, no thread]
│   ├─ Renderer::update(elapsed, delta)   — currently: rotate pyramid via SceneParams
│   └─ (Phase 1–2) scene manager tick, resource eviction
│
├─ Renderer::render()   — DX12 render pass (same as Phase 3, above)
│
└─ ring.push(frame)
```

### 4.4 Physics Thread Note

Physics runs on the **Game Logic thread** (same thread as `on_update()`) using a fixed 60 Hz timestep accumulator. It is **not a separate thread**. This means:

- No thread synchronization needed for physics ↔ script interaction
- Physics callbacks (collision, trigger) execute on the same thread as script `on_update()`
- Jolt Physics state is never accessed from the render thread
- Scene graph transforms are the authority; Jolt bodies are synced from them, not the reverse

**Fixed Timestep Pattern:**

```cpp
// In game loop
static float accumulator = 0;
const float fixed_dt = 1.0f / 60.0f;  // 60 Hz physics

accumulator += delta_time;
while (accumulator >= fixed_dt) {
    physics_world->update(fixed_dt);
    systems::step_physics(ecs_world, fixed_dt);
    accumulator -= fixed_dt;
}

// Render thread sees the physics state as stable (already finished)
```

### 4.5 Audio Thread Note

XAudio2 runs on its own internal threads (managed by XAudio2 itself). `AudioEngine` methods called from the Game Logic thread are **thread-safe** — XAudio2 uses COM thread safety under the hood. This means:

- Calling `audio_engine.play_sound()` from the game thread is safe
- Internal XAudio2 worker threads handle DSP and output independently
- Synchronization happens inside XAudio2 (we don't manage it)
- Audio state is never modified from the render thread

---

## 5. The Synchronization Boundary (Phase 3)

The only point where Render Loop and Game Loop synchronize is via **two binary semaphores**. This is the minimum synchronization needed — it avoids any lock on the ECS world during render.

```
Timeline:

Frame N:
  Game thread:   [─── on_update ───────] game_done.release()
  Main thread:                           game_done.acquire() [─── render ───────────] game_ready.release()
  Game thread:                                                                        game_ready.acquire() [─── on_update ───]

                 ├───── Game N ─────────┤                    ├───── Render N ───────┤├───── Game N+1 ────►

No lock on ECS during either phase. Game writes, then render reads. Never simultaneous.
```

```cpp
// Primitives: std::binary_semaphore (C++20)
std::binary_semaphore game_ready{0};  // starts at 0: game waits for first signal
std::binary_semaphore game_done{0};   // starts at 0: render waits for first game tick

// Main thread (end of frame):
game_ready.release();       // "ECS is now free, start your update"

// Main thread (start of render pass):
game_done.acquire();        // "wait until game logic is done"

// Game Logic thread:
game_ready.acquire();       // wait for render to finish reading
/* ... on_update, physics, etc. ... */
game_done.release();        // "ECS is stable, render can start"
```

---

## 6. IPC Poller Lifecycle

Runs independently of both loops. Low-frequency (50 ms poll). Applies changes via queues — never touches ECS or DX12 directly.

```
IPC POLLER LOOP (IPC Poller thread):
│
LOOP:
├─ sleep(50ms)
│
├─ open command.json → does not exist? continue
│
├─ read + parse JSON
├─ rename command.json → command_processing.json   — prevent double-read
│
├─ IpcRegistry::dispatch(fn, args)
│   ├─ engine.*   → read EngineMetrics (atomic reads, no writes)
│   ├─ scene.*    → push(SceneCommand) to scene_cmd_queue   — Main thread applies
│   ├─ shader.*   → push(path) to shader_reload_queue       — Main thread compiles
│   ├─ asset.*    → push(AssetCommand) to asset_cmd_queue   — Main thread imports
│   └─ file.*     → direct file I/O (sandboxed to project dir)
│
├─ write response.json
├─ delete command_processing.json
│
└─ repeat until stop signal
```

**Key rule**: IPC Poller never modifies shared mutable state directly. It only enqueues commands. The Main thread drains all queues at the start of each frame, in a single-threaded context, before any render work begins.

---

## 7. Boot Sequence

```
WinMain()
│
├─ 1.  timeBeginPeriod(1)             — 1 ms timer resolution (required for 60 fps Sleep accuracy)
├─ 2.  ix::initNetSystem()            — Winsock (required by IXWebSocket on Windows)
├─ 3.  RegisterClassExW()             — Win32 window class
├─ 4.  CreateWindowExW()              — HWND always created (headless: hidden, debug: visible)
│       └─ headless: WS_POPUP + WS_EX_TOOLWINDOW — off taskbar, off Alt+Tab
│
├─ 5.  Arena::init(permanent, 128 MB) — permanent arena, never reset
├─ 6.  Logger::init()                 — OutputDebugString + optional file sink
├─ 7.  ResourceManager::init()        — empty asset cache
│
├─ 8.  WsServer::start(port=9002)     — IXWebSocket server starts here; its threads are created
│
├─ 9.  g_encodeRunning = true
│       std::thread encodeThread(encodeThreadFn)   ← ENCODE THREAD born here
│
├─ 10. DX12Renderer::init(hwnd, w, h)
│       ├─ D3D12CreateDevice()
│       ├─ CreateCommandQueue()
│       ├─ CreateSwapChainForHwnd()
│       ├─ CreateDescriptorHeaps()
│       ├─ CreateRenderTargetViews()
│       └─ CreateReadbackBuffers()    — double-buffered (ping-pong with fence)
│
├─ 11. ShaderCompiler::init()         — (Phase 1) LoadLibraryW(dxcompiler.dll)
│
├─ 12. SceneManager::load(startup_scene)   — (Phase 1) parse .uzscene, populate ECS world
│
├─ 13. IpcRegistry::setup()           — register all handlers (read-only after this point)
├─ 14. IpcPoller::start()             ← IPC POLLER THREAD born here
│
├─ 15. FileWatcher::start(project_dir)  ← FILE WATCHER THREAD born here  (Phase 1+)
│
├─ 16. HotReloadManager::init()         — (Phase 1) shadow-copy + LoadLibraryW(game.dll)
│       └─ game_api.on_start(ctx)       — one-time game init (runs on Main, before game thread)
│           └─ LuaVM::create() inside game.dll on_start  (Phase 3)
│               ├─ luajit_newstate()
│               ├─ lua_sandbox_apply()
│               ├─ register_bindings() (Vec3, Quat, NodeRef, Scene, InputState, etc.)
│               ├─ Load all CScriptRef[SCRIPT_LUA] nodes → call M:on_start(ctx)
│               └─ Drain initial timer queue
│
├─ 17. [Phase 3] start Game Logic thread
│       └─ game_ready semaphore initialized to 0 — game thread waits for first release
│
└─ 18. → RENDER LOOP  (section 3.1)
```

---

## 8. Shutdown Sequence

Strict reverse order — each subsystem stops before the one it depends on is destroyed.

```
WM_QUIT received → exit render loop
│
├─ 1.  [Phase 3] shutdown game thread
│       ├─ set game_shutdown flag
│       ├─ game_ready.release()     — unblock game thread so it can see the flag
│       └─ game_thread.join()
│
├─ 2.  game_api.on_stop(ctx)        — game saves persistent state if needed
├─ 3.  FreeLibrary(game_dll_shadow) — unload game.dll
│
├─ 4.  FileWatcher::stop()          — cancel overlapped I/O, join thread  (Phase 1+)
├─ 5.  IpcPoller::stop()            — set stop flag, join thread, delete temp files
│
├─ 6.  g_encodeRunning = false
│       encodeThread.join()         — drain ring buffer, finish any in-flight JPEG
│
├─ 7.  WsServer::stop()             — close all connections, join IXWebSocket threads
│
├─ 8.  Renderer::shutdown()
│       ├─ WaitForGpu()             — wait for all in-flight GPU frames to complete
│       ├─ release RTVs, readback buffers
│       ├─ release swap chain
│       └─ release D3D12 device + DXGI factory
│
├─ 9.  ResourceManager::shutdown()  — evict all cached assets, release GPU resources
├─ 10. Logger::shutdown()           — flush file sink
│
├─ 11. ix::uninitNetSystem()        — Winsock cleanup
└─ 12. timeEndPeriod(1)             — restore default timer resolution
```

---

## 9. Concurrency Primitives Reference

| Primitive | Instance | Producer | Consumer | Why this primitive |
|-----------|----------|----------|----------|--------------------|
| `RingBuffer<FrameData,8>` | `g_frameRing` | Main | Encode | SPSC lock-free: one producer, one consumer, no contention |
| `std::atomic<float>` | `SceneParams::rotX/Y/Z` | IPC Poller | Main (render) | Single scalar, relaxed ordering sufficient |
| `std::mutex` | `SceneParams::bgMutex` | IPC Poller | Main (render) | Three floats must be read/written as a consistent group |
| `std::atomic<bool>` | `g_encodeRunning` | Main | Encode | Simple stop signal, no ordering guarantee needed |
| `std::mutex` + `std::deque` | `scene_cmd_queue` | IPC Poller | Main | Low-frequency commands; correctness over performance |
| `std::mutex` + `std::vector` | `shader_reload_queue` | File Watcher | Main | Low-frequency, order matters for shader dependency |
| `std::binary_semaphore` | `game_ready`, `game_done` | Main ↔ Game | Game ↔ Main | Ping-pong synchronization with zero lock contention on ECS |
| `ID3D12Fence` | `fence[2]` | GPU | Main (CPU) | GPU → CPU readback synchronization; double-buffered |

---

## 10. Thread Safety Invariants

These rules must never be violated. Each violation is a race condition or undefined behavior.

**Invariant 1 — DX12 is single-threaded (Main thread only)**
```
❌ WRONG: any non-Main thread calls Reset(), DrawInstanced(), ExecuteCommandLists()
✅ RIGHT: only Main thread issues all D3D12 commands
```

**Invariant 2 — Ring buffer has exactly one producer and one consumer**
```
❌ WRONG: two threads calling ring.push(), or two threads calling ring.pop()
✅ RIGHT: push() on Main only, pop() on Encode only
```

**Invariant 3 — Render loop never writes ECS; Game Loop never reads GPU**
```
❌ WRONG: render thread writes CTransform; game thread calls ID3D12CommandList methods
✅ RIGHT: render reads ECS (after game_done.acquire()); game writes ECS (after game_ready.acquire())
```

**Invariant 4 — Scene graph mutated only on Main thread**
```
❌ WRONG: IPC Poller calls scene.add_node() directly
✅ RIGHT: IPC Poller pushes SceneCommand; Main drains and applies at start of frame
```

**Invariant 5 — SceneParams scalars only via atomic**
```
❌ WRONG: g_scene.rotY = 1.5f;          (data race between Main and IPC Poller)
✅ RIGHT: g_scene.rotY.store(1.5f, relaxed);
```

**Invariant 6 — SceneParams multi-field groups only via mutex, read into locals**
```
❌ WRONG: use g_scene.bgR in render without lock (may read partially written group)
✅ RIGHT: lock_guard(bgMutex) → copy bgR/bgG/bgB to locals → use locals outside lock
```

**Invariant 7 — PSO swap never happens mid-frame**
```
❌ WRONG: swap PSO while command list is being recorded
✅ RIGHT: WaitForGpu() first → build new PSO → swap → new frame begins with new PSO
```
