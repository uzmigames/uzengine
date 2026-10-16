# Indoor Lighting System -- Spec Delta

## ADDED Requirements

### Requirement: SkyLight Indoor Parameters
The deferred lighting shader MUST read `indirectLightingIntensity` and `minOcclusion` from the per-frame constant buffer and apply them to the diffuse IBL term.

The ambient occlusion term MUST be clamped to `max(ao, minOcclusion)` so that fully occluded interior areas still receive a configurable minimum ambient fill.

The diffuse IBL contribution MUST be multiplied by `indirectLightingIntensity` to allow independent control of ambient fill without affecting specular reflections.

#### Scenario: Indoor scene with minOcclusion
Given a fully enclosed interior where all pixels have GTAO ao=0.0
When `SkyLightComponent.minOcclusion` is set to 0.15
Then the deferred lighting shader computes `effectiveAO = max(0.0, 0.15) = 0.15`
And the ambient term is `skyIrradiance * albedo * 0.15` (not pitch black)

#### Scenario: IndirectLightingIntensity boost
Given a Sponza interior with default SkyLight settings
When `indirectLightingIntensity` is set to 2.5
Then the diffuse IBL contribution is 2.5x brighter than default
And specular IBL reflections remain at 1.0x (unchanged)

#### Scenario: Lower hemisphere enabled
Given `SkyLightComponent.lowerHemisphereIsBlack = false`
When the SH projection captures sky irradiance
Then the lower hemisphere contributes `lowerHemisphereColor` to the SH coefficients
And floor-facing normals receive non-zero irradiance from below

### Requirement: Irradiance Probe Grid
The engine MUST support a 3D grid of L2 spherical harmonics probes that provide spatially-varying diffuse indirect illumination.

Each probe MUST store 9 L2 SH coefficients per RGB channel (27 floats total, packed as 9 float4 with .w unused or used for validity weight).

The deferred lighting shader MUST sample the probe grid via trilinear interpolation of the world-space position mapped to grid UVW coordinates, then evaluate `IrradianceSH(normal, shCoeffs)` for the diffuse GI contribution.

#### Scenario: Probe grid provides spatial variation
Given a probe grid covering the Sponza atrium (8x4x8 = 256 probes)
When probes near windows capture bright sky irradiance
And probes deep in corridors capture only bounced irradiance
Then the lighting shader produces brighter ambient near windows
And darker ambient in deep corridors
And the transition is smooth (trilinear interpolation, no grid artifacts)

#### Scenario: Probe grid with no volume component
Given a scene with no `LightProbeVolumeComponent` entity
When the renderer initializes
Then no probe grid is allocated
And the lighting shader falls back to the global SH irradiance (existing behavior)

#### Scenario: Probe grid auto-fit
Given a `LightProbeVolumeComponent` with `autoFitScene = true`
When the scene AABB is computed from all mesh bounds
Then the probe volume extent is set to the scene AABB with 10% padding
And the grid dimensions are computed to maintain approximately 200cm cell size

### Requirement: Probe Update via SDF Ray March
The engine MUST update irradiance probes by ray marching through the Global Distance Field to capture indirect illumination from scene geometry.

Each probe update MUST cast at least 64 cosine-weighted hemisphere rays, march through the SDF to find intersections, and accumulate SH contributions from hit surface albedo and sky at miss directions.

Probe updates MUST be temporally spread across frames to amortize GPU cost -- a full grid refresh MUST complete within 4 frames for a 256-probe grid.

#### Scenario: SDF probe update captures bounce light
Given a probe grid and an active Global Distance Field
When the probe update compute shader dispatches
Then probes near bright surfaces accumulate warm SH from surface albedo
And probes near the sky opening accumulate blue SH from sky
And the SH coefficients converge within 4 frames via hysteresis blending

#### Scenario: Hysteresis prevents flicker
Given a probe with converged SH coefficients
When the next frame's ray trace produces slightly different results (noise)
Then the new SH is blended as `lerp(oldSH, newSH, 0.05)`
And visible flicker is imperceptible (frame-to-frame SH delta < 1%)

### Requirement: DDGI Ray Traced Probe Update
The engine MUST support an optional DXR-based probe update path that traces real rays through the TLAS for higher-quality indirect illumination.

DDGI MUST store per-probe irradiance in a 2D texture atlas using octahedral encoding (6x6 texels per probe minimum).

DDGI MUST store per-probe mean depth in a separate atlas (16x16 texels per probe) for Chebyshev visibility testing to prevent light leaking through thin geometry.

DDGI MUST be gated by `r.RayTracing.GI` CVar and gracefully fall back to the SDF probe path when DXR is unavailable.

#### Scenario: DDGI provides colored bounce
Given a scene with red walls and blue sky
When DDGI traces rays from interior probes
Then probes near red walls accumulate red-tinted irradiance
And probes near the sky opening accumulate blue irradiance
And the deferred lighting result shows colored bounce on surfaces

#### Scenario: DDGI prevents light leaking
Given a thin wall separating two rooms (wall thickness < probe cell size)
When the deferred lighting shader samples surrounding probes
Then the Chebyshev visibility test from the depth atlas rejects probes on the other side of the wall
And light does not leak through the thin wall

#### Scenario: DDGI fallback to SDF
Given a GPU without DXR support (r.RayTracing.GI=0)
When the renderer initializes
Then DDGI is not allocated
And the probe grid uses the SDF ray march update path instead

### Requirement: Interior Shadow Enhancement
The engine MUST provide screen-space contact shadows and tuned GTAO for indoor depth perception.

GTAO parameters MUST be adjustable via CVars (`r.GTAO.Radius`, `r.GTAO.Power`, `r.GTAO.Intensity`) to allow indoor-specific tuning without recompilation.

#### Scenario: Contact shadows between columns and floor
Given the Sponza scene with columns on a flat floor
When contact shadows are enabled (`r.ContactShadows=1`)
Then fine shadow detail appears at the base of each column
And the shadow fades smoothly over 5-10cm distance

#### Scenario: GTAO indoor tuning
Given default GTAO parameters optimized for outdoor
When `r.GTAO.Radius=50` and `r.GTAO.Intensity=1.5` are set for indoor
Then occlusion appears in arch corners, column bases, and window recesses
And the occlusion is not over-dark (clipped by `minOcclusion` from SkyLight)
