# Per-Entity Lua Environment Isolation

**Category**: architecture
**Tags**: lua, scripting, sandbox, isolation, environment

## Description

Each Lua script instance gets its own environment table with __index = _G metatable. Scripts can READ all globals (uz.*, math.*, string.*) but any variable SET is isolated to their environment. This prevents scripts from accidentally polluting each other's state while sharing the uz.* API.

## Example

lua_newtable(L);              // env = {}\nlua_newtable(L);              // mt = {}\nlua_pushvalue(L, LUA_GLOBALSINDEX);\nlua_setfield(L, -2, \"__index\"); // mt.__index = _G\nlua_setmetatable(L, -2);      // setmetatable(env, mt)\nlua_setfenv(L, -2);           // chunk runs in env

## When to Use

When loading any per-entity Lua script. When implementing new script types (behaviors, AI, UI scripts).

## When NOT to Use

For engine-level Lua modules that intentionally need to share state. For debug console commands that should modify global state.
