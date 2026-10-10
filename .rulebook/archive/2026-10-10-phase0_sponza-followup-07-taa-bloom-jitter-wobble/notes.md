# Notes — phase0_sponza-followup-07-taa-bloom-jitter-wobble

2026-09-08. All numbers 1920×1080, mean |Δ| of luminance /255 between consecutive frames and the
share of pixels whose luminance changed by more than 40 (`framediff.ps1`), Sponza project defaults
(`r.TAA: 1` since followup-06).

## Repro

User pose from **P**: position (-223.3, -599.6, 576.6), look_at (-418.6, -1577.7, 648.7), yaw −101.3,
pitch 4.1 — the balustrade corner of the first-floor gallery; the near wall (~40 cm) fills the left
half of the frame, the far atrium wall the right half. Static camera, frames 300–303:

| condition | 300→301 | 301→302 | 302→303 |
|---|---|---|---|
| TAA on (before) | 1.06 / 0.25 % | 2.45 / 1.11 % | 1.54 / 0.70 % |
| TAA off | 0.94 / 0.00 % | 0.96 / 0.00 % | 0.98 / 0.00 % |

Pixel (430, 200) on the cornice edge, TAA on: 180.1 → 188.1 → **31.2** → 179.1; TAA off: 194.2 constant.
Scanline y = 200, x = 422…438: with TAA on the ledge→background edge sits at x ≈ 436 in frames 300/301/303
and at x ≈ 428 in frame 302 — a 6–8 px jump with a static camera. Changed-pixel grid (threshold 4):
TAA off 151 k (GI noise everywhere); TAA on 93 k, of which the right half (far wall) drops to
hundreds per cell while the left half (near wall) stays at 6–12 k per cell — TAA accumulates far,
not near.

Moving camera (`sponza-closeup-approach.*`, 2 cm/frame, frames 240–244): TAA on (before) 8.5–9.3 /
3.6–4.3 %, TAA off 12.8–12.9 / 7.2–7.5 %.

## Elimination (static, TAA on, 300→301; baseline 1.06 / 0.25 %)

| toggle | result |
|---|---|
| `r.GTAO=0` | 1.05 / 0.19 % |
| `r.GI.FinalGather=0` | 1.11 / 0.24 % |
| `r.GI.Reflections=0` | 1.29 / 0.23 % |
| all three off | 1.33 / 0.29 % |
| `r.TAA.AntiFlickerMax=0.04` | 0.94 / 0.25 % |
| `r.TAA.VarianceClipGamma=100` | 0.84 / 0.18 % |
| `r.Bloom=0` | 0.98 / 0.23 % (301→302: 2.35 / 1.07 %) |
| `r.CAS=0`, `r.MotionBlur=0` | identical (they do not run in the TAA branch) |

A 1-D model of the resolve (plus filter σ 0.47, min/max + variance clip γ 1.25, anti-flicker cap
0.3, Halton(2) jitter, HDR step/line features, ACES tonemap) oscillates by at most 2.3/255 — the
resolve as written cannot produce the observed changes.

## Cause

`frame_setup.cpp`:

    ctx.jitteredViewProj = ctx.unjitteredViewProj;
    ctx.jitteredViewProj.m.m[2][0] += ctx.jitterX * 2.0f / renderW;
    ctx.jitteredViewProj.m.m[2][1] += ctx.jitterY * -2.0f / renderH;

With the engine's row-vector convention (`mul(worldPos, viewProj)`), row 2 of a *projection* matrix
multiplies view-space z = clip w, so this is a uniform NDC translation — the UE4 recipe the comment
cites. Row 2 of the combined *view-projection* multiplies the vertex's **world z**, so the NDC
offset is `jNdc · world_z / w`, i.e. a pixel shift of `jitter · world_z / w`. Sponza is z-up with the
camera at z ≈ 577: the wall 40 cm ahead moves by ~14 × jitter (±7 px), the atrium wall at 15 m by
~0.4 × jitter, the ground floor (z = 0) not at all. The resolve un-jitters by (jitterX, jitterY) pixels
and clamps history to the current neighbourhood, so on the near wall it follows the displaced frame
every frame. SponzaTest carries the same lines (`frame_setup.cpp:351-352`); nobody saw it because
`r.TAA` was off until this morning.

## Fix and verification

`taa_jitter.h` — `ApplyTaaJitter`: `VP'[i][0] = VP[i][0] + jNdc.x · VP[i][3]`, `VP'[i][1] = VP[i][1] +
jNdc.y · VP[i][3]` (clip'.xy = clip.xy + jNdc · clip.w). gtest `TaaJitter.*`: 3/3 (`TranslatesEveryVertexByExactlyTheJitterInPixels`, `ZeroJitterIsIdentity`, `LegacyRowTwoOffsetWasDepthAndWorldZDependent`), test_runtime rebuilt 17:28.

Static close-up, fixed binary:

| condition | 300→301 | 301→302 | 302→303 |
|---|---|---|---|
| TAA on (after) | 0.19 / 0.00 % | 0.22 / 0.00 % | 0.19 / 0.00 % |

Approach (2 cm/frame, TAA on, fixed): 9.4–9.5 / 4.3–4.4 % (before 8.5–9.3 / 3.6–4.3 %; TAA off 12.8–12.9 / 7.2–7.5 %) — unchanged within noise: at 2 cm/frame next to a wall the adjacent-frame difference is the camera motion itself, so the static test is the discriminating one.

Bloom in the TAA branch now reads the TAA output (`tonemapInputSrv`) — correct on its own terms
(the glow must be built from the displayed image) but measured as **not** the cause: the
`r.Bloom=0` control was identical to the baseline. Guard `scripts/rendering/test_taa_bloom_input.ps1` (11/11).

Tooling notes: PowerShell 5.1 reads a BOM-less script as ANSI, so regexes containing `──` never
match a UTF-8 source — anchor on ASCII; a `[string]$ImgA` parameter and a local `$imgA` are the
same variable and the typed parameter coerces the assigned hashtable to a string; comments in a
`.cpp` are not in the binary (only embedded shader text is) — verify a C++ change by timestamp,
not by `grep -a` on a comment.
