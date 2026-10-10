# Proposal: phase8_8.2.0-vfx-asset-metadata-tagging

## Why
Niagara scripts carry a Tags string array used by the asset browser for filter/search (#fire, #magic, #ambient). UzEngine VFX assets have no such metadata, so authors discover content by filesystem path only. Cheap win that unlocks the snippet-library UI (8.8.0).

## What Changes
Add a "tags" string array field to VfxSystem, ParticleEmitter preset configs, and the VfxNodeGraph asset. JSON round-trip in the loaders; asset-browser filter query helper on top.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/system/vfx_system.h, editor/vfx_node_graph.h
- Breaking change: NO
- User benefit: Tag-based filter in the asset browser + snippet library.
- Estimated effort: S

## Source
UE5 NiagaraScriptBase::Tags + FAssetData metadata system.
