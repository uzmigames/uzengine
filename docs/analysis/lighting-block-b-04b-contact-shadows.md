# Lighting block B, item 4b — contact shadows: depth linearisation and march

Rulebook task `phase0_lighting-block-b-04b-contact-shadows-depth` (proposal, design, spec
`contact-shadows-depth`). Branch `feat/sponza-gi-parity`, 2026-10-06. Side finding of block B.4
(`lighting-block-b-04-froxel-fog.md`).

## Before

- `LinearizeDepthReversedZ` (`runtime/shaders/runtime/include/depth_common.hlsli`) is documented as
  the reversed-Z inverse and implements the standard-Z one: near × far / (far − d × (far − near)).
  For the reversed-Z device depths of `Mat4::Perspective` it collapses to ≈ near + near² / z: 5.07 cm
  for a point at 3.5 m, 5.01 cm at 20 m (CS1's negative control: 5.05 cm for 350 cm). GTAO, its
  denoise and SSR carry the correct form inline; the only user of the shared function was the
  contact shadow march (and, for a few hours of block B.4, the fog composite).
- The contact shadow march (`contact_shadows.hlsli`, sun light in `deferred_lighting_ps.hlsl`)
  linearised the start and the samples with that function, assumed the ray approaches the camera at
  its full world-space length ("toward light = decreasing depth"), stepped 16 ray lengths across the
  screen and accepted a hit inside a window of 0.5 % of the start depth, i.e. 0.025 cm wide while the
  expected depth went negative at the second step: it never found an occluder. Verified before any
  change: Sponza (fog project, fog off) on the committed build 8851f37e with `r.ContactShadow.Length`
  0 and 0.01 is bit-identical in `direct_light` (CS2). `r.ContactShadows` only seeds a pass
  inspector entry nothing reads; the length CVar is the real switch (block A.8 list).

## What changed

- `LinearizeDepthReversedZ` = near × far / (near + d × (far − near)), documentation corrected; the
  fog composite uses the shared function again (block B.4's F2b verifies it through the composite's
  debug output).
- The march: start and end of the 100 cm ray (`r.ContactShadow.Length` × far plane) projected to clip
  space, the end cut at the near plane when the light is behind the camera; linear interpolation in
  clip space (perspective-correct on screen; the clip w is the view depth); screen extent capped at
  0.15 UV; per sample the scene's view depth (through the fixed function) is compared with the ray
  point's: a hit when the surface lies in front by more than 0.05 % of the depth (self-occlusion
  bias) and less than `r.Shadow.ContactShadowThickness` (0.5 %); full shadow on a hit (the old
  fade-in gave the first three steps, where contact lives, a third to two thirds of the shadow).
  Steps and thickness keep their CVars; `r.ContactShadow.Length` keeps its A.8 value.
- `scripts/contact_shadow_reference.py`: the fog shaft scene's floor and wall plus a 40 × 40 × 12 cm
  box in the sun, camera 2.2 m away, with the analytic shadow of wall and box and the distance to the
  box base; `scripts/test_contact_shadows.py` (CS1-CS6); the before captures kept under
  `.screenshots/flicker-elimination/test-contact-shadows/before/`.

What the march finds in Sponza: 2.9 % of the sun-lit pixels, almost all on the far upper wall, whose
plaster is modelled as 7 cm raised bands: the recessed band just below a raised one is occluded by
it (a CPU replica of the march on the dump's depth reproduces 39 of 40 darkened and 0 of 200
undarkened pixels, every hit at steps 1-4 with the sample 4-14 px above on the raised band, 3.3 cm in
front of the ray point). The strips are about two steps (10 cm) tall against 4.7 cm for the
geometric shadow of a 7 cm step under a 56° sun: the 16-step / 1 m march's resolution. CS5's depth
edge threshold was re-specified from 5 % (90 cm at 18 m, which saw none of this) to 0.2 %, with an
enrichment test over the base rate.

Kept: no dithering or temporal filtering of the march, no contact shadows for local lights, the
unread `r.ContactShadows`.

## Gates (`scripts/test_contact_shadows.py`)

| Gate | Criterion | Result |
|---|---|---|
| CS1 | `LinearizeDepthReversedZ` round-trips the reversed-Z projection on the CPU (<= 1e-5) | PASS: worst relative error 0.00e+00 over z = 6, 50, 350, 2000, 9000 cm |
| CS1 (control) | the pre-fix function is off by >= 90 % at 350 cm | PASS: pre-fix value 5.07 cm for z = 350 (error 0.986) |
| CS2 | before: Sponza on the committed build with the march on and off is identical | PASS: direct_light max |diff| 0.00e+00 |
| CS2 (bridge) | this build with the march off reproduces the committed frame | PASS: direct_light max |diff| 0.00e+00 |
| CS3 | reference scene: no false contact shadows on open lit floor (<= 0.2 %) | PASS: 4 of 1578926 px (0.000 %) |
| CS4 | reference scene: the march closes the CSM contact gap at the box base, or keeps the agreement when there is none | PASS: no gap to close (0.001): agreement off 0.999 -> on 0.999 over 4322 px |
| CS5 | Sponza: the march darkens <= 15 % of the lit pixels | PASS: 2.91 % (2137 of 73384) |
| CS5 | ... >= 70 % of them within 8 px of a >= 0.2 % depth step, >= 1.3 x the base rate | PASS: 0.926 of 2137 px; all lit 0.613; enrichment 1.51 |
| CS6 | DeferredLighting cost delta <= 0.5 ms at 1920x1080 | PASS: on 10.9059 ms, off 10.8015 ms |


**Numbers.** Function round trip: worst relative error 0.0e+00 over 6..9000 cm; the pre-fix function returns 5.07 cm for 350 cm and 5.00 cm for 9000 cm. Sponza before pair max |diff| 0.0e+00; bridge 0.0e+00. Reference scene: 1578926 open lit floor pixels, 4 darkened (0.000 %); box base zone 4322 px, agreement with the analytic shadow off 0.999 / on 0.999 (whole box shadow 0.986 / 0.989): the cascaded shadow maps already resolve a 12 cm box at 2.2 m, so the march has no gap to close there and must only do no harm. Sponza: 73384 lit pixels, 2137 darkened by the march (2.91 %), mean visibility drop 0.90; near a >= 0.2 % depth step: 0.926 of them against 0.613 of all lit pixels (enrichment 1.51). DeferredLighting 10.80 -> 10.91 ms. Harness 9/9. Fog harness on this build (the composite through the shared function): 10/10 (F2b through the shared function: median error 0.11 %, control 5.05 cm; F4a-d, F5 as in B.4; Camera C skipped: the march changes Sponza by design).

## Still open

- The march is sharp (16 steps over 1 m, no dither, no temporal filter): contact strips are quantised to
  the step (about 10 cm against 4.7 cm for a 7 cm step under a 56° sun).
- Only the sun has contact shadows; local lights do not.
- `r.ContactShadows` is unread (block A.8 list): `r.ContactShadow.Length 0` is the switch.
- The thickness is relative to depth (0.5 %): thin foreground objects far in front of the ray point are
  ignored by design; a world-space thickness would need a CVar decision.
