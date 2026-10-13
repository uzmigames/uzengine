# Measurement tools

`scripts/rendering/run_sponza_flicker_measurement.py` captures baseline,
diagnostic and motion stages serially. Each capture retains the engine
manifest, log, region records and reference screenshot. The report inherits
executable identity from the capture manifest, checks requested CVars,
requires the complete contiguous frame range and rejects invalid GPU stamps
or nonfinite color. It streams record files into the channels needed for
analysis rather than keeping every raw record resident in memory.

Raw evidence receives transparent NTFS compression because the shared
volume has limited free space. Nothing is deleted. A completed capture can
be reanalysed; an incomplete existing manifest is rejected rather than
quietly being reused. Use a new output directory for a changed binary.

The main JSON records per-pixel temporal luminance standard deviation,
energy ratios, absolute per-pixel energy error, cycle-average Sobel detail,
per-pixel spectral power, RGB energy, and history-versus-previous-output
error. Spectral power weights the conjugate bins twice; the GTAO diagnostic
uses a 192-frame observation window divisible by 2, 6, 8 and 24.

## Opt-in GPU diagnostics

`r.Shadow.FreezeAfterFrame` defaults to -1. After its frame, shadow-pass
registration stops and cascade constant recomputation is held, while a
fresh constant-buffer ring slot receives the previous shadow constants.
Holding both is necessary: stopping atlas rendering but changing its
projection would not isolate shadow-map updates. The first frozen frame
logs `ShadowFreeze active`. This is a diagnostic, not a motion-safe
production rendering mode.

`r.TAA.ClampFreezeAverageFrames` defaults to 1, preserving the existing
last-box freeze. At 8, the final eight boxes through the existing freeze
frame are incrementally averaged in the existing RGBA16F diagnostic
storage. Earlier frames initialize the running value; the first averaging
frame resets it. The frame after the freeze switches the resources to
SRV reads. This includes normal half-float storage rounding, not an
ideal float64 mean. Frozen boxes only have meaning for a static camera.

`r.TAA.HistoryReadMode` defaults to 0 (the existing bicubic history).
At 1, history uses a nearest-texel `Load` at the same clamped reprojected
position. This isolates the history sampling operation; it does not remove
reprojection, clamping, current reconstruction or accumulation.
The CPU/HLSL constant-buffer layouts carry these opt-in parameters together.

The initial diagnostic smoke capture exited 3 because its new unsigned
freeze-frame upload directly converted the float sentinel -1 to uint32.
Conversion now passes through signed int first, so disabled -1 wraps by
the defined integer conversion. No data from that interrupted capture is
reported as a measurement.

## Harness and motion coverage

The no-op, spatial blur and temporal-plus-spatial blur controls are applied
to recorded linear output; they do not change the rendered scene.
The latter separates successful suppression of oscillation from loss of
detail. Flat images report the detail ratio as unmeasurable instead of
dividing by zero.

Pose A has separate measurement scene and Lua trajectory files. The
door, right shadow and floor therefore receive the same hold, 60 cm
strafe, hold, cut-back and hold exercise as pose W. Motion-start indices
come from each run's Lua event. Cut error compares with that condition's
pre-motion settled image; the recovery tolerance comes only from baseline
repeats and is then shared by every variant. Censored recoveries never
count as passing M1. M2 measures the maximum individual pixel error,
separately from the spatial-mean error used for recovery.

The existing consumption study accepts `--roi right_shadow`, retaining
its records-off/on equivalence, overlap, RGB reconstruction and coverage
gates. It stops without a causal reading when an instrumentation gate fails.

The new CPU test module covers cancellation-free energy, no-op rejection,
blur rejection, conjugate spectrum weighting, usable recovery tolerance,
censoring and per-pixel overshoot. The GPU harness covers the new diagnostic
branches; no standalone CPU mock of a GPU pass is presented as that coverage.
