# Proposal: phase7_7.18.0-vfx-baker-flipbook-tool

## Why
Niagara Baker captures an emitter's simulation over N seconds into a flipbook texture (e.g. a 6x6 atlas of a flame's evolution) for super-cheap fallback rendering. UzEngine has no equivalent - every effect pays full simulation cost even on LOD 3 potato hardware.

## What Changes
Implement an offline tool uzbake vfx <effect.uzvfxsys> --frames 36 --resolution 512 that runs the effect headless, captures frames to a texture atlas, and writes a baked .uzvfxbake asset. The engine can then swap to the baked texture at high LOD distances.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/tools/uzbake/ (extension), engine/vfx/runtime lookup
- Breaking change: NO
- User benefit: LOD 3 becomes nearly free for baked effects.

## Source
Reference: UE5 FNiagaraBakerRenderer + UNiagaraBakerSettings.
