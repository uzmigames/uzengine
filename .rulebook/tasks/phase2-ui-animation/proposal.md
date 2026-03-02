# Proposal: phase2-ui-animation

## Why

UI transitions — menu fade-in, button bounce, panel slide — currently require manual per-frame lerp logic in Lua `on_update` scripts. This produces verbose code and makes it impossible for AI tools to author UI animations declaratively. A `TweenScheduler` integrated with `ctx.ui` and the timer system provides a fluent API: `ctx.ui:tween(element, {opacity=1}, 0.3, "ease_out")` — one line to animate any CSS property.

## What Changes

- **`uz_ui_html.dll`** — `TweenScheduler` class: manages active tweens, drives CSS property changes per frame
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.ui:tween(id, props, duration, easing)`, `ctx.ui:sequence(steps[])`, `ctx.ui:cancel_tween(id)`
- **Easing functions** — 12 standard easing functions (linear, ease-in/out, bounce, elastic, back, spring)
- **Page transitions** — `ctx.ui:transition_to(page_id, animation)` for full-page animated navigation
- **Tests** — tween value correctness, easing curves, sequence chaining, cancel behavior

## Impact

- Affected specs: `docs/specs/ui-system.md` (animation section)
- Dependencies: `phase2-ui-system` (UIPass, CSS property API), `phase1-timer-system` (delta time source)
- Breaking change: NO — additive; existing UI code unchanged
- User benefit: declarative UI animation in one line; AI-composable menu transitions; no per-frame Lua boilerplate
