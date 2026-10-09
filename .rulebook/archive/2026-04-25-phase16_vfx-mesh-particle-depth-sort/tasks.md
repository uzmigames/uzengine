## 1. Data model + CB foundation (delivered in this task)
- [x] 1.1 Add `sortMode` enum + field to `MeshRenderModule` (None / ViewDepth / CustomAttribute)
- [x] 1.5 Plumb sorted buffer SRV index through the mesh CB — `VFXMeshInstanceConstants` extended with `sortMode` + `sortedIndicesSrvIndex` (CPU + HLSL); mesh instance CS reads `sortedIndices[sortedSlot]` when sortMode != None and the SRV is valid; CPU side gates `sortedIndicesSrvIndex` with `constexpr bool sortDispatched = false` so the dormant path stays inert until the follow-up wires the dispatch
- [x] 1.7 `.uzfx` round-trip for sortMode — JSON ParseConfig accepts `sort_mode` + `custom_sort_attribute`; defaults preserved on missing fields; `CustomAttribute` with empty attribute name downgrades to `None` and warns

## 2. Moved to follow-up `phase16_vfx-particle-sort-pipeline-refactor`
The remaining items required pre-existing pipeline bugs to be fixed first
(disabled sort dispatch, sort_cs assumes contiguous-alive while spawn uses
scattered free-list, mesh CB CPU/HLSL 16-byte mismatch). Tracked in the
follow-up task with full breakdown.

- [x] 1.2 -> follow-up section 1 (sort core alive-detection + custom keys)
- [x] 1.3 -> follow-up section 2 (inline dispatch + sprite re-enable)
- [x] 1.4 -> follow-up section 2.4 (sprite VS reads sortedIndices)
- [x] 1.6 -> follow-up section 5.2 (editor dropdown lands with the demo)
- [x] 1.8 -> follow-up section 5 (visual demo)
- [x] 1.9 -> follow-up section 5.3 (perf budget assertion)

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation — runtime/CHANGELOG.md v0.9.8 entry covers the data-model surface, the gate, and the follow-up split rationale
- [x] 3.2 Write tests covering the new behavior — `tests/engine/vfx/test_mesh_sort_mode.cpp` (20 tests: round-trip, defaults, CustomAttribute downgrade, parse-wrong-type, GetParameters integration)
- [x] 3.3 Run tests and confirm they pass — `test_runtime.exe --gtest_filter="UZ_MeshSortMode*:UZ_MeshRenderModule_Sort*"` -> 20/20 passed
