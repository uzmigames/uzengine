# Spec: Timer / Delayed Call System

> **Status**: Planned (Phase 1)
> **Owner**: `game.dll` (LuaJIT VM + C++ timer table)
> **Layer**: Lua scripting layer, drained during `on_update` dispatch

---

## Overview

The Timer system gives Lua scripts a way to schedule callbacks after a delay or at a regular interval without writing manual countdown logic in `on_update`. Timers are stored entirely inside `game.dll` — the engine core (`render.exe`) knows nothing about them.

This is the most common scripting quality-of-life feature and dramatically reduces boilerplate in NPC behavior, trigger delays, cooldown systems, and repeating world events.

---

## Lua API

All timer functions live on the `ctx` (GameContext) userdata injected per-call:

### `ctx:schedule_after(delay_sec, callback)` → `timer_id`

Run `callback` once after `delay_sec` seconds have elapsed.

```lua
-- Explode 3 seconds after on_start
function M:on_start(ctx)
    self.explode_timer = ctx:schedule_after(3.0, function()
        ctx:log("BOOM")
        ctx.scene:remove_node(self)
    end)
end
```

- `delay_sec`: float, seconds from now (clamped to 0 if negative)
- `callback`: Lua function (closure is allowed — captures `self`)
- Returns: `timer_id` (integer, unique within the scene lifetime)

### `ctx:schedule_every(interval_sec, callback)` → `timer_id`

Run `callback` repeatedly every `interval_sec` seconds. First invocation happens after one full interval.

```lua
-- Regenerate 5 HP every 2 seconds
function M:on_start(ctx)
    self.regen_id = ctx:schedule_every(2.0, function()
        self.health = math.min(self.health + 5, self.max_health)
    end)
end
```

- `interval_sec`: float, must be > 0 (clamped to 0.016 if smaller — one frame at 60fps)
- `callback`: Lua function
- Returns: `timer_id`

### `ctx:cancel_timer(timer_id)`

Cancel a pending one-shot or repeating timer. Safe to call with an already-expired or already-cancelled id (no-op).

```lua
function M:on_stop(ctx)
    ctx:cancel_timer(self.regen_id)
    ctx:cancel_timer(self.explode_timer)
end
```

### `ctx:schedule_next_frame(callback)`

Run `callback` exactly once at the start of the next frame, before any `on_update` calls. No `timer_id` is returned (cannot be cancelled).

```lua
-- Defer a heavy operation until after the current frame
function M:on_start(ctx)
    ctx:schedule_next_frame(function()
        self:heavy_init(ctx)
    end)
end
```

Use cases:
- Avoid re-entrancy issues when modifying the scene graph during `on_update`
- Execute logic after all current-frame `on_start` callbacks have fired
- Coalesce multiple mutations into the next frame

---

## C++ Implementation

### Data Structure

Timers are stored in a flat array inside `LuaVM` (in `game.dll`):

```cpp
// src/game/lua_vm.h (inside game.dll)
struct LuaTimer {
    uint32_t  id;             // monotonic id
    float     remaining_sec;  // seconds until next fire (countdown)
    float     interval_sec;   // 0 = one-shot, >0 = repeating
    int       callback_ref;   // LUA_REGISTRYINDEX ref to Lua function
    bool      cancelled;
};

class LuaVM {
    // ...
    std::vector<LuaTimer> timers_;
    uint32_t              next_timer_id_ = 1;
    std::vector<int>      next_frame_callbacks_; // LUA_REGISTRYINDEX refs
};
```

### Drain Loop (Called Before Node `on_update`)

```cpp
// src/game/lua_vm.cpp — called from uz_game_update
void LuaVM::drain_timers(float dt) {
    // 1. Drain next-frame callbacks (always fire first)
    for (int ref : next_frame_callbacks_) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            log_lua_error(L);
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    next_frame_callbacks_.clear();

    // 2. Tick all live timers
    for (auto& t : timers_) {
        if (t.cancelled) continue;
        t.remaining_sec -= dt;
        if (t.remaining_sec > 0.0f) continue;

        // Fire callback
        lua_rawgeti(L, LUA_REGISTRYINDEX, t.callback_ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            log_lua_error(L);
            lua_pop(L, 1);
            t.cancelled = true;     // Error → cancel timer
            luaL_unref(L, LUA_REGISTRYINDEX, t.callback_ref);
            continue;
        }

        if (t.interval_sec > 0.0f) {
            // Repeating: reset countdown (no drift accumulation)
            t.remaining_sec = t.interval_sec + t.remaining_sec; // absorb overshoot
        } else {
            // One-shot: mark done
            t.cancelled = true;
            luaL_unref(L, LUA_REGISTRYINDEX, t.callback_ref);
        }
    }

    // 3. Compact cancelled timers (amortized O(n))
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const LuaTimer& t) { return t.cancelled; }),
        timers_.end());
}
```

### Execution Order Per Frame

```
uz_game_update(dt)
  │
  ├── 1. Apply pending SceneCommands (from previous frame)
  ├── 2. LuaVM::drain_timers(dt)          ← next-frame callbacks, then timed callbacks
  ├── 3. For each node with CScriptRef(LUA): on_update(ctx, dt)
  └── 4. Physics step (Phase 3+)
```

Timers always fire **before** per-node `on_update`, so callbacks from one frame are guaranteed to be visible to all scripts in the same frame.

---

## Error Handling

| Error | Behavior |
|-------|----------|
| Callback throws a Lua error | Error logged with file+line; timer cancelled; engine continues |
| `cancel_timer` with unknown id | No-op, no error |
| `schedule_after` with negative delay | Clamped to 0 (fires next frame) |
| `schedule_every` with interval ≤ 0 | Clamped to one frame duration (1/60 sec) |
| Callback error on repeating timer | Timer cancelled after first error (same policy as `on_update` errors) |

Error log format:
```
[lua timer error] timer_id=42 — attempt to index nil value (field 'target')
  stack traceback:
    res://game/enemy_ai.lua:27: in function <res://game/enemy_ai.lua:25>
```

---

## Hot-Reload Behavior

### Lua Script Hot-Reload (~60ms path)

When a `.lua` file changes and hot-reload fires:

1. `M:on_stop(ctx)` is called on all affected nodes — scripts should cancel their timers here
2. Module is reloaded (`luaL_loadfile`)
3. `M:on_start(ctx)` is called — scripts re-register their timers

**Timers are NOT automatically transferred across Lua hot-reload.** Scripts must cancel in `on_stop` and re-register in `on_start`. This is intentional: the callback closure may reference stale upvalues from the old module version.

```lua
-- Correct hot-reload pattern
function M:on_start(ctx)
    self.regen_id = ctx:schedule_every(2.0, function() self:regen(ctx) end)
end

function M:on_stop(ctx)
    ctx:cancel_timer(self.regen_id)  -- always cancel in on_stop
    self.regen_id = nil
end
```

### `game.dll` Hot-Reload (C++ rebuild, ~2.5s path)

When `game.dll` reloads, the entire LuaVM is destroyed and recreated. All timers are cleared. Lua instance state is preserved via `uz_game_save_state` / `uz_game_load_state` (serializes `self` tables), but timer registrations are NOT serialized — scripts must re-register in `on_start`.

### Scene Change

All timers are cleared when the scene unloads (`LuaVM::clear_timers()` called from `uz_game_stop`). No action needed from scripts.

---

## Shipping Behavior

The timer system uses the same code path in all build modes:

| Build Mode | Timers | Notes |
|-----------|--------|-------|
| `--editor` (`UZ_EDITOR=1`) | Enabled | `lua.eval` can register debug timers |
| `--play` (`UZ_DEV=1`) | Enabled | Same as editor, no debug IPC |
| Shipping (`UZ_SHIPPING=1`) | Enabled | No stripping — timers are gameplay-critical |

No IPC handler is needed for timers — they are purely a game logic concern.

---

## Comparison with Other Engines

| Engine | Equivalent |
|--------|-----------|
| Unity | `Invoke(method, delay)`, `InvokeRepeating(method, delay, repeat)` |
| Unreal | `SetTimer` on `UWorld` via `FTimerManager` |
| Godot | `$Timer` node or `get_tree().create_timer(delay).timeout.connect(callback)` |
| UzEngine | `ctx:schedule_after`, `ctx:schedule_every` — closure-based, no node overhead |

UzEngine timers are closure-based (not string-method names like Unity's `Invoke`), which means:
- AI can generate them inline
- Captures `self` naturally (no `[this]` juggling)
- No reflection or method lookup overhead

---

## C++ Source Files

```
src/game/
├── lua_vm.h/cpp          — LuaVM class: drain_timers(), schedule_after_lua(), schedule_every_lua()
src/engine/scripting/
├── lua_binding.h/cpp     — registers ctx:schedule_after, ctx:schedule_every, ctx:cancel_timer,
│                           ctx:schedule_next_frame on the GameContext userdata metatable
```

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 1** | Full timer API (`schedule_after`, `schedule_every`, `cancel_timer`, `schedule_next_frame`), drain loop in `uz_game_update`, error handling, hot-reload documentation |
| **Phase 3** | Physics-frame timers (fire after physics step, not before); integration with EventBus (timer fires emit event) |

---

## Related Docs

- [specs/lua-scripting.md](lua-scripting.md) — LuaJIT VM, scripting lifecycle, `ctx` API
- [specs/scene-system.md](scene-system.md) — Scene lifecycle, scene change clears timers
- [engine/hot-reload.md](../engine/hot-reload.md) — DLL hot-reload sequence, Lua script reload
