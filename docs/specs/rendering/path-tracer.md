# Path Tracer Runtime Mode

<!-- doc-meta
title: Path Tracer Runtime Mode
category: spec
engine: uzengine
theme: rendering
created: 2026-10-13
updated: 2026-10-13
status: active
author: docs-operator
-->

## Scope

This specification describes the shipped DXR-inline fullscreen pixel shader,
not the superseded compute/analytic proposal. Sources:
`runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl`,
`runtime/engine/renderer/passes/pathtracing/path_tracer_pass.cpp`, and
`runtime/engine/renderer/frame/frame_lighting.cpp`. See
[implementation reconciliation](../../analysis/path-tracing/08-implementation-reconciliation.md).

## Requirements

### Runtime controls

The deferred renderer SHALL select the path tracer when `r.PathTracer.Enable`
is enabled and SHALL write its result to the existing HDR scene-color target.
Atmosphere/fog/cloud composition and downstream post-processing SHALL continue.
The GDF preparation chain MUST remain available for bounce-hit shading; this
mode does not eliminate every GI or shadow preparation pass.

| CVar | Default | Range/type | Required effect |
|---|---|---|---|
| `r.PathTracer.Enable` | false | bool | Select PT lighting in the deferred renderer. |
| `r.PathTracer.MaxBounces` | 4 | int, 1-16 | Bound the TLAS bounce loop. |
| `r.PathTracer.SamplesPerFrame` | 4 | int, 1-16 | Set the paired-lobe budget described below. |
| `r.PathTracer.EnvIntensity` | 1 | float, 0-10 | Multiply sky radiance, not direct sun or emissive. |
| `r.PathTracer.RussianRoulette` | true | bool | Enable throughput-based termination. |
| `r.PathTracer.RussianRouletteMinBounce` | 2 | int, 0-8 | First eligible zero-based bounce index. |
| `r.PathTracer.ClampFirefly` | 64 | float, 0-1000000 | Clamp each channel of the frame lighting sample; zero disables. |
| `r.PathTracer.Accumulate` | true | bool | Blend history; false resets history every frame. |
| `r.PathTracer.ResetAccum` | command | exec | Request history reset on the next rendered PT frame. |

The shader SHALL use `max(1, floor(SamplesPerFrame / 2))` samples for each
of diffuse and specular lobes. Values below two still sample both lobes;
odd budgets round down. This is not an exact rays-per-pixel counter.
Control values MUST be transmitted without corrupting adjacent bindings:
the bindless `PathTracerRC` uses 18 uints in a 20-DWORD root-constant block.

#### Scenario: Control change

- **Given** PT is enabled with a valid sky capture
- **When** `EnvIntensity` changes from 1 to 2
- **Then** newly evaluated sky contributions SHALL double, while direct sun
  and emissive SHALL retain their own scales; existing history is retained
  unless reset is requested.

#### Scenario: Preview without accumulation

- **Given** PT is enabled
- **When** `Accumulate` is false
- **Then** each frame SHALL use fresh lighting without blending prior history,
  and the reported accumulated-history counter SHALL be zero.

### Transport and environment

The first surface hit SHALL come from GBuffer depth, world position, normal,
albedo, metallic, roughness, and emissive. First-hit direct sun SHALL use
GGX/Schlick shading and the CSM shadow atlas.

With an enabled, ready, non-empty scene TLAS, both indirect lobes SHALL trace
full paths using DXR inline ray queries. Bounce shading SHALL use shared
GDF/material-table normal and albedo helpers and TLAS sun-visibility queries.
Throughput SHALL be multiplied by hit albedo. Eligible Russian roulette
SHALL use maximum-channel throughput as survival probability and compensate
survivors by its inverse, with the shader's `1e-3` denominator floor.

All PT sky reads MUST use `PT_SkyRadiance`, including diffuse fallback,
specular fallback, and bounce misses. The selected source SHALL be active
HDRI, otherwise a filled atmosphere capture, otherwise the flat-skylight
sentinel fallback. Cubemap sampling MUST map world Z-up to cubemap Y-up via
`(x, z, y)` and apply the shared skylight tint/intensity and `EnvIntensity`.
Sky pixels SHALL be composited by the atmosphere pass after PT.

#### Scenario: Mirror and diffuse environment lighting

- **Given** a filled atmosphere capture, diffuse boxes, and a metallic mirror floor
- **When** PT traces with a valid TLAS
- **Then** diffuse misses SHALL receive sky radiance, and mirror paths SHALL
  reflect both sky and the sky-lit scene rather than only directly sun-lit faces.

#### Scenario: Ray tracing unavailable

- **Given** no usable TLAS, including when `r.RayTracing` is off
- **When** PT evaluates indirect lighting
- **Then** it SHALL use unoccluded sky samples for both lobes; this fallback
  MUST NOT be treated as a geometry-occluded transport reference.

### Accumulation and TAA

The bindless path SHALL ping-pong RGBA32F lighting history and produce RGBA16F
HDR output. With prior count N, it SHALL blend `(history * N + sample)/(N+1)`.
The counter counts accumulated frames, not lobe samples, and saturates at 65535.
Emissive SHALL be added after blending and SHALL NOT enter lighting history.

History MUST reset on a view-projection matrix change above `1e-5`, resolution
change, enable transition, or reset command. The next sample SHALL bypass
prior history. Ordinary transport-control changes do not automatically reset
history; callers SHOULD invoke `ResetAccum` when changing the estimator.

On enabling PT, the renderer MUST save the prior `r.TAA` value and force TAA
off while PT remains enabled. On disabling PT it SHALL restore that saved value.

#### Scenario: Camera cut or explicit reset

- **Given** accumulated stationary-camera history
- **When** the camera matrix changes beyond tolerance or `ResetAccum` executes
- **Then** the next PT frame SHALL start fresh and subsequent stationary frames
  SHALL resume accumulation.

#### Scenario: TAA enable and disable edges

- **Given** TAA was enabled before PT
- **When** PT is enabled and later disabled
- **Then** TAA MUST remain off during PT and SHALL return to its saved value
  on disable.

## Known limitations and acceptance evidence

- Indirect rough-metal specular is an F-weighted roughness-perturbed mirror
  direction, not a PDF-weighted GGX VNDF estimator. Subsequent path vertices
  use diffuse transport; first-hit direct GGX does not imply full microfacet
  transport at every bounce. No analytic primitive ECS is shipped.
- The non-bindless fallback does not bind progressive history or TLAS transport.
  Automatic upscaler-disable safety has not been established by this sign-off.
- Falcor/Mogwai is absent on this workstation. The mandatory 35 dB PSNR
  comparison is deferred to a Falcor-capable machine, not waived or passed.
- The session cold-start comparison was near-deterministic, not bit-identical:
  at most three code values differed on approximately 0.009% of pixels.
  The residual belongs to the accepted frame-0 GDF/sky allocator-layout
  dependence in `phase0_frame-allocation-layout-dependence` section 3.3.
- Reported session evidence: reflection-reference RT-on/off renders, five CPU
  parity tests, two Lua integration tests, and 7646 owned gtests passing with
  zero failures. The sole valid local golden is
  `.sandbox/golden/path_tracer/reflection_reference.png`; committing it and
  integrating regression comparisons remain open. See
  [09 test results](../../analysis/path-tracing/09-test-results.md) for tier evidence.

#### Scenario: Cold-start comparison

- **Given** two cold-start captures of the same scene and frame
- **When** their pixels are compared
- **Then** the result SHALL report the observed residual and its accepted
  frame-allocation class; it MUST NOT claim bit-identical PNGs or a passed
  Falcor PSNR gate.
