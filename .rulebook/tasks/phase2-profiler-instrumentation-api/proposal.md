# Proposal: phase2-profiler-instrumentation-api

## Why
UzEngine currently profiles only its own internal systems via Tracy zones — game developers have no way to add profiling instrumentation to their custom C++ systems or Lua scripts. A function like `find_all_enemies()` that suddenly spikes frame time cannot be isolated without either modifying engine internals or guessing. This creates a blind spot in performance analysis that grows larger as gameplay code becomes more complex.

## What Changes
- Add a `UZ_PROFILE_SCOPE(name)` C++ macro (and `UZ_PROFILE_FUNCTION()` shorthand) that wraps a Tracy `ZoneScoped` via RAII, compiling to a no-op in non-profiler builds so game code has zero overhead in release
- Expose `ctx.profiler:begin(name)` and `ctx.profiler:end(name)` in the Lua scripting API, backed by Tracy `___tracy_emit_zone_begin` / `___tracy_emit_zone_end` with the zone name embedded in the Tracy timeline
- Add an IPC command `profiler.get_frame_data` that returns a JSON array of per-zone timings for the last N frames (zone name, inclusive time µs, call count), allowing the VSCode extension to display a lightweight flame chart without requiring a full Tracy GUI connection
- Implement a `profiler.enable` / `profiler.disable` IPC toggle so the VSCode extension can activate profiling on demand without restarting the engine process

## Impact
- Affected specs: docs/specs/debug-profiling.md, docs/specs/lua-scripting.md
- Affected code: src/profiler/ (Tracy wrapper macros and Lua bindings), src/ipc/ (profiler IPC handlers), vscode-extension/ (profiler panel displaying frame data), game.dll API header (UZ_PROFILE_SCOPE macro exported to game code)
- Breaking change: NO
- User benefit: Game developers can instrument any C++ system or Lua function with one line and see its cost in the VSCode profiler panel without leaving the editor or installing external tools
