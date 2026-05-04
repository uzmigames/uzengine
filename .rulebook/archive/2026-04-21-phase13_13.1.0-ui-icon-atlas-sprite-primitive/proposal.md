# Proposal: phase13_13.1.0-ui-icon-atlas-sprite-primitive

## Why

Phase 12 shipped the full Niagara-parity editor UI but the visual
result looks like a Win32-era program because the current
`TextRenderer` exposes only two primitives: axis-aligned rect fills +
ASCII text via a bitmap font. Every icon in the editor is an ASCII
composition (`||` for pause, `[]` for stop, `:` for drag handle) —
which is unreadable at a glance and betrays the '90s aesthetic.

To reach modern editor fidelity (UE5/Niagara/Blender-level), the
renderer needs to sample icon sprites from a dedicated atlas texture.
This task adds that primitive so subsequent tasks (rounded rects,
MSDF text, theme repaint) can layer real icons into the UI.

## What Changes

1. **Bundle the icon atlas**: bake a 512×512 RGBA PNG (`runtime/assets/editor/icons_atlas.png`)
   with ~64 monochrome UI icons (play, pause, stop, step-fwd/back,
   skip-start/end, restart, loop, chevron up/down/left/right, plus,
   minus, check, cross, gear, search, lock, unlock, grip-dots,
   overflow-dots, move-up/down, duplicate, trash, reset, link, warning,
   info, error, separator, folder, file, save, new, open, undo, redo,
   cut, copy, paste, help, about, and module-category icons for
   simulation/forces/collision/render/di/dynamic/events).
   Authored as 24×24 monochrome with 32px cells (8-pixel padding) so
   sprites sample cleanly at native size and 2× scale.
2. **Atlas metadata header**: `runtime/engine/vfx/editor/vfx_icon_atlas.h`
   exposes `enum class VfxUiIcon : uint16_t` with stable IDs for every
   icon + a compile-time table of UV rects:
   `struct VfxIconUv { float u0, v0, u1, v1; };`
   with `VfxGetIconUv(VfxUiIcon)` accessor.
3. **Runtime atlas loader**: `TextRenderer::LoadIconAtlas(IRHIDevice*,
   UploadContext&, const char* path)` uploads the PNG as a
   `RHIFormat::RGBA8Unorm` texture + SRV. Stored alongside the bitmap
   font texture.
4. **New primitive**: `TextRenderer::AddSprite(VfxUiIcon icon, float x,
   float y, float w, float h, const Vec4& tint)` appends a textured
   quad that samples from the atlas with the icon's UV rect. Tint
   multiplies against the sampled RGB so a single white atlas serves
   every accent color.
5. **UI shader update**: the existing UI PSO gains a second SRV slot
   (icon atlas) + one bit in the per-vertex payload that selects
   "font" vs "icon atlas" sampler in the pixel shader. Fragment chooses
   source at runtime.
6. **Replace ASCII icon constants**: every call site in the phase-12
   widgets that renders `VFX_ICON_*` via `AddText` migrates to
   `AddSprite(VfxUiIcon::...)` in follow-up tasks (13.5 theme repaint).
   Phase 13.1 ships the primitive + atlas + initial conversion of the
   timeline transport strip as proof.

## Impact

- Affected specs: `specs/ui-icon-atlas/spec.md` (new).
- Affected code:
  - `runtime/assets/editor/icons_atlas.png` (new binary asset).
  - `runtime/engine/vfx/editor/vfx_icon_atlas.{h,cpp}` (new).
  - `runtime/editor/font/text_renderer.{h,cpp}` — `LoadIconAtlas` +
    `AddSprite` methods + atlas SRV slot.
  - `runtime/shaders/runtime/editor/ui_font.hlsl` (or equivalent) —
    dual-source fragment shader.
  - `runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.cpp` —
    replace transport ASCII glyphs with sprite calls (proof).
  - `runtime/tests/engine/vfx/test_vfx_icon_atlas.cpp` (new).
- Breaking change: NO — `AddText` path unchanged; `AddSprite` is
  additive.
- User benefit: enables real icons in the UI. Phase 12 buttons that
  today read `|<` / `<<` / `||` / `>>` become proper triangular /
  square transport glyphs. Subsequent theme repaint (13.5) consumes
  this to reach UE5-level icon fidelity.
