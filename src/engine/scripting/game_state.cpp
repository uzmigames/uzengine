// game_state.cpp -- GameState implementation + Lua uz.shared bindings.

#include "engine/scripting/game_state.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cstring>

namespace uz {

// ===========================================================================
// GameState implementation
// ===========================================================================

GameState::GameState(EventBus& event_bus)
    : event_bus_(event_bus)
{}

const GameStateValue* GameState::get_raw(std::string_view key) const {
    auto it = store_.find(std::string(key));
    if (it == store_.end()) return nullptr;
    return &it->second;
}

void GameState::set_value(std::string_view key, const GameStateValue& value) {
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        this->set<T>(key, v);
    }, value);
}

bool GameState::has(std::string_view key) const {
    return store_.contains(std::string(key));
}

void GameState::remove(std::string_view key) {
    std::string key_str(key);
    auto it = store_.find(key_str);
    if (it == store_.end()) return;

    GameStateValue last_val = it->second;
    store_.erase(it);
    non_persistent_keys_.erase(key_str);

    if (!silent_) {
        deferred_removed_.push_back({key_str, last_val});
    }
}

void GameState::clear() {
    store_.clear();
    non_persistent_keys_.clear();
    // clear() does NOT emit per-key events (spec requirement)
}

std::vector<std::string> GameState::keys() const {
    std::vector<std::string> result;
    result.reserve(store_.size());
    for (const auto& [k, v] : store_) {
        result.push_back(k);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void GameState::set_persistent(std::string_view key, bool is_persistent) {
    std::string key_str(key);
    if (is_persistent) {
        non_persistent_keys_.erase(key_str);
    } else {
        non_persistent_keys_.insert(key_str);
    }
}

bool GameState::is_persistent(std::string_view key) const {
    return !non_persistent_keys_.contains(std::string(key));
}

uint32_t GameState::on_change(std::string_view key, ChangeCallback fn) {
    uint32_t id = next_sub_id_++;
    key_subs_.push_back({id, std::string(key), std::move(fn)});
    return id;
}

void GameState::unsubscribe(uint32_t sub_id) {
    key_subs_.erase(
        std::remove_if(key_subs_.begin(), key_subs_.end(),
            [sub_id](const KeySub& s) { return s.id == sub_id; }),
        key_subs_.end()
    );
}

void GameState::dispatch_deferred_events() {
    // Dispatch changed events
    auto changed = std::move(deferred_changed_);
    deferred_changed_.clear();

    for (const auto& evt : changed) {
        // Fire via EventBus for C++ subscribers
        event_bus_.emit(evt);

        // Fire per-key subscriptions
        for (const auto& sub : key_subs_) {
            if (sub.key == evt.key) {
                sub.fn(evt.old_value, evt.new_value);
            }
        }
    }

    // Dispatch removed events
    auto removed = std::move(deferred_removed_);
    deferred_removed_.clear();

    for (const auto& evt : removed) {
        event_bus_.emit(evt);
    }
}

uint32_t GameState::pending_event_count() const {
    return static_cast<uint32_t>(deferred_changed_.size() + deferred_removed_.size());
}

// ===========================================================================
// Lua bindings — uz.shared
// ===========================================================================

static const char* const kGameStateRegistryKey = "uz_game_state";

static GameState* get_game_state(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kGameStateRegistryKey);
    auto* gs = static_cast<GameState*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return gs;
}

// ---------------------------------------------------------------------------
// Helper: push a GameStateValue onto the Lua stack
// ---------------------------------------------------------------------------

static void lua_push_game_state_value(lua_State* L, const GameStateValue& val) {
    std::visit([L](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            lua_pushboolean(L, v ? 1 : 0);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            lua_pushinteger(L, v);
        } else if constexpr (std::is_same_v<T, float>) {
            lua_pushnumber(L, static_cast<double>(v));
        } else if constexpr (std::is_same_v<T, std::string>) {
            lua_pushlstring(L, v.data(), v.size());
        } else if constexpr (std::is_same_v<T, Vec3>) {
            // Push as table {x=..., y=..., z=...}
            lua_newtable(L);
            lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
        }
    }, val);
}

// ---------------------------------------------------------------------------
// Helper: read a GameStateValue from Lua stack at index
// ---------------------------------------------------------------------------

static GameStateValue lua_to_game_state_value(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TBOOLEAN:
            return GameStateValue{static_cast<bool>(lua_toboolean(L, idx) != 0)};
        case LUA_TNUMBER: {
            double d = lua_tonumber(L, idx);
            // Detect integer vs float: if it round-trips exactly, treat as int
            auto as_int = static_cast<int32_t>(d);
            if (static_cast<double>(as_int) == d && d >= -2147483648.0 && d <= 2147483647.0) {
                return GameStateValue{as_int};
            }
            return GameStateValue{static_cast<float>(d)};
        }
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, idx, &len);
            return GameStateValue{std::string(s, len)};
        }
        case LUA_TTABLE: {
            // Check for Vec3 table {x, y, z}
            lua_getfield(L, idx, "x");
            lua_getfield(L, idx, "y");
            lua_getfield(L, idx, "z");
            if (lua_isnumber(L, -3) && lua_isnumber(L, -2) && lua_isnumber(L, -1)) {
                Vec3 v{};
                v.x = static_cast<float>(lua_tonumber(L, -3));
                v.y = static_cast<float>(lua_tonumber(L, -2));
                v.z = static_cast<float>(lua_tonumber(L, -1));
                v._pad = 0.0f;
                lua_pop(L, 3);
                return GameStateValue{v};
            }
            lua_pop(L, 3);
            // Fallback: treat as string description
            return GameStateValue{std::string("[table]")};
        }
        default:
            return GameStateValue{false};
    }
}

// ---------------------------------------------------------------------------
// uz.shared — __index metamethod (get)
// ---------------------------------------------------------------------------

static int l_shared_index(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) { lua_pushnil(L); return 1; }

    // Check if accessing a method (has, remove, keys, on_change, etc.)
    const char* key = luaL_checkstring(L, 2);

    // Method dispatch — check for known method names
    if (std::strcmp(key, "get") == 0 ||
        std::strcmp(key, "has") == 0 ||
        std::strcmp(key, "remove") == 0 ||
        std::strcmp(key, "keys") == 0 ||
        std::strcmp(key, "on_change") == 0 ||
        std::strcmp(key, "unsubscribe") == 0 ||
        std::strcmp(key, "set_persistent") == 0) {
        // Return the method from the methods table stored in the metatable
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, "__methods");
        lua_getfield(L, -1, key);
        return 1;
    }

    // Regular key lookup
    const GameStateValue* val = gs->get_raw(key);
    if (!val) {
        lua_pushnil(L);
        return 1;
    }

    lua_push_game_state_value(L, *val);
    return 1;
}

// ---------------------------------------------------------------------------
// uz.shared — __newindex metamethod (set)
// ---------------------------------------------------------------------------

static int l_shared_newindex(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) return 0;

    const char* key = luaL_checkstring(L, 2);

    // Setting to nil = remove
    if (lua_isnil(L, 3)) {
        gs->remove(key);
        return 0;
    }

    GameStateValue val = lua_to_game_state_value(L, 3);
    gs->set_value(key, val);
    return 0;
}

// ---------------------------------------------------------------------------
// uz.shared:get(key, default)
// ---------------------------------------------------------------------------

static int l_shared_get(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) { lua_pushnil(L); return 1; }

    const char* key = luaL_checkstring(L, 2);
    const GameStateValue* val = gs->get_raw(key);
    if (!val) {
        // Return default (arg 3) or nil
        if (lua_gettop(L) >= 3) {
            lua_pushvalue(L, 3);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }

    lua_push_game_state_value(L, *val);
    return 1;
}

// ---------------------------------------------------------------------------
// uz.shared:has(key) -> bool
// ---------------------------------------------------------------------------

static int l_shared_has(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) { lua_pushboolean(L, 0); return 1; }

    const char* key = luaL_checkstring(L, 2);
    lua_pushboolean(L, gs->has(key) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// uz.shared:remove(key)
// ---------------------------------------------------------------------------

static int l_shared_remove(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) return 0;

    const char* key = luaL_checkstring(L, 2);
    gs->remove(key);
    return 0;
}

// ---------------------------------------------------------------------------
// uz.shared:keys() -> table
// ---------------------------------------------------------------------------

static int l_shared_keys(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) {
        lua_newtable(L);
        return 1;
    }

    auto ks = gs->keys();
    lua_createtable(L, static_cast<int>(ks.size()), 0);
    for (size_t i = 0; i < ks.size(); ++i) {
        lua_pushlstring(L, ks[i].data(), ks[i].size());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

// ---------------------------------------------------------------------------
// uz.shared:on_change(key, callback) -> sub_id
// ---------------------------------------------------------------------------

// Stored Lua callbacks: we keep them in a Lua registry table.
static const char* const kSharedCallbackTable = "uz_shared_callbacks";

static int l_shared_on_change(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) { lua_pushinteger(L, 0); return 1; }

    const char* key = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    // Store the Lua function in the callback registry table
    lua_getfield(L, LUA_REGISTRYINDEX, kSharedCallbackTable);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kSharedCallbackTable);
    }

    // We'll use the sub_id as the table key
    // First, register with GameState to get an ID
    lua_State* Lcap = L;
    uint32_t sub_id = gs->on_change(key, [Lcap, sub_id_placeholder = uint32_t(0)](
        const GameStateValue& old_val, const GameStateValue& new_val) mutable {
        // This will be replaced below after we know the sub_id
        (void)old_val; (void)new_val; (void)Lcap;
    });

    // Remove the placeholder and re-register with the real sub_id
    gs->unsubscribe(sub_id);

    // Now register for real, capturing sub_id
    uint32_t real_id = gs->on_change(key, [Lcap, real_sub_id = sub_id + 1](
        const GameStateValue& old_val, const GameStateValue& new_val) {
        // Retrieve the Lua callback from registry
        lua_getfield(Lcap, LUA_REGISTRYINDEX, kSharedCallbackTable);
        if (lua_isnil(Lcap, -1)) { lua_pop(Lcap, 1); return; }
        lua_rawgeti(Lcap, -1, static_cast<int>(real_sub_id));
        if (!lua_isfunction(Lcap, -1)) {
            lua_pop(Lcap, 2);
            return;
        }
        // Push old and new values
        lua_push_game_state_value(Lcap, old_val);
        lua_push_game_state_value(Lcap, new_val);
        // pcall(fn, old, new)
        if (lua_pcall(Lcap, 2, 0, 0) != 0) {
            const char* err = lua_tostring(Lcap, -1);
            DIAG_FMT("[SharedState] on_change callback error: %s", err ? err : "(unknown)");
            lua_pop(Lcap, 1);
        }
        lua_pop(Lcap, 1); // pop callback table
    });

    // Store the Lua function in the callback table at index real_id
    lua_pushvalue(L, 3);  // push the function
    lua_rawseti(L, -2, static_cast<int>(real_id));
    lua_pop(L, 1); // pop callback table

    lua_pushinteger(L, static_cast<lua_Integer>(real_id));
    return 1;
}

// ---------------------------------------------------------------------------
// uz.shared:unsubscribe(sub_id)
// ---------------------------------------------------------------------------

static int l_shared_unsubscribe(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) return 0;

    uint32_t sub_id = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    gs->unsubscribe(sub_id);

    // Remove the Lua callback from registry
    lua_getfield(L, LUA_REGISTRYINDEX, kSharedCallbackTable);
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L);
        lua_rawseti(L, -2, static_cast<int>(sub_id));
    }
    lua_pop(L, 1);

    return 0;
}

// ---------------------------------------------------------------------------
// uz.shared:set_persistent(key, bool)
// ---------------------------------------------------------------------------

static int l_shared_set_persistent(lua_State* L) {
    auto* gs = get_game_state(L);
    if (!gs) return 0;

    const char* key = luaL_checkstring(L, 2);
    bool persistent = lua_toboolean(L, 3) != 0;
    gs->set_persistent(key, persistent);
    return 0;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_shared_state_bindings(lua_State* L, GameState* state) {
    if (!L || !state) return;

    // Store GameState* in Lua registry
    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, kGameStateRegistryKey);

    // Create the uz.shared userdata (light userdata as proxy)
    lua_getglobal(L, "uz");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "uz");
        lua_getglobal(L, "uz");
    }

    // Create a table (not userdata) as the shared proxy
    lua_newtable(L);

    // Create metatable with __index and __newindex
    lua_newtable(L);

    lua_pushcfunction(L, l_shared_index);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, l_shared_newindex);
    lua_setfield(L, -2, "__newindex");

    // Methods table (accessed via __index for method calls)
    lua_newtable(L);
    lua_pushcfunction(L, l_shared_get);           lua_setfield(L, -2, "get");
    lua_pushcfunction(L, l_shared_has);            lua_setfield(L, -2, "has");
    lua_pushcfunction(L, l_shared_remove);         lua_setfield(L, -2, "remove");
    lua_pushcfunction(L, l_shared_keys);           lua_setfield(L, -2, "keys");
    lua_pushcfunction(L, l_shared_on_change);      lua_setfield(L, -2, "on_change");
    lua_pushcfunction(L, l_shared_unsubscribe);    lua_setfield(L, -2, "unsubscribe");
    lua_pushcfunction(L, l_shared_set_persistent); lua_setfield(L, -2, "set_persistent");
    lua_setfield(L, -2, "__methods");

    lua_setmetatable(L, -2);

    // Set uz.shared = proxy_table
    lua_setfield(L, -2, "shared");

    lua_pop(L, 1); // pop uz table
}

} // namespace uz
