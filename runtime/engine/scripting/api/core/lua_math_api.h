#pragma once

/// lua_math_api — Lua bindings for common 3D math operations.
///
/// Registers the `uz.math` sub-table with vector math utilities that are
/// cumbersome to implement in pure Lua and benefit from engine-side SIMD.
///
/// # Functions
/// - `uz.math.normalize(x, y, z)` → nx, ny, nz
/// - `uz.math.dot(x1,y1,z1, x2,y2,z2)` → number
/// - `uz.math.cross(x1,y1,z1, x2,y2,z2)` → x, y, z
/// - `uz.math.length(x, y, z)` → number
/// - `uz.math.distance(x1,y1,z1, x2,y2,z2)` → number
/// - `uz.math.lerp(a, b, t)` → number
/// - `uz.math.lerp3(x1,y1,z1, x2,y2,z2, t)` → x, y, z
/// - `uz.math.clamp(value, min, max)` → number
/// - `uz.math.rad(degrees)` → radians
/// - `uz.math.deg(radians)` → degrees
/// - `uz.math.angle_between(x1,y1,z1, x2,y2,z2)` → degrees

struct lua_State;

namespace uz {

/// Registers the `uz.math` sub-table into the existing `uz` global table.
///
/// Populates `uz.math` with vector math utilities (normalize, dot, cross,
/// length, distance, lerp, clamp, angle conversion) that benefit from
/// engine-side SIMD. Must be called during LuaVM initialization.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterMathAPI(lua_State* L);

} // namespace uz
