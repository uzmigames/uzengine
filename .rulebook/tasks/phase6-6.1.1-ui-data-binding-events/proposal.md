# Proposal: phase6-6.1.1-ui-data-binding-events

## Why
With UI rendering from phase6-6.1.0, the next step is making UI interactive and data-driven. CSS custom property binding allows game state (health, score, ammo) to flow into UI without JS. Event callbacks (click, hover, change) enable interactive menus and buttons. Input routing ensures UI can capture input when modal dialogs are open. UIAnimator provides tweened transitions for polished UI feel.

## What Changes
- CSS custom property data binding: game → `ui_set_property("--health", 75)` → CSS `var(--health)`
- UI event system: click, hover, change, focus/blur callbacks
- Input routing: modal (blocks game input) vs passthrough (transparent)
- FocusManager: gamepad/keyboard navigation between focusable elements
- UIAnimator: keyframe tweening for game-controlled UI transitions
- Full Lua ctx.ui API: load, set_property, on_event, show/hide

## Impact
- Depends on: phase6-6.1.0-ui-runtime-core
- Affected: runtime/engine/ui/, runtime/engine/scripting/api/
- Spec: ui-system.md
- Breaking change: NO
