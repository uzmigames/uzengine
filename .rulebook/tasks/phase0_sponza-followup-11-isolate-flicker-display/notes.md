# Notes — phase0_sponza-followup-11-isolate-flicker-display

2026-09-09. Diagnostic task: separate what the renderer computes from what the display shows. No
default changed; nothing committed.

Evidence package: `.screenshots/flicker-elimination/11-isolate-flicker-display/`
(`report.md`, `display-study.json`, `manifests/`). Ledger rows H-02b, H-08b, H-15, H-16.

## The replay is validated before it is used (1.1)

`scripts/flicker/display_model.py` implements the production chain of `tonemap_ps.hlsl`
(bloom composite → exposure → saturation → contrast → ACES Hill fit → sRGB). With bloom off the
transform is per-pixel, and the replay reproduces the engine's PNG to **1 code value, on 100 % of
the pixels**. Grain, vignette, chromatic aberration and the LUT are off by default and the replay
states that rather than ignoring it.

## Units on the same pixels (1.2)

Fixed exposure 1.0, 24 frames, ROI 192x512:

| mask | pixels | linear Rec.709 | its std | resolve YCoCg Y | display code | its std |
|---|---|---|---|---|---|---|
| bright pane | 27 712 | 8.251 | 0.0211 | 31.81 | 233.4 | 0.044 |
| dark bars | 64 441 | 0.00394 | 0.00024 | 0.0152 | 2.39 | 0.204 |
| boundary | 6 151 | 1.927 | 0.0544 | 7.43 | 182.4 | 1.576 |
| whole ROI | 98 304 | 2.449 | 0.0095 | 9.44 | 78.8 | 0.245 |

The resolve's YCoCg Y is about 3.9x the Rec.709 luminance on this content: two different
quantities that the earlier packages sometimes reported side by side without saying so.

## Exposure (1.3)

The identical HDR sequence, replayed:

| exposure | display mean | display std | px > 40 / frame | px > 8 / frame |
|---|---|---|---|---|
| 0.5x | 72.66 | 0.201 | 0.7 | 223 |
| 1x | 78.79 | 0.245 | 1.7 | 438 |
| 2x | 83.37 | 0.310 | 22.0 | 930 |

On the GPU the same three exposures leave the linear records **bit-identical**, so the count column
is a property of the display transform alone: a 4x exposure change multiplies the px > 40 count by
31x with the scene untouched (ledger H-15).

*(Correction (2026-10-08): the factor is 33.8x, 22.043 / 0.652 in `display-study.json`; 31x divided
the rounded 22.0 by 0.7, and the package report, rendered from the same JSON, says 34x (ledger
H-15b). "Bit-identical" held in these single-process runs; followup-14 later found the renderer is
not bit-reproducible across processes, so it is not a guarantee.)*

Auto-exposure settles at **66.31** on this pose and swings 0.03 % across the measured sequence, so
with the camera still it sets the gain and does not itself oscillate. Its frame-time dependence
(followup-09) is what makes it a confound *between* runs of different cost.

## Bloom (1.4)

Recorded radiance is bit-identical with bloom on and off, so everything below is display-side:

| run | dark floor (code) | dark p99 | bright (code) | contrast bright/dark | px > 40 / frame | px > 8 / frame |
|---|---|---|---|---|---|---|
| bloom off | 2.37 | 79.6 | 233.3 | 98.5 | 1.7 | 677 |
| bloom on | 5.70 | 83.8 | 233.8 | 41.0 | 1.3 | 408 |

Bloom lifts the dark floor 2.4x and halves the local contrast. Its effect on the threshold count is
**not one-directional**: here (exposure 1.0) it lowers the count, while the 2026-09-09 package
measured the opposite at the auto-exposure operating point of ~67. A count that changes sign with
the exposure cannot stand for the oscillation (ledger H-16, inconclusive as stated).

*(Correction (2026-10-08): the 2026-09-09 package measured the same direction, not the opposite:
at ~67, turning bloom off raised px > 40 from 39 to 1547 per frame over the window
(`.screenshots/flicker-region/report.md` lines 256-268 and 322; followup-08 notes.md). Bloom lowers
the count at both exposures, so it masks the residual rather than revealing it (ledger H-16b). The
px > 40 row above is 5 vs 4 crossings over 3 frame pairs from 4 PNGs, recomputed from the PNGs in
`recovered-2026-10-08/`; px > 8 is 2032 vs 1225 crossings. "Bit-identical" above is, as in 1.3, a
single-process observation.)*

## `r.TAA.PreExposeWeights` (1.5)

The flag multiplies the resolve's HDR weights by the frame exposure, so **at a fixed exposure of 1.0
it is a no-op by construction** — the first run of this study measured exactly that and the check
failed, which is why it was repeated at the operating exposure with a control at the same exposure:

| mask | radiance mean control → flag | temporal std control → flag | std p99 control → flag |
|---|---|---|---|
| bright | 8.251 → 7.954 | 0.0211 → 0.2423 | 0.390 → 2.757 |
| dark | 0.00394 → 0.00008 | 0.00024 → 0.00000 | 0.0051 → 0.0001 |
| boundary | 1.927 → 0.499 | 0.0544 → 0.2007 | 0.293 → 1.273 |
| whole ROI | 2.449 → 2.274 | 0.0095 → 0.0809 | 0.184 → 2.576 |

At the exposure the scene actually runs at, the flag makes the bright side oscillate 11x more and
crushes the dark side to 2 % of its radiance. The lower display counts reported for it on
2026-09-09 are that crushing, not a smaller oscillation. It stays off (ledger H-08b supersedes H-08).

## The reflections-off confound (1.6)

| quantity | reflections on | off | ratio |
|---|---|---|---|
| auto-exposure operating point | 66.31 | 4.23 | 0.06x |
| dark-side radiance | 0.00394 | 0.04404 | 11.2x |
| boundary radiance | 1.927 | 2.013 | 1.05x |
| boundary temporal std | 0.05441 | 0.05373 | 0.99x |

**`r.GI.Reflections=0` is not a subtraction.** In `deferred_lighting_ps.hlsl` the specular radiance
is initialised from the prefiltered IBL cubemap and only *overwritten* by the SDF trace when
reflections are enabled, so the control swaps an occluded traced term for an unoccluded environment
lookup. Inside this interior that makes the dark side 11x brighter, which drops the auto-exposure
operating point 16x, which is what moved the display counts on 2026-09-09. The oscillation itself is
unchanged (0.99x), so reflections are not its source — the same conclusion as H-02, for a different
and now demonstrated reason (ledger H-02b).

## Radiometric attribution table (1.6)

| stage | contribution | measured |
|---|---|---|
| upstream HDR | the oscillation exists here | boundary std 0.054 radiance on a mean of 1.93 (2.8 %); bright side 0.021 on 8.25 (0.26 %) |
| exposure | a gain that decides how many pixels cross a display threshold | identical HDR: 0.7 / 1.7 / 22.0 px > 40 per frame at 0.5x / 1x / 2x |
| auto-exposure | sets that gain (66.31) and is stable when still | swing 0.03 % over the sequence; frame-time dependent between runs |
| bloom | lifts the dark floor 2.4x, halves local contrast | count moves in either direction depending on the exposure |
| tonemap curve | compresses the bright side | the pane sits at 233 code values, and its 0.26 % radiance swing is 0.04 code values |

*(Correction (2026-10-08), bloom row: the count moves the same way, down, at both exposures
measured; see the correction under Bloom (1.4).)*

The visible flicker is an upstream radiance oscillation of a few per cent, amplified into visibility
by the exposure an interior needs, on pixels whose own radiance is small. No stage in this list
creates it; followup-10 showed where it is created.

## Scope and limits

* Pose W only, still camera; the moving-camera behaviour of auto-exposure is followup-13's, where
  the trajectory scenes live. *(Correction (2026-10-08): followup-13 held the exposure at 1.0; the
  moving case was measured by followup-14. See "Close-out review (2026-10-08)" below.)*
* Bloom is not replayed offline (it is spatial); its effect is measured by capturing with it on and
  off on the identical HDR sequence, which is why the replay harness asserts that the replay
  *fails* when bloom is on.
* The tonemap replay covers the operators and the settings this project uses (ACES Hill fit,
  saturation 1.0, contrast 0.65); other operators are implemented but only Hill was validated
  against the engine.


## Record reconciled (2026-09-15)

The checklist reads 9/9 and the index shows this task complete. Accurate, and stated plainly here.
*(Correction (2026-10-08): `.metadata.json` has said `pending` throughout; the index generated at
7eea6b95 showed the task complete, and the current one shows it pending, matching the metadata.
Item 2.3 was unticked on 2026-10-08, see below.)*

**Concluded.** The display and exposure stages were cleared as sources of the still-camera residual:
auto-exposure sets the gain and is stable with the camera still, and the display path was shown not
to manufacture the oscillation. The one dependence found — auto-exposure varying with frame time
between runs — is a measurement hazard for other tasks, not a flicker source, and every flicker study
since has pinned `r.AutoExposure=0` and `r.Exposure=1.0` because of it.
*(Correction (2026-10-08): the frame-time dependence was found by followup-09, and fixing the exposure
is its protocol (`docs/analysis/flicker-hypothesis-ledger.md`, Measurement protocol, line 36), which
predates this task. Nor did every later study pin it: followup-14's coverage study ran
`r.AutoExposure=1` on purpose (`scripts/run_coverage_study.py` lines 8 and 57-63).)*

**Carried elsewhere, deliberately.** Moving-camera auto-exposure is followup-13's.
*(Correction (2026-10-08): it is followup-14's, and it was measured; see below.)*

**Not re-run.** No experiment from this task was repeated to produce this note.

## Close-out review (2026-10-08)

A review on 2026-10-08 found errors in this record and stale checks in its harnesses. This section
says what was corrected, how the corrections were checked, and what is still open. No GPU or engine
run was made for it: the GPU and the executable were in use by other work.

**Bloom's direction was misread.** Item 1.4, the Bloom and attribution sections above, design.md,
the package report and ledger H-16 (with its confound bullet, line 585 at 7eea6b95, under "Confounds
recorded so far") said that bloom's effect on
the threshold count changes sign with the exposure. The 2026-09-09 package says the opposite of what
was attributed to it: at the operating point (~67) turning bloom off raised px > 40 from 39 to 1547
per frame over the window (window px > 8 825 -> 3063; `.screenshots/flicker-region/report.md` lines
256-268 and 322), the same direction as here at exposure 1.0 (px > 40 4 vs 5 crossings, px > 8 1225
vs 2032 over 3 frame pairs, with bloom vs without). There the bloom-on reference ran at a fixed 66.84,
which reproduces the auto-exposure count; the package does not record how the bloom-off run set its
exposure. Auto-exposure meters the HDR scene colour, not the bloom result
(`runtime/engine/renderer/frame/frame_constants.cpp:605-609` today), so bloom does not move the
operating point. Bloom lowers the count at both exposures: it masks the residual. Drafted for the
append-only ledger: H-16b (rejected in scope) supersedes H-16, and H-05b supersedes H-05 (the
exposure half supported, the bloom half rejected), which still read "open (followup-11)". The bloom
measurement at ~67 is a single run; step 5b of the rewritten study repeats it.

**31x is 33.8x.** 22.043 / 0.652 in `display-study.json` (H-15b supersedes H-15 for the factor).

**Moving-camera auto-exposure** was measured by followup-14, not followup-13, which held the exposure
at 1.0 (its tasks.md 1.3). `coverage.json` (followup-14 package): 66.13-73.17 over the run; ranges
0.059 in the first hold, 5.24 in the strafe, 1.08 in the second hold, 4.19 after the cut; no sign
change in the strafe, the second hold or after the cut (113, 53 and 75 frame steps); largest step
0.19, 0.28 % of the mean. It adapts without oscillating.

**`r.GI.Reflections=0` still substitutes.** `deferred_lighting_ps.hlsl` lines 1100-1106 initialise the
specular radiance from the prefiltered cube and lines 1156-1163 overwrite it only when SDF
reflections are on; since 9f1be63f SSR is blended over whichever layer is active (lines 1169-1178).
H-02b's mechanism stands, and the study's reflections-off check (the seventh of the 7eea6b95 version,
the eighth now) re-tests it on every run.

**Harness defects, and the fix.** In the 7eea6b95 versions: `test_display_replay.py` lines 89-91 and
100-102 and `run_display_study.py` lines 157-159, 165-167 and 198-202 asserted `max |delta| == 0.0`
between separate processes, which the renderer does not guarantee (ledger, "The renderer is not
bit-reproducible across processes"); `run_display_study.py` line 262 was `check(True, ...)`; the
study deleted every region dump, kept 3 of 9 manifests and no logs, and its default `--out` was this
package, so a re-run would have overwritten it; `report_display_study.py` wrote the misread bloom
sentence (lines 82-85 and 137) and the first run's numbers into every report. Rewritten 2026-10-08:
* both harnesses capture every compared configuration in 4 processes (at least 3), interleaved, and
  judge equivalence with `indistinguishable()` in `run_display_study.py`: max(between) <= max(within)
  on max |delta| (the rule `scripts/test_correction_gates.py` adopted on 2026-10-08), a mean |delta|
  that must not read SEPARATED, and a same-configuration spread under 2 % of the signal;
* `test_display_replay.py` captures from SCENE_START + 200 instead of + 60, past the frames whose
  colour follows the loading timing (`docs/analysis/lighting-block-b-2026-10.md`, "Conventions and
  traps");
* the study's reflections-off check (formerly the seventh, now the eighth of eight) tests that
  reflections-off brightens the dark side and lowers the exposure beyond max(the same-configuration
  spread, 5 % of the value), after the ledger's decision rule 1;
* step 5b measures bloom at the operating exposure, PNGs cover all 24 frames, every manifest, log
  and PNG goes to `<out>/runs/`, the first process's dump of each configuration to `<out>/raw/`, and
  `--out` refuses a directory that already holds a `display-study.json`;
* the report is derived from the data, including the bloom direction, and an existing `report.md` is
  replaced only with `--overwrite` (the generator's default directory is this package).

**How the fix was checked (statically).** `python -m py_compile` on the three scripts; pyflakes clean
on the two harnesses (the generator's four f-string warnings are on unchanged lines).
`python scripts/run_display_study.py --self-test`: 11/11. Both harnesses' `main()` driven end to end
against a mocked engine (session scratchpad 46a4cf85, `f11/mock_engine.py`, local only): with sparse
process wobble and no fault, 8/8 each; each injected fault (bloom, a fixed exposure or auto-exposure
changing the records by 0.1 %, reflections-off as a true subtraction) fails exactly its own check
(the bloom fault both bloom checks, at exposure 1.0 and at the operating exposure).
The 7eea6b95 versions on the same fault-free mock fail 3 (study) and 2 (replay) checks spuriously, and
their seventh study check passes when reflections-off is a subtraction. On real data, the eight
same-path captures retained in `.screenshots/flicker-elimination/40-default-vs-explicit` (pose W,
frames 237-252) show processes landing in discrete states: two pairs bit-identical, the others
9.5e-7 to 3.9e-3 apart (max |delta|) and differing in 57 to 5 751 of 1.57 M recorded values. Over
every split of the RGB records the rule fails on 16 of 280 at 3 vs 3 and on 1 of 35 at 4 vs 4, each
reading SEPARATED, the same splits on which the max rule alone fails; it detects a uniform +1e-3, a
x1.0001 and a x1.001 change and one pixel moved by 5e-3 in 280 of 280 splits at 3 vs 3, where the max
rule alone detects 265, 265, 280 and 280 (243, 243, 280 and 280 when the change goes to the other
half of each split; re-checked 2026-10-09, this sentence first said 228, 228). On luminance, which `scripts/test_correction_gates.py` compares, the max rule
fails on none of these splits and the mean |delta| test fails on the same 16 and 1 (review re-check,
2026-10-08). Hence four processes by default, and a failure counts as a change only if it repeats.
The report generator, run on the 2026-09-09 `display-study.json` into the scratchpad, differs from
the package report only in the corrected sentences (bloom direction, 33.8x for 34x, bit-identity
qualified as single-process, and narrative figures computed: 11.5x and 15.7x where the text had
rounded to 11x and 16x, 0.025 % where it had 0.03 %) and in presentation (px > 40 to two decimals in
the bloom table, the frame count of the replay validation).

**Evidence recovered.** The 9 manifests, 9 run logs and 4 + 4 bloom PNGs of the 2026-09-09 study,
left in `%TEMP%/uz_display_study`, were copied unchanged into the package under
`recovered-2026-10-08/` (9.7 MB; the three manifests the package kept are byte-identical to them).
*(2026-10-09: those copies are now the only ones. Later on 2026-10-08, at 16:28, a mock-engine run
of the 7eea6b95 `run_display_study.py`, whose work directory is hard-coded to
`%TEMP%/uz_display_study`, overwrote the 9 manifests and 9 run logs there with mock content
(`{"mock": true, ...}`, `mock run`); the 8 PNGs there are unchanged (same SHA-256 as the copies).
Nothing left in `%TEMP%/uz_display_study` is evidence.)*

**The package report is git-ignored and was not edited.** Read
`.screenshots/flicker-elimination/11-isolate-flicker-display/report.md` lines 53-56 and the bloom row
of its attribution table (line 102) as corrected above, its 34x as 33.8x, and its "bit-identical"
(lines 39, 41, 45-46) as single-process observations.

**Protocol shortfall, for the record.** One process per condition, 24 measured frames (bloom 4 PNG
frames), against design.md's three runs of 200 + 256 frames. The cross-process spread measured later
(ledger lines 195-214 at 7eea6b95, "The renderer is not bit-reproducible across processes" and its
"Corrected 2026-09-15" paragraph: up to 2.3e-6 relative in mean energy, 2.4e-4 in oscillation) is orders of
magnitude below the effects reported here (33.8x, 11x, 0.06x), so repeats cannot reverse H-02b, H-08b
or H-15b; the rewritten study repeats every equivalence claim and the operating-point bloom
measurement.

**Still open, and who owns it.**
* Running both rewritten harnesses on the current build: this task, item 2.3.
  `python scripts/test_display_replay.py`, then
  `python scripts/run_display_study.py --out .screenshots/flicker-elimination/11-isolate-flicker-display-2026-10`
  and `python scripts/report_display_study.py --dir .screenshots/flicker-elimination/11-isolate-flicker-display-2026-10`.
  The pipeline has changed since 2026-09-09 (block B and the 2026-10-08 batch, among them 9f1be63f,
  ea13ab21, b8ff5077, 975fc3c9, 513e2983 and c14215ed), so the figures will differ; the checks close
  2.3, and step 5b's bloom direction at the operating exposure confirms or revises H-16b. The study
  keeps about 6.8 GB of region dumps under `raw/` (ten of about 0.68 GB) unless `--no-keep-raw` is
  passed.
* The visible flicker itself: followup-12 and followup-14. The untried display-side candidates
  (partial pre-exposure, an exposure floor for the interior) sit with followup-14, which selects any
  correction (proposal.md, "What Changes").
  *(2026-10-09: `.rulebook/tasks/phase0_sponza-flicker-correction`, created that day from
  followup-12's close-out with the window-residual task that the followup-10 and followup-14
  close-outs proposed merged in, now owns the visible flicker; followup-14's close-out hands it "the
  candidate fixes never tried" (its items 1.3 and 1.4; 1.4 chooses and validates any correction,
  1.3 lists other untried window interventions, not these).)*
* Before item 2.3 runs: `.rulebook/tasks/phase0_postprocess-chain-restore` (created 2026-10-09, item
  2.1 not yet done) puts CAS_Sharpen back into every default frame (`r.CAS` 1, the value the
  2026-09-09 manifests record) and restores the tonemap's colour-grading LUT input. The replay does
  not model CAS and assumes no LUT is bound (`scripts/flicker/display_model.py`, docstring), so on a
  binary with that restoration the replay checks can fail on a change of scope, not of the records.
  Run 2.3 on a binary built before it; otherwise pin `r.CAS=0` in both harnesses first, confirm
  the LUT is unbound, and record both here.
