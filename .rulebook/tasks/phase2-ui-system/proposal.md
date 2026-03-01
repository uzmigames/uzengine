# Phase 2 — UI System: Game Runtime

## Context

Player-facing interfaces (HUDs, menus, pause screens, dialogue) are essential to every game. Currently, UzEngine has **no UI system**. Game developers must either:

1. Skip UI entirely and hardcode debugging text via ImGui (not shipped)
2. Build UI rendering from scratch in C++, coupling it to game logic
3. Use an external UI library (IMGUI, Dear ImGui) — difficult to integrate, adds dependency

The missing UI system blocks:
- **All game projects** that need menus, HUDs, inventory screens, dialogue
- **Phase 3 systems** (animation, skeletal mesh, camera) — can't be showcased without UI feedback
- **Shipping games** — UI is mandatory

## Goal

Implement a **declarative HTML + CSS game UI system** (`uz_ui_html.dll` + integration layer) that enables game developers to author interfaces without writing C++ code, and control them from **Lua scripts**.

**Key features**:
- **HTML subset** (no JavaScript): `<div>`, `<p>`, `<button>`, `<input>`, `<progress>`, `<video>`, `<canvas>`
- **CSS box model + Flexbox**: Standard layout engine, custom properties (`--var`), transitions
- **Data binding**: Game code sets `ui_set_property(doc, "health", "0.73")`, HTML reads via `var(--health)`
- **Event callbacks**: UI elements fire events (click, change, hover) → game code responds
- **Lua-first API**: `ctx.ui:load()`, `ctx.ui:show()`, `ctx.ui:set_property()`, `ctx.ui:on_event()`
- **UIRoot scene node**: UI documents can be attached to scenes in `.uzscene` format
- **Layering**: Sort keys (0–100 world UI, 100–500 HUDs, 500–900 menus, 900+ system)
- **Font system**: SDF-rasterized fonts (msdfgen), pre-baked at Cook time
- **Video playback**: `<video>` element backed by Media Foundation (H.264/H.265 hardware decode)
- **Hot-reload**: `.html` / `.css` changes reflected live in viewport
- **Performance**: 60 FPS with 3+ simultaneous UI documents

## Why Phase 2?

UI is **low-priority for engine development** (Phase 1 focuses on scene system, shaders, editor panels) but **high-impact for game projects**. Phase 2 is the right time because:

1. Scene system (Phase 1) is complete → UI can be attached to nodes
2. Render pipeline (Phase 1) is stable → UIPass integrates easily
3. Lua scripting (Phase 1) enables declarative UI bindings
4. Multiple game projects will benefit immediately

## Dependencies

- **Phase 0** (DX12 renderer, JPEG stream, IPC): ✅ Complete
- **Phase 1** (Scene system, Lua scripting, editor panels): 🔄 In progress
  - Scene nodes must exist for UIRoot integration
  - Lua API must be available for event callbacks and data binding
- **External libs**:
  - `msdfgen` (font SDF rasterization) — vcpkg dependency
  - Windows Media Foundation (video decode) — Win32 API

## Spec

[docs/specs/ui-system.md](../../../docs/specs/ui-system.md)

