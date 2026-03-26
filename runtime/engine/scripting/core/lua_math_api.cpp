#include "lua_math_api.h"

#include "log.h"

/// Lua bindings for math utilities (vec3 and related vector operations).

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

static constexpr double kPi      = 3.14159265358979323846;
static constexpr double kDegToRad = kPi / 180.0;
static constexpr double kRadToDeg = 180.0 / kPi;

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.math.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.math.normalize(x, y, z) → nx, ny, nz
///
/// Returns the unit vector. If the input is zero-length, returns (0, 0, 0).
static int MathNormalize(lua_State* L) {
    const double x = luaL_checknumber(L, 1);
    const double y = luaL_checknumber(L, 2);
    const double z = luaL_checknumber(L, 3);
    const double len = std::sqrt(x*x + y*y + z*z);
    if (len < 1e-12) {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
    } else {
        const double inv = 1.0 / len;
        lua_pushnumber(L, x * inv);
        lua_pushnumber(L, y * inv);
        lua_pushnumber(L, z * inv);
    }
    return 3;
}

/// uz.math.dot(x1,y1,z1, x2,y2,z2) → number
static int MathDot(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1);
    const double y1 = luaL_checknumber(L, 2);
    const double z1 = luaL_checknumber(L, 3);
    const double x2 = luaL_checknumber(L, 4);
    const double y2 = luaL_checknumber(L, 5);
    const double z2 = luaL_checknumber(L, 6);
    lua_pushnumber(L, x1*x2 + y1*y2 + z1*z2);
    return 1;
}

/// uz.math.cross(x1,y1,z1, x2,y2,z2) → x, y, z
static int MathCross(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1);
    const double y1 = luaL_checknumber(L, 2);
    const double z1 = luaL_checknumber(L, 3);
    const double x2 = luaL_checknumber(L, 4);
    const double y2 = luaL_checknumber(L, 5);
    const double z2 = luaL_checknumber(L, 6);
    lua_pushnumber(L, y1*z2 - z1*y2);
    lua_pushnumber(L, z1*x2 - x1*z2);
    lua_pushnumber(L, x1*y2 - y1*x2);
    return 3;
}

/// uz.math.length(x, y, z) → number
static int MathLength(lua_State* L) {
    const double x = luaL_checknumber(L, 1);
    const double y = luaL_checknumber(L, 2);
    const double z = luaL_checknumber(L, 3);
    lua_pushnumber(L, std::sqrt(x*x + y*y + z*z));
    return 1;
}

/// uz.math.distance(x1,y1,z1, x2,y2,z2) → number
static int MathDistance(lua_State* L) {
    const double dx = luaL_checknumber(L, 1) - luaL_checknumber(L, 4);
    const double dy = luaL_checknumber(L, 2) - luaL_checknumber(L, 5);
    const double dz = luaL_checknumber(L, 3) - luaL_checknumber(L, 6);
    lua_pushnumber(L, std::sqrt(dx*dx + dy*dy + dz*dz));
    return 1;
}

/// uz.math.lerp(a, b, t) → number
///
/// Linear interpolation: a + (b - a) * t. Not clamped.
static int MathLerp(lua_State* L) {
    const double a = luaL_checknumber(L, 1);
    const double b = luaL_checknumber(L, 2);
    const double t = luaL_checknumber(L, 3);
    lua_pushnumber(L, a + (b - a) * t);
    return 1;
}

/// uz.math.lerp3(x1,y1,z1, x2,y2,z2, t) → x, y, z
///
/// Component-wise lerp of two 3D vectors.
static int MathLerp3(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1);
    const double y1 = luaL_checknumber(L, 2);
    const double z1 = luaL_checknumber(L, 3);
    const double x2 = luaL_checknumber(L, 4);
    const double y2 = luaL_checknumber(L, 5);
    const double z2 = luaL_checknumber(L, 6);
    const double t  = luaL_checknumber(L, 7);
    lua_pushnumber(L, x1 + (x2 - x1) * t);
    lua_pushnumber(L, y1 + (y2 - y1) * t);
    lua_pushnumber(L, z1 + (z2 - z1) * t);
    return 3;
}

/// uz.math.clamp(value, min, max) → number
static int MathClamp(lua_State* L) {
    const double value = luaL_checknumber(L, 1);
    const double lo    = luaL_checknumber(L, 2);
    const double hi    = luaL_checknumber(L, 3);
    lua_pushnumber(L, value < lo ? lo : (value > hi ? hi : value));
    return 1;
}

/// uz.math.rad(degrees) → radians
static int MathRad(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * kDegToRad);
    return 1;
}

/// uz.math.deg(radians) → degrees
static int MathDeg(lua_State* L) {
    lua_pushnumber(L, luaL_checknumber(L, 1) * kRadToDeg);
    return 1;
}

/// uz.math.angle_between(x1,y1,z1, x2,y2,z2) → degrees
///
/// Returns the angle between two 3D vectors in degrees.
/// Returns 0 if either vector is zero-length.
static int MathAngleBetween(lua_State* L) {
    const double x1 = luaL_checknumber(L, 1);
    const double y1 = luaL_checknumber(L, 2);
    const double z1 = luaL_checknumber(L, 3);
    const double x2 = luaL_checknumber(L, 4);
    const double y2 = luaL_checknumber(L, 5);
    const double z2 = luaL_checknumber(L, 6);

    const double len1 = std::sqrt(x1*x1 + y1*y1 + z1*z1);
    const double len2 = std::sqrt(x2*x2 + y2*y2 + z2*z2);
    if (len1 < 1e-12 || len2 < 1e-12) {
        lua_pushnumber(L, 0.0);
        return 1;
    }

    double cosAngle = (x1*x2 + y1*y2 + z1*z2) / (len1 * len2);
    // Clamp to [-1, 1] to avoid NaN from acos
    if (cosAngle > 1.0) cosAngle = 1.0;
    if (cosAngle < -1.0) cosAngle = -1.0;

    lua_pushnumber(L, std::acos(cosAngle) * kRadToDeg);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 userdata — full metatable type stored as { float x, y, z }
// ─────────────────────────────────────────────────────────────────────────────

/// Raw POD stored inside the Lua full userdata block.
struct Vec3Data {
    float x, y, z;
};

static constexpr const char* kVec3Meta = "uz.Vec3";
static constexpr float kVec3Epsilon    = 1e-7f;

/// Pushes a new Vec3 userdata onto the stack with the "uz.Vec3" metatable.
static void PushVec3(lua_State* L, float x, float y, float z) {
    auto* v = static_cast<Vec3Data*>(lua_newuserdata(L, sizeof(Vec3Data)));
    v->x = x;
    v->y = y;
    v->z = z;
    luaL_getmetatable(L, kVec3Meta);
    lua_setmetatable(L, -2);
}

/// Returns the Vec3Data* at stack index idx, raising a Lua error if wrong type.
static Vec3Data* CheckVec3(lua_State* L, int idx) {
    return static_cast<Vec3Data*>(luaL_checkudata(L, idx, kVec3Meta));
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 metamethods
// ─────────────────────────────────────────────────────────────────────────────

/// uz.math.vec3(x, y, z) — constructor.
/// 0 args → (0,0,0), 1 arg → (x,x,x), 3 args → (x,y,z).
static int Vec3New(lua_State* L) {
    const int argc = lua_gettop(L);
    if (argc == 0) {
        PushVec3(L, 0.f, 0.f, 0.f);
    } else if (argc == 1) {
        const float s = static_cast<float>(luaL_checknumber(L, 1));
        PushVec3(L, s, s, s);
    } else {
        const float x = static_cast<float>(luaL_checknumber(L, 1));
        const float y = static_cast<float>(luaL_checknumber(L, 2));
        const float z = static_cast<float>(luaL_checknumber(L, 3));
        PushVec3(L, x, y, z);
    }
    return 1;
}

/// __add(a, b) — component-wise addition.
static int Vec3Add(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    PushVec3(L, a->x + b->x, a->y + b->y, a->z + b->z);
    return 1;
}

/// __sub(a, b) — component-wise subtraction.
static int Vec3Sub(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    PushVec3(L, a->x - b->x, a->y - b->y, a->z - b->z);
    return 1;
}

/// __mul — vec*number, number*vec, or component-wise vec*vec.
static int Vec3Mul(lua_State* L) {
    const bool aIsVec = lua_isuserdata(L, 1);
    const bool bIsVec = lua_isuserdata(L, 2);

    if (aIsVec && bIsVec) {
        // Component-wise product
        const Vec3Data* a = CheckVec3(L, 1);
        const Vec3Data* b = CheckVec3(L, 2);
        PushVec3(L, a->x * b->x, a->y * b->y, a->z * b->z);
    } else if (aIsVec) {
        const Vec3Data* a = CheckVec3(L, 1);
        const float s = static_cast<float>(luaL_checknumber(L, 2));
        PushVec3(L, a->x * s, a->y * s, a->z * s);
    } else {
        // number * vec
        const float s = static_cast<float>(luaL_checknumber(L, 1));
        const Vec3Data* b = CheckVec3(L, 2);
        PushVec3(L, s * b->x, s * b->y, s * b->z);
    }
    return 1;
}

/// __div(v, n) — divide each component by scalar n.
static int Vec3Div(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    const float s = static_cast<float>(luaL_checknumber(L, 2));
    if (s == 0.f) {
        return luaL_error(L, "Vec3 division by zero");
    }
    const float inv = 1.f / s;
    PushVec3(L, v->x * inv, v->y * inv, v->z * inv);
    return 1;
}

/// __unm(v) — negate all components.
static int Vec3Unm(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    PushVec3(L, -v->x, -v->y, -v->z);
    return 1;
}

/// __len(v) — returns the magnitude (Euclidean length).
static int Vec3Len(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(sqrtf(v->x*v->x + v->y*v->y + v->z*v->z)));
    return 1;
}

/// __tostring(v) — "Vec3(x, y, z)".
static int Vec3ToString(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    lua_pushfstring(L, "Vec3(%f, %f, %f)", v->x, v->y, v->z);
    return 1;
}

/// __eq(a, b) — component-wise equality.
static int Vec3Eq(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    lua_pushboolean(L, a->x == b->x && a->y == b->y && a->z == b->z);
    return 1;
}

/// __index — resolves "x"/"y"/"z" field access and method lookup.
///
/// Field reads return the raw float. Method lookups fall through to the method
/// table stored at upvalue 1 so all methods defined below remain accessible.
static int Vec3Index(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    const char* key = luaL_checkstring(L, 2);

    if (key[1] == '\0') {
        if (key[0] == 'x') { lua_pushnumber(L, v->x); return 1; }
        if (key[0] == 'y') { lua_pushnumber(L, v->y); return 1; }
        if (key[0] == 'z') { lua_pushnumber(L, v->z); return 1; }
    }

    // Fall through to method table stored at upvalue 1
    lua_pushvalue(L, 2);
    lua_gettable(L, lua_upvalueindex(1));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 methods (called as v:method(...))
// ─────────────────────────────────────────────────────────────────────────────

/// v:dot(v2) → number — scalar dot product.
static int Vec3MethodDot(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    lua_pushnumber(L, static_cast<lua_Number>(a->x*b->x + a->y*b->y + a->z*b->z));
    return 1;
}

/// v:cross(v2) → Vec3 — right-hand cross product.
static int Vec3MethodCross(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    PushVec3(L,
        a->y*b->z - a->z*b->y,
        a->z*b->x - a->x*b->z,
        a->x*b->y - a->y*b->x);
    return 1;
}

/// v:normalize() → Vec3 — unit vector. Returns zero vec if length < epsilon.
static int Vec3MethodNormalize(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    const float len = sqrtf(v->x*v->x + v->y*v->y + v->z*v->z);
    if (len < kVec3Epsilon) {
        PushVec3(L, 0.f, 0.f, 0.f);
    } else {
        const float inv = 1.f / len;
        PushVec3(L, v->x * inv, v->y * inv, v->z * inv);
    }
    return 1;
}

/// v:length() → number — Euclidean length.
static int Vec3MethodLength(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(sqrtf(v->x*v->x + v->y*v->y + v->z*v->z)));
    return 1;
}

/// v:length_squared() → number — squared length, avoids sqrtf.
static int Vec3MethodLengthSquared(lua_State* L) {
    const Vec3Data* v = CheckVec3(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(v->x*v->x + v->y*v->y + v->z*v->z));
    return 1;
}

/// v:distance(v2) → number — Euclidean distance between two points.
static int Vec3MethodDistance(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    const float dx = a->x - b->x;
    const float dy = a->y - b->y;
    const float dz = a->z - b->z;
    lua_pushnumber(L, static_cast<lua_Number>(sqrtf(dx*dx + dy*dy + dz*dz)));
    return 1;
}

/// v:lerp(v2, t) → Vec3 — component-wise linear interpolation. Not clamped.
static int Vec3MethodLerp(lua_State* L) {
    const Vec3Data* a = CheckVec3(L, 1);
    const Vec3Data* b = CheckVec3(L, 2);
    const float t = static_cast<float>(luaL_checknumber(L, 3));
    PushVec3(L,
        a->x + (b->x - a->x) * t,
        a->y + (b->y - a->y) * t,
        a->z + (b->z - a->z) * t);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec3 metatable registration
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the "uz.Vec3" metatable and pushes `vec3` constructor + constants
/// onto the uz.math table (which must be at stack top when called).
static void RegisterVec3Metatable(lua_State* L) {
    // Build method table (shared; stored as upvalue for __index)
    lua_newtable(L);                                  // [methods]
    lua_pushcfunction(L, Vec3MethodDot);
    lua_setfield(L, -2, "dot");
    lua_pushcfunction(L, Vec3MethodCross);
    lua_setfield(L, -2, "cross");
    lua_pushcfunction(L, Vec3MethodNormalize);
    lua_setfield(L, -2, "normalize");
    lua_pushcfunction(L, Vec3MethodLength);
    lua_setfield(L, -2, "length");
    lua_pushcfunction(L, Vec3MethodLengthSquared);
    lua_setfield(L, -2, "length_squared");
    lua_pushcfunction(L, Vec3MethodDistance);
    lua_setfield(L, -2, "distance");
    lua_pushcfunction(L, Vec3MethodLerp);
    lua_setfield(L, -2, "lerp");
    // methods table is at top                        // [methods]

    // Create metatable "uz.Vec3"
    luaL_newmetatable(L, kVec3Meta);                  // [methods, mt]

    lua_pushcfunction(L, Vec3Add);
    lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, Vec3Sub);
    lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, Vec3Mul);
    lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, Vec3Div);
    lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, Vec3Unm);
    lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, Vec3Len);
    lua_setfield(L, -2, "__len");
    lua_pushcfunction(L, Vec3ToString);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, Vec3Eq);
    lua_setfield(L, -2, "__eq");

    // __index closure: captures the method table as upvalue 1
    lua_pushvalue(L, -2);                             // dup methods table
    lua_pushcclosure(L, Vec3Index, 1);                // closure(methods)
    lua_setfield(L, -2, "__index");                   // mt.__index = closure

    lua_pop(L, 1);                                    // pop metatable; [methods]
    lua_pop(L, 1);                                    // pop methods table; []
}

/// Pushes Vec3 constants onto the uz.math table (which must be at stack -1).
static void RegisterVec3Constants(lua_State* L) {
    PushVec3(L, 0.f, 0.f, 0.f); lua_setfield(L, -2, "VEC3_ZERO");
    PushVec3(L, 1.f, 1.f, 1.f); lua_setfield(L, -2, "VEC3_ONE");
    PushVec3(L, 0.f, 1.f, 0.f); lua_setfield(L, -2, "VEC3_UP");
    PushVec3(L, 1.f, 0.f, 0.f); lua_setfield(L, -2, "VEC3_RIGHT");
    PushVec3(L, 0.f, 0.f, 1.f); lua_setfield(L, -2, "VEC3_FORWARD");
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterMathAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterMathAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    // Register the Vec3 metatable into the Lua registry first so that
    // PushVec3 (used by RegisterVec3Constants below) can find it.
    RegisterVec3Metatable(L);

    lua_newtable(L);

    lua_pushcfunction(L, MathNormalize);
    lua_setfield(L, -2, "normalize");

    lua_pushcfunction(L, MathDot);
    lua_setfield(L, -2, "dot");

    lua_pushcfunction(L, MathCross);
    lua_setfield(L, -2, "cross");

    lua_pushcfunction(L, MathLength);
    lua_setfield(L, -2, "length");

    lua_pushcfunction(L, MathDistance);
    lua_setfield(L, -2, "distance");

    lua_pushcfunction(L, MathLerp);
    lua_setfield(L, -2, "lerp");

    lua_pushcfunction(L, MathLerp3);
    lua_setfield(L, -2, "lerp3");

    lua_pushcfunction(L, MathClamp);
    lua_setfield(L, -2, "clamp");

    lua_pushcfunction(L, MathRad);
    lua_setfield(L, -2, "rad");

    lua_pushcfunction(L, MathDeg);
    lua_setfield(L, -2, "deg");

    lua_pushcfunction(L, MathAngleBetween);
    lua_setfield(L, -2, "angle_between");

    // Vec3 constructor — uz.math.vec3(...)
    lua_pushcfunction(L, Vec3New);
    lua_setfield(L, -2, "vec3");

    // Vec3 constants — uz.math.VEC3_*
    RegisterVec3Constants(L);

    lua_setfield(L, -2, "math");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.math API registered (Vec3 included)");
}

} // namespace uz
