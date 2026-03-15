#include "lua_camera_api.h"

#include "log.h"
#include "world.h"
#include "camera_component.h"
#include "vector3.h"
#include "quaternion.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cmath>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

// ─────────────────────────────────────────────────────────────────────────────
// The World* registry key is shared with lua_scene_api.
// Declared here as an extern string literal — both TUs use the same key.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    constexpr const char* kWorldRegistryKey = "uz_world";
} // anonymous namespace

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the World* from the Lua registry. Returns null if not set.
[[nodiscard]] static World* GetCameraWorld(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<World*>(p);
}

/// Finds the first active CameraComponent. Returns nullptr if none found.
///
/// Emits a Warning log when the World is not set. Silently returns nullptr
/// when no active camera exists (callers return nil to Lua in that case).
[[nodiscard]] static CameraComponent* FindActiveCamera(lua_State* L, const char* funcName) {
    World* world = GetCameraWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.camera.{}: World not set", funcName);
        return nullptr;
    }

    for (auto [enttEntity, camera] : world->View<CameraComponent>().each()) {
        if (camera.isActive) {
            return &camera;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// look_at quaternion helper
// ─────────────────────────────────────────────────────────────────────────────

/// Computes a quaternion rotation such that the camera at `from` looks toward `to`.
///
/// Uses a standard look-at derivation without an external Mat4 dependency:
/// builds an orthonormal basis (forward, right, up) and derives the quaternion
/// from the rotation matrix columns.
///
/// Assumes left-handed coordinates (+X right, +Y up, +Z forward) matching
/// the engine's DirectXMath convention. `worldUp` is (0, 1, 0).
///
/// # Edge Case
/// If `from == to` (or nearly so), returns the identity quaternion to avoid
/// division by zero from a zero-length direction vector.
[[nodiscard]] static Quat QuatLookAt(const Vec3& from, const Vec3& to) noexcept {
    // Direction vector (forward) in left-handed space.
    Vec3 fwd = {to.x - from.x, to.y - from.y, to.z - from.z};
    const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (len < 1e-6f) {
        return Quat::Identity();
    }
    fwd = {fwd.x / len, fwd.y / len, fwd.z / len};

    // World up = (0, 1, 0). Degenerate when forward is nearly vertical.
    constexpr Vec3 kWorldUp{0.f, 1.f, 0.f};
    const float upDot = std::abs(fwd.y); // dot(fwd, worldUp)

    Vec3 right;
    Vec3 up;

    if (upDot > 0.9999f) {
        // Forward is nearly parallel to world-up — use world-right as reference.
        constexpr Vec3 kWorldRight{1.f, 0.f, 0.f};
        // up = normalize(worldRight × fwd)  (left-handed cross)
        up = {
            kWorldRight.y * fwd.z - kWorldRight.z * fwd.y,
            kWorldRight.z * fwd.x - kWorldRight.x * fwd.z,
            kWorldRight.x * fwd.y - kWorldRight.y * fwd.x,
        };
        const float upLen = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
        up = {up.x / upLen, up.y / upLen, up.z / upLen};
        // right = fwd × up
        right = {
            fwd.y * up.z - fwd.z * up.y,
            fwd.z * up.x - fwd.x * up.z,
            fwd.x * up.y - fwd.y * up.x,
        };
    } else {
        // right = normalize(worldUp × fwd)
        right = {
            kWorldUp.y * fwd.z - kWorldUp.z * fwd.y,
            kWorldUp.z * fwd.x - kWorldUp.x * fwd.z,
            kWorldUp.x * fwd.y - kWorldUp.y * fwd.x,
        };
        const float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
        right = {right.x / rightLen, right.y / rightLen, right.z / rightLen};
        // up = fwd × right
        up = {
            fwd.y * right.z - fwd.z * right.y,
            fwd.z * right.x - fwd.x * right.z,
            fwd.x * right.y - fwd.y * right.x,
        };
    }

    // Build rotation matrix (row-major, left-handed):
    // [ right.x  right.y  right.z ]
    // [ up.x     up.y     up.z    ]
    // [ fwd.x    fwd.y    fwd.z   ]
    //
    // Convert 3x3 rotation matrix to quaternion via Shepperd's method.
    const float m00 = right.x, m01 = right.y, m02 = right.z;
    const float m10 = up.x,    m11 = up.y,    m12 = up.z;
    const float m20 = fwd.x,   m21 = fwd.y,   m22 = fwd.z;

    const float trace = m00 + m11 + m22;
    float qw, qx, qy, qz;

    if (trace > 0.f) {
        const float s = 0.5f / std::sqrt(trace + 1.f);
        qw = 0.25f / s;
        qx = (m12 - m21) * s;
        qy = (m20 - m02) * s;
        qz = (m01 - m10) * s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = 2.f * std::sqrt(1.f + m00 - m11 - m22);
        qw = (m12 - m21) / s;
        qx = 0.25f * s;
        qy = (m01 + m10) / s;
        qz = (m20 + m02) / s;
    } else if (m11 > m22) {
        const float s = 2.f * std::sqrt(1.f + m11 - m00 - m22);
        qw = (m20 - m02) / s;
        qx = (m01 + m10) / s;
        qy = 0.25f * s;
        qz = (m12 + m21) / s;
    } else {
        const float s = 2.f * std::sqrt(1.f + m22 - m00 - m11);
        qw = (m01 - m10) / s;
        qx = (m20 + m02) / s;
        qy = (m12 + m21) / s;
        qz = 0.25f * s;
    }

    return Quat{qx, qy, qz, qw};
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.camera.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.camera.get_position() -> x, y, z | nil
static int CameraGetPosition(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_position");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    const Vec3& pos = cam->GetPosition();
    lua_pushnumber(L, static_cast<double>(pos.x));
    lua_pushnumber(L, static_cast<double>(pos.y));
    lua_pushnumber(L, static_cast<double>(pos.z));
    return 3;
}

/// uz.camera.set_position(x, y, z)
static int CameraSetPosition(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "set_position");
    if (!cam) return 0;

    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float z = static_cast<float>(luaL_checknumber(L, 3));
    cam->SetPosition({x, y, z});
    return 0;
}

/// uz.camera.look_at(x, y, z)
///
/// Computes the rotation required for the active camera at its current position
/// to look toward the world-space target point (x, y, z). No-op if the camera
/// is coincident with the target (distance < 1e-6).
static int CameraLookAt(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "look_at");
    if (!cam) return 0;

    const float tx = static_cast<float>(luaL_checknumber(L, 1));
    const float ty = static_cast<float>(luaL_checknumber(L, 2));
    const float tz = static_cast<float>(luaL_checknumber(L, 3));

    const Vec3 from = cam->GetPosition();
    const Vec3 to   = {tx, ty, tz};

    cam->SetRotation(QuatLookAt(from, to));
    return 0;
}

/// uz.camera.get_fov() -> degrees | nil
static int CameraGetFov(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_fov");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(cam->fov));
    return 1;
}

/// uz.camera.set_fov(degrees)
///
/// Clamps the FOV to the valid range [1, 179] to avoid degenerate projection matrices.
static int CameraSetFov(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "set_fov");
    if (!cam) return 0;

    const float degrees = static_cast<float>(luaL_checknumber(L, 1));
    if (degrees < 1.f || degrees > 179.f) {
        UZ_LOG(Scripting, Warning,
            "uz.camera.set_fov: FOV {} is out of range [1, 179] — clamping", degrees);
    }
    cam->fov = std::fmax(1.f, std::fmin(179.f, degrees));
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterCameraAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterCameraAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L); // uz.camera = {}

    lua_pushcfunction(L, CameraGetPosition);
    lua_setfield(L, -2, "get_position");

    lua_pushcfunction(L, CameraSetPosition);
    lua_setfield(L, -2, "set_position");

    lua_pushcfunction(L, CameraLookAt);
    lua_setfield(L, -2, "look_at");

    lua_pushcfunction(L, CameraGetFov);
    lua_setfield(L, -2, "get_fov");

    lua_pushcfunction(L, CameraSetFov);
    lua_setfield(L, -2, "set_fov");

    lua_setfield(L, -2, "camera"); // uz.camera = { ... }
    lua_pop(L, 1);                 // pop uz table

    UZ_LOG(Scripting, Info, "uz.camera API registered");
}

} // namespace uz
