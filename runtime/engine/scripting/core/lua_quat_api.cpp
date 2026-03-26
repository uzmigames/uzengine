#include "lua_quat_api.h"

#include "log.h"

/// Lua bindings for quaternion operations (rotation, slerp, from_euler, to_euler).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cmath>

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kQuatPi      = 3.14159265358979323846f;
static constexpr float kQuatDegToRad = kQuatPi / 180.0f;
static constexpr float kQuatRadToDeg = 180.0f / kQuatPi;

// Metatable name for the Quat userdata.
static constexpr const char* kQuatMeta = "uz.Quat";

// Metatable name for Vec3 userdata (defined in lua_vec3_api when it exists).
// Used for the Quat * Vec3 rotation branch.
static constexpr const char* kVec3Meta = "uz.Vec3";

// ─────────────────────────────────────────────────────────────────────────────
// Userdata layout
// ─────────────────────────────────────────────────────────────────────────────

/// Full userdata block allocated on the Lua heap for every Quat value.
/// Hamilton convention: (x, y, z) imaginary, w real.
struct QuatData {
    float x, y, z, w;
};

/// Vec3 userdata layout — must match the definition in lua_vec3_api.cpp when
/// that file exists. Currently the engine only has flat number-returning math
/// helpers, so this struct is defined here defensively for the Quat*Vec3 path.
struct Vec3Data {
    float x, y, z;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Checks that the value at stack index `idx` is a Quat userdata.
/// Raises a Lua argument error if it is not.
///
/// # Parameters
/// - L   — Lua state
/// - idx — stack index (1-based or negative)
///
/// # Returns
/// Pointer to the QuatData block. Never null (Lua error is raised instead).
static QuatData* CheckQuat(lua_State* L, int idx) {
    return static_cast<QuatData*>(luaL_checkudata(L, idx, kQuatMeta));
}

/// Allocates a new Quat userdata on the Lua stack and sets its metatable.
///
/// # Parameters
/// - L, x, y, z, w — Lua state and quaternion components
///
/// # Side effects
/// Pushes one userdata value onto the stack.
static void PushQuat(lua_State* L, float x, float y, float z, float w) {
    auto* q = static_cast<QuatData*>(lua_newuserdata(L, sizeof(QuatData)));
    q->x = x;
    q->y = y;
    q->z = z;
    q->w = w;
    luaL_getmetatable(L, kQuatMeta);
    lua_setmetatable(L, -2);
}

/// Hamilton product of two quaternions: result = a * b.
/// Applies b's rotation first, then a's (standard composition order).
static QuatData QuatMul(const QuatData& a, const QuatData& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

/// Rotates a 3D vector `v` by unit quaternion `q` using the sandwich product:
///   v' = q * pure(v) * q^-1
/// Expanded to avoid constructing intermediate quaternions:
///   t  = 2 * cross(q.xyz, v)
///   v' = v + q.w * t + cross(q.xyz, t)
static Vec3Data QuatRotateVec3(const QuatData& q, const Vec3Data& v) {
    // t = 2 * (q.xyz cross v)
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);
    // v' = v + q.w * t + (q.xyz cross t)
    return {
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

/// Pushes a Vec3 userdata result of a Quat*Vec3 rotation onto the Lua stack.
/// Falls back gracefully: if the `uz.Vec3` metatable has not been registered
/// yet, pushes three separate numbers instead so callers are not stranded.
static void PushVec3OrNumbers(lua_State* L, float x, float y, float z) {
    luaL_getmetatable(L, kVec3Meta);
    if (lua_isnil(L, -1)) {
        // Vec3 metatable not available — return flat numbers for compatibility.
        lua_pop(L, 1);
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
        return;
    }
    lua_pop(L, 1); // pop the metatable; newuserdata will re-fetch it via PushVec3

    auto* vd = static_cast<Vec3Data*>(lua_newuserdata(L, sizeof(Vec3Data)));
    vd->x = x;
    vd->y = y;
    vd->z = z;
    luaL_getmetatable(L, kVec3Meta);
    lua_setmetatable(L, -2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metamethods
// ─────────────────────────────────────────────────────────────────────────────

/// __mul — Hamilton product (Quat * Quat) or vector rotation (Quat * Vec3).
///
/// When the right-hand operand is a `uz.Vec3` userdata the quaternion is used
/// to rotate the vector and the result is a `uz.Vec3` (or 3 numbers if the
/// Vec3 metatable is not yet registered).  Otherwise both operands must be
/// `uz.Quat` and the return value is the composed quaternion.
static int QuatMetaMul(lua_State* L) {
    QuatData* a = CheckQuat(L, 1);

    // Attempt Quat * Vec3 first via non-throwing test.
    Vec3Data* vd = static_cast<Vec3Data*>(luaL_testudata(L, 2, kVec3Meta));
    if (vd) {
        Vec3Data r = QuatRotateVec3(*a, *vd);
        PushVec3OrNumbers(L, r.x, r.y, r.z);
        // PushVec3OrNumbers either pushes 1 or 3 values; return accordingly.
        luaL_getmetatable(L, kVec3Meta);
        const bool hasMeta = !lua_isnil(L, -1);
        lua_pop(L, 1);
        return hasMeta ? 1 : 3;
    }

    // Quat * Quat
    QuatData* b = CheckQuat(L, 2);
    QuatData r  = QuatMul(*a, *b);
    PushQuat(L, r.x, r.y, r.z, r.w);
    return 1;
}

/// __tostring — "Quat(x, y, z, w)" with 4-decimal precision.
static int QuatMetaToString(lua_State* L) {
    QuatData* q = CheckQuat(L, 1);
    lua_pushfstring(L, "Quat(%.4f, %.4f, %.4f, %.4f)", q->x, q->y, q->z, q->w);
    return 1;
}

/// __eq — component-wise equality with exact float comparison.
///
/// Note: for fuzzy equality use q:dot(q2) > threshold instead.
static int QuatMetaEq(lua_State* L) {
    QuatData* a = CheckQuat(L, 1);
    QuatData* b = CheckQuat(L, 2);
    lua_pushboolean(L,
        a->x == b->x && a->y == b->y && a->z == b->z && a->w == b->w);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Methods (q:method())
// ─────────────────────────────────────────────────────────────────────────────

/// q:slerp(q2, t) → Quat
///
/// Spherical linear interpolation between two unit quaternions.
/// t=0 returns q, t=1 returns q2. Falls back to nlerp when quaternions are
/// nearly parallel to avoid division by near-zero sin.
static int QuatMethodSlerp(lua_State* L) {
    QuatData* a = CheckQuat(L, 1);
    QuatData* b = CheckQuat(L, 2);
    const float t = static_cast<float>(luaL_checknumber(L, 3));

    float dot = a->x*b->x + a->y*b->y + a->z*b->z + a->w*b->w;

    // Ensure shortest path: negate b when dot < 0.
    float bx = b->x, by = b->y, bz = b->z, bw = b->w;
    if (dot < 0.0f) {
        dot = -dot;
        bx = -bx; by = -by; bz = -bz; bw = -bw;
    }

    float scale0, scale1;
    if (dot > 0.9995f) {
        // Quaternions very close — linear interpolation to avoid singularity.
        scale0 = 1.0f - t;
        scale1 = t;
    } else {
        const float theta    = std::acos(dot);
        const float sinTheta = std::sin(theta);
        scale0 = std::sin((1.0f - t) * theta) / sinTheta;
        scale1 = std::sin(t * theta)           / sinTheta;
    }

    // Normalise to correct any accumulated drift.
    float rx = scale0 * a->x + scale1 * bx;
    float ry = scale0 * a->y + scale1 * by;
    float rz = scale0 * a->z + scale1 * bz;
    float rw = scale0 * a->w + scale1 * bw;
    const float len = std::sqrt(rx*rx + ry*ry + rz*rz + rw*rw);
    if (len > 1e-10f) {
        const float inv = 1.0f / len;
        rx *= inv; ry *= inv; rz *= inv; rw *= inv;
    }

    PushQuat(L, rx, ry, rz, rw);
    return 1;
}

/// q:inverse() → Quat
///
/// Returns the multiplicative inverse: conjugate divided by squared length.
/// For a unit quaternion this is equivalent to the conjugate (x=-x,y=-y,z=-z).
static int QuatMethodInverse(lua_State* L) {
    QuatData* q = CheckQuat(L, 1);
    const float lenSq = q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w;
    if (lenSq < 1e-20f) {
        // Degenerate zero quaternion — return identity to avoid division by zero.
        PushQuat(L, 0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        const float inv = 1.0f / lenSq;
        PushQuat(L, -q->x * inv, -q->y * inv, -q->z * inv, q->w * inv);
    }
    return 1;
}

/// q:normalize() → Quat
///
/// Returns the unit quaternion with the same orientation.
/// Returns identity when the input has near-zero length.
static int QuatMethodNormalize(lua_State* L) {
    QuatData* q = CheckQuat(L, 1);
    const float len = std::sqrt(q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w);
    if (len < 1e-10f) {
        PushQuat(L, 0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        const float inv = 1.0f / len;
        PushQuat(L, q->x * inv, q->y * inv, q->z * inv, q->w * inv);
    }
    return 1;
}

/// q:to_euler() → pitch, yaw, roll  (degrees)
///
/// Decomposes the quaternion into Euler angles using YXZ intrinsic order
/// (yaw around world-Y, then pitch around local-X, then roll around local-Z).
/// This matches the engine's Quaternion::ToEulerAngles convention used by
/// uz.scene.set_rotation / get_rotation.
///
/// # Returns
/// Three numbers: pitch (X-axis), yaw (Y-axis), roll (Z-axis) — all in degrees.
static int QuatMethodToEuler(lua_State* L) {
    QuatData* q = CheckQuat(L, 1);

    // Rotation matrix elements needed for YXZ decomposition.
    // From the quaternion: R = I + 2w[q]x + 2[q]x^2  (skew-symmetric form)
    const float x = q->x, y = q->y, z = q->z, w = q->w;

    // Singularity test value: sin(pitch) = 2*(w*x - y*z)
    const float sinP = 2.0f * (w * x - y * z);

    float pitch, yaw, roll;

    if (sinP >= 0.9999f) {
        // North pole gimbal lock.
        pitch = kQuatPi * 0.5f;
        yaw   = 2.0f * std::atan2(y, w);
        roll  = 0.0f;
    } else if (sinP <= -0.9999f) {
        // South pole gimbal lock.
        pitch = -kQuatPi * 0.5f;
        yaw   = -2.0f * std::atan2(y, w);
        roll  = 0.0f;
    } else {
        pitch = std::asin(sinP);
        yaw   = std::atan2(2.0f * (w * y + x * z), 1.0f - 2.0f * (x*x + y*y));
        roll  = std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (x*x + z*z));
    }

    lua_pushnumber(L, pitch * kQuatRadToDeg);
    lua_pushnumber(L, yaw   * kQuatRadToDeg);
    lua_pushnumber(L, roll  * kQuatRadToDeg);
    return 3;
}

/// q:dot(q2) → number
///
/// 4D dot product of two quaternions. Equals cos(half-angle) for unit quats,
/// useful for measuring similarity (1 = identical, 0 = orthogonal).
static int QuatMethodDot(lua_State* L) {
    QuatData* a = CheckQuat(L, 1);
    QuatData* b = CheckQuat(L, 2);
    lua_pushnumber(L, a->x*b->x + a->y*b->y + a->z*b->z + a->w*b->w);
    return 1;
}

/// q:length() → number
///
/// Euclidean length of the quaternion 4-vector. Returns 1 for unit quaternions.
static int QuatMethodLength(lua_State* L) {
    QuatData* q = CheckQuat(L, 1);
    lua_pushnumber(L, std::sqrt(q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructors (uz.math.*)
// ─────────────────────────────────────────────────────────────────────────────

/// uz.math.quat(x, y, z, w) → Quat
///
/// Constructs a Quat from explicit components. All arguments are optional and
/// default to identity (0, 0, 0, 1). The caller is responsible for passing a
/// normalised quaternion when rotation semantics are expected.
static int MathQuat(lua_State* L) {
    const float x = lua_isnoneornil(L, 1) ? 0.0f : static_cast<float>(luaL_checknumber(L, 1));
    const float y = lua_isnoneornil(L, 2) ? 0.0f : static_cast<float>(luaL_checknumber(L, 2));
    const float z = lua_isnoneornil(L, 3) ? 0.0f : static_cast<float>(luaL_checknumber(L, 3));
    const float w = lua_isnoneornil(L, 4) ? 1.0f : static_cast<float>(luaL_checknumber(L, 4));
    PushQuat(L, x, y, z, w);
    return 1;
}

/// uz.math.quat_identity() → Quat
///
/// Returns the identity quaternion (0, 0, 0, 1) — no rotation.
static int MathQuatIdentity(lua_State* L) {
    PushQuat(L, 0.0f, 0.0f, 0.0f, 1.0f);
    return 1;
}

/// uz.math.quat_from_euler(pitch, yaw, roll) → Quat
///
/// Constructs a quaternion from Euler angles in degrees using YXZ intrinsic
/// order: yaw (Y) is applied first, then pitch (X), then roll (Z).  This
/// matches the convention used by uz.scene.set_rotation / get_rotation.
///
/// # Parameters
/// - pitch — rotation around the X axis, in degrees
/// - yaw   — rotation around the Y axis, in degrees
/// - roll  — rotation around the Z axis, in degrees
static int MathQuatFromEuler(lua_State* L) {
    const float pitch = static_cast<float>(luaL_checknumber(L, 1)) * kQuatDegToRad;
    const float yaw   = static_cast<float>(luaL_checknumber(L, 2)) * kQuatDegToRad;
    const float roll  = static_cast<float>(luaL_checknumber(L, 3)) * kQuatDegToRad;

    // Half-angles.
    const float hp = pitch * 0.5f;
    const float hy = yaw   * 0.5f;
    const float hr = roll  * 0.5f;

    const float cp = std::cos(hp), sp = std::sin(hp);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cr = std::cos(hr), sr = std::sin(hr);

    // YXZ composition: Q = Qy * Qx * Qz
    PushQuat(L,
        cy * sp * cr + sy * cp * sr,   // x
        sy * cp * cr - cy * sp * sr,   // y
        cy * cp * sr - sy * sp * cr,   // z
        cy * cp * cr + sy * sp * sr    // w
    );
    return 1;
}

/// uz.math.quat_from_axis_angle(vec3_ud, angle_degrees) → Quat
///
/// Constructs a quaternion representing a rotation of `angle_degrees` degrees
/// around the axis given by a `uz.Vec3` userdata. The axis is normalised
/// internally — passing a zero-length axis returns the identity quaternion.
///
/// # Parameters
/// - vec3_ud      — a `uz.Vec3` userdata value representing the rotation axis
/// - angle_degrees — rotation angle in degrees
static int MathQuatFromAxisAngle(lua_State* L) {
    // Accept either a uz.Vec3 userdata or three separate numbers for the axis.
    Vec3Data* vd = static_cast<Vec3Data*>(luaL_testudata(L, 1, kVec3Meta));

    float ax, ay, az;
    float angleDeg;

    if (vd) {
        ax = vd->x; ay = vd->y; az = vd->z;
        angleDeg = static_cast<float>(luaL_checknumber(L, 2));
    } else {
        // Fallback: three separate numbers (ax, ay, az, angle).
        ax       = static_cast<float>(luaL_checknumber(L, 1));
        ay       = static_cast<float>(luaL_checknumber(L, 2));
        az       = static_cast<float>(luaL_checknumber(L, 3));
        angleDeg = static_cast<float>(luaL_checknumber(L, 4));
    }

    // Normalise axis.
    const float len = std::sqrt(ax*ax + ay*ay + az*az);
    if (len < 1e-10f) {
        PushQuat(L, 0.0f, 0.0f, 0.0f, 1.0f); // degenerate axis → identity
        return 1;
    }
    const float inv = 1.0f / len;
    ax *= inv; ay *= inv; az *= inv;

    const float halfAngle = angleDeg * kQuatDegToRad * 0.5f;
    const float s = std::sin(halfAngle);
    const float c = std::cos(halfAngle);

    PushQuat(L, ax * s, ay * s, az * s, c);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Metatable setup
// ─────────────────────────────────────────────────────────────────────────────

/// Installs the `uz.Quat` metatable in the registry and creates an __index
/// table populated with all instance methods.
static void RegisterQuatMetatable(lua_State* L) {
    luaL_newmetatable(L, kQuatMeta); // push metatable

    lua_pushcfunction(L, QuatMetaMul);
    lua_setfield(L, -2, "__mul");

    lua_pushcfunction(L, QuatMetaToString);
    lua_setfield(L, -2, "__tostring");

    lua_pushcfunction(L, QuatMetaEq);
    lua_setfield(L, -2, "__eq");

    // __index table — holds all instance methods.
    lua_newtable(L);

    lua_pushcfunction(L, QuatMethodSlerp);
    lua_setfield(L, -2, "slerp");

    lua_pushcfunction(L, QuatMethodInverse);
    lua_setfield(L, -2, "inverse");

    lua_pushcfunction(L, QuatMethodNormalize);
    lua_setfield(L, -2, "normalize");

    lua_pushcfunction(L, QuatMethodToEuler);
    lua_setfield(L, -2, "to_euler");

    lua_pushcfunction(L, QuatMethodDot);
    lua_setfield(L, -2, "dot");

    lua_pushcfunction(L, QuatMethodLength);
    lua_setfield(L, -2, "length");

    lua_setfield(L, -2, "__index");

    lua_pop(L, 1); // pop metatable (stored in registry)
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration entry point
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterQuatAPI(lua_State* L) {
    // Install the metatable so userdata created by any constructor below
    // can find their methods.
    RegisterQuatMetatable(L);

    // Fetch the existing uz.math table.
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterQuatAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "math"); // push uz.math
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterQuatAPI: 'uz.math' is not a table — call LuaRegisterMathAPI first");
        lua_pop(L, 2); // pop non-table + uz
        return;
    }

    // Constructors.
    lua_pushcfunction(L, MathQuat);
    lua_setfield(L, -2, "quat");

    lua_pushcfunction(L, MathQuatIdentity);
    lua_setfield(L, -2, "quat_identity");

    lua_pushcfunction(L, MathQuatFromEuler);
    lua_setfield(L, -2, "quat_from_euler");

    lua_pushcfunction(L, MathQuatFromAxisAngle);
    lua_setfield(L, -2, "quat_from_axis_angle");

    // uz.math.QUAT_IDENTITY constant.
    PushQuat(L, 0.0f, 0.0f, 0.0f, 1.0f);
    lua_setfield(L, -2, "QUAT_IDENTITY");

    lua_pop(L, 2); // pop uz.math, uz

    UZ_LOG(Scripting, Info, "uz.math Quat API registered (uz.Quat metatable installed)");
}

} // namespace uz
