# Contact shadows: fix the reversed-Z depth linearisation and the march

## Why

Side finding of lighting block B.4 (2026-10-06): `LinearizeDepthReversedZ` in
`runtime/shaders/runtime/include/depth_common.hlsli` is documented as the reversed-Z inverse but
implements the standard-Z one (near × far / (far − depth × (far − near))), which returns about the
near plane for every reversed-Z depth of `Mat4::Perspective`. Its only remaining user is the
screen-space contact shadow march (`contact_shadows.hlsli`, the sun in `deferred_lighting_ps.hlsl`):
with every linear depth ≈ 5 cm the march's thickness test (0.5 % of the start depth = 0.025 cm) can
never pass, so the contact shadows have been dead. Verified before any change: Sponza rendered with
`r.ContactShadow.Length 0` and `0.01` on the committed build 8851f37e is bit-identical in
`direct_light` (captures under `.screenshots/flicker-elimination/test-contact-shadows/before/`).
The march itself also assumes the ray approaches the camera at its full world-space length
("toward light = decreasing depth") and steps 16 ray lengths across the screen, so fixing the
function alone would turn a dead feature into an erratic one. `r.ContactShadows` only seeds a pass
inspector entry nothing reads; the length CVar is the real switch.

## What changes

- `LinearizeDepthReversedZ` gets the reversed-Z inverse near × far / (near + depth × (far − near))
  (the form GTAO and SSR already carry inline); the fog composite goes back to the shared function.
- The contact march interpolates the ray in clip space (perspective-correct on screen; clip w is the
  view depth) and compares, per sample, the scene's view depth with the ray point's: a surface in
  front of the ray point by more than a self-occlusion bias and less than the thickness occludes it.
- `scripts/rendering/contact_shadow_reference.py` (floor, wall, a 40 × 40 × 12 cm box in the sun, camera 2.2 m
  away) and `scripts/rendering/test_contact_shadows.py` with the gates below; Sponza before / after montage.

## Gates (pre-registered)

CS1 the function round-trips the reversed-Z projection on the CPU (≤ 1e-5), the pre-fix fixture is
off by ≥ 90 % at 350 cm; CS2 the before pair is identical and this build with the march off
reproduces it; CS3 ≤ 0.2 % of open lit floor pixels darkened by the march in the reference scene;
CS4 the march closes at least half of the CSM's contact gap at the box base (or keeps the agreement
when there is none); CS5 in Sponza the march darkens ≤ 15 % of the lit pixels and ≥ 70 % of those lie
within 8 px of a depth discontinuity; CS6 DeferredLighting cost delta ≤ 0.5 ms at 1080p.

## Out of scope

Temporal filtering or dithering of the march, local-light contact shadows, the unread
`r.ContactShadows` CVar (block A.8 list), the marches of SSR / SSGI (own linearisations).
