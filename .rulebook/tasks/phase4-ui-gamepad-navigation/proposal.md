# Proposal: phase1-ui-gamepad-navigation

## Why

Console and gamepad-primary games require UI navigation via D-pad and analog stick, with a visible focus ring that moves between buttons, sliders, and other interactive elements. The current HTML+CSS UI system has no focus management — mouse-only interaction. Without gamepad navigation, UzEngine games cannot ship on console or be played comfortably with a controller on PC.

A `FocusManager` integrated with the `UIPass` and connected to the input system provides: `data-focusable` HTML attribute marking interactive elements, directional navigation (up/down/left/right from current focus), confirm/cancel gamepad button binding, and a CSS `--focus-ring` highlight. Lua scripts can programmatically set focus via `ctx.ui:set_focus(element_id)`.

## What Changes

- **`uz_ui_html.dll`** — `FocusManager` class, `data-focusable` attribute processing, directional navigation algorithm
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.ui:set_focus(id)`, `ctx.ui:get_focus() -> id`
- **C ABI** — `ui_set_focus(const char* id)`, `ui_get_focus(char* out, size_t n)` exported functions
- **Input system integration** — D-pad and left stick events routed to `FocusManager::navigate(direction)`
- **CSS** — `:focus` pseudo-class and `--uz-focus-ring-color` CSS variable for theming

## Impact

- Affected specs: `docs/specs/ui-system.md` (focus management section)
- Dependencies: `phase2-ui-system` (HTML+CSS UI and UIPass must exist), `phase3-input-system` (gamepad input events)
- Breaking change: NO — additive; mouse-only UIs unaffected without `data-focusable` attributes
- User benefit: console-ready UI; accessible keyboard navigation; Lua scriptable focus for cutscene UI sequences
