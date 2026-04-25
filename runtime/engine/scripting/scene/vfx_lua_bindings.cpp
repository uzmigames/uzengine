/// Lua bindings for the VFX particle system (spawn, parameters, events, LOD, budget).

#include "vfx_lua_bindings.h"

#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/events/vfx_event.h"
#include "engine/vfx/lod/vfx_lod.h"
#include "engine/vfx/budget/vfx_budget.h"
#include "engine/vfx/asset/vfx_composite.h"
#include "engine/vfx/parameters/vfx_parameter_store.h"
#include "engine/logging/log.h"
#include "vector3.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
// Log category
// ─────────────────────────────────────────────────────────────────────────────

// VFX log category — declared here, defined in vfx_system.cpp.
UZ_DECLARE_LOG_CATEGORY(VFX);

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time safety: void* must be at least 64 bits to encode an emitter
// pointer as a Lua integer entity handle.
// ─────────────────────────────────────────────────────────────────────────────

static_assert(sizeof(void*) >= 8,
    "VFX Lua bindings require 64-bit pointers for emitter handle encoding");

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key
// ─────────────────────────────────────────────────────────────────────────────

/// Registry key under which the VFXSystem* lightuserdata is stored.
static constexpr const char* kVFXSystemRegistryKey = "uz_vfx_system";

// ─────────────────────────────────────────────────────────────────────────────
// Emitter handle encoding
//
// Entity handles in this engine are 64-bit values packed as lightuserdata.
// VFX emitters are NOT entities — they are ParticleEmitter* stored as ECS
// components. To give Lua an opaque, stable handle we encode the pointer
// directly as a Lua integer (lua_Integer is 64-bit on LuaJIT).
//
// Using lua_pushinteger / lua_tointeger rather than lightuserdata avoids
// the 64-bit-pointer restriction that affects some lightuserdata checks and
// keeps the value inspectable from Lua (e.g. for debug printing).
//
// The caller (spawn) is responsible for ensuring the emitter lives as long
// as the handle is in use. Stale handles are detected by scanning the
// VFXSystem's active emitter list.
// ─────────────────────────────────────────────────────────────────────────────

/// Encodes a ParticleEmitter* as a lua_Integer for opaque handle storage.
[[nodiscard]] static lua_Integer EmitterToHandle(ParticleEmitter* emitter) noexcept {
    uintptr_t ptr;
    std::memcpy(&ptr, &emitter, sizeof(ptr));
    return static_cast<lua_Integer>(ptr);
}

/// Decodes a lua_Integer back to a ParticleEmitter*.
[[nodiscard]] static ParticleEmitter* HandleToEmitter(lua_Integer handle) noexcept {
    uintptr_t ptr = static_cast<uintptr_t>(handle);
    ParticleEmitter* emitter;
    std::memcpy(&emitter, &ptr, sizeof(emitter));
    return emitter;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the VFXSystem* stored in the Lua registry.
///
/// # Returns
/// Non-null pointer if the system was registered. Null if `RegisterVFXLuaBindings`
/// was never called or `UnregisterVFXLuaBindings` was already called.
[[nodiscard]] static VFXSystem* GetVFXSystem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVFXSystemRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<VFXSystem*>(p);
}

/// Validates that `handle` refers to an emitter that is currently registered
/// in `vfxSystem`. Linear scan over active emitters — acceptable for the
/// typical emitter count (<= a few hundred).
///
/// # Returns
/// The ParticleEmitter* if live, nullptr if the handle is stale or invalid.
[[nodiscard]] static ParticleEmitter* ResolveEmitter(
    VFXSystem* vfxSystem, lua_Integer handle)
{
    if (handle == 0) {
        return nullptr;
    }

    ParticleEmitter* candidate = HandleToEmitter(handle);
    for (ParticleEmitter* active : vfxSystem->GetActiveEmitters()) {
        if (active == candidate) {
            return active;
        }
    }
    return nullptr;  // Handle is stale — emitter was destroyed.
}

/// Reads a Vec3 from a Lua table at stack index `idx`.
///
/// Accepts both array form `{x, y, z}` (keys 1, 2, 3) and named form
/// `{x=..., y=..., z=...}`. Array form is checked first. If neither key
/// style is present for a component, that component defaults to 0.0.
///
/// Does NOT call luaL_error — returns the result regardless so the caller
/// can decide whether to proceed or report an error.
[[nodiscard]] static Vec3 ReadVec3FromTable(lua_State* L, int idx) noexcept {
    Vec3 v{0.0f, 0.0f, 0.0f};

    // Try array form first: {1, 2, 3}
    lua_rawgeti(L, idx, 1);
    lua_rawgeti(L, idx, 2);
    lua_rawgeti(L, idx, 3);

    const bool hasArrayForm = lua_isnumber(L, -3)
                           && lua_isnumber(L, -2)
                           && lua_isnumber(L, -1);

    if (hasArrayForm) {
        v.x = static_cast<float>(lua_tonumber(L, -3));
        v.y = static_cast<float>(lua_tonumber(L, -2));
        v.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 3);
        return v;
    }
    lua_pop(L, 3);

    // Fall back to named form: {x=..., y=..., z=...}
    lua_getfield(L, idx, "x");
    lua_getfield(L, idx, "y");
    lua_getfield(L, idx, "z");

    if (lua_isnumber(L, -3)) v.x = static_cast<float>(lua_tonumber(L, -3));
    if (lua_isnumber(L, -2)) v.y = static_cast<float>(lua_tonumber(L, -2));
    if (lua_isnumber(L, -1)) v.z = static_cast<float>(lua_tonumber(L, -1));

    lua_pop(L, 3);
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.vfx:*
//
// All functions receive `self` (the uz.vfx table) as argument 1 because they
// are registered with the colon-call syntax. Arguments follow from index 2.
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:spawn(graph_path, position) -> entity | nil
///
/// Locates an already-registered emitter whose `graphPath` matches `graph_path`
/// and whose world position is nearest to `position`, then returns its handle.
///
/// # Design Note
/// Emitters are owned and initialized by C++ systems (e.g., the VFX composite
/// loader triggered by a scene load). The Lua API does not allocate GPU memory
/// — it returns a handle to an existing emitter so scripts can drive its
/// parameters and state.
///
/// If no matching emitter is registered, a warning is logged and `nil` is
/// returned. Scripts should check for nil before calling other methods.
///
/// # Parameters
/// - `graph_path` (string): Asset path of the .uzvfx graph (e.g.,
///   `"effects/explosion.uzvfx"`). Must match the path used when the emitter
///   was registered by the C++ layer.
/// - `position` (table): World-space spawn position `{x, y, z}` or `{1, 2, 3}`.
///   Used to disambiguate when multiple emitters share the same graph path.
///
/// # Returns
/// An opaque integer entity handle on success, or `nil` if no matching emitter
/// is registered.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or no match is found.
static int VFXSpawn(lua_State* L) {
    // Argument 1 is self (uz.vfx table) — skip it.
    const char* graphPath = luaL_checkstring(L, 2);

    if (!lua_istable(L, 3)) {
        return luaL_error(L,
            "uz.vfx:spawn — argument 2 (position) must be a table {x,y,z}");
    }

    const Vec3 position = ReadVec3FromTable(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:spawn — VFXSystem not registered; call RegisterVFXLuaBindings first");
        lua_pushnil(L);
        return 1;
    }

    // Find the initialized emitter whose graphPath matches. When multiple
    // emitters share the same graph path, pick the one closest to `position`.
    ParticleEmitter* bestMatch = nullptr;
    float bestDistSq = std::numeric_limits<float>::max();

    for (ParticleEmitter* emitter : vfxSystem->GetActiveEmitters()) {
        if (!emitter || !emitter->initialized) {
            continue;
        }
        if (emitter->graphPath != graphPath) {
            continue;
        }
        // Disambiguate by distance to the requested spawn position.
        const float dx = emitter->spawnVelocity.x - position.x;
        const float dy = emitter->spawnVelocity.y - position.y;
        const float dz = emitter->spawnVelocity.z - position.z;
        const float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestMatch = emitter;
        }
    }

    if (!bestMatch) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:spawn — no registered emitter found for graph '{}'", graphPath);
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, EmitterToHandle(bestMatch));
    return 1;
}

/// uz.vfx:set_parameter(entity, name, value) -> void
///
/// Sets a named parameter on the emitter's `VFXParameterStore`. The value
/// can be a Lua number (stored as float) or a table `{x,y,z}` (stored as
/// Vec3). The parameter is uploaded to the GPU constant buffer on the next
/// `VFXSystem::Tick()`.
///
/// # Parameters
/// - `entity`  (integer handle): Emitter handle returned by `spawn`.
/// - `name`    (string): Parameter name (e.g., `"intensity"`, `"windDir"`).
/// - `value`   (number OR table {x,y,z}): New parameter value.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered, the handle is stale,
///   the name is empty, or the value type is unsupported.
/// - No-op if the emitter handle is stale (does NOT error — the emitter may
///   have been unregistered mid-frame due to budget reclaim).
static int VFXSetParameter(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* name         = luaL_checkstring(L, 3);

    if (name[0] == '\0') {
        return luaL_error(L, "uz.vfx:set_parameter — parameter name must not be empty");
    }

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_parameter — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_parameter('{}') — stale or invalid emitter handle", name);
        return 0;
    }

    if (lua_isnumber(L, 4)) {
        const float value = static_cast<float>(lua_tonumber(L, 4));
        emitter->parameters.SetFloat(name, value);
    } else if (lua_istable(L, 4)) {
        const Vec3 value = ReadVec3FromTable(L, 4);
        emitter->parameters.SetVec3(name, value);
    } else {
        return luaL_error(L,
            "uz.vfx:set_parameter('%s') — value must be a number or a {x,y,z} table, "
            "got %s", name, lua_typename(L, lua_type(L, 4)));
    }

    return 0;
}

/// uz.vfx:emit_event(entity, name [, count [, velocity_scale]]) -> void
///
/// Enqueues a named burst event on the emitter's `VFXEventQueue`. The event
/// is drained by `VFXSystem::Tick()` and dispatched to the compute shader on
/// the next frame, spawning a burst of particles in a single simulation step.
///
/// # Parameters
/// - `entity`         (integer handle): Emitter handle returned by `spawn`.
/// - `name`           (string): Event name (e.g., `"burst"`, `"explode"`).
/// - `count`          (integer, optional): Number of particles to emit. Defaults to 1.
/// - `velocity_scale` (number, optional): Multiplier for spawn velocity. Defaults to 1.0.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
/// - No-op for stale handles.
static int VFXEmitEvent(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* name         = luaL_checkstring(L, 3);

    const uint32_t emissionCount = static_cast<uint32_t>(
        luaL_optinteger(L, 4, 1));
    const float velocityScale = static_cast<float>(
        luaL_optnumber(L, 5, 1.0));

    if (name[0] == '\0') {
        return luaL_error(L, "uz.vfx:emit_event — event name must not be empty");
    }

    if (emissionCount == 0) {
        return 0;
    }

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:emit_event — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:emit_event('{}') — stale or invalid emitter handle", name);
        return 0;
    }

    emitter->eventQueue.Emit(name, emissionCount, velocityScale);
    return 0;
}

/// uz.vfx:start(entity) -> void
///
/// Sets `emitter->isEmitting = true`, resuming particle spawning. Has no
/// effect if the emitter is already emitting.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXStart(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:start — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:start — stale or invalid emitter handle");
        return 0;
    }

    emitter->isEmitting = true;
    return 0;
}

/// uz.vfx:stop(entity) -> void
///
/// Sets `emitter->isEmitting = false`, halting new particle spawning.
/// Already-alive particles continue to simulate until their lifetime expires.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXStop(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:stop — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:stop — stale or invalid emitter handle");
        return 0;
    }

    emitter->isEmitting = false;
    return 0;
}

/// uz.vfx:pause(entity, paused) -> void
///
/// Sets or clears the pause state of an emitter. When `paused` is true,
/// `emitter->isEmitting` is set to false and `emitter->emissionRate` is
/// preserved in the parameter store so it can be restored on un-pause.
/// When `paused` is false, the saved rate is restored.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `paused` (boolean): `true` to pause, `false` to resume.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
/// - Logs `VFX/Warning` if argument 2 is not a boolean.
static int VFXPause(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    if (!lua_isboolean(L, 3)) {
        return luaL_error(L,
            "uz.vfx:pause — argument 2 (paused) must be a boolean, got %s",
            lua_typename(L, lua_type(L, 3)));
    }
    const bool paused = (lua_toboolean(L, 3) != 0);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:pause — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:pause — stale or invalid emitter handle");
        return 0;
    }

    if (paused) {
        // Save the current emission rate before pausing so resume can restore it.
        // Stored as a named parameter so it survives independently of emissionRate.
        emitter->parameters.SetFloat("__lua_saved_rate", emitter->emissionRate);
        emitter->isEmitting = false;
        emitter->emissionRate = 0.0f;
    } else {
        // Restore emission rate from saved value, then re-enable emission.
        const float savedRate = emitter->parameters.GetFloat("__lua_saved_rate",
                                                             emitter->emissionRate);
        emitter->emissionRate = savedRate;
        emitter->parameters.Remove("__lua_saved_rate");
        emitter->isEmitting = true;
    }

    return 0;
}

/// uz.vfx:freeze(entity, frozen) -> void
///
/// Sets the per-emitter `isPaused` flag. Frozen emitters keep alive
/// particles in place — the sim sees `dt = 0` while the render path
/// continues to draw. Distinct from `pause`, which stops emission AND
/// zeroes `emissionRate`. Use `freeze` for screenshot capture / editor
/// scrub; use `pause` to suspend new spawns while letting existing
/// particles age out naturally.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `frozen` (boolean): `true` to freeze sim, `false` to resume.
static int VFXFreeze(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    if (!lua_isboolean(L, 3)) {
        return luaL_error(L,
            "uz.vfx:freeze — argument 2 (frozen) must be a boolean, got %s",
            lua_typename(L, lua_type(L, 3)));
    }
    const bool frozen = (lua_toboolean(L, 3) != 0);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:freeze — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:freeze — stale or invalid emitter handle");
        return 0;
    }

    emitter->isPaused = frozen;
    return 0;
}

/// uz.vfx:set_time_scale(entity, scale) -> void
///
/// Sets the per-emitter dt multiplier. 1.0 = real-time (default), 0.5 =
/// half-speed, 2.0 = double-speed. Applied AFTER the global
/// `r.VFX.TimeScale` so a sequencer can slow individual emitters
/// against a global slow-mo backdrop. Negative values clamp to 0.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `scale` (number): New time-scale value. Clamped to [0, +inf).
static int VFXSetTimeScale(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const lua_Number  scale  = luaL_checknumber(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_time_scale — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_time_scale — stale or invalid emitter handle");
        return 0;
    }

    const float clamped = (scale > 0.0) ? static_cast<float>(scale) : 0.0f;
    emitter->timeScale = clamped;
    return 0;
}

/// uz.vfx:seek(entity, time_seconds) -> void
///
/// Requests a deterministic re-warm of the emitter to `time_seconds`. The
/// next sim tick resets state and replays the emitter from t=0 to the
/// target via fixed-rate sub-ticks. See `ParticleEmitter::Seek` for the
/// runtime contract.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `time_seconds` (number): Replay duration. Negative -> 0 (full reset).
static int VFXSeek(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const lua_Number  time   = luaL_checknumber(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:seek — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:seek — stale or invalid emitter handle");
        return 0;
    }

    emitter->Seek(static_cast<float>(time));
    return 0;
}

/// uz.vfx:find_by_name(name) -> integer | nil
///
/// Looks up a registered emitter by its `name` field. Returns the integer
/// handle on success, `nil` when no emitter is registered under that name.
///
/// Useful for cinematic / sequencer integrations: the timeline references
/// emitters by stable string id rather than capturing pointers across
/// scene reloads.
static int VFXFindByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 2);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        lua_pushnil(L);
        return 1;
    }

    ParticleEmitter* emitter = vfxSystem->FindEmitterByName(std::string(name));
    if (!emitter) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushinteger(L, EmitterToHandle(emitter));
    return 1;
}

/// uz.vfx:get_alive_count(entity) -> integer
///
/// Returns the CPU-side alive particle count for the emitter. This value is
/// updated once per frame by `VFXSystem::Tick()` from a GPU readback. It may
/// lag the actual GPU count by one frame.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
///
/// # Returns
/// - Non-negative integer particle count on success.
/// - `0` if the system is not registered, the handle is stale, or the emitter
///   is not initialized.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXGetAliveCount(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_alive_count — VFXSystem not registered");
        lua_pushinteger(L, 0);
        return 1;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_alive_count — stale or invalid emitter handle");
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(emitter->cpuAliveCount));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter field access — get_field / set_field
// ─────────────────────────────────────────────────────────────────────────────

/// Pushes a Vec3 as a Lua table with named keys {x, y, z}.
///
/// Leaves the new table on top of the Lua stack.
static void PushVec3AsTable(lua_State* L, const Vec3& v) {
    lua_newtable(L);
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, static_cast<double>(v.z));
    lua_setfield(L, -2, "z");
}

/// Pushes a Vec4 as a Lua table with named keys {x, y, z, w}.
///
/// Leaves the new table on top of the Lua stack.
static void PushVec4AsTable(lua_State* L, const Vec4& v) {
    lua_newtable(L);
    lua_pushnumber(L, static_cast<double>(v.x));
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, static_cast<double>(v.y));
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, static_cast<double>(v.z));
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, static_cast<double>(v.w));
    lua_setfield(L, -2, "w");
}

/// uz.vfx:get_field(entity, field_name) -> value
///
/// Reads a named structural field from the ParticleEmitter. This provides
/// read access to emitter configuration that is not stored in the parameter
/// store (which holds user-defined graph parameters).
///
/// # Parameters
/// - `entity`     (integer handle): Emitter handle returned by `spawn`.
/// - `field_name` (string): Name of the struct field to read.
///
/// # Supported Fields
/// | Field Name              | Return Type     | Emitter Member          |
/// |-------------------------|-----------------|-------------------------|
/// | `"emission_rate"`       | number (float)  | `emissionRate`          |
/// | `"lifetime"`            | number (float)  | `lifetime`              |
/// | `"lifetime_variation"`  | number (float)  | `lifetimeVariation`     |
/// | `"spawn_velocity"`      | table {x,y,z}   | `spawnVelocity`         |
/// | `"velocity_variation"`  | number (float)  | `velocityVariation`     |
/// | `"spread_angle"`        | number (float)  | `spreadAngle`           |
/// | `"gravity"`             | table {x,y,z}   | `gravity`               |
/// | `"damping"`             | number (float)  | `damping`               |
/// | `"priority"`            | integer         | `priority`              |
/// | `"start_delay"`         | number (float)  | `startDelay`            |
/// | `"max_particles"`       | integer         | `maxParticles`          |
/// | `"is_emitting"`         | boolean         | `isEmitting`            |
/// | `"graph_path"`          | string          | `graphPath`             |
/// | `"soft_particle_fade_range"` | number (float) | `softParticleFadeRange` |
/// | `"lod_enabled"`            | boolean         | `lodEnabled`            |
/// | `"lod_level"`              | integer (0-3)   | `currentLodLevel`       |
///
/// # Returns
/// The field value (type depends on the field), or errors if the field name
/// is unknown.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
/// - Errors via `luaL_error` if `field_name` is not recognized.
static int VFXGetEmitterField(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* fieldName    = luaL_checkstring(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_field — VFXSystem not registered");
        lua_pushnil(L);
        return 1;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:get_field('{}') — stale or invalid emitter handle", fieldName);
        lua_pushnil(L);
        return 1;
    }

    if (std::strcmp(fieldName, "emission_rate") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->emissionRate));
    } else if (std::strcmp(fieldName, "lifetime") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->lifetime));
    } else if (std::strcmp(fieldName, "lifetime_variation") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->lifetimeVariation));
    } else if (std::strcmp(fieldName, "spawn_velocity") == 0) {
        PushVec3AsTable(L, emitter->spawnVelocity);
    } else if (std::strcmp(fieldName, "velocity_variation") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->velocityVariation));
    } else if (std::strcmp(fieldName, "spread_angle") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->spreadAngle));
    } else if (std::strcmp(fieldName, "gravity") == 0) {
        PushVec3AsTable(L, emitter->gravity);
    } else if (std::strcmp(fieldName, "damping") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->damping));
    } else if (std::strcmp(fieldName, "priority") == 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(emitter->priority));
    } else if (std::strcmp(fieldName, "start_delay") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->startDelay));
    } else if (std::strcmp(fieldName, "max_particles") == 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(emitter->maxParticles));
    } else if (std::strcmp(fieldName, "is_emitting") == 0) {
        lua_pushboolean(L, emitter->isEmitting ? 1 : 0);
    } else if (std::strcmp(fieldName, "graph_path") == 0) {
        lua_pushstring(L, emitter->graphPath.c_str());
    } else if (std::strcmp(fieldName, "soft_particle_fade_range") == 0) {
        lua_pushnumber(L, static_cast<double>(emitter->softParticleFadeRange));
    } else if (std::strcmp(fieldName, "lod_enabled") == 0) {
        lua_pushboolean(L, emitter->lodEnabled ? 1 : 0);
    } else if (std::strcmp(fieldName, "lod_level") == 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(emitter->currentLodLevel));
    } else {
        return luaL_error(L,
            "uz.vfx:get_field — unknown field name '%s'", fieldName);
    }

    return 1;
}

/// uz.vfx:set_field(entity, field_name, value) -> void
///
/// Sets a named structural field on the ParticleEmitter. This provides
/// write access to emitter configuration fields. The `max_particles` field
/// is read-only because changing it after initialization would require
/// GPU buffer reallocation.
///
/// # Parameters
/// - `entity`     (integer handle): Emitter handle returned by `spawn`.
/// - `field_name` (string): Name of the struct field to write.
/// - `value`      (varies): New value. Type must match the field (see get_field).
///
/// # Read-Only Fields
/// - `"max_particles"`: Cannot be set after initialization (GPU buffer sized at init).
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
/// - Errors via `luaL_error` if `field_name` is unknown or read-only.
static int VFXSetEmitterField(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* fieldName    = luaL_checkstring(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_field — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_field('{}') — stale or invalid emitter handle", fieldName);
        return 0;
    }

    if (std::strcmp(fieldName, "emission_rate") == 0) {
        emitter->emissionRate = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "lifetime") == 0) {
        emitter->lifetime = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "lifetime_variation") == 0) {
        emitter->lifetimeVariation = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "spawn_velocity") == 0) {
        luaL_checktype(L, 4, LUA_TTABLE);
        emitter->spawnVelocity = ReadVec3FromTable(L, 4);
    } else if (std::strcmp(fieldName, "velocity_variation") == 0) {
        emitter->velocityVariation = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "spread_angle") == 0) {
        emitter->spreadAngle = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "gravity") == 0) {
        luaL_checktype(L, 4, LUA_TTABLE);
        emitter->gravity = ReadVec3FromTable(L, 4);
    } else if (std::strcmp(fieldName, "damping") == 0) {
        emitter->damping = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "priority") == 0) {
        const lua_Integer val = luaL_checkinteger(L, 4);
        emitter->priority = static_cast<uint8_t>(val);
    } else if (std::strcmp(fieldName, "start_delay") == 0) {
        emitter->startDelay = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "max_particles") == 0) {
        return luaL_error(L,
            "uz.vfx:set_field — 'max_particles' is read-only "
            "(GPU buffer is sized at initialization)");
    } else if (std::strcmp(fieldName, "is_emitting") == 0) {
        if (!lua_isboolean(L, 4)) {
            return luaL_error(L,
                "uz.vfx:set_field('is_emitting') — value must be a boolean, got %s",
                lua_typename(L, lua_type(L, 4)));
        }
        emitter->isEmitting = (lua_toboolean(L, 4) != 0);
    } else if (std::strcmp(fieldName, "graph_path") == 0) {
        emitter->graphPath = luaL_checkstring(L, 4);
    } else if (std::strcmp(fieldName, "soft_particle_fade_range") == 0) {
        emitter->softParticleFadeRange = static_cast<float>(luaL_checknumber(L, 4));
    } else if (std::strcmp(fieldName, "lod_enabled") == 0) {
        if (!lua_isboolean(L, 4)) {
            return luaL_error(L,
                "uz.vfx:set_field('lod_enabled') — value must be a boolean, got %s",
                lua_typename(L, lua_type(L, 4)));
        }
        emitter->lodEnabled = (lua_toboolean(L, 4) != 0);
    } else if (std::strcmp(fieldName, "lod_level") == 0) {
        return luaL_error(L,
            "uz.vfx:set_field — 'lod_level' is read-only "
            "(set by VFXSystem each frame during LOD evaluation)");
    } else {
        return luaL_error(L,
            "uz.vfx:set_field — unknown field name '%s'", fieldName);
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter store — set_parameter_vec4 / get_parameter
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:set_parameter_vec4(entity, name, {x, y, z, w}) -> void
///
/// Sets a named Vec4 parameter on the emitter's `VFXParameterStore`. The
/// value is read from a Lua table with 4 array elements `{x, y, z, w}`.
/// The parameter is uploaded to the GPU constant buffer on the next
/// `VFXSystem::Tick()`.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `name`   (string): Parameter name (e.g., `"colorOverLife"`, `"uvTiling"`).
/// - `value`  (table {1,2,3,4}): Vec4 value as a 4-element array table.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
/// - No-op for stale handles.
static int VFXSetParameterVec4(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* name         = luaL_checkstring(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);

    if (name[0] == '\0') {
        return luaL_error(L, "uz.vfx:set_parameter_vec4 — parameter name must not be empty");
    }

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_parameter_vec4 — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:set_parameter_vec4('{}') — stale or invalid emitter handle", name);
        return 0;
    }

    // Read {x, y, z, w} from array indices 1..4 in the table.
    lua_rawgeti(L, 4, 1);
    const float x = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_rawgeti(L, 4, 2);
    const float y = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_rawgeti(L, 4, 3);
    const float z = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_rawgeti(L, 4, 4);
    const float w = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);

    emitter->parameters.SetVec4(name, Vec4{x, y, z, w});
    return 0;
}

/// uz.vfx:get_parameter(entity, name) -> number | table | nil
///
/// Reads back a named parameter from the emitter's `VFXParameterStore`.
/// The return type depends on how the parameter was stored:
///
/// - If stored via `set_parameter` with a number: returns a number (float).
/// - If stored via `set_parameter` with a {x,y,z} table: returns a {x,y,z} table.
/// - If stored via `set_parameter_vec4`: returns a {x,y,z,w} table.
/// - If the parameter does not exist: returns `nil`.
///
/// Because all parameters are stored internally as Vec4, this function
/// inspects the .w component to disambiguate: if .w is exactly 0.0 and
/// the parameter exists, it checks .y and .z to determine if it was a
/// scalar (all zero except .x) or a Vec3 (.w == 0). A Vec4 has .w != 0.
/// This heuristic works correctly for typical VFX parameters. If exact
/// type round-tripping is needed, use the typed setters and know the type
/// at the call site.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `name`   (string): Parameter name to query.
///
/// # Returns
/// The parameter value (number, {x,y,z} table, or {x,y,z,w} table), or `nil`.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXGetParameter(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char* name         = luaL_checkstring(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_parameter — VFXSystem not registered");
        lua_pushnil(L);
        return 1;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning,
            "uz.vfx:get_parameter('{}') — stale or invalid emitter handle", name);
        lua_pushnil(L);
        return 1;
    }

    if (!emitter->parameters.Has(name)) {
        lua_pushnil(L);
        return 1;
    }

    // All parameters are stored as Vec4 internally. Inspect components to
    // determine the most appropriate Lua representation.
    const Vec4 v = emitter->parameters.GetVec4(name);

    if (v.w != 0.0f) {
        // Non-zero .w — this is a Vec4 parameter.
        PushVec4AsTable(L, v);
    } else if (v.y != 0.0f || v.z != 0.0f) {
        // Non-zero .y or .z with .w == 0 — this is a Vec3 parameter.
        PushVec3AsTable(L, Vec3{v.x, v.y, v.z});
    } else {
        // Only .x may be non-zero, .y/.z/.w are all 0 — return as scalar float.
        lua_pushnumber(L, static_cast<double>(v.x));
    }

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// LOD control — set_lod_distances / get_lod_level / set_lod_enabled
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:set_lod_distances(entity, d0, d1, d2) -> void
///
/// Sets the per-emitter LOD distance thresholds. The three distances define
/// the transitions: Full→Reduced at `d0`, Reduced→Minimal at `d1`,
/// Minimal→Culled at `d2`. All values must be positive and strictly
/// increasing (`d0 < d1 < d2`).
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
/// - `d0` (number): Full → Reduced threshold (world units).
/// - `d1` (number): Reduced → Minimal threshold (world units).
/// - `d2` (number): Minimal → Culled threshold (world units).
///
/// # Errors
/// Raises a Lua error if the distances are not positive and strictly increasing.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXSetLodDistances(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const float d0 = static_cast<float>(luaL_checknumber(L, 3));
    const float d1 = static_cast<float>(luaL_checknumber(L, 4));
    const float d2 = static_cast<float>(luaL_checknumber(L, 5));

    if (d0 <= 0.0f || d0 >= d1 || d1 >= d2) {
        return luaL_error(L,
            "uz.vfx:set_lod_distances — distances must be positive and strictly "
            "increasing (got %.2f, %.2f, %.2f)",
            static_cast<double>(d0), static_cast<double>(d1), static_cast<double>(d2));
    }

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_lod_distances — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_lod_distances — stale or invalid emitter handle");
        return 0;
    }

    emitter->lodSettings.distances[0] = d0;
    emitter->lodSettings.distances[1] = d1;
    emitter->lodSettings.distances[2] = d2;
    return 0;
}

/// uz.vfx:get_lod_level(entity) -> integer
///
/// Returns the current LOD level for the emitter as an integer:
/// 0 = Full, 1 = Reduced, 2 = Minimal, 3 = Culled.
///
/// The LOD level is updated each frame by VFXSystem::Tick(). It reflects
/// the most recent evaluation and may lag the actual camera position by
/// one frame.
///
/// # Parameters
/// - `entity` (integer handle): Emitter handle returned by `spawn`.
///
/// # Returns
/// Integer LOD level (0-3), or 0 if the system/handle is invalid.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXGetLodLevel(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_lod_level — VFXSystem not registered");
        lua_pushinteger(L, 0);
        return 1;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_lod_level — stale or invalid emitter handle");
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(emitter->currentLodLevel));
    return 1;
}

/// uz.vfx:set_lod_enabled(entity, enabled) -> void
///
/// Enables or disables LOD evaluation for an emitter. When disabled, the
/// emitter always runs at Full quality regardless of camera distance.
///
/// # Parameters
/// - `entity`  (integer handle): Emitter handle returned by `spawn`.
/// - `enabled` (boolean): `true` to enable LOD, `false` to force Full quality.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered or the handle is stale.
static int VFXSetLodEnabled(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);

    if (!lua_isboolean(L, 3)) {
        return luaL_error(L,
            "uz.vfx:set_lod_enabled — argument 2 (enabled) must be a boolean, got %s",
            lua_typename(L, lua_type(L, 3)));
    }
    const bool enabled = (lua_toboolean(L, 3) != 0);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_lod_enabled — VFXSystem not registered");
        return 0;
    }

    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:set_lod_enabled — stale or invalid emitter handle");
        return 0;
    }

    emitter->lodEnabled = enabled;
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Budget queries — get_budget_stats / get_total_alive / get_emitter_count
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:get_budget_stats() -> table
///
/// Returns a table with global particle budget statistics:
/// ```lua
/// { total = N, used = N, available = N, utilization_pct = F }
/// ```
/// - `total`: Maximum particles allowed (from ParticleBudget).
/// - `used`: Particles currently allocated across all emitters.
/// - `available`: Remaining allocation capacity.
/// - `utilization_pct`: Usage ratio as a percentage (0.0 to 100.0).
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered.
static int VFXGetBudgetStats(lua_State* L) {
    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_budget_stats — VFXSystem not registered");
        lua_newtable(L);
        return 1;
    }

    const ParticleBudget& budget = vfxSystem->GetBudget();

    lua_newtable(L);

    lua_pushinteger(L, static_cast<lua_Integer>(budget.GetMax()));
    lua_setfield(L, -2, "total");

    lua_pushinteger(L, static_cast<lua_Integer>(budget.GetUsed()));
    lua_setfield(L, -2, "used");

    lua_pushinteger(L, static_cast<lua_Integer>(budget.GetAvailable()));
    lua_setfield(L, -2, "available");

    lua_pushnumber(L, static_cast<double>(budget.GetUsageRatio() * 100.0f));
    lua_setfield(L, -2, "utilization_pct");

    return 1;
}

/// uz.vfx:get_total_alive() -> integer
///
/// Returns the total number of alive particles across all active emitters.
/// This is a CPU-side value updated each frame by VFXSystem::Tick() and
/// may lag the GPU count by one frame.
///
/// # Returns
/// Non-negative integer particle count.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered.
static int VFXGetTotalAlive(lua_State* L) {
    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_total_alive — VFXSystem not registered");
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(vfxSystem->GetTotalAliveParticles()));
    return 1;
}

/// uz.vfx:get_emitter_count() -> integer
///
/// Returns the number of currently registered (active) emitters.
///
/// # Returns
/// Non-negative integer emitter count.
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered.
static int VFXGetEmitterCount(lua_State* L) {
    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:get_emitter_count — VFXSystem not registered");
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_pushinteger(L, static_cast<lua_Integer>(vfxSystem->GetActiveEmitterCount()));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Composite effects — spawn_composite / stop_composite
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfx:spawn_composite(path, position) -> table of handles
///
/// Loads a .uzvfx_composite file and matches each entry to registered
/// emitters by `graphPath`. For each matched emitter, the `startDelay`
/// is set from the composite entry's delay, and the emitter is activated
/// (`isEmitting = true`).
///
/// Entries that do not match any registered emitter are logged as warnings
/// and skipped.
///
/// # Parameters
/// - `path`     (string): Filesystem path to the .uzvfx_composite file.
/// - `position` (table): World-space origin `{x, y, z}` for offset application.
///   Each matched emitter's `spawnVelocity` origin is set to `position + entry.offset`.
///
/// # Returns
/// A Lua array table of integer emitter handles for all matched entries.
/// The table may be empty if no entries matched.
///
// ─────────────────────────────────────────────────────────────────────────────
// User-declared per-particle attributes (Niagara "Parameters" parity)
// ─────────────────────────────────────────────────────────────────────────────

/// Lua signature: `uz.vfx:declare_user_attr(handle, name, type, default)`
///
/// Registers a user-defined per-particle attribute channel on the emitter
/// identified by `handle`. Allocates the matching GPU structured buffer on
/// success. `type` is the string `"float"` or `"vec4"`; `default` is a
/// number for float or a `{x, y, z, w}` array for vec4.
///
/// Returns the assigned slot index (integer) on success, or `nil` when the
/// emitter is invalid, the quota is full, or the declared type differs from
/// a previous declaration with the same name.
static int VFXDeclareUserAttr(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char*       name   = luaL_checkstring(L, 3);
    const char*       typeStr = luaL_optstring(L, 4, "float");

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:declare_user_attr — VFXSystem not registered");
        lua_pushnil(L);
        return 1;
    }
    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        UZ_LOG(VFX, Warning, "uz.vfx:declare_user_attr — stale or invalid handle");
        lua_pushnil(L);
        return 1;
    }

    UserAttributeDef def;
    def.name = name;
    const std::string type(typeStr);
    if (type == "vec4") {
        def.type = UserAttributeType::Vec4;
        if (lua_istable(L, 5)) {
            auto readIdx = [&](int k) -> float {
                lua_rawgeti(L, 5, k);
                float v = lua_isnumber(L, -1)
                    ? static_cast<float>(lua_tonumber(L, -1)) : 0.0f;
                lua_pop(L, 1);
                return v;
            };
            def.defaultVec4 = Vec4{readIdx(1), readIdx(2), readIdx(3), readIdx(4)};
        }
    } else {
        def.type = UserAttributeType::Float;
        if (lua_isnumber(L, 5)) {
            def.defaultFloat = static_cast<float>(lua_tonumber(L, 5));
        }
    }

    const uint32_t slot = emitter->RegisterUserAttribute(def);
    if (slot == UINT32_MAX) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(slot));
    return 1;
}

/// Lua signature: `uz.vfx:get_user_attr_slot(handle, name)`
///
/// Returns the slot index assigned to a named user attribute, or `nil` when
/// no declaration matches. Useful for modules that need to rebind without
/// re-declaring.
static int VFXGetUserAttrSlot(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    const char*       name   = luaL_checkstring(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        lua_pushnil(L);
        return 1;
    }
    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        lua_pushnil(L);
        return 1;
    }
    const UserAttributeDef* a = emitter->FindUserAttribute(name);
    if (!a || a->slotIndex == UINT32_MAX) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(a->slotIndex));
    return 1;
}

/// Lua signature: `uz.vfx:get_user_attr_count(handle)`
///
/// Returns the number of declared user attribute channels on the emitter,
/// or `0` if the handle is invalid.
static int VFXGetUserAttrCount(lua_State* L) {
    const lua_Integer handle = luaL_checkinteger(L, 2);
    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        lua_pushinteger(L, 0);
        return 1;
    }
    ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
    if (!emitter) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(emitter->userAttributes.size()));
    return 1;
}

/// # Side Effects
/// - Logs `VFX/Warning` for unmatched composite entries.
/// - Logs `VFX/Error` if the composite file fails to load.
/// - Modifies `startDelay`, `timeSinceRegistration`, `isEmitting`, and
///   `spawnVelocity` on matched emitters.
static int VFXSpawnComposite(lua_State* L) {
    const char* path = luaL_checkstring(L, 2);

    if (!lua_istable(L, 3)) {
        return luaL_error(L,
            "uz.vfx:spawn_composite — argument 2 (position) must be a table {x,y,z}");
    }
    const Vec3 position = ReadVec3FromTable(L, 3);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:spawn_composite — VFXSystem not registered");
        lua_newtable(L);
        return 1;
    }

    auto composite = VFXComposite::Load(path);
    if (!composite) {
        UZ_LOG(VFX, Error,
            "uz.vfx:spawn_composite — failed to load composite '{}'", path);
        lua_newtable(L);
        return 1;
    }

    const auto& entries = composite->GetEntries();
    const auto& emitters = vfxSystem->GetActiveEmitters();

    // Result table: array of matched emitter handles.
    lua_newtable(L);
    int resultIdx = 1;

    for (const VFXCompositeEntry& entry : entries) {
        // Find the first registered emitter whose graphPath matches this entry.
        ParticleEmitter* matched = nullptr;
        for (ParticleEmitter* emitter : emitters) {
            if (emitter && emitter->graphPath == entry.graphPath) {
                matched = emitter;
                break;
            }
        }

        if (!matched) {
            UZ_LOG(VFX, Warning,
                "uz.vfx:spawn_composite — no registered emitter for entry '{}' (graph '{}')",
                entry.name, entry.graphPath);
            continue;
        }

        // Configure the emitter for composite playback.
        matched->spawnVelocity = Vec3{
            position.x + entry.offset.x,
            position.y + entry.offset.y,
            position.z + entry.offset.z
        };
        matched->startDelay = entry.delay;
        matched->timeSinceRegistration = 0.0f;
        matched->isEmitting = true;

        lua_pushinteger(L, EmitterToHandle(matched));
        lua_rawseti(L, -2, resultIdx);
        ++resultIdx;
    }

    return 1;
}

/// uz.vfx:stop_composite(handles) -> void
///
/// Stops all emitters referenced by a table of handles. Sets
/// `isEmitting = false` on each resolved emitter, allowing existing
/// particles to die naturally.
///
/// Stale handles in the table are silently skipped (the emitter may
/// have been unregistered since the composite was spawned).
///
/// # Parameters
/// - `handles` (table): Array of integer emitter handles (as returned by
///   `spawn_composite`).
///
/// # Side Effects
/// - Logs `VFX/Warning` if the system is not registered.
static int VFXStopComposite(lua_State* L) {
    luaL_checktype(L, 2, LUA_TTABLE);

    VFXSystem* vfxSystem = GetVFXSystem(L);
    if (!vfxSystem) {
        UZ_LOG(VFX, Warning, "uz.vfx:stop_composite — VFXSystem not registered");
        return 0;
    }

    // Iterate the array table at stack index 2.
    const int tableLen = static_cast<int>(lua_objlen(L, 2));
    for (int i = 1; i <= tableLen; ++i) {
        lua_rawgeti(L, 2, i);
        if (lua_isnumber(L, -1)) {
            const lua_Integer handle = lua_tointeger(L, -1);
            ParticleEmitter* emitter = ResolveEmitter(vfxSystem, handle);
            if (emitter) {
                emitter->isEmitting = false;
            }
        }
        lua_pop(L, 1);
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration / Unregistration
// ─────────────────────────────────────────────────────────────────────────────

void RegisterVFXLuaBindings(lua_State* L, VFXSystem* vfxSystem) {
    if (!L) {
        UZ_LOG(VFX, Error, "RegisterVFXLuaBindings: lua_State is null");
        return;
    }
    if (!vfxSystem) {
        UZ_LOG(VFX, Error, "RegisterVFXLuaBindings: vfxSystem is null");
        return;
    }

    // Store VFXSystem* in the registry for retrieval by C closures.
    lua_pushlightuserdata(L, static_cast<void*>(vfxSystem));
    lua_setfield(L, LUA_REGISTRYINDEX, kVFXSystemRegistryKey);

    // Retrieve the existing `uz` global.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(VFX, Error,
            "RegisterVFXLuaBindings: 'uz' global is not a table — "
            "call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // Build the `uz.vfx` table.
    lua_newtable(L);  // uz.vfx = {}

    lua_pushcfunction(L, VFXSpawn);
    lua_setfield(L, -2, "spawn");

    lua_pushcfunction(L, VFXSetParameter);
    lua_setfield(L, -2, "set_parameter");

    lua_pushcfunction(L, VFXEmitEvent);
    lua_setfield(L, -2, "emit_event");

    lua_pushcfunction(L, VFXStart);
    lua_setfield(L, -2, "start");

    lua_pushcfunction(L, VFXStop);
    lua_setfield(L, -2, "stop");

    lua_pushcfunction(L, VFXPause);
    lua_setfield(L, -2, "pause");

    // Scalability + sequencer bindings (phase16 cvars-sequencer task).
    lua_pushcfunction(L, VFXFreeze);
    lua_setfield(L, -2, "freeze");

    lua_pushcfunction(L, VFXSetTimeScale);
    lua_setfield(L, -2, "set_time_scale");

    lua_pushcfunction(L, VFXSeek);
    lua_setfield(L, -2, "seek");

    lua_pushcfunction(L, VFXFindByName);
    lua_setfield(L, -2, "find_by_name");

    lua_pushcfunction(L, VFXGetAliveCount);
    lua_setfield(L, -2, "get_alive_count");

    lua_pushcfunction(L, VFXGetEmitterField);
    lua_setfield(L, -2, "get_field");

    lua_pushcfunction(L, VFXSetEmitterField);
    lua_setfield(L, -2, "set_field");

    lua_pushcfunction(L, VFXSetParameterVec4);
    lua_setfield(L, -2, "set_parameter_vec4");

    lua_pushcfunction(L, VFXGetParameter);
    lua_setfield(L, -2, "get_parameter");

    lua_pushcfunction(L, VFXSetLodDistances);
    lua_setfield(L, -2, "set_lod_distances");

    lua_pushcfunction(L, VFXGetLodLevel);
    lua_setfield(L, -2, "get_lod_level");

    lua_pushcfunction(L, VFXSetLodEnabled);
    lua_setfield(L, -2, "set_lod_enabled");

    lua_pushcfunction(L, VFXGetBudgetStats);
    lua_setfield(L, -2, "get_budget_stats");

    lua_pushcfunction(L, VFXGetTotalAlive);
    lua_setfield(L, -2, "get_total_alive");

    lua_pushcfunction(L, VFXGetEmitterCount);
    lua_setfield(L, -2, "get_emitter_count");

    lua_pushcfunction(L, VFXSpawnComposite);
    lua_setfield(L, -2, "spawn_composite");

    lua_pushcfunction(L, VFXStopComposite);
    lua_setfield(L, -2, "stop_composite");

    lua_pushcfunction(L, VFXDeclareUserAttr);
    lua_setfield(L, -2, "declare_user_attr");

    lua_pushcfunction(L, VFXGetUserAttrSlot);
    lua_setfield(L, -2, "get_user_attr_slot");

    lua_pushcfunction(L, VFXGetUserAttrCount);
    lua_setfield(L, -2, "get_user_attr_count");

    lua_setfield(L, -2, "vfx");  // uz.vfx = { ... }
    lua_pop(L, 1);               // pop uz table

    UZ_LOG(VFX, Info, "uz.vfx Lua API registered");
}

void UnregisterVFXLuaBindings(lua_State* L) {
    if (!L) {
        return;
    }

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kVFXSystemRegistryKey);

    UZ_LOG(VFX, Info, "uz.vfx Lua API unregistered");
}

} // namespace uz
