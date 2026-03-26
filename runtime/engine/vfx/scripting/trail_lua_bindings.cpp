/// trail_lua_bindings — Implementation of Lua bindings for the trail VFX system.
///
/// All functions are registered on the existing `uz.vfx` table. They use
/// colon-call syntax, so argument 1 is always `self` (the `uz.vfx` table)
/// and user arguments start at index 2.

#include "trail_lua_bindings.h"

#include "engine/vfx/trails/trail_system.h"
#include "engine/vfx/trails/trail_component.h"
#include "engine/logging/log.h"
#include "vector4.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key
// ─────────────────────────────────────────────────────────────────────────────

/// Registry key under which the TrailSystem* lightuserdata is stored.
static constexpr const char* kTrailSystemRegistryKey = "uz_trail_system";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the TrailSystem* stored in the Lua registry.
///
/// # Returns
/// Non-null pointer if the system was registered. Null if
/// `RegisterTrailLuaBindings` was never called or
/// `UnregisterTrailLuaBindings` was already called.
[[nodiscard]] static TrailSystem* GetTrailSystem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kTrailSystemRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<TrailSystem*>(p);
}

/// Reads an optional float field from a Lua table at stack index `tableIdx`.
///
/// If the field exists and is a number, writes to `out` and returns true.
/// Otherwise leaves `out` unchanged and returns false.
static bool ReadOptionalFloat(lua_State* L, int tableIdx, const char* field, float& out) {
    lua_getfield(L, tableIdx, field);
    const bool isNumber = (lua_isnumber(L, -1) != 0);
    if (isNumber) {
        out = static_cast<float>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    return isNumber;
}

/// Reads an optional integer field from a Lua table at stack index `tableIdx`.
///
/// If the field exists and is a number, writes to `out` and returns true.
/// Otherwise leaves `out` unchanged and returns false.
static bool ReadOptionalInt(lua_State* L, int tableIdx, const char* field, int32_t& out) {
    lua_getfield(L, tableIdx, field);
    const bool isNumber = (lua_isnumber(L, -1) != 0);
    if (isNumber) {
        out = static_cast<int32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return isNumber;
}

/// Reads an optional uint32_t field from a Lua table at stack index `tableIdx`.
static bool ReadOptionalUint(lua_State* L, int tableIdx, const char* field, uint32_t& out) {
    lua_getfield(L, tableIdx, field);
    const bool isNumber = (lua_isnumber(L, -1) != 0);
    if (isNumber) {
        out = static_cast<uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return isNumber;
}

/// Reads an optional string field from a Lua table at stack index `tableIdx`.
///
/// If the field exists and is a string, writes to `out` and returns true.
/// Otherwise leaves `out` unchanged and returns false.
static bool ReadOptionalString(lua_State* L, int tableIdx, const char* field, std::string& out) {
    lua_getfield(L, tableIdx, field);
    const bool isString = (lua_isstring(L, -1) != 0);
    if (isString) {
        out = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    return isString;
}

/// Reads a Vec4 from a Lua table at stack index `idx`.
///
/// Accepts both array form `{r, g, b, a}` (keys 1..4) and named form
/// `{x=..., y=..., z=..., w=...}`. Array form is checked first.
/// Missing components default to 0.0.
[[nodiscard]] static Vec4 ReadVec4FromTable(lua_State* L, int idx) noexcept {
    Vec4 v{0.0f, 0.0f, 0.0f, 0.0f};

    // Try array form first: {1, 2, 3, 4}
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    lua_rawgeti(L, idx, 3);
    lua_rawgeti(L, idx, 4);

    const bool hasArrayForm = (lua_isnumber(L, -4) != 0)
                           && (lua_isnumber(L, -3) != 0)
                           && (lua_isnumber(L, -2) != 0)
                           && (lua_isnumber(L, -1) != 0);

    if (hasArrayForm) {
        v.x = static_cast<float>(lua_tonumber(L, -4));
        v.y = static_cast<float>(lua_tonumber(L, -3));
        v.z = static_cast<float>(lua_tonumber(L, -2));
        v.w = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 4);
        return v;
    }
    lua_pop(L, 4);

    // Fall back to named form: {x=..., y=..., z=..., w=...}
    lua_getfield(L, idx, "x");
    lua_getfield(L, idx, "y");
    lua_getfield(L, idx, "z");
    lua_getfield(L, idx, "w");

    if (lua_isnumber(L, -4)) v.x = static_cast<float>(lua_tonumber(L, -4));
    if (lua_isnumber(L, -3)) v.y = static_cast<float>(lua_tonumber(L, -3));
    if (lua_isnumber(L, -2)) v.z = static_cast<float>(lua_tonumber(L, -2));
    if (lua_isnumber(L, -1)) v.w = static_cast<float>(lua_tonumber(L, -1));

    lua_pop(L, 4);
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — Task 3.1: create, destroy, set_active, clear, queries
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:create_trail(config_table) -> handle
///
/// Creates a new trail and registers it with the TrailSystem. Returns the
/// opaque trail handle as a Lua integer.
///
/// # Parameters (via config table, all optional)
/// | Key               | Type   | Default | Description                          |
/// |-------------------|--------|---------|--------------------------------------|
/// | `material_path`   | string | `""`    | Material asset path for the ribbon.  |
/// | `max_points`      | int    | 100     | Ring buffer capacity.                |
/// | `width_start`     | float  | 0.5     | Width at the head (metres).          |
/// | `width_end`       | float  | 0.05    | Width at the tail (metres).          |
/// | `point_lifetime`  | float  | 2.0     | Maximum point age (seconds).         |
/// | `min_point_distance` | float | 0.1   | Minimum distance between points (m). |
/// | `sort_order`      | int    | 0       | Rendering sort priority.             |
///
/// # Returns
/// Trail handle (integer). Never returns `kInvalidHandle`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered.
/// - Logs `VFX/Error` if argument 2 is present but not a table.
static int TrailCreate(lua_State* L) {
    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:create_trail — TrailSystem not registered; "
            "call RegisterTrailLuaBindings first");
        lua_pushinteger(L, static_cast<lua_Integer>(TrailSystem::kInvalidHandle));
        return 1;
    }

    TrailComponent component;

    // If a config table is provided (argument 2, after self), read fields.
    if (lua_istable(L, 2)) {
        ReadOptionalString(L, 2, "material_path", component.materialPath);
        ReadOptionalUint(L, 2, "max_points", component.maxTrailPoints);
        ReadOptionalFloat(L, 2, "width_start", component.widthStart);
        ReadOptionalFloat(L, 2, "width_end", component.widthEnd);
        ReadOptionalFloat(L, 2, "point_lifetime", component.pointLifetime);
        ReadOptionalFloat(L, 2, "min_point_distance", component.minPointDistance);

        int32_t sortOrder = component.sortOrder;
        if (ReadOptionalInt(L, 2, "sort_order", sortOrder)) {
            component.sortOrder = sortOrder;
        }
    } else if (!lua_isnoneornil(L, 2)) {
        return luaL_error(L,
            "uz.vfx:create_trail — argument 1 (config) must be a table or nil, got %s",
            lua_typename(L, lua_type(L, 2)));
    }

    const TrailSystem::TrailHandle handle =
        trailSystem->RegisterTrail(std::move(component));

    lua_pushinteger(L, static_cast<lua_Integer>(handle));
    return 1;
}

/// uz.vfx:destroy_trail(handle) -> void
///
/// Unregisters and destroys the trail with the given handle.
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered.
/// - No-op if the handle is invalid (TrailSystem logs a warning internally).
static int TrailDestroy(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:destroy_trail — TrailSystem not registered");
        return 0;
    }

    trailSystem->UnregisterTrail(handle);
    return 0;
}

/// uz.vfx:set_trail_active(handle, active) -> void
///
/// Sets the `isActive` flag on the trail component. When inactive, the trail
/// stops emitting new points and is not rendered (existing points are not aged).
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
/// - `active` (boolean): `true` to enable, `false` to disable.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailSetActive(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));

    if (!lua_isboolean(L, 3)) {
        return luaL_error(L,
            "uz.vfx:set_trail_active — argument 2 (active) must be a boolean, got %s",
            lua_typename(L, lua_type(L, 3)));
    }
    const bool active = (lua_toboolean(L, 3) != 0);

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_trail_active — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_trail_active — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->isActive = active;
    return 0;
}

/// uz.vfx:clear_trail(handle) -> void
///
/// Resets the trail's point ring buffer to empty, as if the trail was just
/// created. Existing points are discarded immediately.
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailClear(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:clear_trail — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:clear_trail — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->Clear();
    return 0;
}

/// uz.vfx:get_trail_length(handle) -> number
///
/// Returns the total arc-length of the live trail in metres. Returns 0 if
/// the trail is empty or has only one point.
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
///
/// # Returns
/// Arc-length in metres (float), or 0 on error.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailGetLength(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_trail_length — TrailSystem not registered");
        lua_pushnumber(L, 0.0);
        return 1;
    }

    const TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:get_trail_length — invalid or unregistered trail handle {}",
            handle);
        lua_pushnumber(L, 0.0);
        return 1;
    }

    lua_pushnumber(L, static_cast<double>(comp->GetTotalLength()));
    return 1;
}

/// uz.vfx:get_trail_point_count(handle) -> integer
///
/// Returns the number of currently active trail points in the ring buffer.
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
///
/// # Returns
/// Point count (integer), or 0 on error.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailGetPointCount(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_trail_point_count — TrailSystem not registered");
        lua_pushinteger(L, 0);
        return 1;
    }

    const TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:get_trail_point_count — invalid or unregistered trail handle {}",
            handle);
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(comp->GetActivePointCount()));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — Task 3.2: width, color, lifetime, uv_scroll setters
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:set_trail_width(handle, width_start, width_end) -> void
///
/// Sets the ribbon width at the head and tail of the trail.
///
/// # Parameters
/// - `handle`      (integer): Trail handle returned by `create_trail`.
/// - `width_start` (number): Width at the head (newest point) in metres.
/// - `width_end`   (number): Width at the tail (oldest point) in metres.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailSetWidth(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));
    const float widthStart = static_cast<float>(luaL_checknumber(L, 3));
    const float widthEnd   = static_cast<float>(luaL_checknumber(L, 4));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_trail_width — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_trail_width — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->widthStart = widthStart;
    comp->widthEnd   = widthEnd;
    return 0;
}

/// uz.vfx:set_trail_color(handle, color_start, color_end) -> void
///
/// Sets the RGBA colors at the head and tail of the trail. Colors are
/// provided as Lua tables with 4 elements `{r, g, b, a}` or named keys
/// `{x=r, y=g, z=b, w=a}`.
///
/// # Parameters
/// - `handle`      (integer): Trail handle returned by `create_trail`.
/// - `color_start` (table): RGBA color at the head (newest point).
/// - `color_end`   (table): RGBA color at the tail (oldest point).
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailSetColor(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TTABLE);

    const Vec4 colorStart = ReadVec4FromTable(L, 3);
    const Vec4 colorEnd   = ReadVec4FromTable(L, 4);

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_trail_color — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_trail_color — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->colorStart = colorStart;
    comp->colorEnd   = colorEnd;
    return 0;
}

/// uz.vfx:set_trail_lifetime(handle, seconds) -> void
///
/// Sets the maximum lifetime of individual trail points. Points older than
/// this value are culled from the tail each frame.
///
/// # Parameters
/// - `handle`  (integer): Trail handle returned by `create_trail`.
/// - `seconds` (number): Maximum point lifetime in seconds. Must be > 0.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailSetLifetime(lua_State* L) {
    const auto handle  = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));
    const float seconds = static_cast<float>(luaL_checknumber(L, 3));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_trail_lifetime — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_trail_lifetime — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->pointLifetime = seconds;
    return 0;
}

/// uz.vfx:set_trail_uv_scroll(handle, rate) -> void
///
/// Sets the V texture coordinate scroll speed along the ribbon's length.
/// Positive values scroll toward the tail; negative values scroll toward the
/// head. 0 disables scrolling.
///
/// # Parameters
/// - `handle` (integer): Trail handle returned by `create_trail`.
/// - `rate`   (number): UV scroll speed in UV units per second.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the TrailSystem is not registered or handle is invalid.
static int TrailSetUVScroll(lua_State* L) {
    const auto handle = static_cast<TrailSystem::TrailHandle>(luaL_checkinteger(L, 2));
    const float rate  = static_cast<float>(luaL_checknumber(L, 3));

    TrailSystem* trailSystem = GetTrailSystem(L);
    if (!trailSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_trail_uv_scroll — TrailSystem not registered");
        return 0;
    }

    TrailComponent* comp = trailSystem->GetComponent(handle);
    if (!comp) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_trail_uv_scroll — invalid or unregistered trail handle {}",
            handle);
        return 0;
    }

    comp->uvScrollRate = rate;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration / Unregistration
// ─────────────────────────────────────────────────────────────────────────────

void RegisterTrailLuaBindings(lua_State* L, TrailSystem* trailSystem) {
    if (!L) {
        UZ_LOG(VFX, Error, "RegisterTrailLuaBindings: lua_State is null");
        return;
    }
    if (!trailSystem) {
        UZ_LOG(VFX, Error, "RegisterTrailLuaBindings: trailSystem is null");
        return;
    }

    // Store TrailSystem* in the registry for retrieval by C closures.
    lua_pushlightuserdata(L, static_cast<void*>(trailSystem));
    lua_setfield(L, LUA_REGISTRYINDEX, kTrailSystemRegistryKey);

    // Retrieve the existing `uz` global.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(VFX, Error,
            "RegisterTrailLuaBindings: 'uz' global is not a table — "
            "call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Retrieve the existing `uz.vfx` sub-table.
    lua_getfield(L, -1, "vfx");
    if (!lua_istable(L, -1)) {
        UZ_LOG(VFX, Error,
            "RegisterTrailLuaBindings: 'uz.vfx' is not a table — "
            "call RegisterVFXLuaBindings first");
        lua_pop(L, 2);  // pop vfx + uz
        return;
    }

    // Task 3.1 — lifecycle and queries
    lua_pushcfunction(L, TrailCreate);
    lua_setfield(L, -2, "create_trail");

    lua_pushcfunction(L, TrailDestroy);
    lua_setfield(L, -2, "destroy_trail");

    lua_pushcfunction(L, TrailSetActive);
    lua_setfield(L, -2, "set_trail_active");

    lua_pushcfunction(L, TrailClear);
    lua_setfield(L, -2, "clear_trail");

    lua_pushcfunction(L, TrailGetLength);
    lua_setfield(L, -2, "get_trail_length");

    lua_pushcfunction(L, TrailGetPointCount);
    lua_setfield(L, -2, "get_trail_point_count");

    // Task 3.2 — property setters
    lua_pushcfunction(L, TrailSetWidth);
    lua_setfield(L, -2, "set_trail_width");

    lua_pushcfunction(L, TrailSetColor);
    lua_setfield(L, -2, "set_trail_color");

    lua_pushcfunction(L, TrailSetLifetime);
    lua_setfield(L, -2, "set_trail_lifetime");

    lua_pushcfunction(L, TrailSetUVScroll);
    lua_setfield(L, -2, "set_trail_uv_scroll");

    lua_pop(L, 2);  // pop vfx + uz

    UZ_LOG(VFX, Info, "uz.vfx trail Lua API registered (10 functions)");
}

void UnregisterTrailLuaBindings(lua_State* L) {
    if (!L) {
        return;
    }

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kTrailSystemRegistryKey);

    UZ_LOG(VFX, Info, "uz.vfx trail Lua API unregistered");
}

} // namespace uz
