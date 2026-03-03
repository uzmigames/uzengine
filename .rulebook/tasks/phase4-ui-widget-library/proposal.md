# Proposal: phase1-ui-widget-library

## Why

Game UIs need common interactive components (buttons, sliders, checkboxes, modals) that every game reimplements from scratch using raw `<div>` and CSS. Without a widget library, each game's UI is one-off HTML with inconsistent behavior, poor accessibility, and no gamepad focus support by default.

A `uz-*` custom element library — implemented as Web Components inside `uz_ui_html.dll` — provides production-quality widgets with: consistent theming via CSS variables, ARIA semantics for accessibility, built-in `data-focusable` support, and Lua event binding. AI agents can generate UI by composing `uz-button`, `uz-slider`, `uz-modal`, etc. without knowing HTML internals.

## What Changes

- **`uz_ui_html.dll`** — custom element registry with 10 widget types
- **`src/ui/widgets/`** — C++ implementation of each `uz-*` element
- **CSS** — default theme stylesheet loaded automatically; overridable via CSS variables
- **Lua** — all widgets emit standard DOM events bindable via `ctx.ui:on(element_id, event, fn)`
- **Tests** — each widget: render, interaction, ARIA attributes, CSS variable theming, focus integration

## Impact

- Affected specs: `docs/specs/ui-system.md` (widget library section)
- Dependencies: `phase2-ui-system` (UIPass, custom element base), `phase1-ui-gamepad-navigation` (focus integration)
- Breaking change: NO — additive; raw HTML still works
- User benefit: AI-composable UI from high-level components; consistent gamepad focus; accessible ARIA semantics out of the box
