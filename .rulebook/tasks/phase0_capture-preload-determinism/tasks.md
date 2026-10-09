## 1. Evidence (done 2026-10-09)
- [x] 1.1 Root cause measured: pre-load 43 vs 44 frames; TAA history carries the difference; equal pre-loads reproduce (proposal).

## 2. Fix
- [ ] 2.1 Pre-load mode: the mesh-SDF bake queue waits for its worker preparations and starts bakes in mesh order.
- [ ] 2.2 Pre-load length in the run manifest.
- [ ] 2.3 Audit the other asynchronous readiness sources (texture decodes, copy uploads, async slots) for the same dependence.
- [ ] 2.4 The allocation-layout dependence: find the pass whose output moves when later resources land elsewhere (an uninitialised read or an index-dependent path; three references moved by one code value when the RT UV test pass skipped its 1920x1080 UAV and two descriptors, render-graph state harness 2026-10-09).

## 3. Verification
- [ ] 3.1 Six-process Sponza gate (proposal), with the build before as the negative control.
- [ ] 3.2 followup-09 `test_region_capture.py` 23/23 and followup-14 `test_correction_gates.py --repeats 4` 11/11.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
