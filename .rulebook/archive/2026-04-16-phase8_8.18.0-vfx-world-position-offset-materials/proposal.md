# Proposal: phase8_8.18.0-vfx-world-position-offset-materials

## Why
Current sprite / mesh shaders use fixed-function billboard / instance-TRS vertex positions. Niagara materials can add a vertex-shader WPO (World Position Offset) term so particles can warp / twist / breathe via material expressions. Essential for organic / magical effects.

## What Changes
Extend the shader include pipeline to splice an author-supplied WPO expression into the VS pipeline. Materials that set a non-zero WPO pick up the permutation. Motion-vector output (8.1.0) reads the WPO delta so TAA reprojects correctly.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/material/compiler + shaders/runtime/vfx/
- Breaking change: NO
- User benefit: Organic / magical effects via material vertex deformation.
- Estimated effort: M

## Source
UE5 FMaterialAttributes::WorldPositionOffset pin.
