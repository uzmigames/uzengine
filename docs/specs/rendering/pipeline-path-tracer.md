# DXR pipeline path tracer

The opt-in DX12 path tracer uses `DispatchRays`, not the older fullscreen
inline-RayQuery path. It requires DXR and SM 6.6 bindless descriptors.

## Running

From the repository root, after `cd runtime && zig build -Doptimize=ReleaseFast`:

```sh
runtime/build/bin/uzengine.exe --project sample/projects/cornell_box.uzproject \
  --path-trace --pt-bounces 4 --frames 210 \
  --camera-pos 0,-700,250 --camera-look 0,100,250 \
  --screenshot cornell.png --screenshot-frames 200
```

`--path-trace` queues `r.RayTracing=1` and `r.PathTracer.Pipeline=1` before
renderer initialization and the pending CVar flush. Explicit CLI overrides
survive project defaults. `--pt-bounces N` sets `r.PathTracer.MaxBounces`.
Neither flag is required for ordinary raster rendering; both gates default off.
The Cornell camera override looks into the room instead of relying on the
shipped scene's camera orientation.

## Pipeline and resources

`PipelinePathTracerPass` creates a private RT pipeline state object and shader
binding table. The ray-generation, closest-hit and miss libraries compile as
`lib_6_6`. The SBT contains raygen, surface/environment miss, shadow miss and
the triangle hit group. The 56-byte surface payload returns radiance,
throughput, bounce state and next-ray origin/direction. Recursion depth is 2:
surface transport is iterative in raygen, with nested visibility rays from
closest-hit.

The private root signature binds 12 DWORDs at b0, the existing PerFrameCB prefix
at b1, the scene TLAS at t0 and radiance UAV at u0. Material/light tables and
history UAVs use bindless descriptors. It does not extend the shared graphics
root signature or PerFrameCB. The pass reuses RTManager's BLAS/TLAS and follows
its instance inclusion/order. Closest-hit uses `InstanceIndex()` for per-instance
materials, so differently colored walls sharing a cube retain their colors.
Indexed and packed vertex streams are supported. Missing material textures use
the scalar factors, including metallic and roughness.

The shared TLAS writer converts the engine row-vector matrix into DXR's
row-major 3x4 column-vector affine transform. That means transposing both the
rotation/scale basis and translation: the final column is `_41/_42/_43`.
Copying the first three engine rows loses translation and reverses rotation.
The DXR contract is documented by
[Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_raytracing_instance_desc).

## Transport and accumulation

Raygen unprojects the camera ray and applies Halton base-2/base-3 subpixel jitter
using the accumulation sample index. Every surface vertex evaluates emission
and next-event estimation: four sampled visibility rays per scene directional
light, averaged with weight 1/4. Directional light values represent integrated
source intensity; no MIS is applied to these lights. Diffuse cosine and GGX VNDF
directions form a BRDF mixture with its combined PDF. Misses sample the sky
environment. Russian roulette uses survival `clamp(max(throughput), 0.05, 1)`
and divides surviving throughput by that probability.

One path per pixel is added each rendered frame. Two persistent RGBA32F UAVs
store the running mean and Kahan correction. Resolve is fused into raygen;
the displayed mean is written to RGBA16F radiance and copied to HDR scene color
before ordinary postprocessing. Render-graph tracked imports preserve resource
state and UAV ordering.

History resets on exact unjittered view/projection changes, resize, scene dirty
signals, transforms, material/light/environment inputs, bounce/RR settings,
skipped frames, or explicit reset generation. The first new sample overwrites
history without reading it. Time and raster TAA jitter do not invalidate history.

## Controls

| CVar | Default | Pipeline behavior |
| --- | --- | --- |
| `r.RayTracing` | 0 | Shared BLAS/TLAS master gate |
| `r.PathTracer.Pipeline` | 0 | Select this RT pipeline |
| `r.PathTracer.MaxBounces` | 4 | Surface vertex budget, clamped to 1-16 |
| `r.PathTracer.RussianRoulette` | 1 | Enable roulette |
| `r.PathTracer.RussianRouletteMinBounce` | 2 | First roulette-eligible vertex |
| `r.PathTracer.Accumulate` | 1 | Keep history; 0 starts fresh every frame |
| `r.PathTracer.ResetAccum` | exec | Reset history on next frame |

`r.PathTracer.Enable` selects the separate legacy inline path. Its
`SamplesPerFrame`, `ClampFirefly` and `EnvIntensity` controls do not change this
pipeline's one-path-per-frame estimator. A finite bounce budget truncates
transport; the current implementation is not an independent ground-truth renderer.

## Verification

`npm run test:layouts` validates the private 48-byte root block, material/light
strides and PerFrameCB prefix. `PipelinePathTracerStage1/Stage2` gtests cover
layout, scalar material defaults, compensated accumulation/reset, roulette and
bounce limits. `TLASInstanceTransform` compares CPU engine point transforms
against the exact conversion used by the production TLAS writer, including
nonzero translation, rotation and nonuniform scale.

Hardware acceptance evidence and capture paths are recorded in
`.rulebook/tasks/phase1_falcor-path-tracer/tasks.md`. Cornell acceptance requires
separate translated walls and red/green indirect tint on white surfaces, with
a one-bounce control to distinguish indirect transport from material color.
Convergence deltas are not an independent FLIP accuracy score.
