## 1. Implementation
- [ ] 1.1 Add `sortMode` enum + field to `MeshRenderModule` (None / ViewDepth / CustomAttribute)
- [ ] 1.2 Generalize the existing bitonic sort pass so it can operate on mesh emitters (mesh emitters also expose a position buffer)
- [ ] 1.3 Extend `frame_particles.cpp` to schedule the sort when any mesh emitter has sortMode != None
- [ ] 1.4 Update mesh VS to index into sortedIndices[] instead of raw instanceID when a sorted buffer is bound
- [ ] 1.5 Plumb sorted buffer SRV index through the mesh VFXBillboardIndices (or a mesh-specific analogue CB)
- [ ] 1.6 Editor: add Sort Mode dropdown in the mesh render module's parameters section
- [ ] 1.7 `.uzfx` round-trip for sortMode
- [ ] 1.8 Visual test: shards demo with 200 overlapping transparent meshes, view-depth ON vs OFF
- [ ] 1.9 Perf test: confirm sort cost <0.3 ms at 16k mesh particles

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
