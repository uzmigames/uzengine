# Proposal: phase7_7.11.0-vfx-grid2d-grid3d-di

## Why
Niagara's Grid2DCollection/Grid3DCollection DIs expose read-write grids for cellular automata, density fields, heat maps, and fluid simulation. UzEngine has a spatial neighbor grid (4.18.0) but it is for neighbor queries, not general-purpose grid read/write.

## What Changes
Add generic VfxGrid2D and VfxGrid3D data interfaces backed by RWTexture2D/3D. Modules can read/write arbitrary cells. Supports float and float4 cell types with configurable resolution.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/, shaders/runtime/vfx/
- Breaking change: NO
- User benefit: Cellular automata, heat maps, density fields, fluid simulation primitives.

## Source
Reference: UE5 UNiagaraDataInterfaceGrid2DCollection + Grid3DCollection.
