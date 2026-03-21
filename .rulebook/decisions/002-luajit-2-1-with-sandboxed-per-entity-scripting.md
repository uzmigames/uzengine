# 2. LuaJIT 2.1 with Sandboxed Per-Entity Scripting

**Status**: proposed
**Date**: 2026-03-15

## Context

UzEngine needs a scripting language for gameplay authoring. Requirements: fast execution (per-frame on_update), safe sandboxing (no file I/O or code loading from scripts), per-entity isolation, C++ interop via lightweight bindings, and hot-reload capability.

## Decision

Use LuaJIT 2.1 with a singleton LuaVM, per-entity CScript components, and sandboxed environment tables. Each script gets its own environment with __index = _G for read-only globals access. Dangerous globals (os, io, load, require, collectgarbage) are stripped. Entity handles use 64-bit packed lightuserdata (no heap allocation). API exposed via uz.* tables (uz.scene, uz.camera, uz.input, uz.time, uz.math, uz.quat, uz.renderer, uz.window). System pointers stored in LUA_REGISTRYINDEX for decoupled access.

## Alternatives Considered

- C# (Unity-style, heavy runtime, complex embedding)
- Python (slow for per-frame, GIL issues)
- Wren (newer but smaller ecosystem)
- AngelScript (C++-like syntax, smaller community)
- JavaScript/V8 (large runtime, complex embedding)

## Consequences

Pros: LuaJIT is fastest scripting VM available, tiny memory footprint, proven in AAA (CryEngine, Roblox), simple C API, 64-bit entity handles avoid GC pressure. Cons: Lua syntax unfamiliar to some devs, no native OOP (mitigated by metatables), LuaJIT development stalled (but stable). Key constraint: every C++ feature MUST have a corresponding Lua binding (API parity rule).
