# Proposal: phase13_13.3.0-rounded-rect-sdf-primitive

## Why

Every panel, card, button, and popup in phase 12 is an axis-aligned
hard-edge rectangle. Modern editor UIs (UE5/Niagara/Blender/Figma)
use soft rounded corners + subtle drop shadows to establish visual
hierarchy and depth. The current `TextRenderer::AddRect` can't emit
either — the rendered result looks like flat 1990s Win32 chrome.

This task adds a signed-distance-field (SDF) rounded-rect primitive
that draws rounded corners, borders, and drop shadows in a single
shader pass. With this + 13.1 icon atlas + 13.2 Unicode text, the
editor gains a modern look without a full retained-mode UI rewrite.

## What Changes

1. **New vertex payload**: UI vertices carry a packed "primitive
   type" tag — 0 = legacy rect, 1 = font quad, 2 = icon sprite
   (from 13.1), 3 = rounded rect / shadow.
2. **Rounded-rect parameters**: the vertex carries
   `{cornerRadius, borderThickness, shadowOffset.xy, shadowBlur,
   fillColor.rgba, borderColor.rgba, shadowColor.rgba}` via an
   extended per-vertex struct or a UI constant ring buffer indexed
   by `vertexID / 4`.
3. **SDF shader**: `runtime/shaders/runtime/editor/ui_rrect.hlsli`
   implements the classic
   `sdRoundedBox(p, b, r)` distance function (Inigo Quilez'
   formulation). Fragment shader:
   - Distance `d` to rounded-rect edge.
   - Inside fill: `mix(fill, border, smoothstep(-border, 0, d))`.
   - Outside shadow: additional `exp(-d/blur)` falloff on the
     shadow sampling rect (offset by shadowOffset).
4. **New primitive**: `TextRenderer::AddRoundedRect(rect,
   cornerRadius, fillColor, borderThickness, borderColor,
   shadowOffset, shadowBlur, shadowColor)`. Each arg defaults to 0 /
   transparent so callers can opt into any subset.
5. **Degenerate fallback**: `cornerRadius == 0 && borderThickness ==
   0 && shadowBlur == 0` emits a simple rect quad (same cost as the
   legacy `AddRect` path).

## Impact

- Affected specs: `specs/rounded-rect-sdf-primitive/spec.md` (new).
- Affected code:
  - `runtime/editor/font/text_renderer.{h,cpp}` — new primitive +
    vertex payload extension.
  - `runtime/shaders/runtime/editor/ui_rrect.hlsli` — SDF helper.
  - `runtime/shaders/runtime/editor/ui_font.hlsl` — dispatch on
    primitive type.
  - `runtime/tests/editor/font/test_text_renderer_rrect.cpp` (new).
- Breaking change: NO — `AddRect` path unchanged; the new primitive
  is additive.
- User benefit: panels, cards, buttons can now render with rounded
  corners + soft shadows. Phase 13.5 theme repaint consumes this to
  ship a Niagara-parity visual pass.
