#include "lua_vfs_api.h"

#include "log.h"
#include "engine/asset/vfs.h"

#include <optional>
#include <string>
#include <vector>

/// Lua bindings for virtual filesystem API (file read, write, directory queries).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key — must match lua_scene_api.cpp
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kVfsRegistryKey = "uz_vfs";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] static IVFS* GetVFS(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kVfsRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<IVFS*>(p);
}

/// Reads argument 1 as a script-supplied VFS path and checks containment.
///
/// Scripts are untrusted content, so the path must be a logical path below
/// the VFS root (`NormalizeVfsPath`) whatever backend is mounted. The full
/// Lua length is kept so an embedded NUL is rejected instead of silently
/// truncating the path at the C boundary. Raises a Lua argument error if
/// argument 1 is not a string.
///
/// # Returns
/// The path exactly as given (backends do their own normalization), or
/// `std::nullopt` after logging a warning when it is rejected.
[[nodiscard]] static std::optional<std::string> CheckScriptPath(lua_State* L, const char* function) {
    size_t length = 0;
    const char* data = luaL_checklstring(L, 1, &length);
    std::string path(data, length);
    if (!NormalizeVfsPath(path)) {
        UZ_LOG(Scripting, Warning,
               "uz.vfs.{}: rejected path outside the VFS root (absolute, drive, NUL or '..' escape)",
               function);
        return std::nullopt;
    }
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.vfs.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.vfs.read(path) -> string (raw bytes) or nil
///
/// Reads an asset as raw binary bytes. The returned Lua string may contain
/// embedded null bytes (uses lua_pushlstring for binary safety).
/// Returns nil if the asset does not exist, the path leaves the VFS root,
/// or VFS is not set.
static int VfsRead(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushnil(L);
        return 1;
    }

    const std::optional<std::string> path = CheckScriptPath(L, "read");
    if (!path) {
        lua_pushnil(L);
        return 1;
    }
    std::vector<uint8_t> data = vfs->Read(*path);
    if (data.empty()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlstring(L, reinterpret_cast<const char*>(data.data()), data.size());
    return 1;
}

/// uz.vfs.read_text(path) -> string (UTF-8 text) or nil
///
/// Reads an asset as a text string. Identical to read() but uses
/// lua_pushstring which expects null-terminated UTF-8 data.
/// Returns nil if the asset does not exist, the path leaves the VFS root,
/// or VFS is not set.
static int VfsReadText(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushnil(L);
        return 1;
    }

    const std::optional<std::string> path = CheckScriptPath(L, "read_text");
    if (!path) {
        lua_pushnil(L);
        return 1;
    }
    std::vector<uint8_t> data = vfs->Read(*path);
    if (data.empty()) {
        lua_pushnil(L);
        return 1;
    }

    // Push as null-terminated string. The data may not have a trailing null,
    // so use lua_pushlstring with the exact size — Lua strings are interned
    // with a trailing \0 regardless.
    lua_pushlstring(L, reinterpret_cast<const char*>(data.data()), data.size());
    return 1;
}

/// uz.vfs.exists(path) -> bool
///
/// Returns true if the asset exists in the VFS, false otherwise.
/// Returns false if the path leaves the VFS root or VFS is not set.
static int VfsExists(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const std::optional<std::string> path = CheckScriptPath(L, "exists");
    lua_pushboolean(L, (path && vfs->Exists(*path)) ? 1 : 0);
    return 1;
}

/// uz.vfs.is_pak_mode() -> bool
///
/// Returns true if the active VFS backend is a PakVFS (pak archive mode).
/// Determined by checking if the VFS name starts with "PakVFS" or "LayeredVFS".
/// For LayeredVFS, checks if any mount is a PakVFS by looking at the name.
/// Returns false if VFS is not set or is a LooseFileVFS.
static int VfsIsPakMode(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushboolean(L, 0);
        return 1;
    }

    const std::string name = vfs->Name();
    bool isPak = name.rfind("PakVFS", 0) == 0;
    lua_pushboolean(L, isPak ? 1 : 0);
    return 1;
}

/// uz.vfs.get_mode() -> string ("pak" or "loose")
///
/// Returns the mode string for the active VFS backend.
/// "pak" if using PakVFS, "loose" otherwise.
/// Returns "loose" if VFS is not set.
static int VfsGetMode(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushstring(L, "loose");
        return 1;
    }

    const std::string name = vfs->Name();
    bool isPak = name.rfind("PakVFS", 0) == 0;
    lua_pushstring(L, isPak ? "pak" : "loose");
    return 1;
}

/// uz.vfs.get_cache_stats() -> table { current_bytes, max_bytes, entry_count } or nil
///
/// Returns a table with LRU cache statistics from the VFS backend.
/// Only meaningful for PakVFS in lazy mode (which has an LRU cache).
/// Returns nil if VFS is not set or if the backend has no cache
/// (all stats are zero).
static int VfsGetCacheStats(lua_State* L) {
    IVFS* vfs = GetVFS(L);
    if (!vfs) {
        lua_pushnil(L);
        return 1;
    }

    IVFS::CacheStats stats = vfs->GetCacheStats();

    // If all zeros, the backend has no cache — return nil
    if (stats.currentBytes == 0 && stats.maxBytes == 0 && stats.entryCount == 0) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushnumber(L, static_cast<double>(stats.currentBytes));
    lua_setfield(L, -2, "current_bytes");

    lua_pushnumber(L, static_cast<double>(stats.maxBytes));
    lua_setfield(L, -2, "max_bytes");

    lua_pushnumber(L, static_cast<double>(stats.entryCount));
    lua_setfield(L, -2, "entry_count");

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterVfsAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterVfsAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L);

    lua_pushcfunction(L, VfsRead);
    lua_setfield(L, -2, "read");

    lua_pushcfunction(L, VfsReadText);
    lua_setfield(L, -2, "read_text");

    lua_pushcfunction(L, VfsExists);
    lua_setfield(L, -2, "exists");

    lua_pushcfunction(L, VfsIsPakMode);
    lua_setfield(L, -2, "is_pak_mode");

    lua_pushcfunction(L, VfsGetMode);
    lua_setfield(L, -2, "get_mode");

    lua_pushcfunction(L, VfsGetCacheStats);
    lua_setfield(L, -2, "get_cache_stats");

    lua_setfield(L, -2, "vfs");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.vfs API registered");
}

} // namespace uz
