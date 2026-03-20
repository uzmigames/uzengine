## Phase 1: Diagnose
- [ ] 1.1 Pass dump with `--pass-toggle tonemap` to bypass tonemap — check if LDR is correct without it.
- [ ] 1.2 Pass dump with `--pass-toggle color_grading` to bypass color grading LUT.
- [ ] 1.3 Check auto-exposure output value — log the exposure float each frame.
- [ ] 1.4 Check `tonemap_ps.hlsl` for channel swizzle (RGB vs BGR), incorrect ACES matrix, or wrong LUT sampling.
- [ ] 1.5 Check `color_grading_lut_cs.hlsl` for LUT generation correctness (neutral LUT should be identity).

## Phase 2: Fix
- [ ] 2.1 Fix the identified root cause.
- [ ] 2.2 Build + pass dump comparison: final-ldr should match HDR after tonemap (correct colors).

## Phase 3: Validation
- [ ] 3.1 Visual verification at multiple camera positions.
- [ ] 3.2 Compare with reference (UE5 default scene tonemap).
