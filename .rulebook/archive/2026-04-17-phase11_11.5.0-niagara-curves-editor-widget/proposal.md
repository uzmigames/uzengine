# Proposal: phase11_11.5.0-niagara-curves-editor-widget

## Why
`VfxTangentCurve` (phase 7.16), `VFXColorRamp`, and `VFXCurve` already store + evaluate keyframes and bake to GPU LUTs. But there's no editor to author curves visually — today, keys get placed by hand-editing `.uzcurve` JSON or calling `SetKey()` from Lua. Niagara's curves editor (color-over-life, size-over-life, spawn rate) is the single most-used authoring surface; without it, artists can't tune effects iteratively.

## What Changes
New `VfxCurvesEditorWidget` in `runtime/engine/vfx/editor/`. Canvas-based editor that binds to either a `VFXCurve*` (scalar) or `VFXColorRamp*` (RGBA gradient). Features: pan + zoom (mouse middle-drag pan, wheel zoom), keyframe drag-to-move (X=time, Y=value), click-empty-canvas-to-add-key, right-click key for context menu (Delete / Set Time / Set Value / Interpolation mode), numeric edit fields at bottom for precise values. Gradient preview strip for color ramps. Live preview updates the bound GPU LUT via the existing `BakeToTexture()` / `BakeToRGBA16F()` paths whenever the curve changes.

## Impact
- Affected specs: docs/engine/25-vfx/ (new: 46-curves-editor.md)
- Affected code: runtime/engine/vfx/editor/vfx_curves_editor_widget.{h,cpp} (new); `VFXCurve`/`VfxTangentCurve`/`VFXColorRamp` expose `AddKey`/`RemoveKey`/`MoveKey` helpers (some may already exist; confirm during implementation)
- Breaking change: NO (editor is read/write layer on top of existing data)
- User benefit: artists tune color/size/spawn curves with immediate visual feedback
- Estimated effort: L

## Source
UE5 `SCurveEditor` + `SNiagaraCurveOverview` + phase 10.4 pan/zoom math.
