# Lighting Parity (Sponza × Intel Reference) — Spec Delta

## ADDED Requirements

### Requirement: Sun Direction Matches Intel Reference
The Sponza scene's directional sun SHALL match the Intel New Sponza reference sun:
elevation approximately 56° (tolerance ±2°), azimuth approximately 100° (tolerance ±5°),
validated by shadow direction in captured frames versus `Render_Main_A/C/E`.

#### Scenario: Shadow placement parity
Given the Sponza scene loaded with the calibrated sun rotation
When a frame is captured from PhysCamera A
Then sunlit floor patches and column shadows SHALL fall on the same surfaces as the Intel reference
And no sunlit surface in the reference SHALL be in full shadow in the capture (or vice versa)

### Requirement: Unified Sun/Sky Illuminance Scale
The directional light intensity and the sky atmosphere sun illuminance SHALL be driven
by the same scale, so the direct:ambient ratio is determined by the Bruneton model
rather than by two independent arbitrary constants.

#### Scenario: Contrast ratio in HDR
Given the calibrated scene with exposure disabled
When a sunlit floor pixel and a shaded colonnade pixel are sampled pre-tonemap
Then their luminance ratio SHALL be ≥ 3:1 (reference renders show high-contrast daylight)

### Requirement: Shadow-Region Ambient Floor Within Shader Guidance
The scene's SkyLight `min_occlusion` SHALL be within the deferred shader's documented
range (0.005–0.02) so that deep shade retains gradient instead of a uniform gray lift.

#### Scenario: Deep alcove retains gradient
Given a camera facing the fountain alcove (reference view C)
When a frame is captured
Then alcove pixels SHALL show measurable luminance falloff with depth/occlusion
And SHALL NOT be clamped to a uniform floor value

### Requirement: Authored Material Response
Sponza materials SHALL preserve the Intel-authored glTF response: roughness and
metallic multipliers of 1.0 and a neutral base color tint.

#### Scenario: No global overrides
Given any material under `sample/content/materials/sponza/`
When its `.uzmat` is parsed
Then `roughnessMultiplier` SHALL equal 1.0, `metallicMultiplier` SHALL equal 1.0,
and `baseColorTint` SHALL equal [1.0, 1.0, 1.0]

### Requirement: GI Resources Bound Only When Valid
The renderer MUST NOT bind GI result textures that have never been written.
SSGI result SRVs SHALL be sentinel while `r.SSGI` is disabled or no output has been
produced. DDGI atlas SRVs SHALL be sentinel until the first successful probe update,
and atlases MUST be cleared at creation. The Radiance Cache SH buffer MUST be zeroed
at creation.

#### Scenario: Default scene binds no unwritten GI data
Given the Sponza scene with default CVars (`r.SSGI` false, `r.RayTracing.GI` false)
When per-frame constants are populated
Then `ssgiSrvIndex` SHALL be the sentinel value
And DDGI SRVs SHALL be sentinel or the atlases SHALL contain only cleared data

#### Scenario: Enabling SSGI produces valid binding
Given `r.SSGI` set to 1 before a frame
When the SSGI pipeline completes its upsample
Then `ssgiSrvIndex` SHALL reference the produced texture

### Requirement: Deterministic Reference Cameras
The Sponza scene SHALL provide fixed camera entities reproducing Intel PhysCamera
views A, C, and E (converted transforms and vertical FOV), and no script SHALL
mutate the active camera during deterministic captures.

#### Scenario: Deterministic capture
Given the parity scene loaded without the fly-camera controller
When N frames are rendered without input
Then frames 2..N SHALL be pixel-identical after temporal settle
And the framing SHALL match the corresponding Intel reference view

### Requirement: Sun Disk Angular Radius
The atmosphere sun disk SHALL use half the light's angular diameter as its radius,
consistent with the shadow-path convention.

#### Scenario: Correct disk size
Given `light_source_angle` of 0.53°
When the sky is rendered
Then the sun disk angular radius SHALL be 0.265° (±0.01°)
