# 08 — Implementation vs plan reconciliation (2026-10-13)

## Final session reconciliation (2026-10-13)

This section supersedes the initial scoping snapshot and recommendation below.
Documents 01-07 are approved as historical design records, not as descriptions
of the shipped implementation. The current contract is
[the path tracer spec](../../specs/rendering/path-tracer.md).

- The shipped implementation remains the DXR-inline pixel shader
  `path_tracer_ps.hlsl`, registered by `path_tracer_pass.cpp` and `frame_lighting.cpp`.
  GBuffer supplies the first hit; TLAS queries supply bounce intersections;
  shared GDF/material-table helpers supply bounce normals/albedo. First-hit sun
  shading uses CSM; subsequent sun visibility uses TLAS shadow rays. The GDF
  preparation chain remains active in PT mode.
- The black-indirect fix described below is complete and verified with
  reflection-reference RT-on and RT-off renders. Specular now traces full paths.
  Rough-metal indirect specular remains an F-weighted, roughness-perturbed mirror
  approximation, not a PDF-weighted GGX VNDF estimator.
- All nine controls are wired: `r.PathTracer.{Enable, MaxBounces,
  SamplesPerFrame, EnvIntensity, RussianRoulette, RussianRouletteMinBounce,
  ClampFirefly, Accumulate, ResetAccum}`. `PathTracerRC` contains 18 uints; the
  bindless root-constant block was expanded from 16 to 20 DWORDs to prevent
  adjacent root-CBV corruption. `UZ_CVAR_EXEC` supports the reset command.
  TAA is forced off while enabled and its saved value restored on disable.
- Five CPU parity tests and two Lua integration tests passed; the session's
  final owned-gtest result was 7646 passing, zero failing. These are reported
  session results, not a new run during this documentation update.
- Tier-5 cold-start comparison ran: differences are at most three code values
  on approximately 0.009% of pixels, concentrated on diffuse-GI box surfaces.
  This is the accepted frame-0 GDF/sky content allocator-layout dependence,
  recorded in `phase0_frame-allocation-layout-dependence` section 3.3, not a
  separate PT-specific defect. Bit-identical PNGs were not achieved.
- The only valid local golden is
  `.sandbox/golden/path_tracer/reflection_reference.png`; committing requires
  explicit user authorization. The Cornell project's default camera is outside
  the box, so its open-sky capture is not a usable golden. See
  [09 test results](09-test-results.md) for the authoritative tier evidence.
- Falcor PSNR remains blocked: Falcor/Mogwai is not installed here.
  A Falcor-capable machine must establish the version/commit and run the
  comparison. The 35 dB gate has not passed, and full task archival remains open.

Scoping pass for `phase0_path-tracer-runtime-mode` before any further work. The task's
`tasks.md` (56 open items) and the 01-07 design docs describe one architecture; the code
on `main` implements a **different** one. This file records the real state so the task can
be re-baselined.

## The architecture pivoted

- **Planned (proposal + 01-07, `status: draft`):** a *pure compute, analytic scene*
  path tracer, explicitly "NOT DXR-based" — `path_tracer_cs.hlsl` + `path_tracer_resolve_cs.hlsl`,
  Shadertoy-style analytic primitives from a `PtPrimitiveComponent` ECS, Falcor *compute*
  parity.
- **Shipped (`main`):** a **DXR-inline, pixel-shader** path tracer. Files that exist:
  `runtime/engine/renderer/passes/pathtracing/path_tracer_pass.{h,cpp}` (196 lines) and
  `runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl` (416 lines). The compute
  shaders and the `PtPrimitiveComponent` the checklist names do **not** exist.

## What the shipped path tracer actually does (`path_tracer_ps.hlsl`)

A fullscreen pixel shader, enabled by `r.PathTracer.Enable=1` (`frame_lighting.cpp:70`),
that replaces deferred lighting / GI / shadow dispatches:

1. First hit from the **GBuffer** (normal, albedo, metallic, roughness, position, emissive).
2. Direct sun with **CSM shadow-atlas** next-event shadow rays + GGX specular.
3. Indirect: `PT_SAMPLES_PER_FRAME` rays/frame, half cosine-diffuse + half VNDF microfacet
   specular, Fresnel-weighted; bounces traced with **DXR inline `RayQuery`/`TraceRayInline`
   against the scene TLAS** (`kPTBounces = 4`, fixed, no Russian roulette), hit shading =
   sun NEE + GDF-clipmap hit albedo, miss = sky cubemap.
4. Progressive accumulation (`g_sampleCount`, history via `g_accumPrevSrv`), firefly clamp
   (`clamp(.,0,64)`).

So the transport of proposal Steps 2-8 is implemented, but via GBuffer+TLAS+GDF, not the
analytic/compute design. The pass binds shadow atlas, TLAS, material table, ShadowDataCB.

## Verified first run (2026-10-13)

`uzengine.exe --project scripts/rendering/reflection-reference.uzproject --frames 302
--screenshot-frame 300 --cvar r.PathTracer.Enable=1` (exit 0). The frame renders the sky
atmosphere and the scene boxes with direct-sun-lit faces, confirming the pass is wired and
accumulates. **Observation to follow up:** the ground plane and most non-sun-facing
surfaces are pure black — indirect environment contribution looks absent or near-zero on
diffuse surfaces (or the test scene's ground albedo is ~0). This is the first concrete
correctness question for the task.

## Checklist status (re-baselined)

- **Moot (superseded design):** 1.5 (compute shaders), 7.x (`PtPrimitiveComponent`
  analytic scene), and the "pure compute / analytic" framing of 8.x and the proposal.
- **Implemented via the pixel-shader design (transport):** first hit, one/multi-bounce,
  direct sun + CSM shadow, GGX specular, accumulation, camera-cut reset, firefly clamp.
- **Genuinely open regardless of architecture:** CVar wiring (the shader uses compile-time
  `kPTBounces`/`PT_SAMPLES_PER_FRAME`, not `r.PathTracer.MaxBounces/Samples` — 1.2 likely
  unwired), Lua API (9), CPU parity tests (10), Falcor PSNR parity (8), visual goldens (11),
  determinism (12), docs promotion (13), sign-off (14), tail (15).

## Black-indirect fix (2026-10-13)

The "black ground / black diffuse surfaces" was two bugs stacked:
- **Primary (RC1):** `m_skyCubemapFilled` (`scene_renderer_impl.h`) was **dead** — nothing ever
  set it to `true`. So `ptDesc.skyCubeSrvIndex` / `skyCubemapSrv` (`frame_lighting.cpp`) were
  permanently the `UINT32_MAX` sentinel: the RT-off fallback and the specular-miss sampled an
  out-of-bounds descriptor (black), and `GITrace_SkyRadiance` returned the flat skylight colour
  (so RT-on boxes were lit by flat skylight, not the atmosphere cubemap, and the mirror floor
  was black). Fix: set `*ctx.skyCubemapFilled = true` after the capture is registered
  (`atmosphere_renderer.cpp`); unify the PT sky SRV (`ptSkySrv`) independent of the TLAS
  (`frame_lighting.cpp`).
- **Secondary (RC2):** the PT's direct `skyCube.SampleLevel(dir)` reads (diffuse fallback,
  specular miss) lacked the world Z-up -> cubemap Y-up `(x, z, y)` swizzle that every other sky
  sampler uses (deferred `WorldToCubemap`, DDGI, radiance cache, SDF reflections). Fixed by
  routing all PT sky reads through a `PT_SkyRadiance` helper (swizzle + tint + sentinel).
- **Quality (architect option ii):** the specular lobe now traces a full path (`PT_TracePath`,
  shared with the diffuse lobe) instead of a single sun-NEE bounce, so the mirror floor reflects
  the sky AND the sky-lit boxes, not only directly-sun-lit faces.

Verified by rendering reflection-reference at frame 300: RT-off and RT-on both light the diffuse
boxes with the real atmosphere sky and show the metallic mirror floor reflecting the sky and the
coloured, sky-lit boxes. (Architect analysis: claude-fable-5-1.)

## Recommendation

Re-baseline the task to the shipped DXR-inline design before executing checklist items (the
compute/analytic items are dead). The highest-value first engineering sub-goal is the
**black-diffuse-surface / indirect-environment** correctness question above (it blocks any
golden or Falcor-parity gate). Second: wire the control CVars (1.2) so bounces/samples are
tunable. The user should pick the next concrete sub-goal given the task's size.
