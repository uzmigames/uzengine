## 1. Implementation

- [x] 1.1 Define LODGroup struct — `lod_group.h`: LODLevel (meshIndex, screenSizeThreshold), LODGroup (levels[8], numLevels), LODSelection (currentLOD, previousLOD, transitionAlpha, Update()). kMaxLODs=8 matching UE5 MAX_STATIC_MESH_LODS. Auto-threshold via pow(0.75, N) matching UE5 AutoComputeLODPowerBase.
- [x] 1.2 Implement screen-size computation — `lod_selector.h`: ComputeScreenSize() using UE5's exact formula (SceneManagement.cpp:972-984): ScreenMultiple * SphereRadius / max(1,Dist) * 2. Also ComputeScreenSizeFromFov() convenience wrapper.
- [x] 1.3 Implement LOD selection — `lod_selector.h`: SelectLOD() walks LOD levels from coarsest to finest (UE5 SceneManagement.cpp:1044-1053). LODSelection::Update() caches previous frame LOD for crossfade transitions (8 frame default).
- [x] 1.4 Extend StaticMeshComponent with LODGroup — StaticMeshComponent now has lodGroup, lodSelection, boundingSphereRadius fields. SyncWorldToDrawList computes screen size per-object and selects LOD mesh index for draw commands. Added lodMeshPaths[7] for LOD1-7 asset paths, lodMeshLoaded[7] for lazy-load tracking, forcedLOD (-1=auto) for debug override. SyncWorldToDrawList lazy-loads LOD meshes and populates LODGroup meshIndex. Scene loader/saver serialize lod1..lod7, lodScreenSizes, forcedLOD, boundingSphereRadius.
- [ ] 1.5 Implement dithered cross-fade — when LOD transitions, render both LODs for N frames (8 default) with screen-door dithering pattern (4x4 Bayer matrix). Pixel shader discards based on dither threshold + blend factor.
- [x] 1.6 Add LOD bias parameter — SceneRenderer::SetLodBias()/GetLodBias() exposed. Bias multiplies screen size thresholds, clamped [0.25, 4.0]. 1.0 = UE5 default. Matches UE5 StaticMeshLODDistanceScale.
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

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
