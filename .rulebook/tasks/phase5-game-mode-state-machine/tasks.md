## 1. GameMode Abstract Base Class
- [ ] 1.1 Create `src/game/game_mode.h` declaring `GameMode` abstract base class with pure virtual methods `on_enter(GameMode* prev_mode)`, `on_exit(GameMode* next_mode)`, `on_update(float dt)`, `on_render()`, `on_pause()`, `on_resume()`
- [ ] 1.2 Add virtual `bool can_exit()` method in `GameMode` returning `true` by default; subclasses may override to block transitions (e.g. `LoadingMode` returning false until load completes)
- [ ] 1.3 Add `const char* name()` pure virtual method returning the mode's name string for logging and Lua API

## 2. GameModeManager Singleton
- [ ] 2.1 Create `src/game/game_mode_manager.h` declaring `GameModeManager` singleton class with methods `switch_to(GameMode* mode)`, `push(GameMode* mode)`, `pop()`, `current()`, and `stack_depth()`
- [ ] 2.2 Create `src/game/game_mode_manager.cpp` implementing the mode stack as a `std::vector<GameMode*>`
- [ ] 2.3 Implement `switch_to(GameMode* mode)`: call `current()->can_exit()`; if false, log a warning and return; otherwise call `current()->on_exit(mode)`, replace the top of the stack, call `mode->on_enter(prev)`; fire `GameModeExited` and `GameModeEntered` events on the EventBus
- [ ] 2.4 Implement `push(GameMode* mode)`: call `current()->on_pause()`, push the new mode, call `mode->on_enter(current_before_push)`; fire `GameModeEntered` event
- [ ] 2.5 Implement `pop()`: if stack depth <= 1, log a warning and return; call `current()->can_exit()`; if false, log warning and return; call `current()->on_exit(prev)`, pop the stack, call `prev->on_resume()`; fire `GameModeExited` event
- [ ] 2.6 Implement `GameModeManager::tick(float dt)` calling `current()->on_update(dt)` once per frame
- [ ] 2.7 Implement `GameModeManager::render()` calling `current()->on_render()` once per frame

## 3. Built-in Engine Modes
- [ ] 3.1 Create `src/game/builtin_modes.h` and `src/game/builtin_modes.cpp` implementing `PlayMode`, `PauseMode`, and `LoadingMode`
- [ ] 3.2 Implement `PlayMode::on_enter()`: enable physics tick, enable timer system, enable full input processing, enable audio; clear controller rumble
- [ ] 3.3 Implement `PlayMode::on_exit()`: no additional teardown required by default; subclasses may override
- [ ] 3.4 Implement `PauseMode::on_enter()`: halt physics tick, halt timer system tick; restrict input to UI-only mode; call `AudioSystem::set_paused(true)`
- [ ] 3.5 Implement `PauseMode::on_exit()`: re-enable physics tick, re-enable timer system; restore full input; call `AudioSystem::set_paused(false)`
- [ ] 3.6 Implement `LoadingMode::on_enter()`: begin async scene load; set `LoadingMode::can_exit()` to return `false`
- [ ] 3.7 Implement `LoadingMode::on_exit()`: called only after async load completes (`can_exit()` returns `true`)
- [ ] 3.8 Handle `reduced_motion` accessibility flag in `PlayMode::on_enter()`: read from settings and disable non-essential animations

## 4. EventBus Events
- [ ] 4.1 Define `GameModeEnteredEvent` struct in `src/game/game_mode_manager.h` with field `const char* mode_name`
- [ ] 4.2 Define `GameModeExitedEvent` struct in `src/game/game_mode_manager.h` with field `const char* mode_name`
- [ ] 4.3 Verify that physics, audio, and input systems subscribe to these events to apply their enable/disable behavior

## 5. Lua API
- [ ] 5.1 Implement `ctx.gamemode:switch("CustomMode")` Lua binding in `src/scripting/lua_gamemode.cpp` calling `GameModeManager::switch_to()` with the named mode
- [ ] 5.2 Implement `ctx.gamemode:push("Pause")` Lua binding calling `GameModeManager::push()`
- [ ] 5.3 Implement `ctx.gamemode:pop()` Lua binding calling `GameModeManager::pop()`
- [ ] 5.4 Implement `ctx.gamemode:current()` Lua binding returning the current mode's `name()` as a string
- [ ] 5.5 Implement `ctx.gamemode:register("ModeName", { on_enter=fn, on_exit=fn, on_update=fn })` Lua binding that wraps the Lua table in a `LuaGameMode` adapter class
- [ ] 5.6 Create `LuaGameMode` adapter class in `src/scripting/lua_gamemode.cpp` that holds a Lua table reference and delegates all `GameMode` virtual calls to the corresponding Lua table fields

## 6. Testing
- [ ] 6.1 Create `tests/core/test_game_mode.cpp` with Google Test suite `GameModeTest`
- [ ] 6.2 Write test `SwitchToCallsEnterAndExit`: create two mock modes; call `switch_to(modeB)`; assert `modeA->on_exit` was called before `modeB->on_enter`
- [ ] 6.3 Write test `SwitchToBlockedWhenCanExitFalse`: mock mode returns `can_exit() = false`; call `switch_to(modeB)`; assert mode is unchanged and `on_exit` was NOT called
- [ ] 6.4 Write test `PushPausesCurrentMode`: call `push(pauseMode)`; assert `playMode->on_pause()` was called
- [ ] 6.5 Write test `PopResumesUnderlyingMode`: push then pop; assert `playMode->on_resume()` was called
- [ ] 6.6 Write test `PopOnSingletonStackLogsWarning`: start with one mode; call `pop()`; assert it returns without crashing and logs a warning
- [ ] 6.7 Write test `GameModeEnteredEventFiredOnSwitch`: subscribe to `GameModeEnteredEvent` on EventBus; call `switch_to(modeB)`; assert event received with `mode_name == modeB->name()`
- [ ] 6.8 Write test `GameModeExitedEventFiredOnSwitch`: subscribe to `GameModeExitedEvent`; call `switch_to(modeB)`; assert event received with `mode_name == modeA->name()`
- [ ] 6.9 Write test `LuaGameModeCallsLuaOnEnter`: register a Lua table mode; push it; assert the Lua `on_enter` function was called
- [ ] 6.10 Write test `StackDepthIncreasesOnPush`: start with depth 1; push twice; assert `stack_depth() == 3`
- [ ] 6.11 Verify C++ line coverage >= 95% and branch coverage >= 95% for `game_mode_manager.cpp` and `builtin_modes.cpp`

## 7. Documentation
- [ ] 7.1 Update `docs/specs/scene-system.md` with `GameMode`, `GameModeManager`, and the mode stack semantics
- [ ] 7.2 Update `docs/specs/lua-scripting.md` with all `ctx.gamemode.*` API entries and the `ctx.gamemode:register` custom mode pattern
- [ ] 7.3 Update `docs/specs/event-bus.md` with `GameModeEnteredEvent` and `GameModeExitedEvent` definitions
