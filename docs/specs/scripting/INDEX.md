# Scripting Specs

| Spec | Description |
|------|-------------|
| [lua-scripting.md](lua-scripting.md) | LuaJIT per-node scripts, coroutines, modules, shared state, bytecode cache, sandbox levels, hot-reload (~60ms) |
| [shared-state.md](shared-state.md) | Global game state store (ctx.shared), reactive bindings for UI/Audio/ECS, change notifications, save/load |
| [async-streaming.md](async-streaming.md) | Async asset loading, AssetHandle<T> API, IO thread pool, streaming memory budgets, level streaming, coroutine integration |
| [data-driven-config.md](data-driven-config.md) | Unified config framework: `.uzevent`, `.uzphysmat`, `.uzentity`, `.uzflow`; hot-reload, validation, editor inspector |
