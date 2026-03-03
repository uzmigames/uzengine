# Proposal: phase4-visual-scripting

## Why

Designers and artists who cannot write Lua code need a way to implement gameplay logic: connecting events to actions, setting up conditional triggers, wiring ability activations to UI responses. Visual scripting — a node graph where data flows through connections between typed ports — provides this without requiring programming knowledge. Each graph compiles to Lua, so execution uses the existing LuaVM with full performance and hot-reload support.

## What Changes

- **VSCode extension** — `NodeGraphEditorPanel`: WebView-based node graph editor with drag, connect, and pan/zoom
- **`src/game/visual_script_compiler.h/cpp`** — `VisualScriptCompiler`: traverses node graph, emits Lua source code
- **Node type library** — built-in nodes: Event (on_start, on_update, event subscribe), Action (set_shared_state, audio_trigger, scene_spawn), Condition (if/else, compare, logical AND/OR), Value (number, string, Vec3 literals)
- **Hot-reload** — graph edits trigger Lua recompile + LuaScriptReloader refresh
- **`.uzflow` format** — node graph serialization (data-driven framework)

## Impact

- Affected specs: new `docs/specs/visual-scripting.md`
- Dependencies: `phase1-lua-scripting` (Lua backend), `phase1-editor-panels` (WebView panel infrastructure)
- Breaking change: NO — additive; Lua scripts unchanged; visual script outputs Lua
- User benefit: gameplay logic accessible to non-programmers; AI can author node graphs as structured data; Lua output is readable and debuggable
