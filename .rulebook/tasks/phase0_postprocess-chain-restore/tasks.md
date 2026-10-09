## 1. Baseline and gates
- [x] 1.1 Record the before state (`scripts/test_postprocess_chain.py --before`: only Bloom and Tonemap in every configuration; all-off floor 0).
- [x] 1.2 Gates I1 / I2 (re-scoped 2026-10-09 when the first premise proved false; see the proposal).

## 2. The change
- [x] 2.1 Comments and CVar descriptions in `frame_postprocess.cpp`, `frame_lighting.cpp`, `lens_flare_pass.cpp`, `lua_renderer_api.cpp` (no rendering change).
- [x] 2.2 `SetLensFlareEnabled` sets `r.LensFlare` (checked with a temporary Lua script: true / 1 at 0, 5 and 20 frames after the call).

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation (`docs/analysis/postprocess-chain-status.md`; the proposal's correction).
- [x] 3.2 Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/test_postprocess_chain.py`: the inventory and the identity against the before captures).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — I1 PASS (no scaffold pass registered in any configuration). I2 as registered FAILS on Sponza only: the reflection reference's default and all-off frames are bit-identical across the change (also across Debug vs ReleaseFast), while Sponza's frame 120 reads mean 0.88 / max 5 code values against the before capture. The before was a ReleaseFast build and the after a Debug build; two Debug runs of the same binary read mean 0.34 / max 6, and the async mesh-SDF bake (block B-03) advances with CPU speed, so Sponza's GI state at frame 120 depends on the build configuration. The change renders nothing (comments and a setter no frame calls).
