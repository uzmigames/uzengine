## 1. Baseline and gates
- [x] 1.1 **Done** Record the before state (doc "Before"): the sphere-only assignment; the cluster light counts on the two scenes with the cone test off (`scripts/rendering/test_cluster_spot_cone.py` measures both on one binary).
- [x] 1.2 **Registered; results in the doc gate table** Pre-register K1-K4 (proposal).

## 2. The cone test
- [x] 2.1 (K1 PASS, K2 PASS) `cluster_assign_cs.hlsl`: the cone-sphere test after the range test; `light_types.hlsli` / `constant_buffers.h`: `kClusterFlag_SpotCone`.
- [x] 2.2 (K3 PASS, K4 PASS) `frame_constants.cpp`: `r.Lights.ClusterSpotCone` and the flag.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-cluster-spot-cone.md`, the known-issues entry, the block-B index, the B.7 document) Update or create documentation covering the implementation (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-cluster-spot-cone.md`; the known-issues "Local lights" section; the block-B index and the B.7 document's open list).
- [x] 3.2 (the sphere-only assignment as the control, the identity, the slot count, the timings) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/rendering/test_cluster_spot_cone.py`: the sphere-only assignment as the control, the identity of the lighting, the slot count and the timings).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_cluster_spot_cone.py` 4/4 on the final binary: K1 identity (taa and direct 0 on both scenes), K2 mean cluster light count dense 126.63 -> 66.88 (47.2 % fewer; ref 8.9 %), K3 LightCull 0.0704 -> 0.0704 ms, K4 DeferredLighting 0.897 -> 0.714 ms. renderer gtests 7471 passed.
