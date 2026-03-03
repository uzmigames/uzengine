# Proposal: phase2-script-linting-static-analysis

## Why
Lua scripts are only validated at runtime when they are executed inside the engine. Syntax errors, undefined variable references, and calls to sandbox-forbidden functions (such as `os.execute` or `io.open`) remain invisible until the game actually runs the script, which can happen deep into a play session. This deferred error discovery wastes significant iteration time and can introduce hard-to-reproduce bugs in shipped builds.

## What Changes
- Integrate luacheck as a VSCode extension language server provider, running in the background as files are edited and pushing diagnostics to the Problems panel in real time
- Ship a `.luacheckrc` configuration file with every UzEngine project that defines the engine sandbox globals (`ctx`, `vec3`, `quat`, etc.) as known and flags any access to forbidden standard library functions (`os`, `io`, `debug`, `load`, `dofile`)
- Add custom luacheck rules specific to UzEngine: warn on `require()` calls that bypass the module system, flag direct use of `coroutine` outside the engine coroutine API, detect missing `return` in node lifecycle callbacks
- Add a CI lint step (`uzengine lint-scripts`) that runs luacheck over all `.lua` files under `scripts/` and exits non-zero on any warning or error, blocking merges with broken scripts

## Impact
- Affected specs: docs/specs/lua-scripting.md, docs/specs/crash-reporting.md
- Affected code: vscode-extension/ (diagnostics provider, luacheck bundling), build system (CI lint target), scripts/ (project .luacheckrc)
- Breaking change: NO
- User benefit: Syntax errors and undefined variable references are caught instantly as the developer types, before running the game; forbidden sandbox calls are flagged at edit time rather than causing cryptic runtime crashes
