## 1. Reproduce and attribute
- [x] 1.1 Reproduce with a static camera at the reported pose. **Done** — `scripts/sponza-closeup-camera.*`: TAA on 1.06 / 2.45 / 1.54 mean |Δ| with 0.25 / 1.11 / 0.70 % px > 40; TAA off 0.94–0.98 with 0.00 %. Cornice-edge pixel 180 → 188 → **31** → 179 (unjittered: 194 constant); scanline edge moves 6–8 px in one frame. Region grid: far half accumulates, near half does not.
- [x] 1.2 Eliminate the screen-space passes, the resolve's mechanisms and the post chain. **Done** — GTAO / final gather / SDF reflections / all three, anti-flicker cap 0.04, variance clip off, bloom off, CAS off, motion blur off: 0.84–1.33 / 0.18–0.29 % (no change). 1-D resolve model ≤ 2/255.
- [x] 1.3 Find the mechanism. **Done** — the jitter offset was added to `VP[2][0]/[2][1]` of the combined view-projection: a `jitter·world_z/w` shear, ±7 px on the near wall, ~0.2 px far away. Same code in SponzaTest; masked until `r.TAA` was enabled today.

## 2. Fix
- [x] 2.1 `taa_jitter.h` — `ApplyTaaJitter` folds the NDC translation through the w column; `frame_setup.cpp` uses it. **Done.**
- [x] 2.2 Bloom in the TAA path reads the TAA output (`tonemapInputSrv`, output resolution) instead of the jittered HDR. **Done** (hygiene; not the cause — `r.Bloom=0` control 0.98 / 0.23 %, 2.35 / 1.07 %).
- [x] 2.3 Verify with the fixed binary. **Done** — static close-up: 0.19 / 0.00 %, 0.22 / 0.00 %, 0.19 / 0.00 % (before 1.06 / 0.25 %, 2.45 / 1.11 %, 1.54 / 0.70 %; TAA off 0.94–0.98 / 0.00 %) — now below the TAA-off noise floor, the cornice-edge scanline identical across frames, changed-pixel grid (threshold 4) 92 897 → ~800. Approach (2 cm/frame, TAA on): 9.4–9.5 / 4.3–4.4 % (before 8.5–9.3 / 3.6–4.3 %; TAA off 12.8–12.9 / 7.2–7.5 %) — unchanged within noise: at 2 cm/frame next to a wall the adjacent-frame difference is the camera motion itself, so the static test is the discriminating one.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation — notes.md, `docs/analysis/known-issues-and-partial-implementations.md`, `docs/engine/12-rendering/33-sdf-gi-final-gather.md` cross-reference.
- [x] 3.2 Write tests covering the new behavior — gtest `TaaJitter.*` (`runtime/tests/engine/renderer/test_taa_jitter.cpp`), `scripts/test_taa_bloom_input.ps1`, QA scenes `sponza-closeup-camera.*`, `sponza-closeup-approach.*`.
- [x] 3.3 Run tests and confirm they pass — 3/3 (`TranslatesEveryVertexByExactlyTheJitterInPixels`, `ZeroJitterIsIdentity`, `LegacyRowTwoOffsetWasDepthAndWorldZDependent`), test_runtime rebuilt 17:28; `test_taa_bloom_input.ps1` 11/11.
