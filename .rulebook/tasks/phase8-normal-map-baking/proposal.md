# Proposal: phase3-normal-map-baking

## Why
Importing a high-poly character model requires pre-baked normal maps to transfer surface detail onto a low-poly game mesh. Artists currently must leave the engine and use external tools (Marmoset Toolbag, Substance Painter) just to perform this bake, breaking the import workflow. Building normal map baking into the asset pipeline eliminates tool switching and makes high-poly-to-low-poly workflows a first-class engine feature.

## What Changes
- GPU raycast baking via compute shader: cast rays from each low-poly surface point along the interpolated normal, sample the high-poly mesh at the hit point
- Tangent-space normal output written to a configurable resolution texture (512–4096)
- Optional cage mesh support: offset rays outward along a cage mesh to avoid self-intersection artifacts
- MikkTSpace tangent generation applied to the low-poly mesh before baking to ensure correct tangent basis
- IPC handler `asset.bake_normals` accepting `{ low_poly_asset, high_poly_asset, output_path, resolution, cage_asset? }` parameters
- Import settings UI in the editor allowing artists to trigger baking directly from the asset import panel
- Progress reporting via IPC status messages during the bake

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`, `docs/specs/renderer.md`, `docs/specs/shader-system.md`
- Affected code: `src/asset_pipeline/`, `src/ipc/`, `shaders/bake/`
- Breaking change: NO
- User benefit: Artists can bake normal maps from high-poly source meshes without leaving the engine, eliminating dependency on external baking tools and reducing iteration time during the asset import step.
