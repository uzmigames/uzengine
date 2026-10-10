## 1. Items
- [ ] 1.1 TLASManager::Rebuild: skip translucent draws and mesh decals through one predicate shared with the distance-field occluder set, with a gtest.
- [ ] 1.2 Re-measure the writers before and after (scripts/test_gi_energy_gpu.ps1 -Frames 200, scripts/gi_energy_populations.py) on a pre-registered population.
- [ ] 1.3 Measure H-61's term with r.GI.MeshSdf.TwoSided 0 and 1 (give scripts/run_probe_indirect_residual.py a CVar override).

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
