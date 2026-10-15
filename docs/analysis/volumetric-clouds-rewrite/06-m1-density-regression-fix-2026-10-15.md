# M1 density-default regression fix (2026-10-15)

## Symptom
After M1-M3 the volumetric clouds rendered as a FLAT WHITE OVERCAST deck for the
shipped scenes (coverage 0.45), instead of the appealing pre-M1 BROKEN CLOUDS with
blue-sky gaps. User flagged this on a visual render review.

## Root cause (not lighting, not a coverage error)
M1 (`b5232d3d`) changed the DEFAULT shape-density algorithm and did not re-calibrate
coverage for the new field:
- Pre-M1: procedural FBM shape + transfer `saturate(shape + coverage - 0.5)` (shape
  median ~0.3-0.6) -> broken clouds at coverage 0.45.
- Post-M1 default: the Perlin-Worley shape TEXTURE + transfer
  `saturate((shape - (1-coverage)) / coverage)` squared (texture median ~0.8039) ->
  at coverage 0.45 almost nothing falls below threshold (0.138% of base voxels, 0% at
  mip 3) -> a continuous overcast deck. M1 introduced
  `r.Cloud.ProceduralNoise=false` (cloud_pass.cpp), flipping the default silently for
  existing scenes. M2 (`8c50c1c0`) did NOT change density/LOD.

Diagnosis evidence (local, gitignored screenshots): an A/B/C toggle study proved it is
NOT lighting (disabling aerial perspective made contrast WORSE 0.124->0.026; the M2
ambient term adds white but no gaps; mip smoothing only +4-5% contrast). A coverage
sweep (0.45/0.30/0.20/0.15) showed lowering coverage compensates but 0.30 gives busy
altocumulus (not the baseline look) and 0.20/0.15 go empty.

## Fix (minimal, root-cause)
Default `r.Cloud.ProceduralNoise` false -> **true** (cloud_pass.cpp:48). This restores
the pre-M1 procedural FBM shape as the default, which reproduces the baseline broken-cloud
look at the ORIGINAL coverage 0.45 while KEEPING every M1-M3 win (adaptive stepping, the
256-iteration cap, STBN jitter, detail mip chains, M2 multi-scatter lighting, M3 temporal
upscale). The Perlin-Worley shape-texture path stays as an explicit opt-in
(`r.Cloud.ProceduralNoise=false`) for future intentional density/morphology calibration.

## Verification
Restoring the procedural branch (equivalent to the flipped default) at coverage 0.45:
| Pose/state | Display contrast | Strict clear fraction |
| --- | ---: | ---: |
| Sun pre-M1 target | 0.2441 | 1.681% |
| Sun current (overcast) | 0.1240 | 0.772% |
| Sun restored | 0.2389 | 1.684% |
| Away pre-M1 target | 0.4565 | 5.812% |
| Away current (overcast) | 0.1196 | 0.000% |
| Away restored | 0.4529 | 6.172% |

Restored contrast = 97.85% / 99.21% of the pre-M1 target; blue-sky openings return in the
same large-scale locations (visually confirmed vs the baseline montage). Build exits 0;
gtests 7661 pass / 3 pre-existing VfxEditorSlate (0/0/0 delta). Follow-up: the opt-in
shape-texture path needs a calibrated density/morphology remap before it can be the default.
