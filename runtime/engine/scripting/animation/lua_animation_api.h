#pragma once

/// lua_animation_api — Lua bindings for the AnimationController.
///
/// Registers the `uz.animation` sub-table with functions that operate on an
/// entity carrying a `SkeletalMeshComponent`. The binding resolves the entity
/// to its controller via `SkeletalMeshSubsystem::GetController(controllerId)`.
///
/// # Registered functions
/// - `uz.animation.play(entity, clipName, loop?, speed?)`
/// - `uz.animation.crossfade(entity, clipName, blendTime, loop?, speed?)`
/// - `uz.animation.stop(entity)`
/// - `uz.animation.get_current_clip(entity) -> string`
/// - `uz.animation.get_current_time(entity) -> number`
/// - `uz.animation.set_bool(entity, name, value)`
/// - `uz.animation.set_float(entity, name, value)`
/// - `uz.animation.set_trigger(entity, name)`
/// - `uz.animation.get_bool(entity, name) -> boolean`
/// - `uz.animation.get_float(entity, name) -> number`
///
/// # Entity encoding
/// Entities are packed into Lua lightuserdata using the same layout as
/// `lua_entity_api.cpp` — high 32 bits = id, low 32 bits = generation.
///
/// # Error handling
/// Missing controller, invalid entity, or unknown clip → the call logs a
/// Warning and returns nil/false. No Lua errors are raised.
///
/// # Thread safety
/// Lua runs on the main (game) thread; the AnimationController internally
/// serialises mutations with a mutex, so calls from Lua are always safe.

struct lua_State;

namespace uz {

/// Registers the `uz.animation` Lua sub-table.
///
/// Must run AFTER the base `uz` table exists. Safe to call once per `lua_State`.
void LuaRegisterAnimationAPI(lua_State* L);

} // namespace uz
