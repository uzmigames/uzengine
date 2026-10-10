# Notes — phase0_sponza-followup-06-firefly-noise-indirect

2026-09-08. Builds: 14:19 (Karis, **stale**), 14:30 (Karis, verified), 14:42 (horizon), 15:04 (probe normal), 15:12 (revert of a diagnostic). All counts: 1920×1080, "isolated bright pixel" = luminance ≥ 120 and ≥ 80 above the mean of the four 2-px neighbours, sampled every 2nd pixel (`scratchpad/fireflies.ps1`). The metric is contrast-sensitive: a brighter or darker base shifts it, so every comparison below holds everything else fixed.

## What the user saw, and its repro

Dense white specks on a shadowed wall next to a barred window, close and grazing, while flying. Repro: `scripts/rendering/sponza-temporal-camera.uzproject --cvar r.GI.FinalGather=1` (the default project's GI configuration), Camera A, frame 260 static / 187 mid-sweep:

| Condition | isolated pixels |
|---|---|
| static | 892 |
| mid-sweep | 996 |
| mid-sweep, `r.TAA=1` | **145** |

`.screenshots/ff-zoom-fg1-sweep.png` (3× nearest) shows the wall beside the window peppered with dots exactly like the user's screenshot; with TAA the same wall is clean.

## Elimination (grazing pose (175,1130,165) → (246,1420,175), frame 300, `r.GI.FinalGather=0` unless noted)

| Toggle | count | base | Reading |
|---|---|---|---|
| reference | 707 | 36.3 | — |
| `r.GI.Reflections=0` | 111 | 66.0 | drops, but the IBL path brightens the whole image (confounded) |
| `r.GI.Reflections.Samples=1` / `=16` | 2470 / 600 | 31.5 / 38.0 | 1 ray: darker base, more contrast; 16 rays: −15 % only |
| `r.GI.Reflections.Steps=16` / `=256` | 710 / 707 | 36.0 / 36.3 | **exhausting the rays changes nothing** → the specks are not surface hits |
| `r.GI.Reflections.Distance=500` | 15304 | 48.7 | distance-exhausted rays count as sky → floods the image |
| `r.GI.GDF.Resolution=128` | 2034 | 32.2 | coarser field: more escapes |
| sun intensity 0 | 677 | 27.5 | **not direct light / shadow map** |
| `r.GTAO=0` | 1355 | 46.9 | brighter base; the pattern is identical in the zoom |
| `r.Shadow.PCFPenumbraRadius=8`, `AtlasResolution=8192` | 707 / 707 | 36.3 | no effect |
| `--view-mode albedo` at the speck positions | smooth | — | not texture grain |
| roughness maps | <0.04: ~0 % of texels | — | no mirror-path pixels |
| `r.FilmGrain` | default 0 | — | not grain |
| legacy SSGI add | no `SSGI activation` line | — | did not run |
| RC-only (sun 0, FG 0, GTAO 0), SH evaluated with the geometric normal (diagnostic build) | 934 vs 893 | 33.8 | **not normal map × SH ringing** |

What is left: reflection rays that leave the field through the window opening within a handful of steps and return sky radiance (`TraceSdfRay` returns −1 → `sky.SampleLevel(...) * skyMultiplier`). Four rays per pixel means a pixel gets 0 or 1 of them; on a dark wall the 1 is a white dot. That is why Steps has no effect (the escape happens early), why 16 rays only dim the dots (Karis of one bright ray among 16 is still ≫ the wall), why `Reflections=0` removes them, and why the specks concentrate beside windows.

## Fixes landed on the way (each a real defect, each measured)

| Fix | Where | RC-only pose | gather-off pose | temporal static / sweep | Camera A / C f640 |
|---|---|---|---|---|---|
| before | — | 1333 | 707 | 869 / 961 | 1104 / 1326 |
| Karis `1/(1+luma)` on the gather (verified in binary) | `sdf_reflections.hlsli` | — | 706 | 841 / 921 | 1092 / 1329 |
| below-horizon rays keep their weight in the denominator | `sdf_reflections.hlsli` | — | 707 | 746 / 804 | 944 / 1323 |
| probe selection by geometric normal | `deferred_lighting_ps.hlsl` | **893** | **365** | 765 / 827 | 944 / 1281 |
| `r.TAA: 1` in the project | `sponza.uzproject` | — | — | (145 mid-sweep) | **244** / — |

Luminance moved at most −0.9 % in the affected configurations; Camera A 53.0 → 54.1 with TAA (SponzaTest 53.1). The horizon fix and the probe-normal fix are both instances of the same estimator error: a partial sample set renormalised as if it were the whole.

## The stale-binary trap, twice

The 14:19 build after the Karis edit produced **identical numbers to the decimal** — the `.hlsli` edit re-embedded into `build/generated/shaders/` but `pipeline_lifecycle.cpp` (the TU that includes `engine_shaders.h`) was not recompiled. `grep -a -c "<unique comment>" runtime/build/bin/uzengine.exe` was 0; a content change in that TU made it 3 and the measurements moved. Every subsequent shader change in this task was verified the same way before being measured. Recorded in memory (`uzengine-build-workflow`).

## Debug views that could not be used

`shadows` and `lighting` render solid magenta, `lightingonly` renders black (mean 1.5), `ao` renders a flat 65.5 with GTAO on. `albedo`, `roughness`, `metallic`, `worldnormal`, `detaillighting` work. The `--diag` targets `gbuffer.emissive` (16.4844 / 4.1211), `lighting.direct/indirect/shadow` (0.25 / 0.5) and `hdr` (0.2501) report the same constants at every pose — not measurements.

## Recommendation

Keep `r.TAA: 1` in the Sponza project (done) and give the SDF reflection path a filter (item 4.1). Until then, any project that runs `r.GI.Reflections=1` without TAA will sparkle next to bright openings.

*Correction (2026-10-08): no filter was built (item 4.1 closed by decision on 2026-09-23, ledger H-68), so the sparkle without TAA stays: a project that runs `r.GI.Reflections=1` must set `r.TAA` to 1, as the Sponza project does.*

## Addendum — the whole-scene flash (item 4.4)

After `r.TAA: 1` the user reported that the whole scene flickers while flying. The live log
(`/tmp/sponza_live2.log`) has the answer: from 15:29:24 `SSGI activation: worldGather=false …`
/ `worldGather=true …` alternate every 0.25–0.5 s (95 lines). Each off blip is one frame.

Cause chain, all in one frame: `frame_constants.cpp` calls `m_radianceCache.Update(camPos)`,
which sets `m_dirty` when the snapped camera crosses a probe cell (spacing 2·2000/15 ≈ 267 cm);
`HasProducedResult()` = `!m_firstWritePending && !m_dirty` → false → `radianceCacheEnabled = 0`
and `rcProbeSHBufferSrv = UINT32_MAX`; the SDF block then zeroes `sdfDiffuseSamples` and
`sdfReflectionsEnabled` because they require `radianceCacheEnabled`; `worldGather` becomes false →
`PrepareFrame` resets the gather history. Lighting for that frame falls to the unoccluded sky-SH /
IBL fallback under an exposure adapted to the interior → a near-white frame. Meanwhile the GI pass
later in the same frame retraces every probe (`resetHistory` → budget = all probes) and clears
`m_dirty`, so the gate was exactly one frame too conservative.

Repro: `scripts/rendering/sponza-longsweep-camera.uzproject` (hold 60 frames, 900 cm along +X over 150
frames, `r.GI.FinalGather=1`, `r.TAA=1`):

| | activation lines | frames 126 → 127 → 128 → 129 (mean /255) | 126→127 |Δ| mean, % px > 40 |
|---|---|---|---|
| before | 7 (1 + 3 blips at x≈267/534/801) | 18.9 → **236.3** → 15.4 → 11.0 | 217.6, 98.0 % |
| after (`HasProducedResult` without `m_dirty`) | **1** | 18.9 → 15.6 → 12.8 → 9.7 | 9.8, 4.4 % |

Control (short 160 cm sweep, no crossing): consecutive frames with TAA differ by |Δ| 6.6 / 2.7 %
(camera motion only), 0.24 when static; `r.MotionBlur=0`, `r.TAA.JitterSequenceLength=16` and
`r.AutoExposure=0` change nothing — the TAA itself does not flicker.

Residual (item 4.5): the retrace on recentre has no temporal hysteresis, so the frames after a
crossing are darker/noisier than mid-cell motion frames. Scrolling the SH buffer would keep it.

Build note: the first rebuild of this fix compiled but failed at **install** (`unable to update file
… AccessDenied`) because a capture batch still held the exe; the old binary stayed and the first
measurement showed 7 activation lines again. Rebuilt after `tasklist` showed no `uzengine.exe`.

## Addendum — the vertical blink (item 4.5)

After 4.4 the user still saw a blink at one point while rising; **P** logged (-33.2, 55.8, 282.9)
and (-33.2, 55.8, 291.7), both just above 266.7 cm — the probe spacing, i.e. the Z cell boundary.
`RadianceCache::Update` snaps with `floor(pos / spacing)`, so crossing z = 266.7 upward recentres
the grid by one probe in +Z. The live log had a single `SSGI activation` line (the 4.4 gate fix
holds); this was the recentre itself, and the horizontal long sweep had under-read it because it
compared only adjacent frames.

Two defects, both in the same frame:

1. **Origin/buffer mismatch.** `frame_constants.cpp` runs before the GI passes, so it publishes the
   SH buffer completed *last* frame (`GetProbeSHBufferSrv()` is the pre-swap buffer) — but it
   published the *current* grid origin. On the recentre frame lighting indexed the old grid one
   cell off: the shadowed floor was lit by the probes 267 cm above it → a whole-scene flash.
2. **History loss.** The retrace ran with first-write semantics: history SRV = sentinel, blend alpha
   1, and `SamplePreviousDiffuse` (the multi-bounce feedback) returned 0. Every probe restarted from
   a single-frame estimate and the lighting sat on a plateau that decayed at alpha 0.12 over ~40
   frames.

Repro: `scripts/sponza-vertical-camera.{uzproject,uzscene,lua}` — the user's pose (yaw −107,
pitch −12.7), hold 60 frames at z = 159, rise 300 cm over 100 frames (3 cm/frame), crossing at
frame 132/133. 1920×1080, mean luminance /255 (`lum.ps1`), `framediff.ps1` for adjacent frames.

| frame (z) | before | after |
|---|---|---|
| 128 (hold, z 159) | 82.5 | 82.5 |
| 131 | 83.4 | 83.4 |
| 132 (recentre: lighting reads last frame's buffer) | **95.3** flash | 83.9 |
| 133 (first frame of the retraced grid) | 88.0 | 84.1 |
| 134 | 88.0 | 84.4 |
| 135 | 88.0 | 84.8 |
| 136 | 88.2 | 85.0 |
| 138 | 88.1 | 85.5 |
| 140 | 88.1 | 85.8 |
| 145 | 88.2 | 86.6 |
| 150 | 88.0 | 86.9 |
| 160 | 86.7 | 86.3 |
| 175 | 84.5 | 84.3 |

| adjacent frames | before: mean abs diff / % px > 40 | after |
|---|---|---|
| 131 → 132 (flash) | 16.4 / **12.35 %** | 6.85 / 2.03 % |
| 132 → 133 | 13.4 / 7.76 % | 6.87 / 2.05 % |
| 133 → 134 (motion only) | 7.3 / 2.19 % | — |

Montage: `.screenshots/vs-pisca-antes-depois.jpg` (before/after, frames 131–133).

Fix (radiance_cache.{h,cpp}, radiance_cache_trace_cs.hlsl, frame_constants.cpp):

- `GetCompletedGridOrigin()` — the origin the completed buffer was traced with — is what
  `frame_constants.cpp` publishes with `rcProbeSHBufferSrv`. One frame of latency, always consistent.
- `Update()` accumulates the grid shift in whole probes (`m_pendingHistoryShift`); `UpdateProbes()`
  passes it in a new 16-byte row of `RadianceCacheTraceConstants` (192 → 208 bytes; both HLSL
  mirrors declare the 13th row, `ddgi_probe_update_cs.hlsl` ignores it). `resetHistory` is now only
  `m_firstWritePending || m_historyInvalid` (the latter set by `InvalidateHistory()` on a GDF scene
  revision); a recentre is a `fullRetrace` (budget = all probes) **with** history.
- Shader: `HistoryGridOrigin() = gridOrigin − shift·spacing` is used by `SamplePreviousDiffuse`, and
  the blend reads `history[historyBase]` at `probe + shift`, replacing only when the shifted
  coordinate is outside the grid or the history probe was invalid (inside geometry). 15/16 of the
  probes keep their history on a one-axis recentre; the entering slab starts fresh.

*Correction (2026-10-08): block B-02 (0a9cc732) grew the struct to 464 bytes. The shift row's pad became `regionCount`, and 16 change-box rows follow it: probes inside a box blend at `r.GI.RadianceCache.RegionBlendAlpha`, which defaults to 1, a replace. Block B-06 (edfd7942) moved the DDGI mirror into `ddgi_probe_common.hlsli`. Change boxes come only from GDF instance changes (`global_distance_field.cpp:349, 376-385`, `frame_gi_passes.cpp:255-268`), so a pure recentre still keeps the history of every probe that stays in the grid.*

Guard: `scripts/rendering/test_rc_recentre_scroll.ps1` — 40 checks (CB layout and row order in both mirrors,
the C++ gates, the shader paths, a CPU model of the index mapping for shifts ±1 / diagonal / beyond
the grid, and an EMA model of the blink: replace → −40 %, blend with feedback → < 2 %, recovery of
the old behaviour ≥ 15 frames).

*Correction (2026-10-08): the guard failed 5 of its 40 checks from 0a9cc732 on and 7 from edfd7942 on, by a Python emulation of its regexes on those commits' sources; the same emulation gives 40/40 on 7eea6b95. It was re-anchored to the current layout: 42 checks, 42/42 against 4501fa74's sources (closure review below).*

Build notes: `test_gi_gpu.cpp` carried its own `== 192` static_assert (now 208 + two `offsetof`
checks). While that assert failed, clang also reported a `gtest-printers.h` char16_t→char32_t
conversion as an *error* in the same TU; it vanished with the assert fixed. PowerShell harness
gotchas that cost a run each: `$v` and `$V` are the same variable, and `@($x + $s[0], $y)` parses
as `$x + ($s[0], $y)` — parenthesise each element.

*2026-10-08: that assert is `== 464` since 0a9cc732, but its comment and message still say 208 (`test_gi_gpu.cpp:774-779`).*

## Re-counted with the validated instrumentation, before any filter (2026-09-15)

Item 4.1 proposed a filter for the SDF reflection path. The artefacts were re-counted first, in
linear radiance from the region records rather than from a tonemapped screenshot — the original
counts were display-side, which followup-09's rule 4 forbids as sole evidence. Isolation requires
both a ratio to the 3x3 neighbourhood median and an absolute floor, so a dark neighbourhood cannot
manufacture one.

| region | `lighting_reflection` | | `taa_output` | |
|---|---|---|---|---|
| | isolated/frame | energy | isolated/frame | energy |
| `rear_door` | 4414 (8.8 % of pixels) | **85.3 %** | 199 | **14.7 %** |
| `right_shadow` | 2519 | 61.9 % | 15 | 0.74 % |
| `shadow_wall` | 1207 | 49.1 % | **0** | 0.00 % |

**The concern was well founded about the term.** An estimator with 49-85 % of its energy in isolated
pixels is behaving exactly as four GGX rays with a binary sky escape would.

**But it does not reach the output where the flicker is.** At `shadow_wall` the output contains zero
isolated pixels by this rule, so a reflection filter is not a priority for the residual this programme
*(corrected 2026-09-15: this first said the filter "would do nothing"; absence of isolated
pixels above a threshold does not prove a filter could not change the oscillation — it justifies
not prioritising it, not ruling it out)*
is chasing. Downstream weighting and the TAA already remove nearly all of it.

**Where a filter would earn its cost is `rear_door`** — 199 isolated pixels per frame carrying 14.7 %
of the output's energy. That is the same region where followup-12 localised the oscillation to the
translucent pass (ledger H-38), and the natural suspicion is that the translucent refraction samples
a scene copy that still carries them. One phenomenon or two is **not** established here, and the
single next test named in followup-12 — holding the refraction source while the translucent pass
keeps drawing — would bear on both.

So: no filter is built on this evidence. A filter justified by the reflection term's own statistics
would be aimed at a region whose output problem may have a different cause, and the test that
separates them is already specified.

Items 4.2 and 4.3 (debug views rendering magenta/black/flat, and `--diag` targets reporting constants)
were **not** needed by followup-12 or -13 — the region-dump instrumentation covered those tasks — so
they stay where they are rather than rising in priority.

## Item 4.2 — the debug views that could not be used (2026-09-16)

Fixed and verified. `scripts/rendering/run_debug_view_check.py`, criteria fixed before the captures:
no more than 1 % pure-magenta pixels (M1), mean >= 5 code values (M2), per-pixel std >= 1 (M3), and a
mean absolute difference from `albedo` of at least 5 code values so a mode cannot pass by showing
another channel (M4). Report: `.screenshots/flicker-elimination/37-debug-views/debug_views.json`.

| mode | before | now | verdict |
|---|---|---|---|
| `ao` | flat 65.5 with GTAO on | mean 133.58, std 27.94 | PASS |
| `lighting` | solid magenta | mean 10.08, std 34.05, magenta 0 % | PASS |
| `shadows` | solid magenta | mean 17.42, std 51.31, magenta 0 % | PASS |
| `lightingonly` | black, mean 1.5 | mean 10.94, std 34.08 | PASS |
| `albedo`, `worldnormal`, `detaillighting` (controls) | worked | 93.18 / 146.86 / 67.14, all vary | PASS |

**The causes, each one specific:**

* **`lighting` and `shadows` had no case in the shader.** `debug_view_ps.hlsl` handled modes 1-7,
  11, 13-16 and 18; 8 and 9 fell to the default branch, which returns magenta as an error colour. The
  data existed all along: the deferred pass writes direct light, indirect light and the shadow/AO mask
  to `SV_Target1..3` whenever those textures exist.
* **`lightingonly` read the target it writes.** It divided the HDR scene colour by albedo, but the
  debug view pass writes that same HDR resource — and clears it first. It now reads the direct and
  indirect targets, which is what the mode claims to show, and the pass no longer reads its own
  output.
* **`ao` showed the G-buffer alpha**, the material's AO, which is flat where the material does not
  author it. It now reads the GTAO result when one exists and falls back to the G-buffer otherwise.

**How the three targets reach the pass:** they are imported into the render graph, the lighting pass
declares them as writes and the debug view pass as reads, so the graph schedules the
RenderTarget -> ShaderResource transition rather than the pass reading a target still bound as an RT.
Root constants go from 7 to 11 of the 16 the signature provides.

**Caught by the harness, worth recording:** the first build of the shader failed to compile at
runtime (`control may reach end of non-void function`) because the new cases break out when their SRV
is not bound. The capture harness refused to measure anything and said so, instead of reporting a
black image. Fixed with a fallback return, which is also what a mode with an unbound source now shows.

**Limit:** the new modes are bindless-only. The SM5 fallback binds t0..t5, so there they fall through
to the error colour rather than displaying another texture as if it were lighting.

## Item 4.3 — `--diag` targets that reported constants (2026-09-16)

Fixed and verified against a fresh capture from the same pose, before and after.

**What the fresh capture showed first.** Most targets already varied plausibly — `lighting.direct`
0.251/1.000/0.476, `hdr` 0.256/1.000/0.480 — so the 2026-09-08 note ("the same constants at every
pose") no longer described the whole of it. Two defects remained, and both made a non-measurement
look like a measurement:

* **A target registered for ONE channel was validated on all four.** `gbuffer.ao` is the albedo
  texture's alpha, `gbuffer.metallic` and `gbuffer.roughness` the normal texture's blue and alpha.
  Each reported its *source texture's* RGBA statistics: `gbuffer.ao` = 0.168627/1.000000/0.406679,
  exactly `gbuffer.albedo`; metallic and roughness both exactly `gbuffer.normal`. `RegisteredTarget`
  already carried the channel and the dump path honoured it; the validator ignored it.
* **Targets the validator cannot read reported 0/0/0 with status OK**, indistinguishable from a black
  image. `gbuffer.depth` and `shadow.atlas` are depth formats; `gbuffer.position` is RGBA32F, whose
  padded rows come to 33 MB, over the 16 MB readback cap.

*Correction (2026-10-08): the 1.000 in the two triples above is the alpha channel, which the lighting pass writes as 1.0 (`deferred_lighting_ps.hlsl:1484-1486`), not a reading of light. The targets this item named first - `gbuffer.emissive`, `lighting.direct/indirect/shadow`, `hdr` - are still validated on all four channels, constant channels included. `gbuffer.emissive` still reads 0 / 16.484375 / 4.121094 (OK) on 2026-10-08: RGB emissive 0 and RT3.a = specular + 16 x shading model, averaged (closure review below).*

**After the fix:**

| target | before | now |
|---|---|---|
| `gbuffer.ao` | 0.1686 / 1.0000 / 0.4067 (= albedo) | channel A: 1.0000 / 1.0000 / 1.0000 |
| `gbuffer.metallic` | 0.0000 / 0.9995 / 0.5381 (= normal) | channel B: 0.0000 / 0.0000 / 0.0000 |
| `gbuffer.roughness` | 0.0000 / 0.9995 / 0.5381 (= normal) | channel A: 0.5889 / 0.7534 / 0.6676 |
| `gbuffer.depth`, `gbuffer.position`, `shadow.atlas` | 0/0/0, status OK | `SKIPPED`, each with its reason |

The JSON now carries `measured`, `channel` and, when skipped, `reason`; skipped targets no longer
raise the report's warning flag, since not reading a target is neither a failure nor a warning.

**Read as measurements, not defects:** `gbuffer.ao` is flat 1.0 because the material AO channel is
authored at 1.0 here — the `ao` debug VIEW shows GTAO instead (item 4.2), which is the other half of
the original complaint. `gbuffer.metallic` reads 0.0 because the sampled materials are dielectric; it
is now flagged `WARN_ZERO`, which is correct for a measured zero.

**Still zero and not investigated here:** `volfog.inscattering`, `volfog.integrated` and `hiz.mip0`
report measured zeros. Whether that is correct for this scene is not part of this item.

*Correction (2026-10-08): both are explained since. Nothing writes the volfog volumes in Sponza. The froxel pass runs only for a `height_fog` entity with volumetric fog enabled (`frame_atmosphere_passes.cpp:129-141`), and `sponza.uzscene` has none (block B-04, 8851f37e, `docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-block-b-04-froxel-fog.md`). The Hi-Z pyramid was built before the GBuffer pass had written depth; 9f1be63f moved it after. The 2026-10-08 `--diag` reads `hiz.mip0` at 0.002337-0.002343, with the two volfog targets still WARN_ZERO.*

**Known limit, now recorded rather than discovered again:** the validator samples a 4x4 block at the
CENTRE of each target and averages the selected channels. A target whose centre does not change
between poses will report the same numbers at every pose, and that is the sampling, not the render.

## Item 4.1 closed — the door's output fireflies are not reflections (2026-09-23)

The 2026-09-15 re-count left one question: `rear_door`'s output still carries ~199 isolated pixels
per frame (14.7 % of its energy), in the region H-38 localised to the translucent pass. Answered with
`scripts/rendering/run_door_firefly_attribution.py` (ledger H-68), H-41's isolation rule and masks unchanged,
3 repeats x 48 frames per condition, decision rule fixed before the captures.

| region | baseline isolated/frame | reflections off | translucency off | overlap with isolated reflection pixels | reflection share of output at those pixels |
|---|---|---|---|---|---|
| `rear_door` | 204.8 | 60.8 (-70 %, mean luma x3.0) | 170 (-16 %) | **3.4 %** | **2.1 %** |
| `right_shadow` | 15.0 | 177 (raised, brighter base) | 0.0 (-99.6 %) | 8.0 % | 3.1 % |

The switch alone would have read as "reflections cause 70 %"; it is confounded - reflections off
lets the IBL fallback in and triples the region's mean, and the isolation rule is a ratio to the
neighbourhood median. The overlap reading has no confound and says the output's isolated pixels are
not where the reflection term's are. Verdict **NOT JUSTIFIED**; no filter built, item closed.
Data: `.screenshots/flicker-elimination/53-door-firefly-attribution/door_fireflies.json`.

*Addendum (2026-10-08, read from the same file, no new capture; ledger H-68b).* With translucency
off the door's isolated output pixels fall only 16 % in number (about 204 -> 170 per frame; 16 % is
H-68's worst-against-best cut), but their share of the region's output luminance falls from
13.9-14.3 % to 1.6 %, while the region's mean output luminance goes from 1.20-1.23e-02 to
0.99-1.03e-02. Share x mean falls from 1.71e-03 to 1.61-1.62e-04: about 90 % of their summed
luminance goes with the translucent pass, the door mechanism recorded by followup-12 (H-38, H-45..H-48)
and owned by the proposed `phase0_sponza-flicker-correction`.
The ~170 per frame left carry little energy and are not reflection pixels either (overlap
3.5-4.7 %). Nothing attributes them, and they are not tracked, because nothing shows them to be
visible. H-68's repeats agree within 1 pixel/frame except for translucency off at `rear_door`
(169.9 / 171.2 / 170.2).

## Closure review (2026-10-08)

Every item was re-checked against the sources at 4501fa74 by reading only: no build, no capture.
HEAD moved to 8246ab5c during the review; none of the files cited here changed. *2026-10-09, HEAD
5fcd62e5: of the files cited here only `runtime/tools/uzengine.cpp` has changed since (7eee2d00;
see the `--screenshot-frames` item below).*

**Still in place.** Karis and horizon weights at `sdf_reflections.hlsli:268, 283-285` (only
ce2caa5e, the toroidal GDF addressing, has touched the file since). `probeFacing` at
`deferred_lighting_ps.hlsl:932`. Readiness at `radiance_cache.h:227-229`. Lighting's completed
origin at `frame_constants.cpp:811`. History shift at `radiance_cache.cpp:776-778`, shifted blend
at `radiance_cache_trace_cs.hlsl:596-633`. Debug-view cases at `debug_view_ps.hlsl:90-100,
147-176`. Per-channel validation and SKIPPED at `inspector_pass_validate.cpp:98-121` and
`inspector_pass_report.cpp:83-91`.

**Guards**, checked by Python emulations of their regexes (each pattern checked to appear verbatim
in the script):
- `test_sdf_reflection_firefly.ps1`: 18/18.
- `test_rc_recentre_scroll.ps1`: 33/40 before this review; re-anchored, now 42/42. The emulation
  gives 40/40 on 7eea6b95. Seven one-point source mutations (size, offsets, a row after the shift
  row, a DDGI include, a DDGI row, the pre-B-02 blend, a recentre that resets history) each fail
  exactly their own check. The edited script parses without errors. It does not check what
  precedes the shift row in the HLSL mirrors, nor where change boxes come from: a row inserted
  before the shift row, or `fieldChanged` set on a recentre, still passes 42/42 (the original
  guard had the first gap too). *2026-10-09: a shader that set `fieldChanged` for every probe with
  history passed 42/42 too; `blend_replace_without_history` now also pins
  `fieldChanged = hasHistory && InChangedRegion(probePos)` (`radiance_cache_trace_cs.hlsl:621`), so
  that mutation fails that check alone. Still 42 checks: 42/42 by emulation, the seven mutations
  above unchanged. Change boxes pushed by the C++ on a recentre would still pass.*

**Dispositions.**
- No reflection filter (4.1, H-68):
  - Accepted and documented: sparkle without TAA. `r.TAA` defaults to false
    (`frame_setup.cpp:47`), so a project that enables `r.GI.Reflections` sets `r.TAA 1`.
  - Declined: the exposure-relative or clamped Karis weight that followup-08 4.1 handed here. The
    output's isolated pixels are not reflection pixels (H-68). The bias is quantified: 88 %
    retained in the layered fixture, under 1 % error with a uniform environment.
    `r.GI.Reflections.HdrWeight=0` gives the plain mean. The energy check on a constant-albedo
    enclosure that `sdf_reflections.hlsli:279-282` still lists as open is that fixture run
    (`test_sdf_reflection_energy.ps1`, followup-08 4.1).
  - H-68 measured the 2026-09-23 build. Pose A's render has changed since, at least through b8ff5077:
    it changed the sky cubemap capture that escaping SDF reflection rays sample
    (`sdf_reflections.hlsli:218-220`, `frame_constants.cpp:839`), as do the radiance-cache probes'
    escaping rays (`radiance_cache.cpp:870`). The other renderer commits since 2026-09-23 were not
    assessed for pose A. *Corrected 2026-10-09: this item said 9f1be63f's Hi-Z pyramid is read by
    GTAO and SSGI, as 9f1be63f's message says. At pose A nothing that runs reads it: the GTAO shader
    ignores the Hi-Z index it is passed (`gtao_cs.hlsl:20, 237`), the SSGI trace disables it
    (`ssgi_renderer.cpp:578`), and the GPU culling that reads it is off by default (`r.GpuCull`,
    `r.DepthPrepass`, `scene_renderer_init.cpp:1093-1094`, `frame_sync.cpp:108`).* 9f1be63f's SSR
    layering and ea13ab21's env-MIS specular do not run at pose A: `r.SSR` is off there, and env-MIS
    is unbound while the SDF reflections are on
    (`frame_lighting_pass.cpp:207-212`). Pose A's scene (`scripts/rendering/sponza-flicker-a.uzscene`,
    unchanged since 7eea6b95) did not get the sqrt 3 that 4501fa74 and 8e3470f8 put back into the
    three Sponza scenes. The region dump still records the SDF term before the SSR blend
    (`deferred_lighting_ps.hlsl:543, 1162, 1477`), so `run_door_firefly_attribution.py` remains
    the re-test.
- The door's isolated output pixels: see the addendum above.
- By design and documented: the 4.2 modes are bindless-only (`debug_view_ps.hlsl:147-149`), and the
  validator reads a 4x4 centre block (`inspector_pass_validate.cpp:1-5`).
- `run_debug_view_check.py` passes `--screenshot-frames 39`, which uzengine.exe rejects because it
  takes A-B (`runtime/tools/uzengine.cpp:749-753`; each run.log warns). It then captures the first
  fully loaded frame (`uzengine.cpp:1175-1182`). The capture is deterministic: six of the seven
  modes are byte-identical across the 09-16, 09-21 and 10-05 binaries. *2026-10-09: 7eee2d00
  (2026-10-08 17:02) makes uzengine.exe take a single frame N as N-N (`uzengine.cpp:744-757` at
  5fcd62e5; the lines above are 8246ab5c's). A binary built from it captures render 39 only, and
  only if the scene is fully loaded by then. Pose A's preload took 37 frames in all three runs
  ("Pre-load complete ... after 37 frames" in each run.log); if a rebuilt binary needs more, the
  harness stops with "no screenshot was written" and `--frames 60` moves the capture to render 59.*

**Left before closing** (engine changes, then runs; the exact text is in the closure output):
1. 4.3 residue: an RGB channel selector in the pass inspector (`inspector_pass.h:60-66`,
   `inspector_pass_validate.cpp:103-109`, `inspector_pass_dump.cpp:134-139, 154`).
   `gbuffer.emissive`, `hdr`, `lighting.direct` and `lighting.indirect` are registered as RGB and
   `lighting.shadow` as R (`frame_lighting.cpp:263, 363-377`). Then `scripts/rendering/verify_sponza_loading.ps1`
   and its DIAG_JSON.
2. Comments that still give the 208-byte layout or name 4.1 as open: `sdf_reflections.hlsli:279-282`,
   `radiance_cache.h:431-432, 437-441`, `radiance_cache_trace_cs.hlsl:91`, `test_gi_gpu.cpp:774-779`.
3. Runs:
   - Both guards in PowerShell (18/18, 42/42).
   - `python scripts/rendering/run_debug_view_check.py` on the current binary (7/7).
   - The GTAO-off `ao` fallback against the lit level of the `shadows` view, both with `r.Bloom 0`.
     Debug views are tonemapped like the lit image: the 0/1 shadow mask shows its lit pixels at 172
     code values in the 37-debug-views captures. A display-referred 0.25 therefore does not by
     itself show a wrong G-buffer alpha. The view and the lighting read the same
     `m_gbuffer.albedoSRV` alpha (`frame_lighting_pass.cpp:154, 202`, `debug_view_ps.hlsl:70, 99`,
     `deferred_lighting_ps.hlsl:442, 448, 489`).
4. Optional:
   - The vertical sweep (`scripts/sponza-vertical-camera.*`), because 0a9cc732 rewrote the blend
     around the 4.5 fix.
   - The long sweep's single `SSGI activation` line (4.4).
   - H-68 on the current build.

**Outside this task, found on the way.** Since 9f1be63f the `reflections` debug view loads the SSR
texture through the sentinel index when SSR is inactive (`frame_lighting_pass.cpp:163`,
`debug_view_ps.hlsl:60, 178-185`).
*2026-10-09: no other task owns it (the known-issues note under `r.SSR` says so). The review of
this closure proposes making the fix and its check closure conditions here: the same sentinel
test at the top of case 18, then a `--view-mode reflections` capture with `r.SSR 0` and
`r.Exposure 1.0` (the exposure `run_debug_view_check.py` uses; pose A's scene sets 0.5) that shows
the "no data" magenta.*

**Not editable here, handed to the closure.** Corrections to:
- `docs/analysis/known-issues-and-partial-implementations.md`: this task's entry, the DDGI
  paragraph, the `--diag` GDF line under `r.RadianceCache`, the `r.TAA` entry, and an open note
  under `r.SSR` for the `reflections` view (the defect above).
- `docs/engine/12-rendering/33-sdf-gi-final-gather.md`.
- The ledger: H-68b, and the H-28b..H-72 rows that sit inside the "Results marked invalid" table.

## Close-out runs (2026-10-09)

Engine changes of the closure review in 8fb13fa1. Runs:

1. Without the GPU: `scripts/rendering/test_rc_recentre_scroll.ps1` 42/42 and `scripts/rendering/test_sdf_reflection_firefly.ps1`
   18/18 (spike 20 + 3 x 0.05: plain 5.038, Karis 0.377), both exit 0. Tasks.md 5.3 holds for today's sources.
2. `scripts/rendering/verify_sponza_loading.ps1` on the 8fb13fa1 Debug build, DIAG_JSON of
   `scripts/sponza-loading-fixed-converged.log`: gbuffer.emissive RGB 0 / 0 WARN_ZERO (no longer 0 / 16.48 /
   4.12), hdr, lighting.direct and lighting.indirect RGB, lighting.shadow R, gi.gdf_cascade0 non-zero. The
   4.3 correction holds.
3. `scripts/rendering/run_debug_view_check.py`: 7/7 at 240 frames on the 2cc2e85c and 8fb13fa1 Debug builds. The default
   40 frames request `--screenshot-frames 39`, an absolute frame index, and on that build the pre-load (which
   renders until Sponza's distance fields are baked and takes no screenshots) lasted 44 frames, so the render
   loop started after frame 39 and no screenshot was written. *(Corrected the same day: the first version said
   the readiness gate never opened.)*
4. GTAO-off ao fallback against the shadows view's lit level: 171 and 171 (within 2 code values). The
   fallback shows the 1.0 the lighting reads.
5. The reflections view (debug view 18) with SSR off: 98.9 % of the frame at the tonemapped magenta (167, 0,
   167), the rest overlay text: "no data", instead of a load through the sentinel descriptor.

The optional runs (4.5 vertical recentre, 4.4 long sweep, the H-68 re-test) were not made. The task closes.
