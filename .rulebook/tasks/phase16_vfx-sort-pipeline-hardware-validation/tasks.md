## 1. Visual A/B + perf budget
- [ ] 1.1 Author / repurpose `demos/vfx_viewport` entry that loads `sample/content/vfx/tests/mesh_sort_shards.uzvfx` and emits 200 transparent shard particles
- [ ] 1.2 Capture before/after screenshots from 4 camera angles via `--screenshot-stdout`; embed into `docs/engine/25-vfx/99-sort-pipeline.md`
- [ ] 1.3 Wire the existing `GpuProfilerBeginTimestamp("VFX_InlineSort")` to a CSV log path enabled by `--vfx-sort-perf <out.csv>` flag; run `--frames 600` at 1k / 4k / 16k particle counts
- [ ] 1.4 Assert sort dispatch stays under 0.3 ms at 16 384 particles on baseline hardware (RTX 3060 / Radeon RX 6600)

## 2. CVar budget fallback
- [ ] 2.1 Register `r.VFX.Sort.MaxParticles` (default `0xFFFFFFFF`) in `cvar_registry.cpp`
- [ ] 2.2 `DispatchVFXSort` reads the CVar at frame start; when an emitter's `capacity > limit`, log a one-time warning per emitter and skip the dispatch (effective sortMode = None)
- [ ] 2.3 Add CVar entry to `docs/engine/25-vfx/cvars.md` with default + recommended low-spec value

## 3. GPU integration test infra
- [ ] 3.1 Extend `runtime/tests/engine/vfx/mock_rhi_device.h` with a `MockComputeDispatch` callback API: record CB binds, SRV / UAV index binds, and dispatch (groupsX, groupsY, groupsZ); test fixture installs a C++ lambda that mutates the registered buffer mocks
- [ ] 3.2 Provide a CPU port of `particle_sort_cs.hlsl` (`detail::SimulateParticleSortCS(VFXSortConstants&, BufferMocks&)`) suitable as the shader-equivalent callback

## 4. GPU integration tests
- [ ] 4.1 `test_particle_sort_alive_detection.cpp` — 1024-particle buffer, 50% alive at scattered slots; verify `sortedIndices[0..aliveCount-1]` is a bijection with the alive set, dead particles trail
- [ ] 4.2 `test_mesh_sort_deterministic_slots.cpp` — feed known sortedIndices, run the mesh CS simulator, verify `instanceXforms[k]` matches the input order
- [ ] 4.3 `test_custom_sort_attribute.cpp` — emitter with `Float` user attribute `team`; sort by it descending; verify produced indices match expected order

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation — visual + perf + CVar sections in `docs/engine/25-vfx/99-sort-pipeline.md`
- [ ] 5.2 Write tests covering the new behavior — sections 3 and 4 above
- [ ] 5.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="*Sort*:*Mesh*:*ParticleSortAlive*:*MeshSortDeterministic*:*CustomSortAttribute*"`
