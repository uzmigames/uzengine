## 1. CLI Argument Parsing

- [x] 1.1 Parse `--editor`, `--play` mode flags from `argv`
- [x] 1.2 Parse `--project=<path>` — sets project root directory
- [x] 1.3 Parse `--scene=<path>` — overrides main_scene from project config
- [x] 1.4 Parse `--port=<n>` — WebSocket port override (default: 9002 editor, 9003 play)
- [x] 1.5 No args → default to `--editor` for backwards compat

## 2. Engine Modes

- [x] 2.1 Create `src/engine/engine_mode.h` — `EngineMode` enum: `Editor`, `Play`
- [x] 2.2 Editor mode: IPC server active, scene manager active, render loop for viewport stream
- [x] 2.3 Editor mode: NO physics tick, NO script execution, NO audio (script_tick guarded by isPlayMode)
- [x] 2.4 Play mode: full render loop + script tick + (future) physics + (future) audio
- [x] 2.5 Play mode: IPC server on alternate port (--port=9003 default for play)
- [x] 2.6 Play mode: `engine.pause`, `engine.resume`, `engine.stop` IPC handlers (ipc_playmode_handlers + main.cpp)

## 3. Process Lifecycle (C++ side)

- [x] 3.1 Play mode exits cleanly on `engine.stop` IPC call (PostQuitMessage(0))
- [x] 3.2 Play mode exits cleanly on WM_CLOSE (DefWindowProc -> WM_DESTROY -> PostQuitMessage)
- [x] 3.3 Play mode reports startup ready via `engine.status` IPC (returns mode, ready, paused, hasScene, port)
- [x] 3.4 Play mode reports scene load errors via `scene.load` IPC error response

## 4. Extension Side (play-controls.ts)

- [x] 4.1 `launchPlayMode(scenePath)` — spawn process, wait for ready signal
- [x] 4.2 `stopPlayMode()` — send `engine.stop` IPC, then SIGTERM if no response in 2s
- [x] 4.3 `pausePlayMode()` — send `engine.pause` IPC
- [x] 4.4 Play process exit event → auto-update status bar to idle
- [x] 4.5 Play process stdout/stderr → append to "UzEngine Play" output channel

## 5. Viewport Switching

- [x] 5.1 Viewport connects to editor port (9002) in editor mode
- [x] 5.2 On ▶ Play: viewport reconnects to play port (9003)
- [x] 5.3 On ■ Stop: viewport reconnects back to editor port (9002)
- [x] 5.4 Seamless switch — no black frames, reconnect within 500ms

## 6. Testing

- [x] 6.1 CLI parsing: 21 unit tests (ParseEngineMode + ParseLaunchArgs) in test_engine_mode.cpp
- [x] 6.2 IPC handlers: 26 unit tests (PlayModeHandlersTest) for pause/resume/stop/status/setPaused
- [x] 6.3 Build succeeds with zero warnings, 1519 tests pass (2 platform-skipped)
- [ ] 6.4 E2E: Extension Play spawns play process, viewport switches to game stream — deferred (manual)
- [ ] 6.5 E2E: Play process crash does not crash editor process — deferred (manual)
