# Proposal: phase1-play-mode

## Why

Currently `render.exe` runs in a single undifferentiated mode where the editor and game simulation share the same process lifetime. Pausing or restarting the editor loop also stops the game simulation, making it impossible to iterate on a running game without restarting the entire engine. Separating the editor process (`--editor`) from the play process (`--play`) mirrors how production engines work: the editor owns scene authoring and asset management, while the play process owns the full game loop including physics, scripts, and real-time simulation. Without this separation, features like Play/Stop buttons, fast re-entry into play mode, and process-isolated crash recovery cannot be implemented.

## What Changes

- Add `--editor` and `--play` CLI flags to `render.exe`; `--editor` starts the IPC/MCP server and scene management with no game simulation; `--play --project=./ --scene=<path>` starts a full game loop (physics, Lua scripts, ECS systems) and streams the viewport to a secondary WebSocket port
- VSCode extension `PlayModeController` class spawns and kills the play process via `child_process.spawn`, passes the active scene path, and opens a secondary viewport panel connected to the play process WebSocket port
- Play/Stop/Pause toolbar buttons added to the VSCode extension status bar; Stop kills the play process, Pause sends an IPC `engine.setPaused` message
- Scene state is serialized by the editor process to a temp file before launch so the play process loads a consistent snapshot without requiring unsaved changes to be flushed to disk
- Editor process detects unexpected play process exit (crash) via the child process `close` event and displays a "Play process crashed" notification in VSCode with a link to the crash log
- IPC protocol extended with `playmode.start`, `playmode.stop`, `playmode.pause`, and `playmode.status` messages for coordinating state between the two processes

## Impact

- Affected specs: `docs/specs/editor-panels.md`, `docs/engine/lifecycle.md`, `docs/guides/editor-workflow.md`
- Affected code: `src/main.cpp` (CLI flag parsing), `src/editor/` (play mode controller), `vscode-extension/src/playModeController.ts`, `vscode-extension/src/panels/`
- Breaking change: NO — existing single-process behavior remains the default when no flag is passed; editor mode is activated by the extension automatically
- User benefit: Developers can click Play in the VSCode toolbar to launch a fully simulated game in a separate viewport, click Stop to return to editing, and iterate without restarting the engine process. Crashes in game code kill only the play process, leaving the editor intact.
