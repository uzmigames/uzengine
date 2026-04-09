# Execution Plan — UE5.6 GTAO Algorithm Implementation in UzEngine

Based on findings in [findings.md](./findings.md), this plan addresses the root cause of flat AO output (0.51-0.60 range on Sponza).

## Phase 1 — Diagnostic: Verify Screen Radius Calculation

**Goal**: Confirm whether UzEngine's screen radius formula matches UE5 exactly, or if the depth division is missing/inverted.

**Depends on**: Nothing (standalone diagnostics)

Tasks:
- [ ] Add logging to screen radius computation: log `FOVScale`, `WorldRadius`, `WorldRadiusAdj`, `ViewSpacePos.z`, `PixelRadius`, `StepRadius` for a test pixel (960, 540)
- [ ] Capture logs at multiple camera depths (100cm, 500cm, 1000cm) with r.GTAO.SearchRadius=4.0 (or equivalent)
- [ ] Compare UzEngine logs to UE5 logs at same scene, same camera position
- [ ] Run --diag to confirm AO is actually flat (not just visually flat due to color quantization)
- [ ] If PixelRadius is constant (doesn't vary with depth), depth division is missing — escalate to Phase 2

---

## Phase 2 — Fix Root Cause: Screen Radius Formula

**Goal**: Implement the correct screen radius formula with depth division.

**Depends on**: Phase 1 (diagnostic confirms missing/wrong depth division)

Tasks:
- [ ] Locate UzEngine's screen radius computation (likely in GTAO shader or C++ setup)
- [ ] Verify `ViewSpacePos.z` is positive and in the correct range (should be linear view-space depth)
- [ ] Implement `PixelRadius = (WorldRadius * FOVScale) / ViewSpacePos.z` with clamping
- [ ] Rebuild and re-test with same parameters
- [ ] Capture per-pixel PixelRadius at 3 depths to verify it now scales with distance
- [ ] Visually inspect AO on Sponza — should vary from strong close-up to weak at distance

---

## Phase 3 — Validate Inner Integral & Horizon Angles

**Goal**: Ensure the horizon angle computation and integral are correct.

**Depends on**: Phase 2 (screen radius fixed)

Tasks:
- [ ] If AO is still flat after Phase 2, add GPU printf to horizon search: log horizon angles (before acos) for representative pixels
- [ ] Log projected normal magnitude (LenProjNormal in ComputeInnerIntegral) — should be non-zero for valid normals
- [ ] Log AO value before normalization and fade
- [ ] If all angles are -1 or 1 (saturated), check viewDir computation — may be inverted
- [ ] If LenProjNormal is near-zero, check normal encoding/conversion from GBuffer

---

## Phase 4 — Cross-Validate Against UE5

**Goal**: Run pixel-level comparison with UE5 GTAO output.

**Depends on**: Phase 3 (inner integral validated)

Tasks:
- [ ] Capture UE5 GTAO output (--screenshot-stdout) at test camera position
- [ ] Capture UzEngine GTAO output at same position
- [ ] Use --pixel-debug on both to read exact per-channel values at 5-10 test pixels
- [ ] PSNR compare (target >40dB for "visually identical")
- [ ] If PSNR <30dB, identify which component differs (attenuation, normal projection, integral formula)

---

## Phase 5 — Temporal Jitter & Quality Settings

**Goal**: Ensure dithering and quality levels match UE5 behavior.

**Depends on**: Phase 4 (core AO values correct)

Tasks:
- [ ] Test with TemporalAA on/off — should see noise reduction over frames
- [ ] Verify Thickness blend behavior: test r.GTAO.ThicknessBlend at 0.0, 0.5, 1.0
- [ ] Compare quality settings (Very Low/Low/Medium/High/Very High) to UE5
- [ ] Verify max pixel radius clamping at GTAO_MAX_PIXEL_SCREEN_RADIUS=256

---

## Phase 6 — Optimize & Document

**Goal**: Performance tuning and knowledge capture.

**Depends on**: Phase 5 (all validation complete)

Tasks:
- [ ] Profile GTAO pass (GPU time) vs UE5 at same quality setting
- [ ] Benchmark HZB mip escalation effect (if performance differs, adjust mip bias)
- [ ] Document any platform-specific quirks (console vs PC, DX12 vs Vulkan)
- [ ] Add code comments citing UE5 source file:line for each major step
- [ ] Update rendering architecture docs with GTAO integration notes

---

## Open Questions

1. **What is the current screen radius formula in UzEngine?** (phase1_diagnostic should answer)
2. **Is ViewSpacePos.z actually view-space depth or something else?** (verify during phase 2)
3. **Does UzEngine use Hi-Z or full-res depth for horizon search?** (affects step size, mip bias)
4. **Are normals from GBuffer or reconstructed from depth?** (affects ComputeInnerIntegral behavior)
5. **Is the attenuation falloff (AttenFactor = 2.0/R²) correctly implemented?** (check phase 3)
6. **What is the current value of r.GTAO.ThicknessBlend in UzEngine's config?** (affects false-occlusion severity)
