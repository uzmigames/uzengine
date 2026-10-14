## 1. Items
- [x] 1.1 TLASManager::Rebuild: skip translucent draws and mesh decals through one predicate shared with the distance-field occluder set, with a gtest.
- [x] 1.2 Re-measure the writers before and after (scripts/rendering/test_gi_energy_gpu.ps1 -Frames 200, scripts/rendering/gi_energy_populations.py) on a pre-registered population.
- [x] 1.3 Measure H-61's term with r.GI.MeshSdf.TwoSided 0 and 1 (give scripts/rendering/run_probe_indirect_residual.py a CVar override).

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass

Evidence: `docs/analysis/ddgi-sdf-writer-parity-head.md`. Vendored Zig build
EXIT=0; owned gtests 36/36 passed. Three known VfxEditorSlate failures are
reported separately. TwoSided runners both exit 0; R1 reads NOT SOLE for
both. The residual fixture uses mode 0, so its supplementary H-61 population
is building/field-valid, not item 1.2's pre-registered class-8 population.
