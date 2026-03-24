#pragma once

/// lua_vfs_api — Lua bindings for VFS (Virtual File System) operations.
///
/// Registers the `uz.vfs` sub-table with functions for reading assets,
/// checking existence, and querying VFS state (pak vs loose mode).
///
/// # Functions
/// - `uz.vfs.read(path)` -> string (raw bytes) or nil
/// - `uz.vfs.read_text(path)` -> string (UTF-8 text) or nil
/// - `uz.vfs.exists(path)` -> bool
/// - `uz.vfs.is_pak_mode()` -> bool
/// - `uz.vfs.get_mode()` -> string ("pak" or "loose")
/// - `uz.vfs.get_cache_stats()` -> table { current_bytes, max_bytes, entry_count } or nil

struct lua_State;

namespace uz {

class IVFS;

/// Registers the `uz.vfs` sub-table into the existing `uz` global table.
///
/// Populates `uz.vfs` with bindings for asset reading, existence checks,
/// and VFS state queries (pak/loose mode, cache statistics).
/// Must be called during LuaVM initialization after the `uz` global exists.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterVfsAPI(lua_State* L);

} // namespace uz
