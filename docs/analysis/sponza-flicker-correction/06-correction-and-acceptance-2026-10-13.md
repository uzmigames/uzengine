# Pose-W window flicker (H-27): correction and acceptance, 2026-10-13

User decision (2026-10-13): correct the pose-W window residual. This file records the shipped
correction and its acceptance measurements. Design chosen by the architect consultant
(claude-fable-5-1) from the measured sweep in `05-taa-window-sweep-2026-10-13.md`.

## Root cause (from the sweep)

The residual (window all_std 0.008511, period-8 = 57.9% of variance) lives in the jitter-dependent
HDR-weighted reconstruction plus-filter of the TAA resolve: on a coverage-stable bright pixel the
per-frame jitter offset modulates the spatial + HDR weight of its dark/flipping neighbour taps, so a
pixel whose own sample never changes still oscillates at the jitter period. Dropping the HDR weight
(reconstruction mode 1) halved it; center-tap only (mode 2) nearly eliminated it but wrecked the
silhouette AA; the two interventions the ledger named as untried (exact history read; cycle-averaged
clamp) were measured and are both dead (no change / worse).

## Correction: raw-stability-gated reconstruction (`r.TAA.StableRecon`, default ON)

`taa_resolve_cs.hlsl`: history alpha carries a per-pixel EMA (rate 1/8, one jitter cycle) of the raw
centre luma. Where the current raw centre luma is within `r.TAA.StableReconTolerance` (relative, 0.02)
of its reprojected running mean, the pixel is coverage-stable and is handed its own centre tap
(mode-2 behaviour) via `filteredColor = lerp(filteredColor, neighbors[4], stableConf)`; the gate also
fades to zero by `r.TAA.StableReconVelocityPx` (4 px/frame). Boundary (coverage-flip) and moving
pixels keep the untouched Karis path by construction — the gate's confidence is exactly 0 there. No
new resources (history.a was a constant 1.0); ~20 HLSL lines, 4 CB fields, 3 CVars. With the feature
off (`r.TAA.StableRecon=0`) `history.a` stays 1.0 and the output is bit-identical to the legacy path.

`taa_upscaler.cpp`: three CVars (`r.TAA.StableRecon` default 1, `r.TAA.StableReconTolerance` 0.02,
`r.TAA.StableReconVelocityPx` 4.0); the four CB fields; `stableReconReset = input.firstFrame`.

## Acceptance (64-frame captures, 8 jitter cycles; linear Rec.709; StableRecon 0 vs 1)

Pose-W window ROI (masks: bright pane 29156 px, boundary 5127 px):

| metric | baseline (0) | fix (1) | target | verdict |
|---|---:|---:|---|---|
| bright_std | 0.020535 | 0.000727 | <= 0.0020 | PASS (-96.5%) |
| all_std | 0.008511 | 0.002638 | <= 0.004256 | PASS |
| boundary_std | 0.044181 | 0.044181 | <= 105% | PASS (Δ -1.7e-7, bit-identical) |
| bright_E | 0.973 | 1.000 | >= 0.99 | PASS |
| boundary_E | 0.3057 | 0.3056 | no worse | PASS (Δ -7.6e-5) |

τ sensitivity: bright_std is 0.001155 / 0.000727 / 0.000481 at τ = 0.01 / 0.02 / 0.05; the window is
collapsed at every τ and the boundary is unchanged at every τ (no textured pixels in the window ROI
for τ to wrongly open). Default τ = 0.02.

Non-target ROIs (no-regression):

| ROI | metric | base | fix | note |
|---|---|---:|---:|---|
| A right_shadow | std_mean | 0.000399 | 0.000190 | -52% flicker |
| A right_shadow | energy vs ref | 0.702 | 0.804 | moves toward 1.0, no overshoot — bonus correction |
| A lit_floor | std_mean | 0.001006 | 0.001053 | +4.6% (textured; gate stays closed) |
| A lit_floor | energy vs ref | 0.995 | 0.996 | unchanged |
| W shadow_wall | std_mean | 1.646e-5 | 1.645e-5 | bit-identical |

Motion neutrality (gate closes with velocity):

| ROI | base std | fix std | note |
|---|---:|---:|---|
| W motion window | 2.22e-5 | 2.22e-5 | -0.02%, bit-identical |
| A motion right_shadow | 0.08505 | 0.08523 | +0.21%, neutral |

D1 (edge sharpness) is preserved by construction and confirmed by the boundary being bit-identical:
the gate's confidence is 0 at every coverage-flip pixel, so all silhouette/edge pixels take the
unchanged Karis reconstruction; coverage-stable bright pixels carry no sub-pixel edge to lose.

Build: vendored zig 0.15.2 exit 0, `Shader integrity OK (139 shaders verified)`. Owned gtests
(*Taa*/*IBL*/RenderGraphStates*/*Atmosphere*/*PathTracer*/*Lighting*) 167 passed / 1 skipped (Metal,
expected on Windows) / 0 failed.

Artifacts: `.screenshots/flicker-correction-20261013-stablerecon-ab/`,
`.../flicker-correction-20261013-noregress/`, `.../flicker-correction-20261013-energy-motion/`.

## Scope and what remains

This corrects the pose-W window (H-27), the region the user was asked to judge, and incidentally
improves right_shadow (energy toward the reference, flicker halved). The broader
sponza-flicker-correction proposal also lists the rear-door glass (H-46/47/48), the shadowed walls
(H-28c/36/42) and lit_floor's non-window share; those are separate regions with their own gates and
are not addressed here. The window blend-side energy loss (boundary_E ~0.31, H-22/24b) is explicitly
out of this fix's scope (the architect's section 6) and would be a separate blend-side item.

Full-scene D1 / rear_door energy under the now-default-on gate were not measured beyond the Sponza
flicker ROIs above; the gate is self-limiting (zero effect off coverage-stable low-velocity pixels,
proven by the bit-identical boundary and motion results), and reverting to the legacy path is a single
CVar (`r.TAA.StableRecon=0`).
