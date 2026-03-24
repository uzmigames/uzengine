#pragma once

/// lua_quat_api — Lua bindings for quaternion math (uz.Quat userdata).
///
/// Registers Quat constructors into the existing `uz.math` sub-table and
/// creates the `uz.Quat` metatable for full-featured quaternion objects.
///
/// # Constructors (uz.math.*)
/// - `uz.math.quat(x, y, z, w)` → Quat  (defaults to identity 0,0,0,1)
/// - `uz.math.quat_from_euler(pitch, yaw, roll)` → Quat  (degrees, YXZ order)
/// - `uz.math.quat_from_axis_angle(vec3_ud, angle_deg)` → Quat
/// - `uz.math.quat_identity()` → Quat
///
/// # Constant
/// - `uz.math.QUAT_IDENTITY` → Quat  (0,0,0,1)
///
/// # Metamethods
/// - `__mul(a, b)` — Hamilton product (Quat*Quat) or rotate vector (Quat*Vec3)
/// - `__tostring(q)` — `"Quat(x, y, z, w)"`
/// - `__eq(a, b)` — component-wise equality
///
/// # Methods (q:method())
/// - `q:slerp(q2, t)` → Quat
/// - `q:inverse()` → Quat
/// - `q:normalize()` → Quat
/// - `q:to_euler()` → pitch, yaw, roll  (degrees, 3 return values)
/// - `q:dot(q2)` → number
/// - `q:length()` → number

struct lua_State;

namespace uz {

/// Registers Quat constructors into the existing `uz.math` table and installs
/// the `uz.Quat` metatable with slerp, inverse, normalize, to_euler, dot,
/// length methods and __mul/__eq/__tostring metamethods.
/// Must be called after LuaRegisterMathAPI so that the `uz.math` table
/// already exists on the Lua state.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterQuatAPI(lua_State* L);

} // namespace uz
