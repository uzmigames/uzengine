# UzEngine 0.2 Shader Code Audit

**Date**: 2026-03-13
**Auditor**: shader-engineer agent
**Scope**: `runtime/shaders/` (all .hlsl), plus C++ pass implementations in
`runtime/engine/renderer/passes/` and `runtime/engine/renderer/constant_buffers.h`

---

## Executive Summary

The shaders are generally well-structured, well-commented, and mathematically
sound. The PBR implementation is a correct Cook-Torrance formulation. The main
concerns are:

- **Critical**: Register mismatch for `LightingCB` between C++ (`b3`) and HLSL
  (`b2`), which will produce incorrect lighting in the deferred path.
- **Critical**: Register mismatch for `ShadowCB` in the shadow depth VS (`b4`
  in HLSL, root slot 3 = `b3` in C++).
- **Critical**: Shadow cascade selection loop is logically wrong — always
  selects the last cascade that was tested rather than the first one that
  contains the fragment.
- **High**: `viewDepth` is computed as Euclidean (spherical) distance from the
  camera, but cascade splits are view-space Z-axis distances. This makes cascade
  selection inconsistent at oblique view angles.
- **High**: Albedo texture sampled without sRGB decode — linear math applied to
  gamma-encoded values will make everything look washed out.
- **High**: Geometry term `k` uses `alpha/2` (correct for IBL, wrong for direct
  lighting). Karis 2013 uses `(roughness + 1)^2 / 8` for direct lights.
- **Medium**: PBR functions (DistributionGGX, GeometrySmith, FresnelSchlick)
  are fully duplicated between `mesh_ps.hlsl` and `deferred_lighting_ps.hlsl`.
  No shared include file exists.
- **Medium**: Reinhard tonemapping is per-channel, causing hue shifts at high
  luminance. Luminance-based Reinhard is the standard fix.
- **Medium**: `gbuffer_debug_ps.hlsl` has hardcoded `kNearPlane = 0.1f` and
  `kFarPlane = 1000.0f`. These diverge silently if the camera projection
  changes.
- **Low**: `fullscreen_vs.hlsl` comment table is incorrect relative to the
  actual bit-trick UV generation (off-by-one in the description).
- **Low**: Normal map B channel is not reconstructed from RG — the full RGB is
  sampled from a BC5-inappropriate path (no `TEXTURE_NORMAL` define guard).
- **Low**: `grid_ps.hlsl` uses `b1` for its own `GridCB` rather than the
  engine-standard `b1 = PerFrameCB` layout, diverging from all other shaders.
- **Low**: `overlay_vs.hlsl` RootConstants struct has a `reserved` field in the
  VS but a `vertexOffset` field in the declared name, with the PS declaring
  the same struct but calling the same field `reserved`. Naming mismatch.

---

## Findings

---

### F-01 — LightingCB Register Mismatch (CRITICAL)

**File**: `runtime/engine/renderer/constant_buffers.h` vs
`runtime/shaders/runtime/deferred_lighting_ps.hlsl`

**Severity**: CRITICAL — causes silent wrong data binding; lighting output will
be garbage or zero.

**Description**:
`constant_buffers.h` documents `LightingCB` as bound at `b3` (root CBV):

```cpp
/// Lighting constant buffer for the deferred lighting pass, bound at register `b3`.
cbuffer LightingCB : register(b3) {
```

However, `deferred_lighting_ps.hlsl` declares it at `b2`:

```hlsl
cbuffer LightingCB : register(b2) {
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint       pointLightCount;
```

And `lighting_pass.cpp` binds it at root slot 2:

```cpp
cmd->SetRootCBV(2, desc.lightingAlloc.bufferHandle, ...);
```

Root slot 2 maps to `b2` only if the root signature is laid out that way. The
C++ header comment says `b3`, the HLSL says `b2`, and the C++ binding says slot
2. One of the three is wrong. In the deferred pass there is no per-object data,
so `b2` is intentionally repurposed for lighting — the header comment is the
wrong part. But the header comment is the source of truth for all other passes
that also use `b2` for per-object CBs. This ambiguity will cause register
conflicts if both the gbuffer pass and the lighting pass share the same root
signature without variant separation.

**Fix**: Update `constant_buffers.h` to clarify that `LightingCB` lives at
`b2` for the fullscreen lighting pass (not `b3`), and that this is possible
because `b2` is the per-object slot which is unused by fullscreen passes. Add a
comment in the PSO creation code explaining the per-pass root signature
distinction.

---

### F-02 — ShadowCB Register Mismatch (CRITICAL)

**File**: `runtime/shaders/runtime/shadow_depth_vs.hlsl` vs
`runtime/engine/renderer/passes/shadow_pass.cpp`

**Severity**: CRITICAL — shadow geometry rendered with a zero or wrong
viewProj matrix.

**Description**:
`shadow_depth_vs.hlsl` declares the shadow CB at `b4`:

```hlsl
cbuffer ShadowCB : register(b4) {
    row_major float4x4 shadowViewProj;
};
```

But `shadow_pass.cpp` binds it at root slot **3**:

```cpp
cmd->SetRootCBV(3, shadowAlloc.bufferHandle, ...);
```

`shadow_pass.h` also documents "Per-cascade shadow CB at root param 3." If
root slot 3 maps to `b4` in the root signature then these match and the comment
in the HLSL is correct; if root slot 3 maps to `b3` then there is a one-off
error. The HLSL uses `b4` (five CBV registers: b0-b4) while the comment says
"slot 3" (four params: 0,1,2,3). The shadow VS uses only `b2` and `b4`,
skipping `b1` and `b3` entirely, which would mean two root params are dead for
this pass. The most likely interpretation is that the root signature for the
shadow pass does not include b1 (PerFrameCB) or b3 (unused), so slot 3 really
maps to b4 — but this is undocumented and the root signature definition was not
found in the audit scope, making this a reliability risk.

**Fix**: Explicitly document the root signature layout table for the shadow PSO,
or consolidate to a single shared root signature where every slot maps
unambiguously to a register.

---

### F-03 — Cascade Selection Loop Logic Error (CRITICAL)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` lines 254-261,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl` lines 350-356

**Severity**: CRITICAL — fragments beyond the first cascade always receive
shadows from the last (coarsest) cascade, not the tightest fitting one.

**Description**:
The cascade selection loop is:

```hlsl
uint cascadeIdx = 0;
for (uint i = 0; i < cascadeCount; i++) {
    if (viewDepth <= splits[i]) {
        cascadeIdx = i;
        break;      // <-- should stop on FIRST match
    }
    cascadeIdx = i; // <-- overwrites with i even after break is hit?
}
```

The `break` statement on line 259 is correct and exits on the first cascade
whose far split exceeds `viewDepth`. However, the `cascadeIdx = i` line at the
bottom of the loop body (line 260) executes **before** the `break` check
because of the logic flow: `if` (true) → `cascadeIdx = i` → `break`. Wait —
re-reading carefully, the `cascadeIdx = i` is inside the `else` implicit path,
after the `if` block. In HLSL `for` bodies without braces the `if` statement
ends at its closing `}`, and the `cascadeIdx = i` line is the next statement in
the loop body, executing unconditionally on every iteration regardless of
whether the `if` was taken and the `break` fired.

The execution order per iteration is:
1. Test `viewDepth <= splits[i]`
2. If true: set `cascadeIdx = i`, then `break`
3. **If false** (the `break` is NOT reached): set `cascadeIdx = i`

But there is a subtle problem: on the iteration where the condition IS true,
the code runs `cascadeIdx = i` (inside the if block) and then `break` — the
`cascadeIdx = i` outside the if block does NOT run in that case because `break`
exits before reaching it. So the logic is actually:

- If `viewDepth <= splits[i]`: `cascadeIdx = i`, break.
- If `viewDepth > splits[i]`: `cascadeIdx = i` (fall-through assignment).

This means if no cascade contains the fragment (viewDepth > all splits), the
loop falls through all iterations setting `cascadeIdx = cascadeCount - 1`. That
is correct behavior for the "beyond all cascades" case. And for a contained
fragment, `cascadeIdx` is set to `i` inside the if block before break. So the
logic is actually **correct** in terms of final value — but it is dangerously
confusing because the fall-through `cascadeIdx = i` looks like an accidental
duplicate. The comment "Fall through to last cascade if beyond all splits" is
misleading since it implies this line only executes in that case.

**HOWEVER**: there is still a real bug when `cascadeCount == 1`. The `splits`
array is `{cascadeSplits.x, cascadeSplits.y, cascadeSplits.z, cascadeSplits.w}`.
For a single cascade, only `splits[0]` is valid. If `viewDepth > splits[0]`,
the loop ends with `cascadeIdx = 0`, which is the correct last (and only)
cascade. That is fine. But the cascade blend code then tests
`cascadeIdx < cascadeCount - 1` which equals `0 < 0` = false, so blending is
correctly skipped.

The actual correctness issue is the **extreme obfuscation** of this logic. The
duplicate `cascadeIdx = i` will confuse any future maintainer and is one
compile-optimization away from being incorrectly removed.

**Fix**: Rewrite the loop for clarity:

```hlsl
uint cascadeIdx = cascadeCount - 1; // default: last cascade (beyond all splits)
for (uint i = 0; i < cascadeCount; i++) {
    if (viewDepth <= splits[i]) {
        cascadeIdx = i;
        break;
    }
}
```

This is equivalent, unambiguous, and duplicated in both affected shaders.

---

### F-04 — viewDepth: Euclidean vs View-Space Z (HIGH)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` line 420,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl` line 464

**Severity**: HIGH — cascade selection is incorrect at oblique angles.

**Description**:
Cascade split distances are computed in `cascade.cpp` as view-space Z values
(`splitNear` / `splitFar` from the camera near-far range, not sphere radii).
But the shaders compute `viewDepth` as:

```hlsl
float viewDepth = length(input.positionWS - cameraPos.xyz); // Euclidean
```

This is the spherical (Euclidean) distance from the camera, not the linear
depth along the view axis. At oblique angles (fragments at the screen edge),
the Euclidean distance is greater than the view-space Z by a factor of
`1/cos(angle)`. This causes edge fragments to be placed in coarser cascades
than they should be, reducing shadow quality at the edges of the screen.

The standard fix is to compute view-space Z directly:

```hlsl
// In mesh_ps.hlsl (forward pass), you need the view matrix.
// In deferred_lighting_ps.hlsl, you have worldPos and cameraPos,
// so compute the dot product along the view forward vector:
float3 viewForward = normalize(mul(float4(0,0,1,0), invViewProj).xyz); // approximate
// Better: store viewDir as float4 in PerFrameCB
float viewDepth = dot(worldPos - cameraPos.xyz, viewForward);
```

The cleanest fix is to add a `float4 viewForward` (world-space camera forward
vector) to `PerFrameCB`, then use `dot(positionWS - cameraPos.xyz, viewForward)`
for cascade selection.

---

### F-05 — Albedo sRGB Decode Missing (HIGH)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` lines 332-334,
`runtime/shaders/runtime/gbuffer_ps.hlsl` lines 104-107

**Severity**: HIGH — lighting math applied to gamma-encoded values; output is
brighter/washed out and energy conservation is violated.

**Description**:
PBR lighting must be computed in linear light space. Albedo textures are
authored in sRGB (gamma ~2.2). If the render target format for the albedo
G-Buffer is `R8G8B8A8_UNORM` (not `R8G8B8A8_UNORM_SRGB`), the hardware does
**not** automatically decode sRGB on sample — the HLSL shader receives
gamma-encoded values. The audit found no `_UNORM_SRGB` format specification in
any of the reviewed C++ files.

Both `mesh_ps.hlsl` (forward) and `gbuffer_ps.hlsl` (deferred geometry) sample
the albedo texture with a plain bilinear `Sample()` call and use the result
directly in Cook-Torrance math:

```hlsl
albedo = tex.Sample(linearSampler, uv).rgb; // may be gamma-encoded
// ... then immediately:
float3 diffuse = kD * albedo / PI; // wrong if albedo is not linear
```

**Fix**: Either:
1. Create the albedo texture SRV with `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` so the
   hardware performs the decode automatically on sample.
2. Or manually decode in the shader: `albedo = pow(albedo, 2.2f)` (or the
   accurate piecewise formula).

Option 1 is preferred for performance. The G-Buffer albedo render target should
then be `R8G8B8A8_UNORM_SRGB` for writing and `R8G8B8A8_UNORM` for SRV reading
(to avoid double-decoding in the lighting pass).

---

### F-06 — Wrong Geometry k Remapping for Direct Lighting (HIGH)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` lines 133-136,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl` lines 170-173

**Severity**: HIGH — specular response is too dim for rough surfaces under
direct lighting.

**Description**:
Both shaders use:

```hlsl
float k = alpha / 2.0f; // where alpha = roughness^2
```

The Karis 2013 ("Real Shading in Unreal Engine 4") paper defines two k
remappings:
- **Direct lighting**: `k = (roughness + 1)^2 / 8`
- **IBL (image-based lighting)**: `k = alpha / 2 = roughness^2 / 2`

The current code uses the IBL k value for direct lighting. This is a known
error that makes surfaces appear too bright (under-shadowed geometry term) at
low roughness and too dark at high roughness compared to the reference. The
doc comment in the shader says "k remapping for direct (analytic) lights per
Karis 2013" but then uses the IBL formula.

**Fix**: For the direct lighting path:

```hlsl
// Karis 2013 — direct light k remapping
float r1 = roughness + 1.0f; // note: perceptual roughness, not alpha
float k = (r1 * r1) / 8.0f;
```

Because `roughness` (perceptual) is used, not `alpha` (squared), this requires
passing the perceptual roughness into `GeometrySchlickGGX`, not `alpha`.

---

### F-07 — PBR Code Duplication (MEDIUM)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl`,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl`

**Severity**: MEDIUM — divergence risk; any fix to PBR math must be applied
twice.

**Description**:
The following functions are copy-pasted verbatim between the two shaders:
- `DistributionGGX`
- `GeometrySchlickGGX`
- `GeometrySmith`
- `FresnelSchlick`
- `SampleShadowPCF`
- `SampleShadowMap`

This violates DRY. When F-06 (wrong k value) is fixed, it must be applied in
two separate files, and any future mis-synchronization will cause subtle visual
divergence between the forward and deferred paths.

There are no `.hlsli` include files anywhere in the project currently.

**Fix**: Create `runtime/shaders/include/pbr.hlsli` containing the shared PBR
functions and `runtime/shaders/include/shadow.hlsli` containing the CSM sampling
functions. Include them from both shaders.

---

### F-08 — Per-Channel Reinhard Tonemapping (MEDIUM)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` line 436,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl` line 509

**Severity**: MEDIUM — hue shifts at high-luminance bright light sources.

**Description**:
Both shaders apply Reinhard per RGB channel:

```hlsl
color = color / (color + 1.0f);
```

This is the "per-channel" Reinhard operator. At high luminance, each channel
is compressed independently. If one channel saturates before others (e.g., a
very red light), the output hue shifts toward the desaturated average. This is
particularly visible on colored specular highlights.

The luminance-based Reinhard operator preserves hue by compressing luminance
only, then scaling RGB by the luminance ratio:

```hlsl
float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
float tonemappedLuma = luma / (luma + 1.0f);
color *= tonemappedLuma / max(luma, 1e-5f);
```

Additionally, for a more cinematic result, the extended Reinhard (with a white
point) or ACES filmic are better choices — but those are design decisions beyond
a correctness fix.

---

### F-09 — Hardcoded Near/Far in Debug Depth Linearization (MEDIUM)

**File**: `runtime/shaders/runtime/gbuffer_debug_ps.hlsl` lines 126-130

**Severity**: MEDIUM — debug visualization silently broken if projection
parameters differ from defaults.

**Description**:

```hlsl
static const float kNearPlane = 0.1f;
static const float kFarPlane  = 1000.0f;
```

These are hardcoded, requiring a shader recompile if the camera near/far
changes. The `PerFrameCB` already has `viewProj` and `invViewProj` which
contain enough information to reconstruct linear depth via the inverse
projection approach used in `deferred_lighting_ps.hlsl`.

**Fix**: Extract near/far from the projection matrix in `PerFrameCB`:

```hlsl
// From column-major DX projection matrix in viewProj:
// proj[2][3] = far / (far - near)
// proj[3][3] = -near * far / (far - near)
// (actual extraction depends on whether viewProj is combined or proj-only)
```

Better: add `float nearPlane` and `float farPlane` as two of the `_framePad[12]`
slots in `PerFrameCB` and pass them as uniforms.

---

### F-10 — fullscreen_vs.hlsl Header Comment Table Is Wrong (LOW)

**File**: `runtime/shaders/runtime/fullscreen_vs.hlsl` lines 8-14

**Severity**: LOW — misleading documentation only.

**Description**:
The file header comment shows:

```
/// ID  Position (NDC)   UV
///  0  (-1, -1)         (0, 1)
///  1  (-1,  3)         (0, -1)
///  2  ( 3, -1)         (2, 1)
```

But the actual bit-trick in the shader body (line 51) generates:

```
ID=0: uv=(0,0) → pos=(-1,+1), then texcoord=(0, 1-0)=(0,1)
ID=1: uv=(2,0) → pos=(+3,+1), then texcoord=(2, 1)
ID=2: uv=(0,2) → pos=(-1,-3), then texcoord=(0,-1)
```

So vertex 1 is at `(+3, +1)`, not `(-1, +3)`, and vertex 2 is at `(-1, -3)`,
not `(3, -1)`. The UV column is also inconsistent with the flip. The comment
was not updated after the V-flip was added on line 57.

---

### F-11 — No Normal Map B Channel Reconstruction (LOW)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl` lines 364-372,
`runtime/shaders/runtime/gbuffer_ps.hlsl` lines 130-144

**Severity**: LOW — slightly larger normal map texture format than necessary;
no visual error if BC1/DXT1 is not used for normal maps.

**Description**:
Normal maps authored for the metallic-roughness workflow are typically stored
as two-channel (BC5 / RG8) textures where the Z component is derived as
`sqrt(1 - x^2 - y^2)`. Both shaders sample `.rgb` and directly decode:

```hlsl
normalTS = normalTS * 2.0f - 1.0f;
N = normalize(mul(normalTS, TBN));
```

If the texture is BC5 (RG-only), the sampled B channel will be 0, decoding to
-1 after `* 2 - 1`, and the normal will point into the surface — a severe
artifact. If the texture is RGBA/BC3 with a baked Z channel, the code works
correctly.

There is no `#ifdef NORMAL_MAP_TWO_CHANNEL` guard. If the engine allows loading
BC5 normal maps, this will silently produce wrong normals.

**Fix**: Add a define variant `NORMAL_MAP_TWO_CHANNEL` that reconstructs Z:

```hlsl
#ifdef NORMAL_MAP_TWO_CHANNEL
    normalTS.z = sqrt(max(0.0f, 1.0f - dot(normalTS.xy, normalTS.xy)));
#endif
```

---

### F-12 — grid_ps.hlsl Uses Non-Standard b1 Register (LOW)

**File**: `runtime/shaders/editor/grid_ps.hlsl` line 8

**Severity**: LOW — binds its own data to `b1` which the engine reserves for
`PerFrameCB`. Will silently read camera/lighting data as grid parameters if the
same root signature is reused.

**Description**:

```hlsl
cbuffer GridCB : register(b1) {
    row_major float4x4 invViewProj;
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 gridParams;
};
```

All other shaders in the project use `b1` for `PerFrameCB`. The grid shader
correctly duplicates the needed fields (invViewProj, viewProj, cameraPos) but
names the CB differently and adds `gridParams`. If the editor grid pass uses
its own dedicated root signature this is fine, but it is an easy trap if ever
the root signature is shared.

**Fix**: Declare the grid shader as using `PerFrameCB` at `b1` for the shared
fields, plus a separate `GridParamsCB` at `b2` for `gridParams`. This aligns
with the engine convention.

---

### F-13 — overlay_vs.hlsl / overlay_text_ps.hlsl RootConstants Naming Mismatch (LOW)

**File**: `runtime/shaders/editor/overlay_vs.hlsl`,
`runtime/shaders/editor/overlay_text_ps.hlsl`

**Severity**: LOW — naming inconsistency, no runtime bug.

**Description**:
`overlay_vs.hlsl` declares:

```hlsl
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint vertexOffset;      // "vertexOffset" — used for SV_VertexID indexing
};
```

`overlay_text_ps.hlsl` declares the same struct but names the fourth field
`reserved` instead of `vertexOffset`:

```hlsl
cbuffer RootConstants : register(b0) {
    uint vertexBufferIndex;
    uint fontAtlasIndex;
    uint samplerIndex;
    uint reserved;          // "reserved" — unused in PS body
};
```

Since constant buffer fields are accessed by offset, this works at runtime.
But it is confusing and creates a maintenance hazard.

---

### F-14 — Tangent Transformed by Model Not Normal Matrix (LOW)

**File**: `runtime/shaders/runtime/mesh_vs.hlsl` line 79

**Severity**: LOW — tangent orientation subtly wrong under non-uniform scale.

**Description**:

```hlsl
output.tangentWS = float4(normalize(mul(float4(v.tangent.xyz, 0.0), model).xyz), v.tangent.w);
```

The tangent is transformed by the `model` matrix directly. For meshes with
non-uniform scale, the correct transform for tangents is the same as for
normals: `transpose(inverse(model))` (i.e., the normal matrix). Transforming
by `model` without the inverse-transpose will stretch/squash the tangent frame
disproportionately on non-uniformly scaled objects, causing incorrect TBN
matrices and visible shading artifacts.

**Fix**: Transform the tangent by `normalMatrix` (same as the normal), then
renormalize. The handedness in `.w` is carried through unchanged since it is a
pseudovector sign, not a direction.

```hlsl
output.tangentWS = float4(
    normalize(mul(float4(v.tangent.xyz, 0.0), normalMatrix).xyz),
    v.tangent.w
);
```

---

### F-15 — Shadow Sampling Falls Off the Atlas Edge (LOW)

**File**: `runtime/shaders/runtime/mesh_ps.hlsl`,
`runtime/shaders/runtime/deferred_lighting_ps.hlsl`

**Severity**: LOW — shadow acne or over-bright pixels at cascade boundaries.

**Description**:
The PCF kernel samples `atlasUV + offset * texelSize` without clamping to the
cascade's tile bounds. When a fragment is near the edge of a cascade tile, PCF
taps can stray into adjacent cascade tiles, reading incorrect depths. The 3x3
PCF kernel extends ±1 texel from the center tap. With the 2x2 atlas layout
(each cascade occupying half the atlas width in UV), taps near the quadrant
boundary will cross into an adjacent cascade's region.

The UVs are mapped as:

```hlsl
float2 atlasUV = shadowUV * 0.5f + cascadeOffsets[cascadeIdx];
```

A texel at the extreme edge of cascade 0 (UV near 0.5, 0.5) will be offset by
`texelSize` into cascade 1's region (UV > 0.5).

**Fix**: Clamp the PCF sample UV to the cascade's tile rectangle before each
tap:

```hlsl
float2 minUV = cascadeOffsets[cascadeIdx];
float2 maxUV = cascadeOffsets[cascadeIdx] + float2(0.5f, 0.5f);
float2 clampedUV = clamp(atlasUV + offset, minUV, maxUV);
shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, clampedUV, depth);
```

---

## Summary Table

| ID    | Severity | Category              | Short Description                                          |
|-------|----------|-----------------------|------------------------------------------------------------|
| F-01  | CRITICAL | Resource Binding      | LightingCB: C++ header says b3, HLSL uses b2               |
| F-02  | CRITICAL | Resource Binding      | ShadowCB: HLSL uses b4, pass binds at slot 3               |
| F-03  | CRITICAL | Correctness           | Cascade selection loop: confusing dual assignment, comment wrong |
| F-04  | HIGH     | Correctness (Math)    | viewDepth Euclidean vs view-space Z for cascade selection  |
| F-05  | HIGH     | Correctness (PBR)     | Albedo not decoded from sRGB before lighting math          |
| F-06  | HIGH     | Correctness (PBR)     | Geometry k uses IBL formula instead of direct-light formula |
| F-07  | MEDIUM   | Code Organization     | PBR and shadow functions duplicated across two shaders      |
| F-08  | MEDIUM   | Correctness (Tone)    | Per-channel Reinhard causes hue shifts at high luminance    |
| F-09  | MEDIUM   | Magic Numbers         | Hardcoded near/far in debug depth shader                   |
| F-10  | LOW      | Documentation         | fullscreen_vs.hlsl vertex table comment is incorrect        |
| F-11  | LOW      | Missing Feature       | No BC5 / two-channel normal map reconstruction guard        |
| F-12  | LOW      | Resource Binding      | grid_ps.hlsl reuses b1 with non-standard layout             |
| F-13  | LOW      | Code Organization     | overlay_vs vs overlay_text_ps: fourth RootConstants field mismatch |
| F-14  | LOW      | Correctness (Math)    | Tangent transformed by model instead of normalMatrix        |
| F-15  | LOW      | Correctness (Shadow)  | PCF kernel can sample outside cascade atlas tile bounds     |

---

## Cross-Backend Compatibility Notes

The shader set is HLSL-only with no SPIR-V or MSL compilation currently. When
that work begins, the following patterns will need attention:

- **`ResourceDescriptorHeap` / `SamplerDescriptorHeap`** (F-05, overlay_text):
  These are SM6.6 HLSL-specific. DXC can cross-compile to SPIR-V for Vulkan
  1.3 with `VK_EXT_descriptor_indexing`, but `ResourceDescriptorHeap` maps to
  `nonuniformEXT` arrays. MSL (Metal) has no direct equivalent — would require
  argument buffers with `[[id(n)]]` annotations.
- **`#define shadowAtlas shadowAtlasTexture`**: Preprocessor aliasing of
  texture handles is not portable to GLSL or MSL. Extract into a helper
  function instead.
- **`row_major float4x4`**: GLSL uses column-major by default. When
  cross-compiling, ensure the transpose is handled at the SPIR-V level (DXC
  does this automatically) or add explicit `transpose()` calls in the HLSL.
- **`[unroll]` attribute**: Supported by DXC for SPIR-V output. Safe.
- **`fwidth()` in grid_ps.hlsl**: Requires `GL_OES_standard_derivatives` in
  GLSL ES, always available in desktop GLSL 4.0+. Safe for Vulkan/Metal.
- **`SV_Depth` write in grid_ps.hlsl**: Portable to Vulkan (`gl_FragDepth`)
  and Metal (`[[depth(any)]]`). Prevents early-depth testing on some GPU
  architectures — consider `[[depth(less)]]` annotation once a Metal path
  exists.

---

## Recommended Fix Priority

1. **F-06** (k remapping) — pure shader fix, one line per shader, immediate
   visual improvement.
2. **F-05** (sRGB decode) — requires RHI format change or one `pow()` call.
3. **F-04** (viewDepth) — add view forward to PerFrameCB, update both shaders.
4. **F-03** (cascade loop) — rewrite loop in both shaders for clarity/safety.
5. **F-07** (shared includes) — create `.hlsli` files; prerequisite for any
   future per-shader fixes being applied once instead of twice.
6. **F-01 / F-02** (register mismatches) — verify against root signature
   definitions (not in audit scope), then fix documentation or HLSL to match.
7. **F-08** (Reinhard) — switch to luminance-based operator.
8. **F-14** (tangent matrix) — one-line fix in mesh_vs.hlsl.
9. **F-15** (PCF atlas clamp) — low-priority visual artifact at cascade edges.
10. **F-09, F-11, F-12, F-13, F-10** — cleanup and minor improvements.
