# Phase 7-7.11.0 - VFX Grid2D / Grid3D Collection DI

General-purpose read/write grid textures for cellular automata, density fields, heat maps.

Reference: UE5 UNiagaraDataInterfaceGrid2DCollection + Grid3DCollection

## 1. Config

- [ ] 1.1 VfxGrid2DConfig / VfxGrid3DConfig structs (size, cell type, wrap/clamp).
- [ ] 1.2 Support float and float4 cell types.

## 2. GPU allocation

- [ ] 2.1 Allocate RWTexture2D/3D<float4> per grid.
- [ ] 2.2 Register bindless UAV and SRV indices in emitter CB.

## 3. HLSL helpers

- [ ] 3.1 ReadGrid2D(gridIdx, cell) / WriteGrid2D(gridIdx, cell, v).
- [ ] 3.2 ReadGrid3D / WriteGrid3D analogous.
- [ ] 3.3 Bilinear/trilinear sampling helpers for smooth reads.

## 4. Sample module + demo

- [ ] 4.1 GridDiffusionModule sample - diffuses density across a Grid2D each frame.
- [ ] 4.2 Demo: Fire Grid preset using a Grid2D as a scrolling heat map that particles read.

## 5. Docs and tests

- [ ] 5.1 Documentation on grid lifecycle, read/write contract, barrier rules.
- [ ] 5.2 Tests: round-trip write/read, wrap/clamp boundary behavior.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
