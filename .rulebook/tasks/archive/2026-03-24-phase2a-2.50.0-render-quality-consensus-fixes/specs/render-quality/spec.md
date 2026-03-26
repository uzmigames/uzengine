# Render Quality Consensus Fixes — Spec Delta

## ADDED Requirements

### Requirement: GTAO AO Power Must Match Reference Default
The GTAO compute shader MUST use `GTAO_AO_POWER = 1.0` as the AO power curve exponent. This value corresponds to the reference AmbientOcclusionPower=2.0 multiplied by the GTAO path factor of 0.5, yielding an effective power of 1.0.

#### Scenario: AO darkening at sphere-ground contact
Given a sphere resting on a flat surface
When GTAO computes ambient occlusion for pixels at the contact point
Then the AO value MUST be less than 0.6 (visible darkening)
And `pow(rawAO, 1.0)` MUST produce stronger darkening than `pow(rawAO, 0.3)`

#### Scenario: AO on flat open surface
Given a large flat surface with no nearby occluders
When GTAO computes ambient occlusion for pixels far from any edge
Then the AO value MUST be greater than 0.85 (minimal darkening on open surfaces)
And flat surfaces MUST NOT appear unnaturally dark

### Requirement: Micro-Shadow Must Be Active When GTAO Is Enabled
The deferred lighting shader MUST apply micro-shadow modulation to direct lighting when the GTAO SRV is valid (`gtaoSrvIndex != 0xFFFFFFFF`). The formula MUST be: `microShadow = saturate(NdotL + 2.0 * ao * ao - 1.0)` where `ao` is the GTAO-sampled ambient occlusion value.

#### Scenario: Micro-shadow on textured surface at grazing light angle
Given a stone/brick surface with normal-mapped detail
When the directional light hits at a low angle (NdotL < 0.3)
And the GTAO AO value is 0.5 (moderate occlusion)
Then microShadow = saturate(0.3 + 2*0.25 - 1.0) = saturate(-0.2) = 0.0
And the direct light contribution MUST be fully attenuated at that pixel

#### Scenario: Micro-shadow on well-lit surface
Given any surface with NdotL = 0.8 and AO = 0.9
When micro-shadow is computed
Then microShadow = saturate(0.8 + 2*0.81 - 1.0) = saturate(1.42) = 1.0
And direct light MUST NOT be attenuated (micro-shadow has no effect on well-lit, unoccluded surfaces)

#### Scenario: Micro-shadow disabled when GTAO is off
Given GTAO is disabled (r.GTAO=false, gtaoSrvIndex == 0xFFFFFFFF)
When the deferred lighting shader runs
Then microShadow MUST be 1.0 (no attenuation)
And the direct lighting output MUST be identical to the non-micro-shadow path

### Requirement: SSR MaxRoughness Must Be 0.8
The SSR trace compute shader MUST accept reflections from surfaces with roughness up to 0.8. The maxRoughness parameter passed via root constants MUST be 0.8 (matching UE5 `r.SSR.MaxRoughness` default).

#### Scenario: Semi-rough surface receives SSR
Given a surface with roughness = 0.6 (rough concrete, weathered wood)
When SSR trace is executed
Then the surface MUST receive screen-space reflections (blurred by roughness)
And the reflection confidence MUST be greater than 0.0

#### Scenario: Very rough surface excluded from SSR
Given a surface with roughness = 0.9 (rough fabric, matte paint)
When SSR trace is executed
Then the surface MUST NOT receive screen-space reflections
And the reflection confidence MUST be 0.0
And the surface MUST fall back to IBL/probe reflections only

### Requirement: PCF Over-Blur Correction Must Be Active
The shadow sampling pipeline MUST apply PCF over-blur correction via `shadow = shadow * shadow` (Square function) after the PCF/Poisson filter. This compensates for the inherent softening of the PCF kernel by sharpening the shadow transition.

#### Scenario: Shadow edge sharpness
Given a shadow edge from cascade 0 (near camera)
When PCF over-blur correction is applied
Then the shadow transition width MUST be narrower than without correction
And the shadow MUST NOT exhibit new self-shadowing artifacts (acne)

## MODIFIED Requirements

### Requirement: GTAO AO Intensity Calibration
The GTAO AO intensity (`GTAO_AO_INTENSITY`) MAY be adjusted between 0.4 and 1.0 to achieve visually correct AO strength after the AO power correction. The final calibration MUST be verified visually at multiple camera distances.

#### Scenario: AO intensity balance
Given GTAO_AO_POWER = 1.0 (corrected)
When GTAO_AO_INTENSITY is set to 0.5
Then the AO effect MUST be visible at contact points (sphere-floor, wall-floor)
And the AO effect MUST NOT cause unnaturally dark flat surfaces
And the overall scene brightness MUST remain within acceptable range (no global darkening)

### Requirement: SSR Hi-Z Acceleration
The SSR trace shader SHOULD use the Hi-Z mip pyramid for depth sampling when available (`m_hizMipCount > 0`). When Hi-Z is not available, the trace MUST fall back to raw depth buffer sampling.

#### Scenario: SSR trace with Hi-Z available
Given the Hi-Z pyramid has been computed (m_hizMipCount > 0)
When SSR trace executes
Then distant samples MUST use coarser Hi-Z mip levels
And trace range MUST be at least 2x longer than raw-depth tracing at the same sample count

#### Scenario: SSR trace without Hi-Z
Given the Hi-Z pyramid is not available (m_hizMipCount == 0)
When SSR trace executes
Then the trace MUST use the raw depth buffer (current behavior)
And no crash or visual artifact MUST occur

### Requirement: SSR Temporal Accumulation
The SSR resolve output SHOULD be temporally accumulated using a history texture with exponential moving average (alpha = 0.1). Motion vectors MUST be used for history reprojection to prevent ghosting.

#### Scenario: Static camera SSR temporal convergence
Given a static camera viewing reflective surfaces
When 10 frames have been rendered
Then the SSR result MUST be visually noise-free
And the reflection MUST match the scene content (no ghosting from old frames)

#### Scenario: Moving camera SSR temporal invalidation
Given a camera that moves or rotates rapidly
When motion vectors indicate large per-pixel displacement
Then the temporal blend factor MUST increase toward 1.0 (favor current frame)
And no ghosting artifacts MUST appear from stale history
