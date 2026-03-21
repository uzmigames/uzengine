# Lua Registry-Based Pointer Injection

**Category**: architecture
**Tags**: lua, scripting, api-binding, decoupling, registry

## Description

Each Lua API module stores its backing C++ system pointer in LUA_REGISTRYINDEX via a string key (e.g., 'uz_world', 'uz_time_manager'). A static GetSystem() helper retrieves the pointer. This decouples Lua C functions from global singletons — the API works regardless of how the system is instantiated. Injection happens after system init via LuaSet*(L, ptr) calls.

## Example

// Header\nvoid LuaRegisterPhysicsAPI(lua_State* L);\nvoid LuaSetPhysicsWorld(lua_State* L, PhysicsWorld* world);\n\n// Implementation\nstatic constexpr const char* kKey = \"uz_physics_world\";\nstatic PhysicsWorld* GetSystem(lua_State* L) {\n    lua_getfield(L, LUA_REGISTRYINDEX, kKey);\n    auto* p = static_cast<PhysicsWorld*>(lua_touserdata(L, -1));\n    lua_pop(L, 1);\n    return p;\n}

## When to Use

When creating new Lua API bindings for any C++ system. When the backing system is created after LuaVM::Init() and needs late binding.

## When NOT to Use

For Lua-internal state that doesn't need C++ backing (pure Lua utilities). For systems that are always available (use uz global table directly).
