## 1. Baseline and gates
- [x] 1.1 Record the before state (doc "Before"): `scripts/rendering/test_cloud_noise_assets.py --before` on the binary before the change (no noise loaded, 183 log lines about the noise files, CloudCompute 1.158 ms, no TDR).
- [x] 1.2 Pre-register N1-N5 (proposal).

## 2. The lookup
- [x] 2.1 `runtime/build.zig`: install `runtime/content/atmosphere` as `bin/content/atmosphere` (N4 PASS).
- [x] 2.2 `cloud_pass.{h,cpp}`: `ReadCloudAsset` (project VFS, engine content, working directory), one attempt per lifetime (N1, N2, N3 PASS).
- [x] 2.3 The MIT notice beside the data; `docs/references.md`.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation (`docs/analysis/atmosphere-sky/clouds/clouds-noise-assets.md`; the clouds task's 0.B4 / 0.12 ticks; `docs/analysis/hardcoded/03-content-paths.md` 3.3).
- [x] 3.2 Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/rendering/test_cloud_noise_assets.py`: the before run as the negative control, the installed bytes, the log).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_cloud_noise_assets.py` 4/4 on the final binary (before: 1/3); N5: 95.8 % of the pixels changed, mean |Δ luminance| 0.0071; CloudCompute 1.158 → 1.297 ms.
