<!-- doc-meta
title: Three.js Screen-Space Reflections — Complete Technical Analysis
subsystem: rendering
tags: ssr, screen-space-reflections, ray-marching, three.js, reference
status: reference
created: 2026-04-05
-->

# Three.js Screen-Space Reflections — Complete Technical Analysis

Reference implementation analysis of `SSRShader.js`, `SSRPass.js`, and `SSRNode.js`
from the three.js repository (`mrdoob/three.js`, dev branch, April 2026).

Upstream reference cited in three.js source:
> [3D Game Shaders For Beginners, Screen Space Reflection](https://lettier.github.io/3d-game-shaders-for-beginners/screen-space-reflection.html)

---

## 1. Architecture Overview

Three.js provides two parallel SSR implementations:

| Implementation | File | Renderer | API Style |
|----------------|------|----------|-----------|
| `SSRPass` | `examples/jsm/postprocessing/SSRPass.js` | WebGL (`WebGLRenderer`) | Class, `EffectComposer` pass |
| `SSRNode` | `examples/jsm/tsl/display/SSRNode.js` | WebGPU (`WebGPURenderer`) | TSL node graph |
| `SSRShader` | `examples/jsm/shaders/SSRShader.js` | WebGL | Raw GLSL, consumed by `SSRPass` |

Both implementations share the same algorithm — the WebGPU version (`SSRNode`) is a
direct TSL translation of the WebGL GLSL shader. The analysis below covers both
simultaneously, noting where they diverge.

---

## 2. Tracing Algorithm

### 2.1 Algorithm Family: Screen-Space DDA (Digital Differential Analyzer)

Three.js SSR uses **screen-space linear marching** — also called DDA or rasterized ray
marching. This is NOT hierarchical depth tracing (Hi-Z), NOT binary search (used only
in community libs like `0beqz/screen-space-reflections`), and NOT cone tracing.

The key insight: the ray is parameterized entirely in **screen (pixel) space**, not in
view or world space. This makes step size constant in pixels but variable in world units
depending on depth and camera projection.

### 2.2 Step Count Computation

```glsl
// SSRShader.js fragment shader (verbatim)
float totalStep = max(abs(xLen), abs(yLen));
float xSpan = xLen / totalStep;
float ySpan = yLen / totalStep;
float sStep = 1.0 / totalStep;
```

Where:
- `xLen = d1.x - d0.x` — pixel-space horizontal distance from start to end of ray
- `yLen = d1.y - d0.y` — pixel-space vertical distance
- `d0 = gl_FragCoord.xy` — current pixel in screen pixels
- `d1 = viewPositionToXY(d1viewPosition)` — projected endpoint of reflected ray

`totalStep` is the Chebyshev (L-infinity) distance in pixels — equivalent to taking
one pixel step along the dominant axis. This ensures the ray visits every pixel along
the longest dimension of the ray's screen-space projection.

The shader constant `MAX_STEP` caps the loop:
```glsl
for(float i = 1.; i < float(MAX_STEP); i++) {
    if(i >= totalStep) break;
    ...
}
```

`MAX_STEP` is set by the CPU at material creation time:
```js
// SSRPass.js
MAX_STEP: Math.sqrt(this.width * this.width + this.height * this.height)
```

At 1920×1080 this evaluates to `Math.sqrt(1920² + 1080²) ≈ 2203`. At 512×512 it is
`Math.sqrt(512² + 512²) ≈ 724`. The value is the maximum possible pixel-space ray
length (screen diagonal), which bounds the worst-case iteration count.

**Performance implication**: `MAX_STEP` is a compile-time `#define`. Changing it
requires a shader recompile (`material.needsUpdate = true`).

### 2.3 Ray Endpoint Computation

```glsl
// max ray length scaled by incidence angle
float maxReflectRayLen = maxDistance / dot(-viewIncidentDir, viewNormal);

// endpoint in view space
vec3 d1viewPosition = viewPosition + viewReflectDir * maxReflectRayLen;

// clamp to near plane to prevent going behind camera
if(d1viewPosition.z > -cameraNear) {
    float t = (-cameraNear - viewPosition.z) / viewReflectDir.z;
    d1viewPosition = viewPosition + viewReflectDir * t;
}

// project to screen pixels
d1 = viewPositionToXY(d1viewPosition);
```

The near-plane clamp is critical — without it, rays pointing toward the camera produce
wildly out-of-range screen coordinates and the loop degenerates.

---

## 3. Depth Comparison and Intersection Test

### 3.1 Perspective-Correct Ray Interpolation

The parametric position along the ray in view-space Z is computed with
perspective-correct interpolation (NOT linear in screen space):

```glsl
// Perspective camera (SSRShader.js, verbatim)
// Reference: https://comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf
float recipVPZ = 1. / viewPosition.z;
float viewReflectRayZ = 1. / (recipVPZ + s * (1./d1viewPosition.z - recipVPZ));

// Orthographic camera
float viewReflectRayZ = viewPosition.z + s * (d1viewPosition.z - viewPosition.z);
```

`s` is the parametric progress along the ray: `s = i * sStep = i / totalStep`.

The perspective formula is the projective interpolation `1/(1/z0 + t*(1/z1 - 1/z0))`
which is linear in clip space, not in view space — required to avoid the incorrect
reflections that naive linear Z interpolation would produce.

In `SSRNode.js` (WebGPU), `s` is computed differently — using length instead of step
fraction:
```js
// SSRNode.js
const s = xy.sub(d0).length().div(totalLen);
```

This is mathematically equivalent for uniform marching but slightly more expensive.

### 3.2 Primary Depth Test

```glsl
if(viewReflectRayZ <= vZ) {
    // potential intersection — test thickness
}
```

Where `vZ = getViewZ(depth)` is the negative view-space depth (Z is negative into
screen for perspective cameras). The ray intersects geometry when the reflected ray's
reconstructed Z goes behind (more negative) than the scene surface Z.

Note the original comment in SSRShader.js:
```glsl
// if(viewReflectRayZ > vZ) continue; // will cause hang
// https://github.com/mrdoob/three.js/pull/21539#issuecomment-821061164
```

The early-continue form can produce an infinite loop on some drivers; the early-out
form (`<=` entering the intersection block) is the safe pattern.

### 3.3 Thickness Test (Finite-Volume Hit Detection)

After the primary depth test passes, three.js uses a **3D point-to-line distance
test** to validate the hit:

```glsl
// pointToLineDistance: perpendicular distance from point vP to the line (viewPosition → d1viewPosition)
// https://mathworld.wolfram.com/Point-LineDistance3-Dimensional.html
float pointToLineDistance(vec3 x0, vec3 x1, vec3 x2) {
    return length(cross(x0 - x1, x0 - x2)) / length(x2 - x1);
}

float away = pointToLineDistance(vP, viewPosition, d1viewPosition);
```

`vP` is the reconstructed view-space position of the scene surface at the current
screen pixel. The test checks whether the surface point is within a cylinder of radius
`tk` around the reflection ray.

The thickness `tk` is dynamically clamped against a neighbor-derived minimum:

```glsl
// Compute adaptive minimum thickness from neighboring pixel
vec2 xyNeighbor = xy;
xyNeighbor.x += 1.0;   // one pixel to the right
vec2 uvNeighbor = xyNeighbor / resolution;
vec3 vPNeighbor = getViewPosition(uvNeighbor, d, cW);
float minThickness = vPNeighbor.x - vP.x;  // view-space x delta per pixel
minThickness *= 3.0;   // expand 3× to absorb numerical error

float tk = max(minThickness, thickness);
hit = away <= tk;
```

**Rationale**: The per-pixel world-space footprint grows with depth. Using a fixed
`thickness` at far distances misses valid hits. The adaptive `minThickness` scales with
the world-space size of one screen pixel at that depth.

The `INFINITE_THICK` define removes all this and immediately marks any `vZ <= ray` as
a hit — useful for debugging but produces obviously wrong results on thin geometry.

### 3.4 Backface Rejection

```glsl
vec3 vN = getViewNormal(uv);
if(dot(viewReflectDir, vN) >= 0.0) break;
```

If the reflected ray is heading in roughly the same direction as the surface normal at
the hit point (i.e., the hit surface is facing away), the ray is terminated. This
prevents the shader from "reflecting" off the back of objects.

Note: this uses `break` not `continue` — once a backface is hit, the loop stops
entirely rather than continuing to search. This is conservative but fast.

---

## 4. Attenuation and Output

### 4.1 Distance Attenuation (`#ifdef DISTANCE_ATTENUATION`)

```glsl
float distance = pointPlaneDistance(vP, viewPosition, viewNormal);
// pointPlaneDistance: signed distance from vP to the plane through viewPosition with normal viewNormal

float ratio = 1.0 - (distance / maxDistance);
float attenuation = ratio * ratio;   // quadratic falloff
op = opacity * attenuation;
```

Falloff is quadratic (squared ratio), producing a smooth fade as reflections approach
`maxDistance`. Default `maxDistance = 180` (Three.js world units).

### 4.2 Fresnel (`#ifdef FRESNEL`)

```glsl
float fresnelCoe = (dot(viewIncidentDir, viewReflectDir) + 1.0) / 2.0;
op *= fresnelCoe;
```

This is **not** a physically correct Schlick Fresnel. It is a cheap approximation:
`dot(incident, reflect)` ranges from `-1` (grazing) to `1` (normal incidence), shifted
and scaled to `[0, 1]`. Physically, Fresnel should be maximum at grazing angles, not
minimum — the formula is therefore **inverted** relative to real Fresnel behavior.
Three.js's formula gives:
- `fresnelCoe → 1.0` when `dot ≈ 1` (flat incidence, near-normal view) — maximum reflection
- `fresnelCoe → 0.0` when `dot ≈ -1` (grazing) — no reflection

This is the opposite of Schlick: real surfaces reflect more at grazing angles, not
less. Three.js's "Fresnel" effectively fades reflections at grazing, which actually
reduces edge-bleeding artifacts but is not physically based.

### 4.3 Final Color Output

```glsl
vec4 reflectColor = texture2D(tDiffuse, uv);   // beauty pass color at hit UV
gl_FragColor.xyz = reflectColor.xyz;
gl_FragColor.a = op;
```

The SSR target stores `(reflected_color, opacity)` as a pre-multiplied-style RGBA.
Blending with the beauty pass uses standard `SrcAlpha + OneMinusSrcAlpha`.

---

## 5. Render Pass Pipeline (SSRPass.js)

Full execution order per frame:

```
1. renderer.render(scene, camera)  →  beautyRenderTarget (HalfFloat RGBA + depth)
2. _renderOverride(normalMaterial) →  normalRenderTarget (HalfFloat RGBA, MeshNormalMaterial)
3. [if selective] _renderMetalness →  metalnessRenderTarget (white=selected, black=not)
4. _renderPass(ssrMaterial)        →  ssrRenderTarget
5. [if blur] _renderPass(blur1)    →  blurRenderTarget
6. [if blur] _renderPass(blur2)    →  blurRenderTarget2
7. copyMaterial (NoBlending)       →  writeBuffer  (beauty)
8. copyMaterial (NormalBlending)   →  writeBuffer  (SSR composite)
```

### 5.1 Render Target Specifications

| Target | Format | Filter | Size |
|--------|--------|--------|------|
| `beautyRenderTarget` | HalfFloat | Nearest | full resolution |
| `normalRenderTarget` | HalfFloat | Nearest | full resolution |
| `metalnessRenderTarget` | HalfFloat | Nearest | full resolution |
| `ssrRenderTarget` | default | Nearest | `effectiveWidth × effectiveHeight` |
| `blurRenderTarget` | default | Nearest | effective |
| `blurRenderTarget2` | default | Nearest | effective |
| `prevRenderTarget` | default | Nearest | effective (bouncing only) |

Beauty, normal, and metalness targets always render at full resolution. Only the SSR
computation and blur targets scale with `resolutionScale`. This means GBuffer reads
in the ray marching shader always sample at full precision.

### 5.2 Resolution Scaling

```js
// SSRPass.js setSize()
const effectiveWidth  = Math.round(this.resolutionScale * width);
const effectiveHeight = Math.round(this.resolutionScale * height);

// MAX_STEP recalculated for scaled resolution
this.ssrMaterial.defines.MAX_STEP =
    Math.sqrt(effectiveWidth * effectiveWidth + effectiveHeight * effectiveHeight);
this.ssrMaterial.needsUpdate = true;  // triggers recompile
```

`resolutionScale = 0.5` cuts SSR resolution in half while keeping GBuffer at full
resolution. This halves fragment shader invocations for the SSR pass and reduces
blur texture reads, with minimal quality impact on the GBuffer sampling.

Added in PR #31629 (2025), closing issue #31624.

---

## 6. Blur / Denoising

### 6.1 SSRBlurShader: 5-tap Cross Filter (Alpha-Weighted)

```glsl
// SSRBlurShader.js fragment shader (verbatim)
float coeCenter = 0.2;
float coeSide   = 0.2;

float a = c.a*coeCenter + cl.a*coeSide + cr.a*coeSide + cb.a*coeSide + ct.a*coeSide;
vec3 rgb = (c.rgb*c.a*coeCenter + cl.rgb*cl.a*coeSide + cr.rgb*cr.a*coeSide
          + cb.rgb*cb.a*coeSide + ct.rgb*ct.a*coeSide) / a;
gl_FragColor = vec4(rgb, a);
```

Taps: center + 4 cardinal neighbors (left, right, bottom, top) at 1-texel offset.
All five coefficients are equal (`0.2`), making this a **uniform box filter** in
alpha-weighted space. The alpha-weighting prevents blurring of reflection data across
regions with no reflection hit (zero alpha), which would cause color bleeding.

This filter is applied **twice** in sequence (`blurMaterial` → `blurMaterial2`),
giving a 2-pass cross blur equivalent to a separable box blur approximation.

The comment in the source: `// reverse engineering from PhotoShop blur filter, then
change coefficient` — the original intent was Gaussian-like but was simplified to equal
weights.

### 6.2 No Temporal Filtering in SSRPass

The base `SSRPass` has **no temporal accumulation**. The `bouncing` feature (enabled
via `SSRPass.bouncing = true`) feeds the previous frame's composed result back into
`tDiffuse`, enabling recursive multi-bounce reflections but not true TAA denoising.

There is **no jitter** in three.js's built-in SSR.

### 6.3 SSRNode Roughness-Based Blurring (WebGPU Only)

The `SSRNode` adds roughness-aware blur via mipmapped render targets:

```js
// SSRNode.js constructor
this._blurRenderTarget = new RenderTarget(1, 1, {
    type: HalfFloatType,
    minFilter: LinearMipmapLinearFilter,  // trilinear
    magFilter: LinearFilter,
    mipmaps: [{}, {}, {}, {}, {}]  // 5 mip levels pre-allocated
});

// Per-mip blur with increasing spread
for(let i = 0; i < blurRenderTarget.texture.mipmaps.length; i++) {
    _quadMesh.material = (i === 0) ? this._copyMaterial : this._blurMaterial;
    this._blurSpread.value = i;
    renderer.setRenderTarget(blurRenderTarget, 0, i);
    _quadMesh.render(renderer);
}

// Read LOD based on roughness²
const lod = r.mul(r).mul(mips).clamp(0, mips);
blurredTextureNode = passTexture(this, this._blurRenderTarget.texture).level(lod);
```

Blur kernel: `boxBlur(reflectionBuffer, { size: blurQuality, separation: blurSpread })`.

This gives approximate rough reflections by reading a blurred mip corresponding to
`roughness²` — a common approximation for GGX lobe spread. Only available in the WebGPU
(`SSRNode`) path; the legacy `SSRPass` treats all surfaces as mirror-like.

### 6.4 SSRNode Quality Parameter

```js
// SSRNode.js — quality scales the totalStep iteration count
const totalStep = int(max(abs(xLen), abs(yLen)).mul(this.quality.clamp())).toConst();
```

`quality` is `[0, 1]`, default `0.5`. At `quality = 0.5`, the shader takes half the
maximum possible steps. This is a direct linear scaling of the iteration budget.

In `SSRPass`, there is no equivalent — it always marches at full `totalStep` (up to
`MAX_STEP = screen diagonal`).

---

## 7. Jitter

**Three.js's built-in SSR has no stochastic jitter.** There is no Halton sequence,
blue noise, or random offset applied to ray origin or direction.

Community solutions:
- `0beqz/screen-space-reflections` (archived, deprecated June 2023): adds `jitter`
  uniform (default 0) and `jitterRoughness` (default 0) applied to the ray direction,
  combined with temporal reprojection at `blend = 0.9`.
- `0beqz/realism-effects` (successor): uses TRAA (Temporal Reprojection AA) as the
  denoising stage.

---

## 8. Roughness Handling

### SSRPass (WebGL): No Roughness Support

The legacy `SSRPass` has no roughness input. All surfaces reflect as perfect mirrors.
The only roughness-adjacent control is `opacity` (0–1) which globally scales all
reflection contributions.

The `SELECTIVE` define with the metalness mask (`tMetalness`) allows excluding
non-metallic surfaces, but this is binary (metalness == 0 skips entirely).

### SSRNode (WebGPU): Roughness via Blur LOD

As described in section 6.3, `SSRNode` accepts an optional `roughnessNode`. When
provided, reflections are sampled from a mip level proportional to `roughness²`:

```
roughness = 0.0  →  lod = 0  (base mip, sharp)
roughness = 0.5  →  lod = 1  (1 blur level)
roughness = 1.0  →  lod = 4  (most blurred mip)
```

This approximates GGX specular lobe spread with a pre-filtered mip chain, similar to
the specular IBL split-sum approach. However, unlike true specular IBL, the blur kernel
is a box filter, not a GGX-weighted importance sample.

The opacity in `SSRNode` is additionally multiplied by metalness:
```js
const op = this.opacity.mul(metalness);
```

---

## 9. Screen-Edge Fade

Three.js SSR has **no explicit screen-edge fade**. The loop simply breaks when a ray
goes out of bounds:

```glsl
if(xy.x < 0. || xy.x > resolution.x || xy.y < 0. || xy.y > resolution.y) break;
```

This produces a hard cutoff at screen edges with no fade. The reflection abruptly
disappears at the screen border, which is a known visual artifact.

Community solutions (realism-effects, kode80) add an edge fade by computing distance
to the screen boundary and multiplying opacity by a falloff factor. Three.js's built-in
implementation does not include this.

---

## 10. Depth Buffer Requirements

```glsl
// SSRShader.js: early out on skybox/background
if(-viewZ >= cameraFar) return;
```

The shader reads a `DepthTexture` created with:
```js
const depthTexture = new DepthTexture();
depthTexture.type = UnsignedShortType;
depthTexture.minFilter = NearestFilter;
depthTexture.magFilter = NearestFilter;
```

`UnsignedShortType` means 16-bit depth precision. `NearestFilter` is mandatory —
bilinear interpolation of depth would produce incorrect view-space Z reconstructions.

Normals are packed via `unpackRGBToNormal` from a `MeshNormalMaterial` render, which
outputs world-space normals in `[0,1]` range encoded as RGB.

---

## 11. Selective Reflections (`#ifdef SELECTIVE`)

```glsl
#ifdef SELECTIVE
    float metalness = texture2D(tMetalness, vUv).r;
    if(metalness == 0.) return;
#endif
```

When `selects` is a non-null array of `Object3D`, the pass renders the scene twice:
once with white material (selected objects) and once with black material (others) into
`metalnessRenderTarget`. The SSR shader samples this mask and skips fragments not in
the selection. This is the only per-object SSR control available in `SSRPass`.

---

## 12. Known Issues and Artifacts

### 12.1 No Temporal Denoising → Visible Noise

The built-in SSR has no temporal accumulation. Under camera motion, surfaces with
moderate-to-high roughness show frame-to-frame noise. Mitigation: enable `blur = true`
(two-pass cross filter), but this only reduces spatial noise, not temporal flickering.

### 12.2 Screen-Space Occlusion Leaking

Objects occluded from the camera cannot be reflected. Any reflector angled toward an
occluded region produces no reflection or falls back to missing data, typically showing
black holes in the reflection.

### 12.3 Hard Screen Edge Cutoff

As noted in section 9, reflections terminate with a hard boundary at screen edges.
This is particularly visible on horizontal reflective surfaces (floors) near the
bottom of the screen.

### 12.4 Incompatibility with SSAO Pass

Reported on three.js forum: `SSRPass` and `SSAOPass` in the same `EffectComposer`
chain produce incorrect results because both write to the compositor in overlapping
ways. The GBuffer targets must be shared carefully.

### 12.5 `thickness` Tuning Sensitivity

The adaptive thickness (`minThickness * 3 + thickness`) is scene-scale-dependent.
The default `thickness = 0.018` works for scenes at ~unit scale but fails completely
at large or small scales — causing either no hits (too small) or thick-slab artifacts
(too large). Users must manually tune per-scene.

### 12.6 No Anti-Aliasing Integration

`SSRPass` does not integrate with FXAA or SMAA. TAA would require jitter, which is
absent. Running any AA pass after SSR compositing blurs both the scene and the
reflections uniformly, which reduces aliasing but also softens reflection edges
inappropriately.

### 12.7 Perspective Interpolation Hang Bug (Fixed)

PR #21539 documented that `if(viewReflectRayZ > vZ) continue;` (the intuitive early-
continue form) causes loop hangs on some GPU drivers due to divergent branch behavior
in certain loop patterns. The fix was to invert the condition to `if(viewReflectRayZ
<= vZ)` entering the body. The original broken form was left as a comment.

### 12.8 Reflector Depth Mismatch (Fixed)

Commit `07e58027` (`SSRPass: Correct Reflector depth`): when a `ReflectorForSSRPass`
is present, depth comparison produced incorrect results because the reflector's depth
was not in the same space as the scene depth. Fixed by toggling reflector visibility
during the beauty pass render.

---

## 13. All Parameters — Complete Reference

### SSRPass Constructor Options

| Parameter | Default | Type | Description |
|-----------|---------|------|-------------|
| `renderer` | required | `WebGLRenderer` | Renderer instance |
| `scene` | required | `Scene` | Scene to reflect |
| `camera` | required | `Camera` | Scene camera |
| `width` | `512` | `number` | Effect width in pixels |
| `height` | `512` | `number` | Effect height in pixels |
| `selects` | `null` | `Object3D[]` or `null` | Objects to selectively reflect |
| `bouncing` | `false` | `boolean` | Multi-bounce (prev frame feedback) |
| `groundReflector` | `null` | `ReflectorForSSRPass` | Ground plane reflector |

### SSRPass Runtime Properties

| Property | Default | GLSL Define / Uniform |
|----------|---------|-----------------------|
| `opacity` | `0.5` | `uniform float opacity` |
| `maxDistance` | `180` | `uniform float maxDistance` |
| `thickness` | `0.018` | `uniform float thickness` |
| `blur` | `true` | Enables 2-pass cross filter |
| `distanceAttenuation` | `true` | `#define DISTANCE_ATTENUATION` |
| `fresnel` | `true` | `#define FRESNEL` |
| `infiniteThick` | `false` | `#define INFINITE_THICK` |
| `selective` | `false` | `#define SELECTIVE` |
| `resolutionScale` | `1` | `[0,1]`, scales SSR target size |
| `output` | `0` | Output mode enum |

### SSRShader Defines

| Define | Default | Effect |
|--------|---------|--------|
| `MAX_STEP` | `sqrt(w²+h²)` | Loop upper bound (compile-time) |
| `PERSPECTIVE_CAMERA` | `true` | Enables perspective depth conversion + ray interpolation |
| `DISTANCE_ATTENUATION` | `true` | Quadratic `(1 - dist/maxDist)²` falloff |
| `FRESNEL` | `true` | `(dot(incident, reflect) + 1) / 2` opacity scale |
| `INFINITE_THICK` | `false` | Skip thickness test, treat all `vZ <= ray` as hit |
| `SELECTIVE` | `false` | Sample metalness mask, skip zero-metalness fragments |

### SSRNode Parameters (WebGPU)

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `maxDistance` | `1.0` | uniform float | Max ray distance (world units) |
| `thickness` | `0.1` | uniform float | Intersection thickness |
| `opacity` | `1.0` | uniform float | Global reflection opacity |
| `quality` | `0.5` | `[0,1]` | Fraction of max steps to execute |
| `blurQuality` | `2` | int `[1,3]` | Blur kernel size |
| `resolutionScale` | `1` | `[0,1]` | Effect resolution fraction |
| `roughnessNode` | `null` | optional | Enables blur-LOD roughness approximation |

---

## 14. Community SSR Implementations

### 14.1 `0beqz/screen-space-reflections` (Archived)

- **Status**: Deprecated June 2023. Incompatible with three.js > r151.
- **Algorithm**: Linear ray march (same as three.js) + binary search refinement
- **Steps**: `steps = 20` (initial march), `refineSteps = 5` (binary search)
- **Denoising**: Temporal reprojection with `blend = 0.9`, box blur, velocity buffer
- **Jitter**: `jitter` and `jitterRoughness` uniforms (default 0)
- **Roughness**: `maxRoughness` threshold, `roughnessFade` blend weight
- **Fresnel**: Schlick-like via `ior = 1.45`
- **Max depth tolerance**: `maxDepthDifference = 10` (post-refinement)
- **Successor**: `0beqz/realism-effects`

### 14.2 `0beqz/realism-effects`

- Active development. Integrates SSR with SSGI, TRAA, motion blur.
- SSR code credited to "Epsilon Engine" (imanolfotia.com/blog/1) and kode80 (edge fade).
- Uses TRAA as temporal denoiser — jitter provided by TRAA pass, not SSR itself.
- No detailed public parameter documentation; examine source in `/src`.

---

## 15. Comparison: Three.js vs Production SSR

| Feature | Three.js SSRPass | Three.js SSRNode | UzEngine target |
|---------|-----------------|-----------------|-----------------|
| Algorithm | Screen-space DDA | Screen-space DDA | Hi-Z + linear fallback |
| Step count | `sqrt(w²+h²)` max | `quality * max` | configurable |
| Depth format | 16-bit UShort | full precision | 32-bit float |
| Thickness | adaptive + fixed | adaptive + fixed | adaptive |
| Jitter | none | none | Halton / blue noise |
| Temporal | none (bouncing only) | none | TAA accumulation |
| Roughness | none | blur-LOD (`roughness²`) | GGX-convolved mip |
| Fresnel | inverted approx | inverted approx | Schlick |
| Edge fade | none (hard cutoff) | none | smooth cosine fade |
| Blur | 2-pass 5-tap cross | boxBlur + mips | bilateral / SVGF |
| Resolution scale | yes (SSR targets only) | yes (all targets) | yes |
| Denoising | spatial blur only | spatial + roughness mip | temporal SVGF |

---

## 16. Key Constants Summary

| Constant | Value | Location |
|----------|-------|----------|
| Default opacity | `0.5` | `SSRShader.uniforms.opacity.value` |
| Default maxDistance | `180` | `SSRShader.uniforms.maxDistance.value` |
| Default thickness | `0.018` | `SSRShader.uniforms.thickness.value` |
| Thickness neighbor multiplier | `3.0` | `minThickness *= 3.` in SSRShader.js |
| Distance attenuation power | `2.0` (quadratic) | `ratio * ratio` |
| Blur center coefficient | `0.2` | `SSRBlurShader` |
| Blur side coefficient | `0.2` | `SSRBlurShader` (uniform, 4 taps) |
| Default resolution scale | `1.0` | `SSRPass._resolutionScale` |
| Bounce blend | `NormalBlending` | SSRPass output composite |
| MAX_STEP formula | `sqrt(w² + h²)` | `SSRPass.js` constructor + `setSize()` |
| SSRNode default quality | `0.5` | `SSRNode.quality = uniform(0.5)` |
| SSRNode default maxDistance | `1.0` | `SSRNode.maxDistance = uniform(1)` |
| SSRNode default thickness | `0.1` | `SSRNode.thickness = uniform(0.1)` |
| SSRNode blur mip count | `5` | `mipmaps: [{},{},{},{},{}]` |

---

## Sources

- [Three.js SSRShader.js source](https://github.com/mrdoob/three.js/blob/dev/examples/jsm/shaders/SSRShader.js)
- [Three.js SSRPass.js source](https://github.com/mrdoob/three.js/blob/dev/examples/jsm/postprocessing/SSRPass.js)
- [Three.js SSRNode.js source](https://github.com/mrdoob/three.js/blob/dev/examples/jsm/tsl/display/SSRNode.js)
- [SSRPass: Resolution Scale? Issue #31624](https://github.com/mrdoob/three.js/issues/31624)
- [Add SSRPass PR #20156](https://github.com/mrdoob/three.js/commit/05cc046c6a)
- [Perspective-correct interpolation tech report (Lowk)](https://comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf)
- [Point-Line Distance 3D (Wolfram MathWorld)](https://mathworld.wolfram.com/Point-LineDistance3-Dimensional.html)
- [3D Game Shaders For Beginners: SSR (lettier)](https://lettier.github.io/3d-game-shaders-for-beginners/screen-space-reflection.html)
- [screen-space-reflections by 0beqz (archived)](https://github.com/0beqz/screen-space-reflections)
- [realism-effects by 0beqz](https://github.com/0beqz/realism-effects)
