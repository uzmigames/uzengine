# The final gather's energy deficit and gather-side coupling

## Why

Against an independent hashed-rotation reference the shipping 8-sample SDF final gather read 8.20 % low on
shadow_wall and 17.23 % low on right_shadow, without converging steadily with the sample count, and the reference's
own convergence was never shown (H-34). Changing only the gather's rotation moved the SDF reflection term
deterministically (H-35). Under r.GI.FinalGather.FreezeAfterFrame, specular changed on the first reuse and AO showed a
nine-frame transient (H-28d).

Origin: followup-12 close-out (2026-10-08): ledger H-34, H-35, H-28d, recorded off the flicker path, owned by nothing.

## What Changes

- Establish the reference's convergence and uncertainty; re-measure the deficit on the current build (scripts/run_gather_bias_study.py into a new directory); decide defect or not without losing the parity-locked rotation's stability (H-36).
- Find the gather-to-reflection coupling route with a confinement test (the radiance-cache probes ShadeSdfReflectionHit samples are the candidate).
- Re-run scripts/run_h28_revalidation.py; if the breach reproduces, test resource aliasing and barriers (the render-graph state registry work).

## Impact
- Affected specs: gi-final-gather-bias-coupling (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.
