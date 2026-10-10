## 1. Evidence (done 2026-10-09)
- [x] 1.1 Root cause measured: pre-load 43 vs 44 frames; TAA history carries the difference; equal pre-loads reproduce (proposal).

## 2. Fix
- [x] 2.1 Pre-load mode: the mesh-SDF bake queue waits for its worker preparations and starts bakes in mesh order. **Done 2026-10-09** (`r.GI.MeshSdf.PreloadWait`, default 1; `r.GI.MeshSdf.Diagnostic.PrepareDelayMs` for the gate).
- [x] 2.2 Pre-load length in the run manifest. **Done** (`frames.preload`; the gate runs read it).
- [x] 2.3 Audit the other asynchronous readiness sources (texture decodes, copy uploads, async slots) for the same dependence. **Done**: mesh loads are budgeted 4 per pre-load frame, texture decodes are drained to completion every pre-load frame, copy uploads are waited for; with the bake wait alone every process pre-loads in 38 frames, under CPU load too (docs/analysis/rendering-arch/flicker/capture-preload-determinism.md).
- [x] 2.4 The allocation-layout dependence. **Moved 2026-10-09** to `phase0_frame-allocation-layout-dependence`, which owns it (a different mechanism from the pre-load; this task's gate does not depend on it).

## 3. Verification
- [x] 3.1 Six-process Sponza gate (proposal), with the build before as the negative control. **PASS 2026-10-09** with the registered amendment: `--gate` A/B 38 frames in all six, screenshots within 1 code value, C (no wait, delay) 45; `--stress 32` six processes all 38. The build before split 38/39 under the same load.
- [x] 3.2 followup-09 `test_region_capture.py` 23/23 and followup-14 `test_correction_gates.py --repeats 4` 11/11. **Both on 2026-10-09** on the fix build; followup-14's default spread 0.192 -> 1.19e-07.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation (docs/analysis/rendering-arch/flicker/capture-preload-determinism.md, known-issues, ledger H-73)
- [x] 4.2 Write tests covering the new behavior (scripts/rendering/test_preload_determinism.py: the intervention gate and the stressed run)
- [x] 4.3 Run tests and confirm they pass (gate PASS, stressed PASS, followup-09 23/23, followup-14 11/11, the identity regression on the small references)
