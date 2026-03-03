# Proposal: phase3-game-mode-state-machine

## Why
There is no concept of game state in the engine. Every game must manage transitions between MainMenu, Loading, Playing, Paused, and GameOver with ad-hoc Lua boolean flags and manual system enable/disable calls. Without a formal state machine, transitions such as play to pause to resume require custom code per game, are error-prone (input still processes while paused, physics still steps during loading), and cannot fire reliable events that other systems can react to. A standardized game mode stack is the correct architectural solution and is present in every professional engine.

## What Changes
- Add `GameMode` abstract base class with virtual methods `on_enter(prev_mode)`, `on_exit(next_mode)`, `on_update(dt)`, `on_render()`, and `on_pause()`/`on_resume()` for stack semantics
- Add `GameModeManager` singleton with `switch_to(mode)` (replaces top of stack), `push(mode)` (overlays, e.g. pausing without destroying play mode), and `pop()` (returns to previous mode); all transitions fire `GameModeChanged` event on EventBus
- Built-in engine modes: `PlayMode` (normal simulation tick, input enabled), `PauseMode` (halts physics and timer ticks, UI input only), `LoadingMode` (async scene load, renders loading screen)
- `PlayMode` respects `reduced_motion` accessibility flag and clears controller rumble on enter/exit
- Lua API: `ctx.gamemode:switch("CustomMode")`, `ctx.gamemode:push("Pause")`, `ctx.gamemode:pop()`, `ctx.gamemode:current()` returning mode name string
- Custom Lua game modes: `ctx.gamemode:register("GameOver", { on_enter=fn, on_exit=fn, on_update=fn })` — the Lua table is wrapped in a `LuaGameMode` adapter class
- Transition guards: `GameMode::can_exit()` virtual returning bool; if false, `switch_to/pop` is rejected and logs a warning (prevents leaving LoadingMode before load completes)
- EventBus events: `GameModeEntered{mode_name}`, `GameModeExited{mode_name}` — systems subscribe to pause/resume physics, audio, input, etc.

## Impact
- Affected specs: `docs/specs/scene-system.md`, `docs/specs/input-system.md`, `docs/specs/physics-system.md`, `docs/specs/audio-system.md`, `docs/specs/event-bus.md`
- Affected code: `src/game/game_mode.h` (new), `src/game/game_mode_manager.cpp` (new), `src/game/game_mode_manager.h` (new), `src/game/builtin_modes.cpp` (new), `src/scripting/lua_gamemode.cpp` (new)
- Breaking change: NO (existing games with ad-hoc state management continue to work; adopting GameModes is opt-in)
- User benefit: Pause menus, loading screens, and game-over screens are implemented in minutes with guaranteed correctness. Physics, input, timers, and audio automatically respond to mode transitions via EventBus without game code manually toggling each system.
