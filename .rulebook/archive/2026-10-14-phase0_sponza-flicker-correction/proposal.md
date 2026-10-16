# Select, implement and validate corrections for the attributed Sponza flicker regions

## Why

The flicker programme (followup-09..14) attributed the Sponza flicker regions at pass level but chose no
correction: the rear-door glass (coverage flips at the opaque silhouette, H-47; energy lost in the TAA
reconstruction / history, H-46, H-48), the shadowed walls (H-28c, H-36, H-42), right_shadow (translucent-dominated,
H-38b), lit_floor and the door's non-glass share (a shadow-map update control never built, H-18b), and the pose-W
window's static residual and HDR-weighting energy bias (H-22, H-24b, H-25b, H-26, H-27), which the only candidate
(r.TAA.ReconstructionHdrWeightExponent) failed to fix. Followup-12's 2026-09-17 acceptance criteria are incomplete
(no usable M1 recovery tolerance, a per-mask energy ratio on a 137-pixel mask, no window ROI), and every baseline
predates b8ff5077, c14215ed, 8aeb8917, 31b5c14a and 513e2983.

Origin: followup-12 close-out (2026-10-08), with the pose-W window residual that followup-10 and followup-14 proposed as followup-15 merged in.

## What Changes

- Complete the acceptance criteria under a new dated pre-registration (M1 tolerance, an energy criterion, a shadowed-wall ROI above the floor, right_shadow, lit_floor, the door in motion, the W window); build the E1..M2 harness and show its no-op and blur controls fail.
- Re-measure the baselines and followup-09's per-ROI thresholds on the current build; decide whether scripts/sponza-flicker-*.uzscene take the Sponza SkyLight recalibration.
- Run the owed diagnostics: H-45 at right_shadow, a shadow-map freeze CVar (H-18b), the GTAO period test (H-09b), the window interventions never tried (cycle-averaged clamp bounds, exact history read) and the AABB motion repeats (H-23b).
- Get the user's judgement on whether the window residual as modelled by H-27 is acceptable; then choose and validate a correction per region, or record the acceptance.

## Impact
- Affected specs: sponza-flicker-correction (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.
