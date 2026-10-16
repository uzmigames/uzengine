# Remaining Sponza flicker regions after the window fix, 2026-10-13

User direction: after the pose-W window (H-27) fix, continue to the other regions in the proposal
(rear-door glass, shadowed walls, lit_floor). Measured landscape on the shipped StableRecon-on build
(64-frame captures, linear Rec.709; std = mean per-pixel temporal std; energy = output.mean / the
supersampled raw-cycle mean):

| ROI (pose) | std | energy | mechanism / verdict |
|---|---:|---:|---|
| rear_door (A) | 0.000195 | 0.518 | HDR-weighted reconstruction+blend drains ~48% of the glass energy — real defect, CORRECTED (FlipBlend) |
| right_shadow (A) | 0.000190 | 0.804 | same mechanism; lifted for free by the fix |
| lit_floor (A) | 0.001053 | 0.996 | TAA texture AA on the sunlit floor — ACCEPT |
| shadow_wall (W) | 1.64e-5 | 0.999 | minuscule absolute, GI drift owned by the gather task — ACCEPT |

## Diagnostics (owed, now run)

- **H-18b (shadow-map freeze)**: `r.Shadow.FreezeAfterFrame` already exists (frame_shadows.cpp:24).
  Freezing the shadow map changes lit_floor std by 0.0% — shadow-map update is NOT the cause. (It
  does not hold the receiver-side PCSS kernel rotation, shadow_pcss.hlsli:21, but that does not
  change the verdict.)
- **H-09b (GI/AO ablation)**: forcing r.GTAO/r.SSGI/r.SSR off changes lit_floor std by +0.2% (noise)
  — GI/AO temporal is NOT the cause. lit_floor is inherent TAA texture AA (0.41% of the mean, below
  perception). ACCEPT.
- **H-23b (AABB motion)**: skipped (architect triage — nothing above depends on ClampMode=3, which is
  already rejected on still-camera grounds).

## rear_door correction: FlipBlend (`r.TAA.FlipBlend`, default OFF — opt-in)

Mechanism (architect, claude-fable-5-1): the glass loses energy in two stages — the HDR-weighted
plus-filter reconstruction (a dark occluder tap weighs ~8.5x a bright glass tap, so a coverage-flip
pixel reconstructs at ~0.2x energy) and the HDR-weighted blend fixed point (a further ~0.81x). The
window's StableRecon gate does not cover this because rear-door glass is nearly all 1-3 px rim pixels
(coverage-flip), not coverage-stable pane.

Design (complement of StableRecon): at static, neighbour-corroborated coverage-flip pixels (not
stable, not isolated-bright, low velocity) use the spatial-only reconstruction (drops the HDR tap
weighting) and equal blend weights (drops the blend's HDR bias). Isolated bright samples (fireflies),
moving pixels, and StableRecon-stable pixels keep the Karis path. `taa_resolve_cs.hlsl`: a parallel
`filteredSpatial` sum, a `flipConf` gate, and the blend `filterWeight` folded toward `historyWeight`
by flipConf. Four CVars (FlipBlend, FlipBlendIsolationRatio 2.0, FlipBlendVelocityPx 4.0,
FlipBlendWeight -1). Requires `r.TAA.StableRecon=1` (the complement gate).

### Static acceptance (FlipBlend 0 -> 1, StableRecon on) — PASS

| metric | off | on | target | verdict |
|---|---:|---:|---|---|
| rear_door energy | 0.518 | 0.909 | 0.85-1.02 | PASS |
| right_shadow energy | 0.804 | 1.017 | >=0.90, <=1.02 | PASS |
| window boundary energy | 0.306 | 0.998 | >=0.80 | PASS |
| window bright_std | 0.000726 | 0.000839 | ~unchanged | PASS (23x below the 0.002 target) |
| window boundary_std (abs) | 0.044182 | 0.060235 | relative <=1.1x | PASS: +36% abs, but energy +3.3x so RELATIVE flicker dropped to ~0.41x |
| lit_floor std / energy | 0.001053 / 0.996 | 0.001037 / 0.999 | unchanged | PASS |

Visual (`.screenshots/flipblend-visual/rear_door_montage.jpg`): the glass dots brighten with the fix;
the rest of the frame is unchanged (localized to the coverage-flip glass).

### Motion caveat — why FlipBlend ships OFF

Motion captures (sponza-flicker-a-motion / w-motion, FlipBlend 0 -> 1):

| ROI | off std | on std | note |
|---|---:|---:|---|
| W motion window | 2.2e-5 | 2.2e-5 | 0.0%, bit-identical |
| A motion right_shadow | 0.08523 | 0.08977 | +5.3% std, +14% mean |
| A motion rear_door | 0.00230 | 0.01169 | **+408% std, +113% mean** |

Under camera motion the rear-door glass recovers its energy (mean up, correct) but its coverage-flip
rims show markedly more temporal variation. The velocity gate cannot suppress this: the rear door is
far from the camera, so its screen velocity stays below 1 px/frame even while the camera strafes, and
lowering `FlipBlendVelocityPx` to 1 only moves rear_door motion std from +408% to +358% while the
static fix is unchanged (energy still 0.909). Whether the brighter-but-livelier glass under motion
reads as a correct energy recovery or as objectionable rim shimmer is a visual judgment the metric
cannot settle, so FlipBlend ships OFF (opt-in via `r.TAA.FlipBlend=1`) pending that judgment. The
static win is unconditional; the default flip is the open decision.

Build: zig exit 0, Shader integrity OK (139). Owned gtests 167 passed / 1 skipped (Metal) / 0 failed.
