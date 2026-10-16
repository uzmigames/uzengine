<!-- doc-meta -->
title: Shader Design - HLSL Path Tracer Compute
date: 2026-04-07
status: approved
<!-- end-doc-meta -->

# 3. Shader Design

> Implementation note: the shipped tracer uses DXR-inline pixel shaders, not this compute/analytic design; [08 reconciliation](08-implementation-reconciliation.md) is authoritative.

Reference algorithm: Shadertoy-style progressive Monte Carlo path tracer. One
compute thread per pixel, analytic scene, progressive accumulation across frames.

Files to create under `runtime/shaders/runtime/pathtracing/`:

| File | Purpose |
|------|---------|
| `path_tracer_cs.hlsl` | Main trace compute. Entry point `main`. |
| `path_tracer_resolve_cs.hlsl` | Accumulation blend + copy to HDR scene color. Entry point `main`. |
| `include/pt_common.hlsli` | RNG, ray, material, hit structs shared by both. |
| `include/pt_scene.hlsli` | Analytic scene (spheres + quads) for Steps 1-6. |
| `include/pt_brdf.hlsli` | Thin wrappers over existing `material/brdf.hlsli` for cosine + GGX sampling. |

## 3.1 Entry point & thread group

```hlsl
// @ref Kajiya 1986 "The Rendering Equation"
// @ref Jensen 2001 "Realistic Image Synthesis Using Photon Mapping"
[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) { ... }
```

Dispatch: `((width+7)/8, (height+7)/8, 1)`. Entry point **must** be `main` per
`.claude/rules/shader-entry-point.md`.

## 3.2 RNG — Wang hash (matches reference exactly)

```hlsl
/// @ref Wang 1997 "Integer Hash Function"
uint WangHash(inout uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float Rand01(inout uint s)      { return float(WangHash(s)) / 4294967296.0; }
float3 RandUnitVector(inout uint s) {
    float z = Rand01(s) * 2.0 - 1.0;
    float a = Rand01(s) * TWO_PI;
    float r = sqrt(1.0 - z*z);
    return float3(r*cos(a), r*sin(a), z);
}
```

Seed per pixel: `rngState = pixel.x*1973 + pixel.y*9277 + frameIndex*26699 | 1u`.
This matches the reference verbatim and is cheap enough for 1 SPP/frame.

For higher quality later we can swap to PCG (`@ref O'Neill 2014`) without
changing the call sites.

## 3.3 Ray / hit / material structs

```hlsl
struct PtRay       { float3 o; float3 d; };
struct PtMaterial {
    float3 albedo;
    float3 emissive;
    float  percentSpecular;  // probability of specular lobe
    float  roughness;        // [0,1], squared before use
    float3 specularColor;    // tinted F0 (metals use albedo)
};
struct PtHit {
    float    dist;
    float3   normal;
    PtMaterial mat;
};
```

## 3.4 Scene intersection (Steps 1-6: analytic)

Ported from reference with UzEngine naming. Both functions live in
`pt_scene.hlsli`.

- `bool IntersectSphere(PtRay r, float4 sph, inout PtHit h)` —
  quadratic formula, inside/outside aware, `@ref Shirley "Ray Tracing in One Weekend" Ch. 6`.
- `bool IntersectQuad(PtRay r, float3 a, float3 b, float3 c, float3 d, inout PtHit h)` —
  scalar-triple-product test, `@ref Ericson 2005 "Real-Time Collision Detection" §5.4.5`.

`TraceScene(ray, hit)` walks a hardcoded list of quads (Cornell box walls +
emissive ceiling) and spheres. At Step 7+ this becomes a structured buffer
filled from a `PathTracerScene` component (no DXR).

## 3.5 BRDF sampling — reuse existing includes

`pt_brdf.hlsli` wraps `runtime/shaders/runtime/material/brdf.hlsli` and
`common/sampling.hlsli`:

- Diffuse dir: `normalize(n + RandUnitVector(rng))` — cosine-weighted hemisphere,
  `@ref Shirley 1992 "Nonuniform Random Point Sets via Warping"`.
- Specular dir (smooth): `reflect(rayDir, n)`.
- Glossy lerp: `normalize(lerp(specDir, diffuseDir, rough*rough))` — matches
  reference; `rough*rough` is the Disney convention, `@ref Burley 2012
  "Physically-Based Shading at Disney" §5.4`.
- Lobe selection: `doSpec = Rand01(rng) < mat.percentSpecular ? 1 : 0`.

At Step 5 we replace the "glossy lerp hack" with true GGX VNDF sampling
(`@ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals"`) using
`D_GGX` / `G_SmithGGXCorrelated` / `F_Schlick` from the existing
`material/brdf.hlsli`.

## 3.6 Bounce loop (matches reference semantics)

```hlsl
float3 L = 0;
float3 throughput = 1;
for (int b = 0; b <= maxBounces; ++b) {
    PtHit h; h.dist = PT_FAR;
    TraceScene(ray, h);
    if (h.dist >= PT_FAR) {
        L += throughput * SampleEnvironment(ray.d);
        break;
    }
    ray.o = ray.o + ray.d * h.dist + h.normal * PT_NORMAL_NUDGE;

    float doSpec = (Rand01(rng) < h.mat.percentSpecular) ? 1.0 : 0.0;
    float3 dDir = normalize(h.normal + RandUnitVector(rng));
    float3 sDir = normalize(lerp(reflect(ray.d, h.normal), dDir,
                                 h.mat.roughness * h.mat.roughness));
    ray.d = lerp(dDir, sDir, doSpec);

    L += throughput * h.mat.emissive;
    throughput *= lerp(h.mat.albedo, h.mat.specularColor, doSpec);

    // Russian roulette — @ref Arvo & Kirk 1990 "Particle Transport"
    float p = max(throughput.r, max(throughput.g, throughput.b));
    if (Rand01(rng) > p) break;
    throughput /= p;
}
return L;
```

Constants (match reference):
- `PT_FAR = 10000.0`
- `PT_MIN_T = 0.01`
- `PT_NORMAL_NUDGE = 0.01`

## 3.7 Camera ray generation

Uses view/proj inverses from the per-frame CB (`FrameConstants`, see 02):

```hlsl
float2 uv = (pixel + jitter) / resolution;  // jitter is Rand01*2-1 subpixel
float4 ndc = float4(uv * 2 - 1, 1, 1);      // reversed-Z: near=1
ndc.y = -ndc.y;
float4 vp  = mul(gInvProj, ndc);  vp /= vp.w;
float3 wd  = normalize(mul((float3x3)gInvView, vp.xyz));
float3 wo  = gCamPos;
```

Jitter per sample gives free antialiasing since samples accumulate over time.

## 3.8 Accumulation (resolve pass)

`path_tracer_resolve_cs.hlsl` runs after trace:

```hlsl
float4 curr = uTraceResult[pix];          // RGB = L, A = unused
float4 hist = uAccumHistory[pix];         // RGB = running mean, A = sample count
if (gResetAccum || hist.a == 0) {
    uAccumOut[pix] = float4(curr.rgb, 1);
} else {
    float n = hist.a + 1;
    uAccumOut[pix] = float4(lerp(hist.rgb, curr.rgb, 1.0/n), n);
}
// write to HDR scene color (linear, NO tonemap — existing tonemap pass handles it)
uSceneColor[pix] = float4(uAccumOut[pix].rgb, 1);
```

We intentionally do NOT apply ACES/sRGB in the path tracer — the existing
tonemap/bloom/exposure stack downstream of `hdrSceneColor` does that. This
differs from the shadertoy reference (which has a dedicated tonemap buffer)
and is the single biggest integration win.

Reset triggers:
- Camera view matrix changed (any component) frame-to-frame
- Resolution changed
- CVar `r.PathTracer.ResetAccum` executed
- First frame after `r.PathTracer.Enable` toggled on

## 3.9 Environment

Step 1-6: uniform sky color from CVar, or `lerp(horizon, zenith, saturate(d.y))`.
Step 8+: sample the existing sky cubemap SRV via bindless, multiply by
`r.PathTracer.EnvIntensity`. Matches reference `c_skyboxBrightnessMultiplier`.

## 3.10 Mandatory citations (per RULE 3)

Every shader function gets a `/// @ref` header. Papers already cited above:
Kajiya 1986, Jensen 2001, Wang 1997, Shirley 1992, Burley 2012, Heitz 2018,
Walter 2007 (GGX), Schlick 1994, Arvo & Kirk 1990, Ericson 2005.

---

Next: See 04-incremental-plan.md
