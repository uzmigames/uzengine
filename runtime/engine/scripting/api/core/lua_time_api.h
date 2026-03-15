#pragma once

/// lua_time_api — Lua bindings for time control.
///
/// Registers the `uz.time` sub-table with functions for querying and
/// controlling time scale, fixed timestep, and frame count.
///
/// # Functions
/// - `uz.time.get_time_scale()` → number
/// - `uz.time.set_time_scale(scale)` → void
/// - `uz.time.get_frame_count()` → integer
/// - `uz.time.get_fixed_dt()` → number
/// - `uz.time.set_fixed_dt(dt)` → void
/// - `uz.time.get_raw_dt()` → number (unscaled wall-clock delta)
/// - `uz.time.pause()` → void (sets time_scale to 0)
/// - `uz.time.unpause()` → void (sets time_scale to 1)
/// - `uz.time.is_paused()` → bool

struct lua_State;

namespace uz {

class TimeManager;

/// Registers the `uz.time` sub-table into the existing `uz` global table.
void LuaRegisterTimeAPI(lua_State* L);

/// Stores a TimeManager pointer in the Lua registry for use by time callbacks.
void LuaSetTimeManager(lua_State* L, TimeManager* timeManager);

} // namespace uz
