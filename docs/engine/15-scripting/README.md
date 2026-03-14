# Scripting System

LuaJIT 2.1 scripting — sandbox, script runner, hot-reload, and the Lua API.

> **Status**: Active (Phase 1 Complete)
> **Source**: `runtime/engine/scripting/`, `lua_vm.h/cpp`, `script_component.h/cpp`, `lua_*_api.cpp`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Scripting architecture, LuaJIT integration, per-node lifecycle |
| [02-luajit-integration.md](02-luajit-integration.md) | LuaJIT 2.1 embedding, state management, C API bindings |
| [03-sandbox.md](03-sandbox.md) | Security sandbox — blocked globals, execution timeout, memory limits |
| [04-script-runner.md](04-script-runner.md) | Script loading, compilation, execution, error handling |
| [05-hot-reload.md](05-hot-reload.md) | File watcher integration, live script reloading |
| [06-lua-api.md](06-lua-api.md) | Full Lua API reference — `uz.*`, `ctx.*`, scene, camera, input |
