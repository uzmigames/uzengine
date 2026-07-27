# Spec Delta: GI System — Lumen-Lite Core

## ADDED Requirements

### Requirement: Diagnostic visibility of disabled GI passes
The diagnostic system SHALL register the `ssgi`, `gtao`, `rc` (radiance cache), and `ddgi` render-target groups unconditionally, reporting status `SKIPPED` for any target whose owning pass is disabled, so that `--diag` output distinguishes "pass off" from "pass absent".

#### Scenario: Diag with all GI disabled
Given a scene with `r.SSGI=0`, `r.GTAO=0`, `r.RadianceCache=0`, and `r.RayTracing.GI=0`
When the runtime executes `--diag --frames 120`
Then the DIAG_JSON target list contains ssgi/gtao/rc/ddgi entries with status `SKIPPED`
And no GI target is silently missing from the list

### Requirement: Probe hit lighting is shadowed, oriented, and colored
Probe ray tracers (SDF radiance cache and DXR DDGI) SHALL compute hit radiance using a real surface normal (GDF central-difference gradient for SDF hits; geometric triangle normal for DXR hits), a sun visibility test at the hit point (occlusion trace toward the light), and a per-surface albedo term, replacing the previous unshadowed `dot(-rayDir, L) * constantGroundAlbedo` model.

#### Scenario: Enclosed probe receives no direct sun energy
Given a probe whose sun-facing rays are fully occluded by scene geometry
When the probe update dispatch completes
Then the probe's accumulated SH irradiance contains no direct-sun contribution
And interior pixels sampling that probe show no sun-tinted energy in `--pixel-debug`

#### Scenario: Colored bounce from lit surfaces
Given a sun-lit surface with non-neutral albedo adjacent to a shadowed surface
When probes between them are updated and the deferred pass samples the probe grid
Then the shadowed surface receives irradiance tinted by the lit surface's albedo

### Requirement: Multi-bounce feedback through the probe grid
The probe hit-lighting model SHALL add an indirect term sampled from the previous frame's probe irradiance at the hit point, modulated by hit albedo, so bounce depth accumulates across frames (@ref Majercik 2019, "infinite bounces").

#### Scenario: Second-bounce accumulation
Given a static scene and camera with the radiance cache enabled
When 120 frames accumulate
Then interior regions reachable only by two light bounces show non-zero irradiance
And total irradiance converges (no unbounded growth across a further 500 frames)

### Requirement: Per-instance average material table
The renderer SHALL build a per-instance average albedo and emissive table at scene load, uploaded as a StructuredBuffer and indexable by DXR `InstanceID()`, for use as the hit-albedo term in probe tracers.

#### Scenario: Table indexed by instance
Given a loaded scene with N mesh instances
When the DXR probe tracer commits a triangle hit
Then it reads the hit instance's averaged albedo/emissive from the table via `InstanceID()`

## MODIFIED Requirements

### Requirement: Indirect diffuse composition
The deferred lighting pass SHALL compose indirect diffuse as a validity-prioritized cascade — screen-space GI where the screen ray is valid, then world-space probes, then sky SH — blending by validity weights and never summing two sources for the same pixel energy. Scene-radiance sources (SSGI, probes) SHALL NOT be tinted by the skylight color; the tint applies only to the sky-SH fallback. GTAO SHALL modulate the unified indirect diffuse exactly once.

#### Scenario: No double counting with SSGI active
Given `r.SSGI=1` and `r.RadianceCache=1`
When a pixel has a valid SSGI sample
Then the pixel's indirect diffuse uses SSGI radiance blended over the probe value by validity weight
And the probe/sky contribution is correspondingly reduced so total energy is not summed twice
