#include "lua_camera_api.h"

#include "camera_manager.h"
#include "units.h"
#include "log.h"
#include "world.h"
#include "entity.h"
#include "camera_component.h"
#include "vector3.h"
#include "quaternion.h"
#include "mat4.h"

/// Lua bindings for the camera API (set_position, look_at, field_of_view, aspect_ratio).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cmath>
#include <cstdint>
#include <cstring>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

// ─────────────────────────────────────────────────────────────────────────────
// The World* registry key is shared with lua_scene_api.
// Declared here as an extern string literal — both TUs use the same key.
// ─────────────────────────────────────────────────────────────────────────────

using namespace uz;

namespace {
    constexpr const char* kWorldRegistryKey = "uz_world";

    // ── Entity <-> lightuserdata encoding ────────────────────────────────────
    // Mirrors the same encoding used in lua_scene_api.cpp.
    // High 32 bits = entity.id, low 32 bits = entity.generation.

    static_assert(sizeof(void*) >= 8,
        "uz::Entity requires 64-bit pointers for lightuserdata encoding");

    /// Unpacks an Entity from a Lua lightuserdata void*.
    [[nodiscard]] static uz::Entity UserdataToEntity(void* p) noexcept {
        uint64_t packed;
        std::memcpy(&packed, &p, sizeof(packed));
        uz::Entity e;
        e.id         = static_cast<uint32_t>(packed >> 32);
        e.generation = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
        return e;
    }

    /// Reads an entity lightuserdata from stack index `idx`.
    /// Returns Entity::Invalid() and logs a warning on type mismatch.
    [[nodiscard]] static uz::Entity CheckEntityArg(lua_State* L, int idx, const char* funcName) {
        if (!lua_islightuserdata(L, idx)) {
            UZ_LOG(Scripting, Warning,
                "uz.camera.{}: argument {} must be an entity (lightuserdata), got {}",
                funcName, idx, lua_typename(L, lua_type(L, idx)));
            return uz::Entity::Invalid();
        }
        return UserdataToEntity(lua_touserdata(L, idx));
    }
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
/// Assumes Z-up left-handed coordinates (+X forward, +Y right, +Z up)
/// matching the engine's coordinate system. `worldUp` is (0, 0, 1).
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

    // World up = (0, 0, 1) for Z-up. Degenerate when forward is nearly vertical.
    constexpr Vec3 kWorldUp{0.f, 0.f, 1.f};
    const float upDot = std::abs(fwd.z); // dot(fwd, worldUp)

    Vec3 right;
    Vec3 up;

    if (upDot > 0.9999f) {
        // Forward is nearly parallel to world-up — use world-forward as reference.
        constexpr Vec3 kWorldRight{1.f, 0.f, 0.f}; // X=forward in Z-up
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
/// Sets the camera's look-at target in world-space Z-up coordinates.
/// The renderer will use Camera::LookAt() which handles Z-up → Y-up
/// conversion internally, avoiding quaternion coordinate mismatches.
static int CameraLookAt(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "look_at");
    if (!cam) return 0;

    const float tx = static_cast<float>(luaL_checknumber(L, 1));
    const float ty = static_cast<float>(luaL_checknumber(L, 2));
    const float tz = static_cast<float>(luaL_checknumber(L, 3));

    cam->lookTarget = {tx, ty, tz};
    cam->hasLookTarget = true;
    return 0;
}

/// uz.camera.get_forward() -> x, y, z | nil
///
/// World-space (Z-up) unit forward direction of the active camera, using the
/// same rule the renderer applies in frame_setup.cpp: the look-at target when
/// one is set (scene `look_at` or `uz.camera.look_at`), otherwise the
/// quaternion rotation applied to `Vec3::Forward()`. Lets a controller script
/// start from the scene camera's pose instead of teleporting to a hard-coded
/// one on its first update.
static int CameraGetForward(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_forward");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    Vec3 fwd = cam->hasLookTarget
        ? (cam->lookTarget - cam->GetPosition())
        : (cam->GetRotation() * Vec3::Forward());
    const float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (len > 1e-6f) {
        fwd.x /= len; fwd.y /= len; fwd.z /= len;
    } else {
        fwd = Vec3::Forward();
    }
    lua_pushnumber(L, static_cast<double>(fwd.x));
    lua_pushnumber(L, static_cast<double>(fwd.y));
    lua_pushnumber(L, static_cast<double>(fwd.z));
    return 3;
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
// Lua C functions — clip planes, orthographic, active camera, matrices
// ─────────────────────────────────────────────────────────────────────────────

/// uz.camera.get_near() -> number | nil
///
/// Returns the near clipping plane distance of the active camera. Returns nil
/// if no active camera exists in the world.
static int CameraGetNear(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_near");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(cam->nearPlane));
    return 1;
}

/// uz.camera.set_near(n)
///
/// Sets the near clipping plane of the active camera. Clamped to [0.001, farPlane).
/// Values outside that range are clamped with a Warning log.
///
/// # Arguments
/// * `n` — Near plane distance in world units. Must be > 0.
static int CameraSetNear(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "set_near");
    if (!cam) return 0;

    const float n = static_cast<float>(luaL_checknumber(L, 1));
    constexpr float kMinNear = 0.1f;  // 0.1 cm minimum near plane
    const float maxNear = cam->farPlane - 1e-4f;
    if (n < kMinNear || n >= cam->farPlane) {
        UZ_LOG(Scripting, Warning,
            "uz.camera.set_near: value {} out of range [0.1, farPlane) — clamping", n);
    }
    cam->nearPlane = std::fmax(kMinNear, std::fmin(maxNear, n));
    return 0;
}

/// uz.camera.get_far() -> number | nil
///
/// Returns the far clipping plane distance of the active camera. Returns nil
/// if no active camera exists in the world.
static int CameraGetFar(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_far");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, static_cast<double>(cam->farPlane));
    return 1;
}

/// uz.camera.set_far(f)
///
/// Sets the far clipping plane of the active camera. Clamped to (nearPlane, 100000].
/// Values outside that range are clamped with a Warning log.
///
/// # Arguments
/// * `f` — Far plane distance in world units. Must be > nearPlane.
static int CameraSetFar(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "set_far");
    if (!cam) return 0;

    const float f = static_cast<float>(luaL_checknumber(L, 1));
    constexpr float kMaxFar = uz::Units::kDefaultFarPlaneCm;
    const float minFar = cam->nearPlane + 1e-4f;
    if (f <= cam->nearPlane || f > kMaxFar) {
        UZ_LOG(Scripting, Warning,
            "uz.camera.set_far: value {} out of range (nearPlane, {}] — clamping", f, kMaxFar);
    }
    cam->farPlane = std::fmax(minFar, std::fmin(kMaxFar, f));
    return 0;
}

/// uz.camera.set_orthographic(enabled [, size])
///
/// Switches the active camera between perspective and orthographic projection.
///
/// # Arguments
/// * `enabled` (bool) — true to enable orthographic; false to restore perspective.
/// * `size`    (number, optional) — Orthographic half-height in world units.
///              If omitted the existing `orthoSize` value is unchanged.
static int CameraSetOrthographic(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "set_orthographic");
    if (!cam) return 0;

    if (!lua_isboolean(L, 1)) {
        return luaL_error(L, "uz.camera.set_orthographic: argument 1 must be a boolean");
    }
    cam->isOrtho = lua_toboolean(L, 1) != 0;

    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        const float size = static_cast<float>(luaL_checknumber(L, 2));
        if (size <= 0.f) {
            UZ_LOG(Scripting, Warning,
                "uz.camera.set_orthographic: size {} must be > 0 — ignoring", size);
        } else {
            cam->orthoSize = size;
        }
    }
    return 0;
}

/// uz.camera.is_orthographic() -> bool | nil
///
/// Returns whether the active camera is in orthographic mode.
/// Returns nil if no active camera exists.
static int CameraIsOrthographic(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "is_orthographic");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, cam->isOrtho ? 1 : 0);
    return 1;
}

/// uz.camera.set_active(entity)
///
/// Deactivates all cameras in the world, then activates the CameraComponent
/// on the given entity. A Warning is emitted if the entity has no CameraComponent.
///
/// # Arguments
/// * `entity` (lightuserdata) — Target entity with a CameraComponent.
static int CameraSetActive(lua_State* L) {
    World* world = GetCameraWorld(L);
    if (!world) {
        UZ_LOG(Scripting, Warning, "uz.camera.set_active: World not set");
        return 0;
    }

    const Entity target = CheckEntityArg(L, 1, "set_active");
    if (!target.IsValid() || !world->IsValid(target)) {
        UZ_LOG(Scripting, Warning, "uz.camera.set_active: invalid or stale entity");
        return 0;
    }
    if (!world->HasComponent<CameraComponent>(target)) {
        UZ_LOG(Scripting, Warning,
            "uz.camera.set_active: entity {} has no CameraComponent", target.id);
        return 0;
    }

    // Deactivate all cameras first, then activate the target.
    for (auto [enttEntity, camera] : world->View<CameraComponent>().each()) {
        camera.isActive = false;
    }
    world->GetComponent<CameraComponent>(target).isActive = true;
    return 0;
}

/// uz.camera.get_view_matrix() -> m11, m12, ..., m44 (16 numbers, row-major) | nil
///
/// Returns all 16 elements of the active camera's view matrix as individual Lua numbers
/// in row-major order: _11, _12, _13, _14, _21, _22, ..., _44.
///
/// Returns nil (1 value) if no active camera exists.
static int CameraGetViewMatrix(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_view_matrix");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    const Mat4 mat = cam->GetViewMatrix();
    const float* p = mat.data(); // points to _11, row-major
    for (int i = 0; i < 16; ++i) {
        lua_pushnumber(L, static_cast<double>(p[i]));
    }
    return 16;
}

/// uz.camera.get_projection_matrix() -> m11, m12, ..., m44 (16 numbers, row-major) | nil
///
/// Returns all 16 elements of the active camera's projection matrix as individual
/// Lua numbers in row-major order: _11, _12, _13, _14, _21, _22, ..., _44.
///
/// Returns nil (1 value) if no active camera exists.
static int CameraGetProjectionMatrix(lua_State* L) {
    CameraComponent* cam = FindActiveCamera(L, "get_projection_matrix");
    if (!cam) {
        lua_pushnil(L);
        return 1;
    }
    const Mat4 mat = cam->GetProjectionMatrix();
    const float* p = mat.data();
    for (int i = 0; i < 16; ++i) {
        lua_pushnumber(L, static_cast<double>(p[i]));
    }
    return 16;
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

    lua_pushcfunction(L, CameraGetForward);
    lua_setfield(L, -2, "get_forward");

    lua_pushcfunction(L, CameraGetFov);
    lua_setfield(L, -2, "get_fov");

    lua_pushcfunction(L, CameraSetFov);
    lua_setfield(L, -2, "set_fov");

    lua_pushcfunction(L, CameraGetNear);
    lua_setfield(L, -2, "get_near");

    lua_pushcfunction(L, CameraSetNear);
    lua_setfield(L, -2, "set_near");

    lua_pushcfunction(L, CameraGetFar);
    lua_setfield(L, -2, "get_far");

    lua_pushcfunction(L, CameraSetFar);
    lua_setfield(L, -2, "set_far");

    lua_pushcfunction(L, CameraSetOrthographic);
    lua_setfield(L, -2, "set_orthographic");

    lua_pushcfunction(L, CameraIsOrthographic);
    lua_setfield(L, -2, "is_orthographic");

    lua_pushcfunction(L, CameraSetActive);
    lua_setfield(L, -2, "set_active");

    lua_pushcfunction(L, CameraGetViewMatrix);
    lua_setfield(L, -2, "get_view_matrix");

    lua_pushcfunction(L, CameraGetProjectionMatrix);
    lua_setfield(L, -2, "get_projection_matrix");

    lua_setfield(L, -2, "camera"); // uz.camera = { ... }
    lua_pop(L, 1);                 // pop uz table

    UZ_LOG(Scripting, Info, "uz.camera API registered");
}

// ─────────────────────────────────────────────────────────────────────────────
// CameraManager Lua Bindings
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kCameraManagerKey = "uz_camera_manager";

void LuaSetCameraManager(lua_State* L, CameraManager* mgr) {
    lua_pushlightuserdata(L, mgr);
    lua_setfield(L, LUA_REGISTRYINDEX, kCameraManagerKey);
}

static CameraManager* GetCameraManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kCameraManagerKey);
    auto* mgr = static_cast<CameraManager*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return mgr;
}

/// uz.camera.mgr_create(name) → id
static int CameraMgrCreate(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    const char* name = luaL_checkstring(L, 1);
    CameraId id = mgr->CreateCamera(name);
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    return 1;
}

/// uz.camera.mgr_switch(id) → bool
static int CameraMgrSwitch(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) { lua_pushboolean(L, 0); return 1; }
    CameraId id = static_cast<CameraId>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, mgr->SetActiveCamera(id) ? 1 : 0);
    return 1;
}

/// uz.camera.mgr_list() → table of {id=N, name="..."}
static int CameraMgrList(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) { lua_newtable(L); return 1; }
    lua_newtable(L);
    for (uint32_t i = 0; i < mgr->GetCameraCount(); ++i) {
        lua_newtable(L);
        // Note: CameraManager uses sequential IDs starting from 0.
        // We iterate by index but the ID may differ if cameras were destroyed.
        // For simplicity, expose by index.
        lua_pushinteger(L, static_cast<lua_Integer>(i));
        lua_setfield(L, -2, "index");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

/// uz.camera.mgr_get_active() → id
static int CameraMgrGetActive(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(mgr->GetActiveCameraId()));
    return 1;
}

/// uz.camera.mgr_destroy(id) → bool
static int CameraMgrDestroy(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) { lua_pushboolean(L, 0); return 1; }
    CameraId id = static_cast<CameraId>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, mgr->DestroyCamera(id) ? 1 : 0);
    return 1;
}

/// uz.camera.mgr_set_position(x, y, z) — sets position on the active CameraManager camera directly
static int CameraMgrSetPosition(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) return 0;
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    const float z = static_cast<float>(luaL_checknumber(L, 3));
    mgr->GetActiveCamera().SetPosition({x, y, z});
    return 0;
}

/// uz.camera.mgr_look_at(x, y, z) — sets look-at on the active CameraManager camera directly
static int CameraMgrLookAt(lua_State* L) {
    auto* mgr = GetCameraManager(L);
    if (!mgr) return 0;
    const float tx = static_cast<float>(luaL_checknumber(L, 1));
    const float ty = static_cast<float>(luaL_checknumber(L, 2));
    const float tz = static_cast<float>(luaL_checknumber(L, 3));
    mgr->GetActiveCamera().LookAt({tx, ty, tz}, Vec3::Up());
    return 0;
}

void LuaRegisterCameraManagerAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }

    // Get existing uz.camera table
    lua_getfield(L, -1, "camera");
    if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }

    lua_pushcfunction(L, CameraMgrCreate);
    lua_setfield(L, -2, "mgr_create");

    lua_pushcfunction(L, CameraMgrSwitch);
    lua_setfield(L, -2, "mgr_switch");

    lua_pushcfunction(L, CameraMgrList);
    lua_setfield(L, -2, "mgr_list");

    lua_pushcfunction(L, CameraMgrGetActive);
    lua_setfield(L, -2, "mgr_get_active");

    lua_pushcfunction(L, CameraMgrDestroy);
    lua_setfield(L, -2, "mgr_destroy");

    lua_pushcfunction(L, CameraMgrSetPosition);
    lua_setfield(L, -2, "mgr_set_position");

    lua_pushcfunction(L, CameraMgrLookAt);
    lua_setfield(L, -2, "mgr_look_at");

    lua_pop(L, 2); // pop camera table + uz table

    UZ_LOG(Scripting, Info, "uz.camera manager API registered");
}

} // namespace uz
