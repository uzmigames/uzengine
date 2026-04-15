# Proposal: phase7_7.16.0-vfx-curve-editor-ui

## Why
Most VFX parameters are curves (sizeOverLife, colorOverLife, emissionRate over time). Artists need a visual curve editor: drag handles, tangent modes (linear/smooth/broken), curve preview. UzEngine currently stores curves as float start/peak/end triplets - insufficient for expressive art direction.

## What Changes
Add a full curve editor widget supporting Bezier/Hermite curves with multiple keys, tangent modes, and time-axis zoom/pan. Save as a .uzcurve asset referenced by emitter configs. Backwards-compatible with existing triplet fields (promote to single-key curve on load).

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_curve_editor.* (new), asset/vfx_curve.*
- Breaking change: NO (backward-compat with triplet auto-promotion)
- User benefit: Expressive artist-authored parameter curves.

## Source
Reference: UE5 SNiagaraCurveEditor.cpp.
