## 1. Implementation

- [ ] 1.1 Define LODGroup struct — array of LODLevel (mesh handle, screenSizeThreshold float). Max 4 LOD levels. LOD0 = full detail (threshold 1.0), LOD3 = lowest (threshold 0.02).
- [ ] 1.2 Implement screen-size computation — project bounding sphere radius to screen pixels: `screenSize = (sphereRadius * screenHeight) / (distance * 2 * tan(fov/2))`. Normalize to [0,1] as fraction of screen height.
- [ ] 1.3 Implement LOD selection — per-object per-frame: compare screenSize against LODLevel thresholds, select highest LOD where screenSize >= threshold. Cache previous frame LOD to detect transitions.
- [ ] 1.4 Extend StaticMeshProxy with LODGroup — proxy stores LODGroup reference; draw list builder reads current LOD level to select mesh/index buffer for draw call.
- [ ] 1.5 Implement dithered cross-fade — when LOD transitions, render both LODs for N frames (8 default) with screen-door dithering pattern (4x4 Bayer matrix). Pixel shader discards based on dither threshold + blend factor.
- [ ] 1.6 Add LOD bias parameter — global LOD bias (-2 to +2) that shifts all thresholds, allowing quality/performance tradeoff. Exposed in renderer config.
- [ ] 1.7 Extend glTF importer — import LOD meshes from glTF MSFT_lod extension or from mesh name suffixes (_LOD0, _LOD1, _LOD2, _LOD3).
- [ ] 1.8 Create LOD demo — single mesh with 4 LOD levels (10K, 2K, 500, 100 triangles), camera dolly showing automatic LOD switching with dithered crossfade visible.

## 2. Visual Verification

- [ ] 2.1 Screenshot: camera close — LOD0 full detail mesh
- [ ] 2.2 Screenshot: camera far — LOD2/3 simplified mesh, no visual pop
- [ ] 2.3 Screenshot: mid-transition — dithered crossfade visible between two LOD levels
- [ ] 2.4 Screenshot: LOD debug overlay — color-coded LOD levels (green=0, yellow=1, orange=2, red=3)
- [ ] 2.5 Performance: 500 objects scene — measure draw call count and GPU time with LOD vs without

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/02-visibility-pipeline.md` with LOD selection algorithm
- [ ] 3.2 Document LOD authoring guidelines (triangle count targets per level, threshold defaults)
