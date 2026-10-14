# 06 — Item 1.3: P2 against a physical irradiance reference

Date: 2026-10-13. Scope: compare the shipped engine's diffuse-indirect (P2) at fixed
receivers against an independent physical Lambertian irradiance solution. No adoption
decision is made from this measurement (it has no pass/fail oracle threshold on the
engine; the only gate is on the reference's own precision).

## Method

`scripts/rendering/run_probe_reference_irradiance.py` with the CPU solver
`probe_reference_irradiance.py`:

- 64 receiver positions per label, sampled from the captured region.
- Independent reference: exact ray/box visibility, cosine-hemisphere Monte Carlo,
  next-event sun, and the captured cubemap as the incident environment (not RC/DDGI
  coefficients). Four fixed-seed replicates (1701..1704), 2048 rays/position, depth 12.
- Receiver BRDF factored out analytically (Karis environment-BRDF fit, roughness 1,
  albedo 0.5): `receiver_diffuse_factor = 0.49176`.
- Omitted-tail upper bound (throughput `rho**12` at depth 12): `3.82e-4` luminance,
  folded into the precision gate.
- The physical box visibility differs deliberately from the biased SDF; this is a
  calibration reference, not a replacement estimator.

## Result

Oracle precision gate (`CI half-width + omitted-tail bound <= 1% of oracle mean`):
**PASS** for both labels — the reference is precise enough to trust at the 1% level.

| Label | engine (P2) | oracle mean | engine / oracle | 95% CI half-width |
|---|---|---|---|---|
| CORE:lit_face | 0.32731 | 0.34929 | **0.9371** | 0.00143 |
| NEAR_IN:lit_face | 0.32692 | 0.34943 | **0.9356** | 0.00144 |

The engine reads about **6.3–6.4% below** the independent physical reference.

## Interpretation

The reference integrates exact box visibility; the shipped SDF gather uses
voxel-conservative visibility over a finite clipmap, a deliberately biased estimator.
A ~6% low read is consistent with that bias and with the `gi-final-gather-bias-coupling`
finding (the diffuse deficit is dominated by low-sample-count convergence, not an energy
defect). Recorded as a calibrated measurement with a precise oracle; no leak/adoption or
threshold decision is asserted, per the task and the script's own stated assumptions.

Report: `.screenshots/rc-probe-support-followups/item13/probe_reference_irradiance.json`.
