<!-- doc-meta -->
title: Known Issues & Partial Implementations
date: 2026-04-07
status: living-document
<!-- end-doc-meta -->

# Known Issues & Partial Implementations

Living registry of systems/features that are **partially implemented**,
**disabled by default**, or **known to have bugs**. Everything listed here
has either been worked on and paused, or was added speculatively and not
integrated. The goal is to prevent rediscovery cost — before touching any
of these, read this doc and the linked code locations.

**Ordering**: user-visible impact first, then size of remaining work.
**Status legend**:
- `OFF` — implemented code path exists, disabled via CVar (documented below)
- `PARTIAL` — builds and runs but known incorrect / incomplete
- `STUB` — scaffolding only, never integrated / tested
- `BUG` — enabled but produces wrong output

---

## Anti-Aliasing / Temporal / Upscalers

### `r.TAA` — `OFF` (BUG)
**File**: [runtime/engine/renderer/frame/frame_setup.cpp:42](../../runtime/engine/renderer/frame/frame_setup.cpp)
**Default**: `false`

TAA causes visible mesh trembling / vertex wobble. Multiple tuning sessions
on jitter sequences, clamping, and history rejection did NOT resolve it.
Suspected root cause: motion vector generation or history reprojection bug
introduced by an unrelated change. Needs a ground-up audit.

**Workaround**: leave disabled. FXAA is on by default for edge smoothing.

**Memory reference**: MEMORY.md documents the bug as
`project_taa_jitter_trembling_bug.md` (2026-04-01), attributed to commit
`24c4d801` (decals).

---

### `r.TSR` — `PARTIAL`
**File**: [runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/tsr_upscaler.cpp)

Temporal Super Resolution (UE5-style) is implemented with 3 passes
(dilate-velocity, reject-shading, update-history), but known to produce
artifacts. Not recommended over DLSS/FSR2 for now. Needs a tuning pass
once TAA is fixed (TSR and TAA share the temporal infrastructure).

---

### `r.DLSS.Quality` — `PARTIAL`
**Files**: [dlss_upscaler.cpp](../../runtime/engine/renderer/passes/postprocess/dlss_upscaler.cpp),
`extension/native` (NGX wrapper)
**Status**: upscale path works; Ray Reconstruction + DLSS-G (frame generation)
are NOT integrated.

Base DLSS 2/3 upscale via NGX ("NVSDK_NGX_D3D12_CreateFeature") is functional
after the 2026-03-25 fix (raw vtable access + exposure default 10.0). Remaining gaps:
- **Ray Reconstruction (DLSS 3.5)** not integrated
- **DLSS-G / DLSS Multi Frame Gen** not integrated
- **Motion vector coverage**: some passes don't feed motion vectors, so
  DLSS occasionally ghost-smears — same root as the TAA bug
- **No runtime fallback**: if NGX fails, the pass silently returns low-res

---

### `r.FSR2` — `OFF`
**File**: [fsr2_upscaler.cpp:76](../../runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp)

FSR 2.x temporal upscaler is scaffolded but never enabled by default. Build
path compiles, integration untested at runtime.

### `r.FSR` — `OFF`
**File**: [fsr_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/fsr_pass.cpp)

FSR 1.0 spatial upscaler (EASU + RCAS). Works but not wired into any
default pipeline. Useful for old hardware; low priority.

---

### `r.SMAA` — `OFF`
**File**: [smaa_pass.cpp:33](../../runtime/engine/renderer/passes/postprocess/smaa_pass.cpp)

SMAA (edge + blend + resolve) shaders exist and compile, but the pass is
off by default. Needs validation vs. FXAA (which is the current AA path)
and a quality comparison.

---

## Global Illumination

### `r.SSGI` — `OFF` (BUG)
**File**: [ssgi_renderer.cpp:22](../../runtime/engine/renderer/gi/ssgi_renderer.cpp)
**Shaders**: [shaders/runtime/ssgi/*.hlsl](../../runtime/shaders/runtime/ssgi/)

SSGI was scaffolded (trace, spatial denoise, temporal accum, upsample) but
never functionally validated. The `ssgi_trace_cs.hlsl` has a hardcoded
`expectedLinearDepth = lerp(linearRayDepth, linearRayDepth + 5000.0 * t, t)`
with no physical basis — 5km depth assumption in a cm-unit engine. Result:
screen-space rays never register hits on architectural-scale scenes,
`ssgi_ao` writes constant 1.0, `ssgi_result` stays black. Thickness CVar
bumped to 50 cm in the recent session but that's a band-aid.

**Recommendation**: rewrite the march with proper view-space hit-distance
accumulation, or replace entirely with Brixelizer GI which works.

---

### `r.BrixelizerGI` — `OFF` → `ON (via scene CVars)`
**File**: [brixelizer_gi_pass.cpp:50](../../runtime/engine/renderer/passes/gi/brixelizer_gi_pass.cpp)

AMD FidelityFX Brixelizer GI (sparse SDF voxel GI + screen-space probes).
Implementation is **functional** — produces diffuse GI visible in Sponza.
Current gaps:
- **Specular GI output** produces values that saturate the pass-dump
  visualization (HDR magnitude > 10, Reinhard clamps to white). Unclear
  if the magnitudes are physically correct or if the shader is scaling
  them wrong.
- **Cascade tuning** uses the default base voxel size 2.0 cm × 4 cascades
  which works for Sponza but hasn't been profiled on larger scenes.
- **LOD-aware re-registration** uses a simple delete-all + re-create-all
  per-frame approach. Works for static scenes but is suboptimal for
  dynamic geometry.

---

### `r.RadianceCache` — `OFF` (PARTIAL)
**File**: [radiance_cache.cpp:24](../../runtime/engine/renderer/gi/radiance_cache.cpp)
**Shader**: [radiance_cache_trace_cs.hlsl](../../runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl)

SH probe grid + SDF cone trace GI (16³ probes). Implemented but disabled
because Brixelizer GI supersedes it. Left in the codebase because the
probe grid is reused by the `IrradianceProbeGrid` (LightProbeVolume) path.

---

### DDGI (`DDGISystem`) — `STUB` (INFRA ONLY)
**File**: [ddgi_system.cpp](../../runtime/engine/renderer/gi/ddgi_system.cpp)

DDGI infrastructure: octahedral irradiance + depth atlases, probe grid
layout, bindless SRV/UAV slots. NO probe update shader, NO sampling path
in deferred lighting. The atlases are created and never written. Needs:
- Probe update compute shader (see `ddgi_probe_update_cs.hlsl` — exists
  but not dispatched)
- DXR ray update path (`r.RayTracing.GI=1` tries to enable but TLAS is
  also not integrated — see RT section)
- Chebyshev visibility sampling in `deferred_lighting_ps.hlsl`

---

### `GlobalSDF` — `PARTIAL`
**File**: [global_distance_field.cpp](../../runtime/engine/renderer/gi/global_distance_field.cpp)
**Shader**: [global_sdf_composite_cs.hlsl](../../runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl)

Global distance field composite exists for DFS cone tracing. Works with
individual mesh SDFs baked offline by `mesh_sdf_baker.cpp`. Brixelizer
has its own SDF path so this is partially redundant now.

---

### `IrradianceProbeGrid` / `LightProbeVolume` — `PARTIAL`
**File**: [irradiance_probe_grid.cpp](../../runtime/engine/renderer/gi/irradiance_probe_grid.cpp)

ECS component + probe grid added in commit `1892a567` (Phase 0 tasks 2.1/2.2).
Infrastructure is in place but there's no probe bake pipeline yet. Probes
are spawned as ECS entities and the grid is uploaded to the GPU, but the
irradiance data is zero until a bake step (which doesn't exist).

---

## Shadows

### `r.DFS` (Distance Field Shadows) — `ON`
**File**: [frame_lighting.cpp:52](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/shadows/dfs_*.hlsl](../../runtime/shaders/runtime/shadows/)

Cone-traced distance field shadows. Works on large-scale scenes but can
produce splotchy noise on cm-scale interiors (low sample count, no temporal
accumulation). Currently ON by default; if you see splotchy AO-like noise
inside buildings, this is the first thing to disable.

### Shadow atlas depth dump — `STUB`
The pass-dump code skips depth textures other than the GBuffer depth. The
shadow atlas is D32Float and would need a depth → R8 copy path to be
dumpable. Not critical; shadow atlas can be inspected via RenderDoc.

---

## Reflections

### `r.SSR` — `OFF` (BUG)
**File**: [frame_lighting.cpp:44](../../runtime/engine/renderer/frame/frame_lighting.cpp)
**Shaders**: [shaders/runtime/postprocess/ssr_*.hlsl](../../runtime/shaders/runtime/postprocess/)

Full SSR pipeline exists: tile classify → trace → spatial denoise → temporal
accum → resolve → composite. **Produces visible noise** that multiple tuning
sessions could NOT resolve. The resolved-history pass at one point wrote
zero output. Off by default pending a ground-up rewrite based on the
`diharaw/hybrid-rendering` reference (hit-point reprojection, curvature-adaptive
strategy, tile-based dispatch).

**Recommendation**: wait for RT reflections (inline ray query against TLAS)
to replace SSR entirely for the default path. Keep SSR as SM5 / low-end
fallback.

---

### Reflection Probes — `PARTIAL`
**File**: [reflection_probe_renderer.cpp](../../runtime/engine/renderer/probes/reflection_probe_renderer.cpp)

`ReflectionProbeRenderer` gathers `ReflectionProbeComponent` entities from
ECS, manages dirty capture via `IBLBaker`, uploads a structured buffer of
`ReflectionProbeGPU`. The **renderer side is functional** — probes are
captured and bound — but:
- No parallax correction in the deferred lighting shader
- Probe selection is single-nearest, not blended
- No priority/weight field on `ReflectionProbeComponent`

---

## Raytracing (DXR)

### `r.RayTracing` — `OFF` (SCAFFOLDING)
**Files**:
- [rt_manager.cpp](../../runtime/engine/renderer/raytracing/rt_manager.cpp)
- [blas_manager.cpp](../../runtime/engine/renderer/raytracing/blas_manager.cpp)
- [tlas_manager.cpp](../../runtime/engine/renderer/raytracing/tlas_manager.cpp)
- [shader_table_builder.cpp](../../runtime/engine/renderer/raytracing/shader_table_builder.cpp)
- [passes/raytracing/rt_uv_test_pass.cpp](../../runtime/engine/renderer/passes/raytracing/rt_uv_test_pass.cpp)

Infrastructure exists:
- `RTManager` singleton with per-frame TLAS rebuild
- `BLASManager` builds per-mesh on first draw, cached
- `ShaderTableBuilder` for hit groups / miss shaders
- Only one RT shader: `rt_uv_test_lib.hlsl` — a barycentric UV visualizer

**Sub-CVars declared but not wired:**
- `r.RayTracing.Shadows`
- `r.RayTracing.Reflections`
- `r.RayTracing.AO`
- `r.RayTracing.GI`

Each would need: a ray gen shader, hit/miss shaders, dispatch integration
into the render graph, denoiser, composite into deferred lighting.

**Path forward**: the `diharaw/hybrid-rendering` paper is the reference
architecture — start with `RTShadows` (1 visibility ray/pixel vs sun
direction → SVGF-style tile-based denoise → multiply into direct sun term),
then `RTAO`, then `RTReflections`, then `DDGI` update rays.

---

## Path Tracing

### `r.PathTracer.Enable` — `OFF` (WORKING, PARTIAL FEATURES)
**Files**:
- [passes/pathtracing/path_tracer_pass.cpp](../../runtime/engine/renderer/passes/pathtracing/path_tracer_pass.cpp)
- [shaders/runtime/pathtracing/path_tracer_ps.hlsl](../../runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl)

Shadertoy-style compute path tracer as a runtime mode toggle. Steps 1-6
working (skeleton → GBuffer first hit → 1-bounce diffuse → direct sun +
multi-sample → GGX specular → progressive accumulation with camera-cut
reset). Full design in
[docs/analysis/path-tracing/](./path-tracing/).

**Known gaps**:
- **Step 7** (ECS analytic primitives for true multi-bounce against arbitrary
  geometry) deferred — currently all bounces escape to sky
- **Step 7 hybrid** (screen-space sun shadow) was implemented then removed
  because it was view-dependent (camera-relative shadow artifacts)
- **Step 8** (Falcor PSNR parity) only has a stub script at
  `scripts/falcor/uzengine_pt_reference.py`; Mogwai execution + scene
  export to `.pyscene` not done
- **No TAA interaction safety** — user must manually `r.TAA=0` when
  enabling PT (both do temporal accumulation)
- **No DLSS/FSR/TSR safety** — must manually disable upscalers
- **CB ring stale-buffer race** (tracked separately in MEMORY.md) can cause
  intermittent artifacts in accumulation

---

## Atmosphere / Sky / Fog

### `r.AerialPerspective` — `OFF` (PARTIAL)
**File**: [frame_constants.cpp:76](../../runtime/engine/renderer/frame/frame_constants.cpp)

3D aerial perspective volume (Hillaire 2020) is computed each frame via
`AerialPerspectiveVolume` compute pipeline, but the **deferred lighting
shader integration is gated off** by this CVar. When enabled, the fog
color blending is incorrect — suspect transmittance LUT UV mapping or
view-space Z reconstruction bug. Needs a side-by-side comparison vs. the
Hillaire reference shader.

---

### `r.ScatteringLut` — `OFF` (EXPERIMENTAL)
**File**: [atmosphere_renderer.cpp:37](../../runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp)

Bruneton 2017 3D scattering LUT sky path. Shaders were stubs and got
deleted in the recent session; the C++ pipeline factories were also
removed. The CVar is still declared for compat but the code path is
effectively dead — sky uses the Hillaire 2020 transmittance LUT +
analytic ray march.

---

### Volumetric Clouds — `PARTIAL`
**Files**:
- [cloud_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp)
- Shaders deleted in recent session: `cloud_noise_shape_cs`, `cloud_noise_detail_cs`, `cloud_weather_cs`

Cloud raymarch compute works (see existing screenshots), but **the noise
volume bakers were stub shaders that got deleted**. The compiled `.bin`
noise assets under `content/atmosphere/` still exist and the renderer
reads them, so the runtime cloud pass works — but regenerating the noise
volumes requires rewriting the 3 baker shaders or baking externally.

Additional gaps:
- No temporal reprojection — clouds flicker
- No cheap LOD (always full quality)
- No cloud shadowing onto the ground
- No sun godray integration from clouds

User comment: "ainda precisa de vários ajustes, não é prioridade agora".

---

### Volumetric Fog — `PARTIAL`
**Files**:
- [volumetric_fog_pass.cpp](../../runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.cpp)
- Inject / scatter compute shaders exist

Froxel volumetric fog (160×90×64 inscattering volume + temporal integration).
Builds and runs but **not composited into the final HDR** by default — the
integrated volume is computed and left unread. Needs the composite step
wired into `frame_atmosphere_passes.cpp` after height fog.

---

### `r.Fog` (Height Fog) — `ON`
Exponential height fog, Hillaire 2016 unified volumetric. Works correctly.
No known bugs. Listed here for completeness.

---

## Post-Process

### `r.GTAO` — `OFF` (WEAK EFFECT / SCENE-DEPENDENT)
**File**: [gtao_pass.cpp:24](../../runtime/engine/renderer/passes/postprocess/gtao_pass.cpp)

GTAO pipeline (horizon-based AO + bilateral denoise + temporal accumulation
+ half-res upsample) is functional and dispatches correctly, but the
visual contribution is marginal in well-lit scenes because the directional
sun light + sky light dominate. Previously was disabled due to an
over-occlusion bug (MEMORY.md `project_ambient_light_zero_investigation`).

**Current state (post-2026-04-07 overnight session)**: the over-occlusion
bug hasn't been confirmed fixed, but enabling GTAO with intensity 0.5 /
radius 60 in Sponza produces visible crevice contact shadows. Needs
testing in more scenes, and PIXEL_DEBUG pass-dump validation that the
AO channel matches expected values.

---

### `r.DOF` — `OFF` (PARTIAL)
**File**: [frame_postprocess.cpp:27](../../runtime/engine/renderer/frame/frame_postprocess.cpp)
**Shaders**: `dof_setup_cs.hlsl`, `dof_blur_cs.hlsl`

Depth of field setup + blur compute shaders exist. Off by default, uses
fixed lens parameters, no bokeh kernel sampling, no near-field handling.
Works as a cheap gaussian blur based on circle-of-confusion — acceptable
for background blur but nothing fancy.

---

### `r.LensFlare` — `OFF`
**File**: [lens_flare_pass.cpp:30](../../runtime/engine/renderer/passes/postprocess/lens_flare_pass.cpp)
**Shader**: `lensflare_cs.hlsl`

Ghost-based lens flare compute pass. Builds, runs, visible artifacts around
bright light sources. Off by default because the ghost positions and
chromatic aberration are un-tuned.

---

### `r.MotionBlur` — `ON`
Object-space motion blur (tile max / neighbor max / reconstruction). Works
correctly. Listed for completeness.

---

### Distortion (Heat haze / refraction) — `STUB` (UNWIRED)
**Files**:
- [distortion_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_pass.cpp)
- [distortion_resolve_pass.cpp](../../runtime/engine/renderer/passes/vfx/distortion_resolve_pass.cpp)
- Shaders: `distortion_ps.hlsl`, `distortion_haze_ps.hlsl`, `distortion_resolve_ps.hlsl`

Pass classes and shaders are all implemented, but **there is no wiring
in `frame_postprocess.cpp` or `frame_particles.cpp` that calls
`AddDistortionAccumulationPass` or `AddDistortionResolvePass`**. Scene VFX
entities that flag themselves as distortion emitters will NOT render any
refraction until the pass is added to the frame graph.

---

## Materials

### Named pins + MaterialParams — `DONE`
Recent session added UE5/Godot-style named pin serialization (`dstPin`,
`srcPin` as strings) and `MaterialParams` (baseColorTint, roughnessMul, etc).
Translator emits explicit defaults for unconnected pins. Documented in
commit `eb839ed9`.

### `specularMultiplier` — `PARTIAL`
`MaterialParams::specularMultiplier` is declared and serialized but NOT
consumed by the HLSL translator yet (stub `(void)mp.specularMultiplier`).
Needs either an extra out parameter on `ComputeMaterialOutputs()` (requires
changing the gbuffer RT layout to carry per-pixel F0 scale) or baking it
into the baseColor at authoring time for dielectrics.

### HDRI `.hdr` / `.exr` loading — `DONE`
PMREM atlas + raw equirect paths both work. No known bugs.

---

## Subsurface Scattering / Hair / ClearCoat — `STUB`
Shading models `Subsurface`, `ClearCoat`, `Hair`, `ThinTranslucent` are
declared in `MaterialShadingModel` enum and space is reserved in the
GBuffer RT2.w `customW` channel, but:
- No specialized BRDF for any of them in `deferred_lighting_ps.hlsl`
- No authoring support in the material graph compiler
- No scene-side validation

---

## Skeletal Animation — `PARTIAL`
**Files**: [engine/animation/](../../runtime/engine/animation/)

`Skeleton` struct, `BoneTransform`, `.uzskel` file I/O all exist. What's
missing:
- Animation clip / `.uzanim` loading
- State machine / blend tree
- GPU skinning compute shader (currently no bone palette upload)
- Animation-driven morph targets
- IK solvers (FABRIK, CCD)

Listed in the Phase 3 task list but never implemented. ECS has no
`SkeletalMeshComponent` yet.

---

## VFX / Particles

### Particle system — `PARTIAL` (MOSTLY FUNCTIONAL)
Spawn/update/render compute pipeline works. Mesh particle rendering
works. Trails work. Known gaps:
- **Collision** via SDF exists but only tested against Brixelizer global SDF
- **Vector fields / curl noise** helpers exist but no integration test
- **Ribbons / trails** interpolation has a minor popping artifact at
  segment transitions
- **GPU sort** works for alpha-blended particles but no frustum cull yet

### Decals — `DONE`
Deferred decal system (DBuffer) works. Decal material graph integration
works. No known issues.

---

## Lua Scripting

### Base API — `DONE`
LuaJIT integration, sandboxing, hot-reload, `uz.scene` / `uz.mesh` /
`uz.camera` / `uz.material` / `uz.light` APIs all work.

### `uz.pathtracer` — `STUB`
Listed in the path tracer milestone plan but the Lua binding file
`runtime/engine/scripting/api/lua_pathtracer.cpp` does not exist yet.
The C++ side is accessible only via CVars.

### Coroutines / DAP server / LSP IntelliSense — `NOT STARTED`
Planned in Phase 1 specs, no implementation.

---

## RHI Backends

### DX12 — `PRIMARY`
Fully functional, bindless SM6.6+, DXR support detected. All development
happens here.

### DX11 — `PARTIAL`
Files in `runtime/engine/rhi/dx11/` compile but path hasn't been validated
recently. Bindless not supported (SM5 fallback paths exist in shaders).
Used occasionally for backend parity testing; breaks regularly.

### Vulkan — `PARTIAL`
13 files in `runtime/engine/rhi/vulkan/`. SPIRV-Cross wrapper for HLSL →
SPIRV transpilation is set up. Never booted end-to-end. Listed as a
future target in the architecture docs.

### Metal — `STUB`
`metal_device.mm`, `metal_command_list.cpp`, etc. exist but are
Windows-built stubs (no macOS CI). Assume broken until validated on Mac.

---

## Other scaffolded-but-unused

### `Job System` — `STUB`
`engine/job/` directory exists. No current consumers. Renderer is 2-thread
(game thread + render thread) with no task-based parallelism.

### `Async streaming` — `STUB`
Spec exists in `docs/specs/`, no implementation.

### `Undo/Redo` — `NOT STARTED`
### `Editor gizmos` — `NOT STARTED` (beyond the base grid + outline)
### `Multi-select` — `NOT STARTED`
### `Prefab edit mode` — `NOT STARTED`
### `Navmesh` — `NOT STARTED`
### `Plugin system` — `NOT STARTED`
### `Terrain system` — `NOT STARTED`
### `Audio (XAudio2)` — `NOT STARTED` (spec only)
### `Physics (Jolt)` — `NOT STARTED` (spec only)

All of these have specs in `docs/specs/` and tasks in `.rulebook/tasks/`
but no code yet.

---

## Build / Tooling

### Runtime leaks — FIXED (2026-04-07)
`PT_Accum0` / `PT_Accum1` textures leaked at shutdown, triggering D3D12MA
assert. Fixed in commit `cb9f4028`. If similar "Unfreed committed allocations"
errors appear for other textures, grep for their creation site and ensure
`DestroyTexture` is called in the matching shutdown path.

### Shader cache — `STABLE`
DXC-based pipeline cache at `runtime/build/bin/cache/shaders/`. Clearing
it (`rm -rf`) forces full recompile on next boot — useful when shader
includes change without touching the top-level file.

### PSO disk cache — `STABLE`
Pipeline state objects are serialized to disk for fast boot on subsequent
runs.

### Pass-dump — `IMPROVED (2026-04-07)`
Recent session added `DumpChannel` for split views (gbuffer.ao, metallic,
roughness from packed RTs) and enabled gbuffer depth dump via a sqrt curve.
Volfog inscattering/integrated + Brixelizer diffuse/specular outputs are
now registered. Shadow atlas depth still skipped.

---

## Follow-up priority (personal recommendation)

1. **Fix TAA** — unblocks DLSS ghost-fix, TSR tuning, and path tracer
   accumulation coordination. Highest cross-system impact.
2. **RT Shadows via inline ray query** — `diharaw/hybrid-rendering` pattern.
   Replaces the view-dependent contact shadow march. Most visible quality
   upgrade.
3. **Rewrite SSGI march** — or formally retire it in favor of Brixelizer GI.
4. **Wire distortion passes** into frame_postprocess — zero new code,
   just graph registration. Unlocks heat haze / refraction VFX.
5. **GTAO validation + AO-map authoring path** — for when assets actually
   ship with baked AO.
6. **Skeletal animation Phase 1** — GPU skinning compute + clip playback.
   Unblocks characters in the engine.
7. **Path tracer Step 7 ECS primitives** — analytic Cornell-style test
   scene, serves as BRDF validation reference against Falcor.
8. **DLSS Ray Reconstruction + DLSS-G** — once TAA / motion vectors are
   fixed.
9. **Backend parity** — validate DX11 / Vulkan builds compile + boot.
10. **Audio + Physics** — large unblocked deliverables for the next phase.
