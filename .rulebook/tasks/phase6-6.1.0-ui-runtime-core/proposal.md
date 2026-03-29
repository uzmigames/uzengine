# Proposal: phase6-6.1.0-ui-runtime-core

## Why
Games need UI: health bars, menus, inventory screens, dialog boxes. UzEngine's approach is HTML/CSS-based game UI rendered via a lightweight parser and layout engine (no JavaScript, no full browser). This keeps UI authoring familiar to web developers while being orders of magnitude lighter than Electron/CEF. This task covers the core: HTML subset parsing, CSS box model + flexbox layout, SDF font rendering, and the render graph UIPass.

## What Changes
- HTML parser for safe subset (div, span, p, h1-h6, img, button, progress)
- CSS parser for box model + flexbox + custom properties (--vars)
- Flexbox layout engine (row/column, justify, align, wrap, gap)
- SDF font rasterizer (msdfgen) for text rendering
- UIPass in render graph (sort_key=800, blended over 3D scene)
- UiDocument loaded from .html files, hot-reloadable
- UIRoot scene node type for attaching UI to scene

## Impact
- New directory: `runtime/engine/ui/`
- New shaders: `runtime/shaders/runtime/ui/` (rect, text, image)
- Affected: render graph (new UIPass), ECS (UIRoot node)
- Spec: ui-system.md
- Breaking change: NO
- Large scope — this is the foundation, data binding/events are in phase6-6.1.1
