#include "lua_math_api.h"

#include "log.h"

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
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterMathAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterMathAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

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

    lua_setfield(L, -2, "math");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.math API registered");
}

} // namespace uz
