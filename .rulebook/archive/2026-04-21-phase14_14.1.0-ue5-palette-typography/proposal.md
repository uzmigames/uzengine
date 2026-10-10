# Proposal: phase14_14.1.0-ue5-palette-typography

## Why

Phase 13 shipped a Linear/Arc-style minimalist palette. The user
wants to replicate Unreal Engine 5's editor visual identity — Slate
dark theme with specific grey tones, orange/blue accents, and a
Roboto-family typography stack. Matching UE5's palette exactly is
the foundation for every downstream widget migration: tabs, panels,
details view, toolbar, content browser.

## What Changes

New palette tokens replacing V3 in `vfx_editor_theme.h`:

1. Background ladder — UE5 uses 5 discrete grey steps:
   `#0B0B0B` outer, `#1C1C1C` panel, `#242424` header/tab bar,
   `#2C2C2C` row/card, `#383838` hovered row/button.
2. Accent colors — UE5 blue `#0070E0` for selection + focus; UE5
   orange `#FF7B00` for transient / build-in-progress state. Add
   both tokens.
3. Text ladder — UE5 uses `#E5E5E5` primary, `#A0A0A0` secondary,
   `#707070` dim, `#4A4A4A` disabled.
4. Typography scale — UE5 uses 9 px menu / 10 px body / 14 px title.
   Update `VfxTypographyScale`.
5. Radius scale — UE5 uses 2-3 px on buttons / tabs, 0 on panels.
   Shrink the radius scale; document panel tier as `radius.none=0`.

## Impact

- Affected specs: `specs/ue5-palette-typography/spec.md` (new).
- Affected code: `runtime/engine/vfx/editor/vfx_editor_theme.{h,cpp}`.
- Breaking change: NO — token names preserved; only values change.
- User benefit: every phase-14 widget migration consumes the same
  UE5 ladder, so the editor reads as UE5-family instantly.
