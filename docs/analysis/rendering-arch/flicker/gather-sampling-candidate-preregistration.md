# Pre-registration: a sampling candidate for the final gather

**Written 2026-09-10, before any candidate was implemented or measured.** Nothing in this document
may be revised after a number is seen. If a criterion turns out to be wrong, the correction is
recorded as a separate, dated entry saying what changed and why, and the original stays.

Task: `phase0_sponza-followup-12-isolate-shadow-lighting`. Ledger rows H-30, H-31.

## What is being corrected, and what is not

H-31 established, in one ROI and confirmed in a second pose, that the gather's input alternates with
frame parity and that the alternation is attached to the **sample sequence**: shifting only that
sequence by one frame swaps the bright and dark groups completely, while shifting by two returns
them. The seed advances by an odd stride, so its low bit flips every frame, and the base-2 radical
inverse maps that low bit to the high bit of the rotation — which fixes *which half of the turn*
each parity falls in.

What is **not** established, and must not be assumed by the candidate's design or its evaluation:

- that consecutive frames differ by half a turn (measured: median circular distance 0.326, max
  0.492, none within 1e-6 of 0.5);
- that the jitter plays no part — it was active throughout and is unexcluded as an interacting term;
- that every resource along every ray path is constant, which the probe reads do not cover;
- that the same structure reaches `GatherSdfReflections`, which shares the construction but has been
  measured separately or not at all.

## The candidate

**Requirement R0 — it replaces the frame decorrelation, nothing else.** The candidate changes how
the sample set is rotated from frame to frame. It does not change the ray count, the cosine-weighted
hemisphere distribution, the estimator, the trace, the denoiser or the accumulator. "More random" is
not a design: a sequence with worse stratification would trade a periodic artefact for a noisier
image and could pass a period-2 test while looking worse.

**R1 — the per-frame point set is unchanged in distribution.** The rotation must remain a
measure-preserving shift of the same Hammersley set (a Cranley–Patterson rotation), so each frame's
set is still cosine-weighted and still stratified. Verified by: the elevation distribution and the
mean pairwise cosine within a set must match the baseline's to within 1 %, per parity and overall.

**R2 — the ray count is unchanged.** Eight, from `r.GI.FinalGather.Samples`. Verified by the
recorded ray count and the completeness gate (the contributions must average to the trace output up
to the one constant factor).

**R3 — the expected energy is unchanged.** The estimator's expectation does not depend on the
rotation, so the long-run mean must not move. Verified by: the region mean over 128 frames within
1 % of the baseline's, and — because a regional mean can hide compensating changes — the same
within each of at least three ROIs across two poses.

**R4 — the rotation sequence has no low-order structure in the frame index.** Stated as a property
to be tested, not a property of a particular formula: over 128 consecutive frames, the rotation
series must show no bin of its power spectrum holding more than 10 % of the total, and the
group-mean separation by frame parity must be smaller than the baseline's by at least 20x.

The candidate to be implemented and measured under these requirements is a **Kronecker (golden
ratio) rotation in the frame index**: the per-frame offset advances by an irrational-derived
constant rather than by a fixed integer stride through a bit-reversal. It satisfies R1 and R2 by
construction (it is still a toroidal shift of the same set) and R3 by the same argument; R4 is the
part that has to be measured rather than argued. It is implemented behind a CVar defaulting to the
current behaviour, exactly as the followup-14 candidate was.

## Acceptance criteria

All measured against the baseline in the same captures, 128 consecutive frames, three repeats per
condition, worst repeat deciding. A candidate must pass **every** criterion; failing one is a
rejection, not a trade to be argued.

| | criterion | threshold |
|---|---|---|
| **C1** | period-2 share of the gather input's region-mean variance | ≤ 10 % (baseline 99.1 %) |
| **C2** | parity separation of the gather input's region-mean series | no longer separates, in every repeat |
| **C3** | **total** temporal variance of the gather input's region-mean series | ≤ baseline. This is the criterion that stops period-2 being exported to other frequencies: removing the parity structure while raising the overall variance is a failure, not a partial success |
| **C4** | error against an independent reference — a high-sample-count gather accumulated over many frames with the camera static, which neither condition is tuned to | ≤ baseline, per ROI |
| **C5** | energy: region mean over the sequence, per ROI, both poses | within 1 % of baseline (R3) |
| **C6** | convergence: frames for the accumulated output to settle within 1 % of its final value from a cold history | ≤ baseline + 4 frames |
| **C7** | motion: the same measurements on a moving-camera capture, where reprojection rejects history and the gather's own sequence matters more | C1–C3 hold, and no ROI's oscillation rises above baseline |
| **C8** | cost: GPU time of the SSGI passes, from the per-pass timestamps | ≤ baseline + 2 % |
| **C9** | the flicker the programme is actually about: per-ROI oscillation of the **TAA output** | ≤ baseline, and the followup-09 per-ROI thresholds are not exceeded |

C9 is listed last deliberately. C1 and C2 concern the mechanism; C9 concerns the symptom, and a
candidate that fixes the mechanism without moving the symptom has still failed to justify a change
to shipping behaviour — it would mean the mechanism was not what drives the residual, which is a
finding about H-31's reach and not a reason to relax the criterion.

## Controls that must accompany the result

- **The intervention fired.** As in the phase study: the capture itself must show the candidate's
  rotation in the recorded values, not merely the CVar having been passed. A control that did not
  fire is void, and this has already happened once in this task.
- **A negative control that can fail.** The candidate measured with its CVar off must reproduce the
  baseline exactly, bit for bit where the records allow it. If "off" differs from baseline, the
  change is not confined to what it claims.
- **The reference is independent.** C4's reference must not be produced by either condition's
  sampling sequence.

## Reflections

`GatherSdfReflections` builds its rotation from the same seed and the same radical inverse, so the
same parity structure is *expected* there. Expected is not measured. It is tested separately, with
its own capture and its own ROIs, before any claim is made about it and before any change touches
it. Similar code justifies the test; it does not extend the diagnosis.

---

## Follow-up, 2026-09-11 — what C5 assumed

Dated entry, appended. Nothing above is revised: the candidate was evaluated against the criteria as
written and rejected, and that verdict stands.

**C5 took the baseline's energy as correct.** It required the candidate's region mean to stay within
1 % of the baseline's, on the reasoning in R3 that a Cranley-Patterson rotation is measure-preserving
so the expectation cannot move. The reasoning is right about the limit and silent about the finite
case, and the finite case is what ships.

Measured against an independent reference (ledger H-34 — rotation mode 2, an unbiased hashed
rotation per pixel and frame, 32 samples over 128 frames), the shipping 8-sample gather is **8.2 %
low on `shadow_wall` and 17.2 % low on `right_shadow`**, and the candidate is within 2 % on both. So
C5 was measuring the candidate's distance from a biased value, and passing it would have required
reproducing the bias.

What this does and does not change:

- It does **not** revive the candidate. C3 fails on pose A (+63 % total temporal variance) and C8
  fails on cost (+6.88 %); neither depends on C5's assumption, and the pre-registration's rule is
  that one failure is a rejection.
- It does mean **any future criterion of this kind must name its reference**, not a condition. A
  criterion phrased "within x % of baseline" is only as good as the baseline's own accuracy, and
  here the baseline's accuracy was the thing in question.
- It opens a defect that is not about flicker at all: a systematic energy deficit in the final
  gather, recorded as H-34 and to be pursued on its own terms rather than as part of this task's
  flicker work.

The first reference construction is kept on the record as well: 32 samples under each scheme,
required to agree within 1 %, disagreed by 1.56 % and 3.75 %. Raising the sample count of a scheme
does not make it scheme-independent, and a reference that fails its own agreement gate must not be
used to adjudicate — it was not.
