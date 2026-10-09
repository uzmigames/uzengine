# Proposal: phase9_9.13.0-vfx-wpo-material-compiler-wiring

## Why
Phase 8.18.0 shipped the WPO attribute + splice-generator helpers, but the material compiler never calls them. Authored WPO expressions in material assets are ignored — particles don't deform.

## What Changes
Integrate `VfxWPOEmitEvaluateBody` / `VfxWPOSpliceVS` / `VfxWPOSplicePrevVS` into the material → HLSL codegen. The compiler inserts the `EvaluateWPO` function body at the shader's function-scope, splices the current-frame call before the `viewProj` multiply, and the prev-frame call immediately after — so motion-vector reprojection (phase 8.1.0) works.

## Impact
- Affected specs: docs/engine/25-vfx/35-world-position-offset.md
- Affected code: runtime/engine/material/compiler/* + VFX shader composition
- Breaking change: NO
- User benefit: WPO expressions on materials actually deform vertices.
- Estimated effort: M

## Source
UE5 FMaterialAttributes::WorldPositionOffset codegen.
