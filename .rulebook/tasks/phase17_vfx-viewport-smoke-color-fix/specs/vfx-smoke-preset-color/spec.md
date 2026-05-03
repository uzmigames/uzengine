# VFX Smoke Preset Color - Spec Delta

## ADDED Requirements

### Requirement: Smoke Preset Renders With Authored Color-Over-Life

The "Smoke" preset (preset index 2,
`ApplyPreset_Smoke` in `engine/vfx/components/emitter_presets.h`) MUST
render particles with the color values specified in the preset
function. Specifically, the per-particle color at any given age `t`
in `[0, lifetime]` MUST satisfy
`color(t) ~= lerp(colorLifeStart, colorLifeEnd, t / lifetime)` within
tolerance of 0.05 per channel.

#### Scenario: Smoke particle color evolves from start to end over lifetime

Given the demo runs the Smoke preset at 60 fps with default emission rate
And a single particle is born at frame N with `lifetime = 4.5s`
When the particle is sampled at `age == 0.1 * lifetime`, `0.5 * lifetime`, `0.9 * lifetime`
Then the sampled colors are within 0.05 of `lerp(colorLifeStart, colorLifeEnd, t/lifetime)` per RGBA channel
And the alpha channel decreases monotonically from `colorLifeStartA` to `colorLifeEndA`

#### Scenario: Smoke preset visual matches grey-to-dark intent

Given the demo cycles to the Smoke preset
And a screenshot is captured at frame 90 (steady-state with multiple particles)
When the screenshot is sampled at the centroid of the visible particle cloud
Then the sampled color is in the desaturated grey-to-dark family (RGB channels within 0.1 of each other, no warm tint > 0.15 difference between R and B)
And the alpha is non-zero (particles are visible)

### Requirement: Color-Over-Life Pipeline Works for All Presets

The color-over-life interpolation in
`runtime/shaders/runtime/vfx/particle_update_cs.hlsl` MUST apply
uniformly to ALL presets that set `colorLifeStart` and `colorLifeEnd`
(currently includes Fire, Smoke, Sparks, Fountain, MagicOrb, Burst,
ExplosionSmoke, Fireball, RingOfFire, ConeJet, CurlSmoke, Tornado,
Whirlpool, WindySmoke, LitSmoke, Orbital). The implementation MUST NOT
have per-preset gating.

#### Scenario: Fire preset color also evolves over lifetime (regression guard)

Given the demo cycles to the Fire preset
And a single particle is born at frame N with the preset's `lifetime`
When the particle is sampled at `age == 0.5 * lifetime`
Then the sampled color is in the warm range matching `lerp(Fire.colorLifeStart, Fire.colorLifeEnd, 0.5)` within tolerance 0.05
