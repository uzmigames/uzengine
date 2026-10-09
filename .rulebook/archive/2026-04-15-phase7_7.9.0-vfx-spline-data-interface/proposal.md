# Proposal: phase7_7.9.0-vfx-spline-data-interface

## Why
Path-following effects (energy along a tether, homing missile trails, ribbon-guided sparks) need spline sampling in the simulation shader. Niagara's Spline DI exposes GetSplineLength, GetSplinePosition(distance), GetSplineTangent. UzEngine has no spline primitive or DI at all.

## What Changes
Add CPU-side VfxSpline (Catmull-Rom control points) plus GPU upload as a structured buffer plus HLSL helper SampleSpline(t) returning pos and tangent. Bind as a data interface accessible by modules via Emitter.splines[i].

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/ (new), shaders/runtime/vfx/flipbook_spline.hlsli (new)
- Breaking change: NO
- User benefit: Path-following effects without CPU glue code.

## Source
Reference: UE5 UNiagaraDataInterfaceSpline.
