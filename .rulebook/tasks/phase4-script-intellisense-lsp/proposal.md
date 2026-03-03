# Proposal: phase2-script-intellisense-lsp

## Why
Lua scripts in UzEngine currently have no IDE support — no autocomplete, no type hints, and no function signature help in VSCode. Developers must memorize the entire `ctx.*` API surface to write scripts, and every function call is blind. Typos in function names or wrong argument types cause silent runtime errors that only surface when the game is running, making iteration slow and error-prone.

## What Changes
- Integrate LuaLS (lua-language-server) as a VSCode extension dependency, configured automatically when opening a UzEngine project
- Generate a `ctx.d.lua` type definition file from engine source during build, covering all `ctx.*` namespaces (scene, ui, audio, physics, timers, saves, etc.)
- Expose API documentation strings as hover tooltips via the generated type definitions (parameter names, return types, brief descriptions)
- Enable go-to-definition for engine API functions, resolving into the generated type file
- Add `.luarc.json` workspace config pointing LuaLS at the generated definitions and restricting globals to the engine sandbox

## Impact
- Affected specs: docs/specs/lua-scripting.md, docs/specs/editor-panels.md
- Affected code: vscode-extension/ (extension manifest, activation), src/scripting/ (API surface enumeration), build system (definition file generation step)
- Breaking change: NO
- User benefit: Full autocomplete and type checking for all `ctx.*` calls in VSCode; typos and wrong argument counts caught before running the game, significantly reducing script iteration time
