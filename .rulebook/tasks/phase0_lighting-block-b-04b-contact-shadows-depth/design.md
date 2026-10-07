# Design: contact shadows depth linearisation and march

## The function
`Mat4::Perspective` is reversed-Z: device depth d = near × (far − z) / (z × (far − near)), so the view
depth is z = near × far / (near + d × (far − near)). `LinearizeDepthReversedZ` carried
near × far / (far − d × (far − near)), the standard-Z inverse, which for d in (0, 1] of a reversed-Z
buffer collapses to ≈ near + near² / z: 5.07 cm at 3.5 m, 5.01 cm at 20 m. GTAO, the GTAO denoise
and SSR carry the correct form inline; nothing else called the shared function.

## The march (before)
`ComputeContactShadow` projected the receiver and the end of a 100 cm world-space ray
(`r.ContactShadow.Length` × far plane, block A.8), then stepped `min(16 × rayLenUV, 0.15)` across the
screen in 16 steps (16 ray lengths), and compared each sample's linear depth with
`startLinZ − rayLength × t` ("toward light = decreasing depth"), accepting a hit when the difference
lay in (0, 0.5 % of startLinZ). With startLinZ ≈ 5 cm the window was 0.025 cm wide and the expected
depth went negative at the second step: no hit, ever. With a correct linearisation the model would
still be wrong whenever the light is not on the camera's axis.

## The march (after)
- Start and end of the ray are projected to clip space; the end is cut at the near plane when the
  light is behind the camera. The ray is interpolated linearly in clip space, which is
  perspective-correct in screen space, and the clip w of each sample is its view depth in cm.
- The screen-space extent keeps the 0.15 UV cap (long rays near the camera): `tMax`.
- Per sample: scene view depth from the depth buffer through the fixed function; `diff = rayZ − sceneZ`;
  hit when `diff` is above a self-occlusion bias (0.05 % of rayZ, the receiver's own surface seen at
  the first samples) and below the thickness (`r.Shadow.ContactShadowThickness`, 0.5 % of rayZ:
  surfaces farther in front are foreground objects, not contact). Full shadow on hit (the old step
  fade-in gave the first three steps, where the contact lives, 1/3 to 2/3 of the shadow).
- Steps `r.Shadow.ContactShadowSteps` (16), sky samples skipped, bounds exit. Cost: the same loop as
  before, now reaching its loads.

## Evidence
- `scripts/test_contact_shadows.py`: CS1 CPU round trip of the extracted function (fixture of the old
  one as the negative control); CS2 the before pair captured on 8851f37e (identical) and the bridge
  (this build, march off, reproduces it, so the march-off captures stand for the committed build);
  CS5 re-specified after the first run (depth-step threshold 5 % → 0.2 %, plus enrichment over the base
  rate): a CPU replica of the march on the dump (same clip-space interpolation, the dump's depth)
  reproduces the darkened pixels and shows each is a recessed band of the far wall's 7 cm plaster
  relief occluded by the raised band above it, a physical contact shadow the first threshold (90 cm
  at 18 m) could not see;
  CS3 / CS4 on `scripts/contact_shadow_reference.py` (floor, wall, a 40 × 40 × 12 cm box in the sun
  at 40°, camera 2.2 m away; analytic shadow of wall and box, distance to the box base): no false
  shadows on open lit floor, the CSM's contact gap at the box base closes; CS5 Sponza: how many lit
  pixels the march darkens and whether they sit at depth discontinuities; CS6 DeferredLighting cost.
- Montages (fog_montage.py) of Sponza and the reference scene, march off / on.

## Rejected / deferred
- Fixing only the function: it would turn a dead march into an erratic one (depth model wrong off
  the camera axis).
- Dithered start / temporal filtering of the march (UE): no temporal path in this engine's deferred
  lighting; sharp 16-step contact shadows are the Battlefield 1 practice.
- `r.ContactShadows` (unread): block A.8's CVar list.
