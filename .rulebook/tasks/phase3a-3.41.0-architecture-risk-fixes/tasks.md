## 1. Root Signature Refactor

- [ ] 1.1 Audit current root signature — document all 14 params, their usage frequency, and which can be moved to descriptor tables.
- [ ] 1.2 Move per-material texture indices to a single bindless descriptor table — replace 10 individual texture table slots with a single StructuredBuffer<MaterialData> indexed by materialID.
- [ ] 1.3 Reclaim freed root slots — reserve 3-4 slots for future use (GBufferD, reflection probes, post-process UBO, fog CB).
- [ ] 1.4 Update all PSOs — rebuild pipeline states with new root signature layout. Update shader root parameter indices.
- [ ] 1.5 Update root_slots.h — new slot definitions matching refactored layout. Document slot budget and remaining capacity.

## 2. Light Data GPU Migration

- [ ] 2.1 Replace per-light CPU iteration with StructuredBuffer<LightData> — upload all visible lights to a single GPU buffer. Deferred lighting shader iterates buffer via light count uniform.
- [ ] 2.2 Remove hardcoded light array limit (64) — use dynamic count from StructuredBuffer. Support up to 256 lights.
- [ ] 2.3 Add CPU-side frustum pre-cull — only upload lights that intersect camera frustum. Output: sorted light buffer + count.

## 3. Shadow Atlas

- [ ] 3.1 Replace fixed point_light_shadows[8] array with dynamic ShadowAtlas — single D32F texture (4096x4096), rect-pack allocator for shadow tiles.
- [ ] 3.2 Implement priority-sorted shadow assignment — sort by screen-space contribution (size / distance). Top N lights get shadow tiles. Others are unshadowed.
- [ ] 3.3 Implement shadow tile sizes — close/important lights get 1024x1024 tiles, distant lights get 256x256. Dynamic reallocation per frame.
- [ ] 3.4 Update shadow sampling in lighting shader — sample from atlas rect (UV offset + scale) instead of individual textures.

## 4. SceneEnvironment Consolidation

- [ ] 4.1 Define SceneEnvironmentState struct — contains all per-frame environment: fog params, sky params, tonemap params, bloom params, exposure params. Single version uint32 for dirty tracking.
- [ ] 4.2 Implement SceneEnvironmentGatherer refactor — collect all environment components into single SceneEnvironmentState per frame. One CB upload, one dirty check.
- [ ] 4.3 Update all passes to read from SceneEnvironmentState — lighting, fog, tonemap, bloom all read from same CB instead of separate sources.

## 5. Documentation

- [ ] 5.1 Update `docs/architecture/render-pipeline.md` — add explicit "MSAA is unsupported by design. TAA is the only anti-aliasing path for deferred rendering."
- [ ] 5.2 Update `docs/engine/12-rendering/13-register-binding-convention.md` — new root signature layout after refactor.
- [ ] 5.3 Document shadow atlas allocation algorithm and priority sorting.
- [ ] 5.4 Document SceneEnvironmentState struct and version-based dirty tracking.

## 6. Visual Verification

- [ ] 6.1 Regression: all 9 existing demos produce identical visual output after root signature refactor
- [ ] 6.2 Test: scene with 50+ point lights — all lights rendered, no silent failures
- [ ] 6.3 Test: scene with 12+ shadowed lights — shadow atlas correctly assigns tiles, low-priority lights gracefully degrade to unshadowed
