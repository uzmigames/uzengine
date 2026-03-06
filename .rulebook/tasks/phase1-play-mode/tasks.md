## 1. CLI Argument Parsing

- [x] 1.1 Parse `--editor`, `--play` mode flags from `argv`
- [x] 1.2 Parse `--project=<path>` — sets project root directory
- [x] 1.3 Parse `--scene=<path>` — overrides main_scene from project config
- [x] 1.4 Parse `--port=<n>` — WebSocket port override (default: 9002 editor, 9003 play)
- [x] 1.5 No args → default to `--editor` for backwards compat

## 2. Engine Modes

- [x] 2.1 Create `src/engine/engine_mode.h` — `EngineMode` enum: `Editor`, `Play`
- [ ] 2.2 Editor mode: IPC server active, scene manager active, render loop for viewport stream
- [ ] 2.3 Editor mode: NO physics tick, NO script execution, NO audio
- [ ] 2.4 Play mode: full render loop + (future) physics + (future) audio
- [ ] 2.5 Play mode: IPC server on alternate port for pause/stop from extension
- [ ] 2.6 Play mode: `engine.pause`, `engine.resume`, `engine.stop` IPC handlers

## 3. Process Lifecycle (C++ side)

- [ ] 3.1 Play mode exits cleanly on `engine.stop` IPC call
- [ ] 3.2 Play mode exits cleanly on SIGTERM / WM_CLOSE
- [ ] 3.3 Play mode reports startup ready via IPC response (extension polls for ready)
- [ ] 3.4 Play mode reports scene load errors via IPC

## 4. Extension Side (play-controls.ts)

- [x] 4.1 `launchPlayMode(scenePath)` — spawn process, wait for ready signal
- [x] 4.2 `stopPlayMode()` — send `engine.stop` IPC, then SIGTERM if no response in 2s
- [x] 4.3 `pausePlayMode()` — send `engine.pause` IPC
- [x] 4.4 Play process exit event → auto-update status bar to idle
- [x] 4.5 Play process stdout/stderr → append to "UzEngine Play" output channel

## 5. Viewport Switching

- [ ] 5.1 Viewport connects to editor port (9002) in editor mode
- [ ] 5.2 On ▶ Play: viewport reconnects to play port (9003)
- [ ] 5.3 On ■ Stop: viewport reconnects back to editor port (9002)
- [ ] 5.4 Seamless switch — no black frames, reconnect within 500ms

## 6. Testing

- [ ] 6.1 `render.exe --editor` starts, serves IPC on 9002, streams viewport
- [ ] 6.2 `render.exe --play --scene=default.uzscene` starts, streams on 9003
- [ ] 6.3 Extension ▶ Play spawns play process, viewport switches to game stream
- [ ] 6.4 Extension ■ Stop kills play process, viewport returns to editor stream
- [ ] 6.5 Play process crash does not crash editor process
