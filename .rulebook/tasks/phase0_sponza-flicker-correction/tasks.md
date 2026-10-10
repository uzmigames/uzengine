## 1. Items
- [ ] 1.1 Complete the acceptance criteria under a new dated pre-registration (M1 tolerance, an energy criterion, a shadowed-wall ROI above the floor, right_shadow, lit_floor, the door in motion, the W window); build the E1..M2 harness and show its no-op and blur controls fail.
- [ ] 1.2 Re-measure the baselines and followup-09's per-ROI thresholds on the current build; decide whether scripts/sponza-flicker-*.uzscene take the Sponza SkyLight recalibration.
- [ ] 1.3 Run the owed diagnostics: H-45 at right_shadow, a shadow-map freeze CVar (H-18b), the GTAO period test (H-09b), the window interventions never tried (cycle-averaged clamp bounds, exact history read) and the AABB motion repeats (H-23b).
- [ ] 1.4 Get the user's judgement on whether the window residual as modelled by H-27 is acceptable; then choose and validate a correction per region, or record the acceptance.

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
