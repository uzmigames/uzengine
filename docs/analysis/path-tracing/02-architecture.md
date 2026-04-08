<!-- doc-meta -->
title: Architecture - Runtime Toggle, Replaces GI/Lighting/Shadows
date: 2026-04-07
status: draft
<!-- end-doc-meta -->

# 2. Architecture

## 2.1 Core principle

Path tracer is a **runtime mode toggle inside the existing renderer**. It is
NOT a separate demo, NOT a separate executable, NOT a separate pipeline.

When `r.PathTracer.Enable=1`:

- GBuffer pass still runs (we reuse depth, normal, material IDs for the first
  hit — free primary-ray acceleration)
- Shadow passes are **skipped** (path tracer computes visibility via bounce)
- GI passes (Brixelizer/DDGI/SSGI/IrradianceProbeGrid) are **skipped**
- Deferred lighting pass is **skipped**
- `PathTracerPass` runs in their place and writes the final lit HDR into
  `hdrSceneColor`
- Bloom, exposure, tonemap, AA — **all unchanged**, consume `hdrSceneColor`
  the same way they do in the rasterized path

The toggle flips at the `SceneRenderer::Render` dispatch level: one `if`
picks the lighting/GI chain vs. the path tracer pass.

## 2.2 Passes disabled when PT is on

From grep of `runtime/engine/renderer/frame/`:
- `frame_lighting.cpp` / `frame_lighting_pass.cpp` — deferred lighting
- `frame_gi_passes.cpp` — Brixelizer, DDGI, probe grids, radiance cache
- `frame_shadows.cpp` — CSM, point/spot shadow atlas, DFS
- `frame_atmosphere_passes.cpp` — keep running (sky cube feeds PT environment)

The pass runner checks `cv_PtEnable` at the top and early-outs for each of
the above. Zero new skip flags — we gate at the registration point in
`scene_renderer_init.cpp` so disabled passes don't even allocate transient
resources.

## 2.3 New file layout

```
runtime/engine/renderer/passes/pathtracing/
    path_tracer_pass.h
    path_tracer_pass.cpp

runtime/shaders/runtime/pathtracing/
    path_tracer_cs.hlsl           // entry: main — trace
    path_tracer_resolve_cs.hlsl   // entry: main — accumulate + write scene color
    include/
        pt_common.hlsli           // rng, structs, constants
        pt_scene.hlsli            // analytic primitives iteration
        pt_brdf.hlsli             // wraps material/brdf.hlsli for PT sampling
        pt_material.hlsli         // PBRMaterial -> PT material mapping

runtime/engine/scripting/api/
    lua_pathtracer.cpp            // uz.pathtracer.* bindings

runtime/engine/ecs/pathtracing/
    pt_primitive_component.h      // analytic spheres/quads/lights (Step 7+)
```

## 2.4 PathTracerPass class

```cpp
class PathTracerPass {
public:
    bool Init(IRHIDevice*, ShaderManager*, uint32_t w, uint32_t h);
    void Resize(uint32_t w, uint32_t h);
    void Execute(IRHICommandList*, CBRingAllocator*,
                 const FrameConstants&,
                 RhiTextureHandle hdrSceneColor,
                 RhiTextureHandle gbufferDepth,     // for first-hit reuse
                 RhiTextureHandle gbufferNormal,
                 RhiTextureHandle gbufferMaterial);
    void ResetAccumulation();
    void Shutdown();

    bool IsEnabled() const;           // reads cv_PtEnable
    uint32_t SamplesAccumulated() const { return m_samplesAccum; }

private:
    // owned GPU resources
    RhiTextureHandle m_accum[2];      // ping-pong RGBA32F, A = sample count
    RhiTextureHandle m_traceOut;      // RGBA16F per-frame trace result
    uint32_t         m_historyIdx = 0;
    uint32_t         m_samplesAccum = 0;
    Mat4             m_prevView;      // for camera-cut detection
    bool             m_historyValid = false;

    // pipelines
    RhiPipelineHandle m_tracePso;
    RhiPipelineHandle m_resolvePso;

    // non-owning
    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;
    uint32_t       m_width = 0, m_height = 0;
};
```

Owned by `SceneRenderer` alongside the other frame-scoped passes. Lifetime
matches the renderer.

## 2.5 Constant buffer

New `PathTracerConstants` defined in
`runtime/engine/renderer/core/params/constant_buffers.h` (same pattern as
`GtaoConstants`):

```cpp
struct PathTracerConstants {
    float4x4 invView;
    float4x4 invProj;
    float3   camPos;      uint frameIndex;
    uint2    resolution;  uint maxBounces;    uint samplesPerFrame;
    uint     rrEnabled;   uint rrMinBounce;   uint resetAccum;  uint jitterAA;
    float    envIntensity; float fireflyClamp; float2 _pad;
};
// 16 DWORDs of scalar + 2 matrices = 144 bytes, 16-byte aligned
```

Uploaded through the existing `CBRingAllocator`. (See R4 in 07 for the
known ring-aliasing issue and mitigation.)

## 2.6 Root signature

Single root signature shared by both compute shaders:

| Slot | Type | Binding |
|------|------|---------|
| b0 | CBV | `PathTracerConstants` |
| t0 | SRV (Tex2D) | gbuffer depth (first hit) |
| t1 | SRV (Tex2D) | gbuffer normal |
| t2 | SRV (Tex2D) | gbuffer material |
| t3 | SRV (TexCube) | sky cubemap (environment) |
| t4 | SRV (StructuredBuffer) | scene primitives (Step 7+) |
| t5 | SRV (StructuredBuffer) | scene materials |
| t6 | SRV (Tex2D) | accumulation history (ping-pong read) |
| u0 | UAV (Tex2D) | trace output |
| u1 | UAV (Tex2D) | accumulation write |
| u2 | UAV (Tex2D) | hdrSceneColor (resolve pass only) |
| s0 | static sampler | linear wrap for env cube |

Bindless heap is available but not required — path tracer bindings are
small and static, direct bindings are clearer.

## 2.7 Frame graph insertion

In `SceneRenderer::Render` (see `runtime/engine/renderer/scene/scene_render_frame.cpp`):

```
[depth prepass]
[gbuffer pass]
[velocity pass]
if (cv_PtEnable->value >= 0.5f) {
    // SKIP: shadow passes, gi passes, deferred lighting
    m_pathTracerPass->Execute(cmd, cbRing, frameConstants,
                              hdrSceneColor, gbDepth, gbNormal, gbMaterial);
} else {
    [shadow passes]
    [gi passes]
    [deferred lighting → hdrSceneColor]
}
[atmosphere passes]   // still runs, composites sky into hdrSceneColor
[bloom]
[auto exposure]
[tonemap]
[AA]
[ui/overlay]
```

The skip is done inside `scene_renderer_init.cpp` at pass registration
time, so no transient memory is reserved for disabled passes.

## 2.8 Camera-cut detection

`PathTracerPass::Execute` compares `frameConstants.view` against
`m_prevView`. Any diff above a tiny epsilon invalidates history and
resets `m_samplesAccum = 0`. Also resets on:

- Resolution change (`Resize` called)
- `cv_PtEnable` toggled on this frame
- `cv_PtResetAccum` exec triggered
- `PathTracerConstants::resetAccum` CPU flag set for one frame

## 2.9 History texture lifetime

Two RGBA32F textures at render resolution. Ping-pong:

```
Frame N:     read m_accum[historyIdx^1], write m_accum[historyIdx]
Frame N+1:   historyIdx ^= 1, swap roles
```

RGBA32F is required — we accumulate sums in the RGB and sample count in A.
RGBA16F loses precision after ~200 samples. Cost: `w*h*16` bytes per
texture, so 1080p ≈ 32 MiB each, 64 MiB total. Acceptable for a
non-default toggle.

## 2.10 Interaction with atmosphere / sky

Sky atmosphere passes continue to run — they write the sky cubemap used by
the path tracer as the environment light (`t3` binding). When a primary ray
misses all geometry, it samples this cube. When a bounced ray escapes, it
also samples this cube × `envIntensity`. This keeps time-of-day / sun
direction / sky color fully functional in path-traced mode.

## 2.11 Lua binding surface

```
uz.pathtracer.enable(bool)
uz.pathtracer.is_enabled() -> bool
uz.pathtracer.set_max_bounces(int)
uz.pathtracer.set_samples_per_frame(int)
uz.pathtracer.reset_accum()
uz.pathtracer.samples_accumulated() -> int
```

Per RULE / `.claude/rules/lua-parity.md` — every C++ feature ships a Lua
binding.

---

Next: See 03-shader-design.md
