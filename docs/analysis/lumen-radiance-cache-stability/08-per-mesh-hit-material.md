# 08 - Per-mesh roughness and metallic for the GI hit material: implementation spec

Date 2026-11-03. UzEngine, branch feat/soft-shadows-area-lights, HEAD 97cb546d. Design spec only (no
code in this lane); every claim is cited to the file and line it was read from so the implementation
is one pass. Line numbers are those of HEAD 97cb546d; at the time of writing the working tree carried
someone's uncommitted probe-feed instrumentation in radiance_cache_trace_cs.hlsl (`g_pfDiag`), which
shifts that one file's lines by about +10. This is doc 06's ranked follow-up #1
(06-hit-vertex-specular-gather.md:582-583): the
hit-vertex sun lobe landed with a scene-constant roughness and F0 = 0.04 because the GI hit material
carries no roughness or metallic (06:124-128, :504-512, :530-533).

## 0. Decision in five lines

1. Source: the draw list already carries everything per mesh - `roughnessSrvIndex`,
   `metallicSrvIndex`, `aoSrvIndex` (the ORM map), `roughnessMultiplier`, `metallicMultiplier` and
   `materialFlags` bit 2 = ORM (draw_list.h:69-79, :92-96, :112), filled for MaterialInterface draws
   (scene_sync.cpp:429-440, :468-486) and mirrored out of material graphs (:568-660). The GI table
   builder copies none of it (frame_lighting.cpp:379-422) and `GIMeshMaterialGPU` has no slot for it
   (path_tracer_pass.h:49-66). The source is complete; only the record is missing fields.
2. Where: NOT inside `GIMeshMaterialGPU`. Its 64-byte stride is baked into the DXIL of eight readers
   (section 2.5), so growing it cannot be byte-identical when OFF. A 32-byte sibling record
   `GIMeshMaterialExtGPU`, indexed by the same `meshId` = TLAS `InstanceID`, is built in the same loop
   and bound by one root DWORD only to the four new gather permutations (section 4).
3. Two hit classes. TLAS hits (the DXR gather, `GITrace_HitSurface`) read the exact per-mesh material
   at the triangle UV; SDF hits (the plain SDF gather, the radiance-cache probe writer) have no mesh
   identity and keep today's scene-constant dielectric path (section 8). Correction to the brief's
   premise: on the TLAS path a metal is NOT under-lit today - `GITrace_HitSurface` returns the raw
   base colour (gi_hit_material.hlsli:100-113; Chrome = a white Lambertian), so its diffuse is
   over-lit and only its glint is missing; the SDF clipmap is the path where a metal is black + a 0.04
   lobe (global_sdf_composite_cs.hlsl:55-62). ON moves both toward the path tracer from opposite
   sides, so the acceptance gate is "closer to the oracle", not "brighter" (section 9).
4. Shading = the path tracer's `EvaluatePBR` structure at the hit (pathtracer_shared.hlsli:144-176):
   `F0 = ComputeF0(albedo, metallic)`, diffuse x `(1 - metallic)` x `PathDiffuseEnergy`, the GGX sun
   lobe with the real roughness, AND the probe-irradiance term coupled the same way
   (`(diffuseAlbedo * w + E_spec) * E/pi`). The last item is required, not optional: with the diffuse
   suppressed and no specular reflection of the probe irradiance, a metal in shadow turns black
   (section 6). Dielectrics shift by at most +1-3% on the probe term (06:514-519).
5. Gate: `r.GI.HitMaterial.PerMesh` (bool, 0) + `.MinRoughness` (float, 0.4) select
   `UZ_GI_HIT_PERMESH=1` permutations of the DXR HitSpecular gather trace/upsample (x WrapUv) = four
   lazily built pipelines; every existing pipeline preprocesses to its current text (section 7).
   Validation on `material_showcase` (Chrome / DarkMetal beside Diffuse / GreenPlastic) with the
   sky-off PT bounce ladder as the oracle, `pbr_showcase` for a textured metal over a floor, and the
   Sponza matrix for dielectric non-regression (section 9).

## 1. Evidence this spec rests on

- Doc 06 sections 2.5 (what the hit lacks), 8.4 (metallic hits), 8.7 (scene-constant roughness),
  10 (follow-up #1 names `GIMeshMaterial.padding`; section 3 below explains why one DWORD is not
  enough and why the record cannot grow), 12 (the implementation record: OFF = 21 DXIL blobs SHA-256
  identical; ON = `rt_dxr_k0_nosky` 0.485 -> 0.641x; the hemisphere maximum of `D*Vis*NoL` is 1.24 at
  roughness 0.7, 32 at 0.4, so the cap at 1.0 is a no-op from 0.74 up).
- Doc 07 (the WrapUv permutation of every reader of the hit material; pipeline_gi.cpp:333-385 is the
  table-driven lazy-variant mechanism this spec reuses).
- The path tracer's per-hit material read, the oracle this spec mirrors: pathtracer_closesthit.hlsl:
  83-102 (albedo at the UV, ORM `g/b` or separate `r` maps, `saturate(x * multiplier)`), fed by
  `PipelinePathMaterial` (pipeline_path_tracer_pass.h:21-33) from the same DrawCommand fields
  (pipeline_path_tracer_pass.cpp:414, :439-441, :467-468).
- The raster rule for the same fields: gbuffer_ps.hlsl:177-200 (defaults roughness 1, metallic 0;
  ORM through `aoIndex` with R = AO, G = roughness, B = metallic; a missing map means "the multiplier
  is the value").

## 2. The material at a hit today

### 2.1 The record and its builder

`GIMeshMaterialGPU` (path_tracer_pass.h:49-66, 64 bytes, static_asserts :68-84) = tint, albedo SRV,
raw vertex/index views + layout, emissive SRV + colour, one `padding` DWORD at offset 60. HLSL mirror
`GIMeshMaterial` (gi_hit_material.hlsli:48-61); the layout test pins the two
(test_cbuffer_layouts.py:483-484, Tier C = reflected stride vs `sizeof`).

`SceneRenderer::UpdateGIMaterialTable` (frame_lighting.cpp:346-427): one Upload-heap structured
buffer sized to `m_meshes.size()` (:350-370; members scene_renderer_impl.h:459-461), zeroed
(:376-378), then per draw command `entries[dc.meshId]` receives tint (:382-384), the albedo SRV with
slot 0 = unset (:386-389), the emission with the graph-draw pass-through rule (:399-407) and the raw
geometry views for the three known vertex strides (:408-422). Called from five sites, every one
returning the same SRV: frame_gi_passes.cpp:158 (DXR gather), :364 (surface cache), :1093, :1246,
frame_lighting.cpp:231 (legacy PT).

### 2.2 TLAS hits read the exact material - but only albedo, normal, emission

`GITrace_HitSurface` (gi_hit_material.hlsli:66-119): triangle UV + object-space normal from the raw
views (:76-98), `albedo = mat.tint` x the albedo texture at the UV (:100-113; clamp sampler by
default, the s3 WRAP sampler under `UZ_GI_HIT_UV_WRAP`, :108-112), NaN firewall (:115-117).
`GITrace_HitUV` re-reads the UV alone (:125-139; used by `GITrace_HitEmissive` :146-166). Nothing
reads `padding`.

Consequence for a metal on this path: Chrome (`baseColor` 1,1,1, `metallicMultiplier` 1,
material_showcase.uzscene:81-85) returns albedo (1,1,1) and is shaded as a white Lambertian
(dxr_diffuse_gather.hlsli:94-102). Its diffuse bounce is invented, its sun glint is missing.

### 2.3 SDF hits read the clipmap - (1 - metallic)-weighted albedo, no roughness, no mesh id

The composite's per-mesh record carries `albedoSrv`, `metallicSrv`, `baseColorTint`,
`metallicMultiplier`, `materialFlags` (global_sdf_composite_cs.hlsl:23-38; CPU fill
global_distance_field.cpp:280-285) and folds them into one RGB: `DiffuseReflectance = saturate(albedo
* tint) * (1 - saturate(metallic * multiplier))` from the textures' last mip (:44-50, :55-62), stored
as `float4(distance, rgb)` (:113). No channel is left for roughness or F0, and a voxel does not know
which mesh it came from (the Phase-D finding). A metal is black here and, under HitSpecular, gets the
0.04 lobe only (06:504-512). Readers: `ShadeSdfReflectionHit` (sdf_reflections.hlsli:239-299, albedo
:265), the RC probe writer (radiance_cache_trace_cs.hlsl:767-790, albedo via `SampleGdfSurface`), the
DXR gather's own fallback when a hit has no raw attributes (dxr_diffuse_gather.hlsli:62-68) and its
mode-2 control (:37-46).

### 2.4 How the hit-vertex specular consumes material today

`EvalGiHitSunSpecular` (gi_hit_specular.hlsli:53-84) takes `roughness` and `F0` as arguments; every
site passes the scene constant and `kGiHitSpecularF0 = 0.04` (:37): dxr_diffuse_gather.hlsli:95-97
(`scene.hitSpecularRoughness`), sdf_reflections.hlsli:271-273, radiance_cache_trace_cs.hlsl:774-775
(`asfloat(g_hitSpecularRoughnessBits)`). The roughness travels as a root DWORD (ssgi_trace_cs.hlsl:
36-41, ssgi_upsample_cs.hlsl:23, radiance_cache_trace_cs.hlsl:60-67) into `SdfReflectionScene`
(sdf_reflections.hlsli:59-65) through `BuildSurfaceScene` (ssgi_world_gather.hlsli:38-41), from
`SSGIRenderer::HitSpecularRoughness()` (ssgi_renderer.cpp:90-92, clamp [0.4, 1]). The function also
computes `specularAlbedo = F0 * A + B` from the LUT (:73-79) but returns only the sun-Lambert weight
derived from it (:80-82); section 6 needs `E_spec` itself.

### 2.5 Readers of `GIMeshMaterial` (why the record cannot grow)

| reader | read site | root DWORD |
|---|---|---|
| DXR gather trace + upsample (and their HitSpecular / WrapUv variants) | dxr_diffuse_gather.hlsli:62, :75 | ssgi_trace_cs.hlsl:33, ssgi_upsample_cs.hlsl (`g_gatherMaterialSrv`) |
| DDGI probe writer | ddgi_probe_common.hlsli:270, ddgi_probe_trace_cs.hlsl:158 | ddgi_probe_trace_cs.hlsl:45 |
| surface-cache feed + indirect (relight) writers | surface_cache_hit.hlsli:33, :47, :141 | surface_cache_feed_cs.hlsl:34, surface_cache_relight_cs.hlsl:34 |
| RT reflections | rt_reflection_cs.hlsl:328, :349, :422 | rt_reflection_cs.hlsl:101 (rt_reflection_pass.h:44) |
| legacy path tracer | path_tracer_ps.hlsl:204, :215 | path_tracer_ps.hlsl:52 |
| `GITrace_HitAlbedo` (mip-average fallback) | gi_trace_common.hlsli:128-141 | (callers above) |

A `StructuredBuffer<T>` load bakes `sizeof(T)` into the DXIL addressing, so a 64 -> 80 byte record
changes every one of these pipelines' bytecode, plus their WrapUv permutations (pipeline_gi.cpp:
345-355: eight entries). OFF byte-identity (06:423-432, the repository's standard for this lane) is
therefore unreachable with a grown record, and permuting all readers is a combinatorial diff for a
feature that only the gather consumes in pass one. One buffer is also shared by all five binders in a
frame (2.1), so a CVar-dependent stride is not an option either.

### 2.6 The source of truth per mesh

- MaterialInterface draws: `cmd.roughnessSrvIndex` / `metallicSrvIndex` from the loaded material
  (scene_sync.cpp:429-440), else from paths (:475-477); ORM-packed materials bind the single map to
  `cmd.aoSrvIndex` and set `materialFlags |= 0x4` (:468-473); multipliers :485-486.
- Material-graph draws (every Sponza mesh, frame_lighting.cpp:398): the graph PSO computes the
  G-buffer values itself (gbuffer_ps.hlsl:324-327 overrides), but scene_sync.cpp:568-660 resolves
  "ordinary draw metadata" for the shadow, distance-field and deferred consumers: multipliers from the
  graph params (:592-593), a Roughness texture input (:634-637) or a constant folded into the
  multiplier (:638-639), a Metallic input from channel B = ORM (:646-650) or R (:651-653), or a
  constant via the white fallback texture x multiplier (:655-658). Exact for texture-sampled and
  constant inputs, approximate when the graph does math between them - the same approximation the
  SDF composite (2.3) and the pipeline path tracer (1) already live with, so parity with the oracle
  holds by construction.
- The raster consumer of these fields (gbuffer_ps.hlsl:177-200) is the rule to mirror: defaults
  roughness 1 / metallic 0; ORM through `aoIndex` (G, B); separate maps' R x multiplier; no map = the
  multiplier itself. The pipeline PT applies the multiplier in the ORM branch too and saturates
  (pathtracer_closesthit.hlsl:101-102); the composite does the same for metallic
  (global_sdf_composite_cs.hlsl:60-61). Section 5 adopts the PT/composite form (multiplier in both
  branches, saturate): identical to the raster rule whenever the multiplier is 1 (every textured
  graph draw: :592-593), and the PT is the oracle the gates compare against.
- Caveat on the oracle: `PipelinePathMaterial.roughnessSrv` is filled from `draw.roughnessSrvIndex`
  (pipeline_path_tracer_pass.cpp:439) while a MaterialInterface ORM map lives only in `aoSrvIndex`
  (scene_sync.cpp:471-473), so for ORM-packed MaterialInterface materials the PT reads no map and
  lands on `roughness = metallic = multiplier` (pathtracer_closesthit.hlsl:85-86 default both to 1).
  Graph draws put the ORM map in both slots (scene_sync.cpp:635, :649) so Sponza is unaffected, and
  neither showcase scene packs ORM (section 9). Follow-up for the PT lane (section 11), not this diff.

## 3. Designs considered

### Design A - grow `GIMeshMaterialGPU` to 80 bytes (five fields replace `padding`)

Pros: one table, one read, no new SRV. Cons: 2.5 - every reader's DXIL changes (stride), OFF cannot
be asserted byte-identical, and the WrapUv table doubles the blast radius. Rejected on the lane's
OFF-identity rule alone.

### Design B - two 16-bit per-mesh averages in `padding` (doc 06's sketch)

Pros: zero layout change, one DWORD read. Cons: the CPU has no texel access - the averages exist only
on the GPU (the composite samples the last mip, global_sdf_composite_cs.hlsl:44-50), so this needs a
GPU patch pass into a default-heap copy of an Upload-heap buffer that is not UAV-writable
(frame_lighting.cpp:363-364), i.e. a copy + a compute pass + a second SRV anyway. And an average
cannot carry the constant-metallic graph encoding (white texture x multiplier, scene_sync.cpp:
655-658) or a textured roughness map; the PT reads the map at the hit UV. Rejected: more machinery
than Design C for less fidelity.

### Design C (recommended) - a 32-byte sibling table bound only to the permuted gather

`GIMeshMaterialExtGPU[meshId]` = { roughness/ORM SRV, metallic SRV, `materialFlags`, multipliers },
built in the same `UpdateGIMaterialTable` loop from the same DrawCommand, its SRV passed as a new
root DWORD declared only under `UZ_GI_HIT_PERMESH`. The hit reads it at the triangle UV with the
albedo path's sampler. Pros: `GIMeshMaterialGPU` and its eight readers are untouched; OFF is
byte-identical by construction; the read is the PT's own rule (one or two extra texture taps per
hit); the table is ready for every later reader (section 11) without another layout change. Cons:
one more small Upload buffer (32 B x meshCap, 16 KB at 512 meshes) refreshed with the primary table;
one more root DWORD per gather shader; the UV is re-read for the material taps (3 index + 3 UV loads,
the `GITrace_HitEmissive` precedent, gi_hit_material.hlsli:153-154).

### The shading decision inside C: sun term only (C1) or sun + probe term coupled (C2)

- C1: per-mesh roughness/F0 and `(1 - metallic)` on the sun term; the probe-irradiance Lambert keeps
  `albedo * E/pi` with the raw base colour. Never black, energy <= 1, no new LUT output. But a metal's
  probe term stays the invented base-colour Lambert: 1.0 x E/pi for Chrome where the PT reflects
  about E_spec x E/pi = 0.40 (rough) to 0.87 (smooth) x E/pi (Karis-fit sizing, F0 0.9; the LUT is
  the source of truth) - 2.5x over for a rough metal, and inconsistent with its own sun term.
- C2: the probe term becomes `(diffuseAlbedo * diffuseWeight + E_spec) * E/pi` with `E_spec = F0 * A
  + B` at `(NoV, roughness)` - exact for uniform incident radiance, the same assumption the Lambert
  already makes of the SH probe, and the full coupling doc 06 deferred (06:514-519). A metal in
  shadow reflects the probe irradiance instead of going black; a dielectric's probe term moves by
  `E_spec * (1 - albedo) - 0.005 * albedo` = +1-3%. One float3 added to the result struct under the
  define, two FMAs.

Recommendation: C with C2. C1 would leave the dominant metal error (the invented diffuse of bright
metals) on the probe term, which in a sky-off ladder is most of a shadowed hit's radiance. Boundaries
assumed: the gather's hit radiance is radiance leaving the hit toward the ray origin (true, 06:202-208);
`SdfReflectionIrradiance` returns the probe's E/pi (radiance_cache_trace_cs.hlsl:760 "packed SH
already stores E/pi"); the sun is the only light evaluated inline at hits (06:206-208). The SDF sites
keep doc 06's sun-only asymmetry; the DXR gather is the only site where both terms are coupled, a
known inconsistency of at most 3% on dielectrics between the two gather paths (section 10).

## 4. The record and its fill (CPU)

### 4.1 `GIMeshMaterialExtGPU` - path_tracer_pass.h, next to `GIMeshMaterialGPU` (:49-84)

```
struct alignas(16) GIMeshMaterialExtGPU {               // 32 bytes; HLSL mirror GIMeshMaterialExt (section 5.1)
    uint32_t roughnessSrv        = UINT32_MAX;          // [0]  ORM map when materialFlags bit 2 is set (R = AO, G = roughness, B = metallic:
                                                        //      the G-buffer's aoIndex), else the roughness map (R). UINT32_MAX = none.
    uint32_t metallicSrv         = UINT32_MAX;          // [4]  metallic map (R); unused under ORM. UINT32_MAX = none.
    uint32_t materialFlags       = 0;                   // [8]  DrawCommand::materialFlags verbatim (bit 2 = ORM; bits 24-27 shading model).
    uint32_t padding0            = 0;                   // [12]
    float    roughnessMultiplier = 1.f;                 // [16] no map: the value itself (gbuffer_ps.hlsl:193).
    float    metallicMultiplier  = 0.f;                 // [20] no map: the value itself (:200). Default 0 = dielectric, never a metal by accident.
    uint32_t padding1            = 0;                   // [24]
    uint32_t padding2            = 0;                   // [28]
};
static_assert(sizeof(GIMeshMaterialExtGPU) == 32); static_assert(alignof(GIMeshMaterialExtGPU) == 16);
static_assert(offsetof(GIMeshMaterialExtGPU, materialFlags) == 8);
static_assert(offsetof(GIMeshMaterialExtGPU, roughnessMultiplier) == 16);
static_assert(offsetof(GIMeshMaterialExtGPU, metallicMultiplier) == 20);
```

Unpacked floats rather than two fp16 in one DWORD: the engine has no CPU half-conversion helper in
this path, and 16 KB does not buy one. A zeroed entry (no draw this frame) is a rough dielectric; the
hit never reads it anyway, because the primary record of such a mesh has sentinel geometry views and
`GITrace_HitSurface` already returned false (gi_hit_material.hlsli:73-74; the gather then takes the
SDF-material branch, dxr_diffuse_gather.hlsli:62-68).

### 4.2 Builder - frame_lighting.cpp `UpdateGIMaterialTable` (:346-427)

- Members next to :459-461 of scene_renderer_impl.h: `m_giMaterialTableExt`, `m_giMaterialTableExtSrv`
  (same `m_giMaterialTableCapacity`); accessor `GetGIMaterialTableExtSrv()` for the binder (4.3).
- Create/recreate with the primary buffer (:351-370): same desc, `sizeBytes = meshCap *
  sizeof(GIMeshMaterialExtGPU)`, `stride = 32`, `debugName = "GI_MaterialTableExt"`, destroyed
  wherever the primary is (:354-358 and the renderer's shutdown path that destroys `m_giMaterialTable`).
- Map both (:373-374), zero both (:376-378), then inside the draw loop after :389:

```
const bool orm = (dc.materialFlags & 0x4u) != 0u;
auto srv = [](uint32_t i) { return (i == 0 || i == UINT32_MAX) ? UINT32_MAX : i; };   // slot 0 = unset, as :386-389 and pipeline_path_tracer_pass.cpp:414
auto& ext = ext_entries[dc.meshId];
ext.roughnessSrv        = srv(orm ? dc.aoSrvIndex : dc.roughnessSrvIndex);          // the G-buffer's rule, gbuffer_ps.hlsl:183-190; NOT the PT's :439 (2.6 caveat)
ext.metallicSrv         = orm ? UINT32_MAX : srv(dc.metallicSrvIndex);
ext.materialFlags       = dc.materialFlags;
ext.roughnessMultiplier = dc.roughnessMultiplier;
ext.metallicMultiplier  = dc.metallicMultiplier;
```

  Graph draws need no special case: scene_sync.cpp:568-660 already resolved these fields for them.
  Always filled (six stores per draw inside a loop that already runs); the SRV is only ever bound to
  the PerMesh variants, so OFF rendering cannot observe it.
- Return value unchanged (the primary SRV); the ext SRV is read through the accessor by the one
  binder that needs it.

### 4.3 Binding - frame_gi_passes.cpp:158-197, ssgi_renderer.h:73-111

`materialExtSrv = (requestedDxr && dxrReady) ? GetGIMaterialTableExtSrv() : UINT32_MAX` next to :158
(after `UpdateGIMaterialTable` has run), captured by the pass lambda with `materialSrv` (:164, :169),
`ssgiParams.gatherMaterialExtSrv = materialExtSrv` next to :197; new field `gatherMaterialExtSrv =
UINT32_MAX` in `SSGIRenderParams` after :111.

## 5. The hit read (HLSL)

### 5.1 gi_hit_material.hlsli - everything under `#ifdef UZ_GI_HIT_PERMESH`, after `GITrace_HitEmissive` (:166)

```
#ifdef UZ_GI_HIT_PERMESH
/// Mirrors GIMeshMaterialExtGPU (path_tracer_pass.h, 32 bytes): r.GI.HitMaterial.PerMesh.
struct GIMeshMaterialExt {
    uint  roughnessSrv;         ///< ORM map (G = roughness, B = metallic) under materialFlags bit 2, else the roughness map (R).
    uint  metallicSrv;          ///< Metallic map (R); unused under ORM.
    uint  materialFlags;        ///< DrawCommand::materialFlags (bit 2 = ORM packed).
    uint  padding0;
    float roughnessMultiplier;
    float metallicMultiplier;
    uint2 padding1;
};

/// The hit's roughness and metallic: the sibling table at the triangle UV, the path tracer's rule
/// (pathtracer_closesthit.hlsl:83-102) with the albedo path's sampler. False when the table is unbound
/// or the mesh has no raw views (the caller keeps the scene-constant path).
bool GITrace_HitRoughnessMetallic(uint meshId, uint primIndex, float2 bary, uint materialTableSrv,
                                  uint materialExtSrv, out float roughness, out float metallic) {
    roughness = 1.0; metallic = 0.0;
    if (materialTableSrv == kGITraceSentinelSrv || materialExtSrv == kGITraceSentinelSrv) return false;
    StructuredBuffer<GIMeshMaterial> table = ResourceDescriptorHeap[materialTableSrv];
    GIMeshMaterial mat = table[meshId];
    if (mat.vbSrv == kGITraceSentinelSrv || mat.ibSrv == kGITraceSentinelSrv) return false;
    StructuredBuffer<GIMeshMaterialExt> extTable = ResourceDescriptorHeap[materialExtSrv];
    GIMeshMaterialExt ext = extTable[meshId];
    float2 uv = GITrace_HitUV(mat, primIndex, bary);                              // :125-139
    float2 safeUv = (isfinite(uv.x) && isfinite(uv.y)) ? uv : float2(0.0, 0.0);   // the albedo guard, :103-106
    if ((ext.materialFlags & 4u) != 0u && ext.roughnessSrv != kGITraceSentinelSrv) {
        Texture2D<float4> orm = ResourceDescriptorHeap[NonUniformResourceIndex(ext.roughnessSrv)];
        float3 c = orm.SampleLevel(UZ_GI_HIT_MATERIAL_SAMPLER, safeUv, 0.0).rgb;
        roughness = c.g; metallic = c.b;
    } else {
        if (ext.roughnessSrv != kGITraceSentinelSrv) {
            Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(ext.roughnessSrv)];
            roughness = t.SampleLevel(UZ_GI_HIT_MATERIAL_SAMPLER, safeUv, 0.0).r;
        }
        if (ext.metallicSrv != kGITraceSentinelSrv) {
            Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(ext.metallicSrv)];
            metallic = t.SampleLevel(UZ_GI_HIT_MATERIAL_SAMPLER, safeUv, 0.0).r;
        }
    }
    roughness = saturate(roughness * ext.roughnessMultiplier);   // pathtracer_closesthit.hlsl:101-102
    metallic  = saturate(metallic  * ext.metallicMultiplier);
    if (!isfinite(roughness)) roughness = 1.0;                   // firewall, the :115-117 policy
    if (!isfinite(metallic))  metallic  = 0.0;
    return true;
}
#endif // UZ_GI_HIT_PERMESH
```

- `UZ_GI_HIT_MATERIAL_SAMPLER` = `UZ_GI_HIT_WRAP_SAMPLER` under `UZ_GI_HIT_UV_WRAP`, else
  `linearClamp` - defined inside the new block from the existing :20-42 machinery, so the ORM /
  roughness / metallic taps follow the albedo tap's sampler exactly (a tiled ORM map otherwise reads
  its edge texel: doc 07's bug class applied to roughness). `NonUniformResourceIndex` because the SRV
  diverges per hit, as the albedo read (:107).
- "No map = the multiplier itself": with `roughness = 1, metallic = 0` initialised and the multiply
  applied unconditionally, a missing roughness map yields `1 * roughnessMultiplier` and a missing
  metallic map `0 * metallicMultiplier = 0`. That last one differs from the raster's "no map ->
  `metallic = metallicMultiplier`" (gbuffer_ps.hlsl:199-200): a MaterialInterface material that sets
  metallic by scalar alone (material_showcase's spheres, `metallicMultiplier` 1.0 with no map,
  material_showcase.uzscene:84-85, :111-112) would read as a dielectric. So initialise `metallic =
  1.0` before the multiply exactly as the PT does (pathtracer_closesthit.hlsl:85-86: both default to
  1.0, then `saturate(x * multiplier)`): no map -> `metallicMultiplier`, which is the raster rule's
  value; a map -> `map.r * multiplier`. Graph draws that author a constant metallic use the white
  texture x multiplier encoding (scene_sync.cpp:655-658) and read the same either way. The C++ default
  `metallicMultiplier = 0` (4.1) is what makes an absent entry a dielectric under this rule.
- Not touched: `GITrace_HitSurface`, `GITrace_HitUV`, `GITrace_HitEmissive` (verbatim; OFF identity).

### 5.2 Site B - dxr_diffuse_gather.hlsli, the only consumer in pass one

1. Before the `if (g_gatherDxrMode == 2u)` at :37, under the define:
   `float hitRoughness = scene.hitSpecularRoughness; float hitMetallic = 0.0;` (the scene constant
   and dielectric = today's values; the mode-2 control and the no-raw-attributes fallback keep them).
2. In the table-hit branch after `normal = mul(objectNormal, ...)` (:70), under the define:
   ```
   float r, m;
   if (GITrace_HitRoughnessMetallic(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(),
                                    query.CommittedTriangleBarycentrics(), g_gatherMaterialSrv,
                                    scene.hitMaterialExtSrv, r, m)) {
       hitRoughness = max(r, scene.hitMinRoughness);   // the variance floor, section 7.1
       hitMetallic  = m;
   }
   ```
3. The shading tail: inside the existing `#ifdef UZ_GI_HIT_SPECULAR` block (:90-99), nest
   `#ifdef UZ_GI_HIT_PERMESH` with section 6's expression and keep :94-99 verbatim in its `#else`.
   The outer `#else` (:100-103) is untouched. A `#if defined(UZ_GI_HIT_PERMESH) &&
   !defined(UZ_GI_HIT_SPECULAR)` `#error` at the top of the file documents the dependency.

## 6. The shading at a per-mesh hit (site B)

With unit `normal` facing the ray, `V = -direction`, `L = scene.sunDirection`, `ndl`, `visibility`,
`albedo` = the base colour from `GITrace_HitSurface`, `hitRoughness`, `hitMetallic` (5.2):

```
F0            = ComputeF0(albedo, hitMetallic)                       // brdf.hlsli:189-192 = lerp(0.04, albedo, metallic)
diffuseAlbedo = albedo * (1.0 - hitMetallic)                         // the PT's (1 - metallic) * albedo, pathtracer_shared.hlsli:174
hs            = EvalGiHitMaterialSpecular(normal, V, L, ndl, hitRoughness, F0, scene.hitSpecularLutSrv, UZ_SDF_REFLECTION_SAMPLER)
sunShaded     = diffuseAlbedo * (scene.sunColor * (ndl * visibility * M_1_PI)) * hs.diffuseWeight
              + scene.sunColor * (visibility * hs.specularCos)
probeShaded   = (diffuseAlbedo * hs.diffuseWeight + hs.specularAlbedo) * SdfReflectionIrradiance(scene, position, normal, hitT)
return emission + sunShaded + probeShaded
```

- `EvalGiHitMaterialSpecular` is a new function in gi_hit_specular.hlsli under `#ifdef
  UZ_GI_HIT_PERMESH` (after :84), returning `GiHitMaterialSpecular { float3 specularCos; float3
  diffuseWeight; float3 specularAlbedo; }`: the body of `EvalGiHitSunSpecular` (:53-84) with
  `result.specularAlbedo = specularAlbedo` added before the return and zero in the identity early-out.
  `EvalGiHitSunSpecular` itself is not edited (the HitSpecular-only permutations must preprocess to
  their current text). The duplication is ~30 lines and is the price of byte-identity for the
  existing variants; collapsing the two once PerMesh is default-on is a one-line follow-up.
- Sun term: exactly the PT's `(diffuseEnergy * (1 - metallic) * albedo / PI + specular) * NoL` with
  `E_sun * visibility` (pathtracer_shared.hlsli:163-174), the D7 coupling reused verbatim
  (`PathDiffuseEnergy`, pathtracer_math.hlsli:46; LUT clamp :73-78 of gi_hit_specular.hlsli).
  Directional albedo for the sun `<= diffuseAlbedo * (1 - E_spec - 0.005) + E_spec <= 1`: conserved.
- Probe term (C2, section 3): the same BRDF integrated against uniform incident radiance `E/pi`.
  Hemispherical albedo `diffuseAlbedo * w + E_spec <= 1`. For a metal `diffuseAlbedo = 0`, so the
  term is `E_spec * E/pi`: Chrome about 0.4-0.87 x E/pi by roughness instead of today's 1.0 x E/pi;
  DarkMetal (base 0.4, roughness 0.4: F0 0.4, E_spec ~0.31 at normal incidence) about 0.31 x E/pi
  instead of 0.40. For Sponza's dielectrics (F0 0.04, E_spec 0.025-0.05, albedo ~0.5) the change is
  `+E_spec * (1 - albedo) - 0.005 * albedo` = +1 to +2.5% of the probe term (06:514-519 sized it the
  same way).
- Identity cases: LUT unbound (before the IBL bake), `hitRoughness <= 0`, `NoV <= 0` or `ndl <= 0`
  return `specularCos = 0, diffuseWeight = 1, specularAlbedo = 0`, so the expression reduces to
  `emission + diffuseAlbedo * (sunLambert + E/pi)`. Note `ndl <= 0` (sun below the hit's horizon) hits
  this path too: the sun term is zero there anyway, but the probe term then loses `E_spec` for that
  ray. Split the early-outs: `NoL <= 0` must zero `specularCos` only and still evaluate the LUT for
  `specularAlbedo` / `diffuseWeight` (the LUT depends on `NoV` and roughness, not on L); keep the
  LUT-unbound / `NoV <= 0` / `roughness <= 0` cases as full identity. A metal hit whose sun is below
  the horizon is the common shadowed case and must not go black.
- Emission unchanged (:75-76). The SDF-material fallback and mode 2 reach this tail with the scene
  constant and `hitMetallic = 0`: `F0 = 0.04`, `diffuseAlbedo = albedo` - the only difference from
  today's HitSpecular text for them is the probe-term coupling (+1-3%), which keeps one expression per
  permutation instead of a per-ray branch.

## 7. Gating: CVars, permutations, root DWORDs, selection, OFF identity

### 7.1 CVars - ssgi_renderer.cpp next to `cv_HitMaterialWrapUv` (:67-70); statics next to :94-96 / ssgi_renderer.h:220-227

- `r.GI.HitMaterial.PerMesh` - bool, default false. "Per-mesh roughness and metallic at the DXR final
  gather's hits (the draw's roughness/metallic/ORM maps and multipliers, the path tracer's rule):
  F0 = lerp(0.04, baseColor, metallic), diffuse x (1 - metallic), the probe term coupled by the LUT's
  E_spec. Requires r.GI.FinalGather.HitSpecular 1 and r.GI.FinalGather.DXR 1; the SDF paths keep the
  scene-constant lobe." (The brief's `r.GI.HitMaterial.PerMeshRoughness` is the same knob under a
  name that also covers metallic.)
- `r.GI.HitMaterial.PerMesh.MinRoughness` - float, default 0.4, range [0.1, 1.0]. The floor applied to
  the hit's authored roughness before the lobe and the LUT (5.2 item 2). 0.4 is where doc 06 bounded
  the scene constant (06:353-355; the hemisphere maximum of `D*Vis*NoL` is 32 there, 06 section 12),
  so the cap at 1.0 engages only in the grazing-mirror sliver; 0.74 makes it a no-op. The floor is the
  standard secondary-bounce glossy blur (Blender Cycles "Filter Glossy"): it widens a smooth metal's
  glint at the hit at equal energy, trading a spatial spread for a bounded per-ray tail (section 10).
- `SSGIRenderer::HitMaterialPerMeshEnabled()`, `HitMaterialPerMeshMinRoughness()` (clamped).

### 7.2 Permutations (`UZ_GI_HIT_PERMESH=1`) - pipeline_gi.cpp, the :333-385 table mechanism

| variant | shader | defines | debug name |
|---|---|---|---|
| SsgiDxrTrace | SsgiTraceCS | `UZ_GATHER_DXR=1;UZ_GI_HIT_SPECULAR=1;UZ_GI_HIT_PERMESH=1` | SSGI_DXRTrace_HitSpecular_PerMesh |
| SsgiDxrTraceWrapUv | SsgiTraceCS | the above + `;UZ_GI_HIT_UV_WRAP=1` | SSGI_DXRTrace_HitSpecular_WrapUv_PerMesh |
| SsgiDxrUpsample | SsgiUpsampleCS | `UZ_GATHER_DXR=1;UZ_GI_HIT_SPECULAR=1;UZ_GI_HIT_PERMESH=1` | SSGI_DXRUpsample_HitSpecular_PerMesh |
| SsgiDxrUpsampleWrapUv | SsgiUpsampleCS | the above + `;UZ_GI_HIT_UV_WRAP=1` | SSGI_DXRUpsample_HitSpecular_WrapUv_PerMesh |

A sibling `kGiHitPerMeshSources[]` + `enum class GiHitPerMeshVariant` + `GetGiHitPerMesh(variant)`
(pipeline_library.h next to :642-653; handle arrays next to pipeline_library_impl.h:171 / :270;
destruction next to pipeline_lifecycle.cpp:157-162 / :220-225), copying `GetGiHitUvWrap` (:359-385)
including its `Failed()` memo and the one-time error log. Why not permute the plain (non-DXR) gather,
the RC writer, the surface-cache writers, DDGI or RT reflections: no mesh id at an SDF hit (section
8), and the TLAS writers have no HitSpecular variant to extend yet (section 11). ReSTIR GI is not
permuted, as for HitSpecular (06:521-528); the existing `m_loggedHitSpecularRestir` log
(ssgi_renderer.h:374) covers PerMesh because PerMesh implies HitSpecular.

### 7.3 Root DWORDs

Root parameter 0 holds 24 DWORDs (dx12_pso_creation.cpp:186); the trace uses 18 under HitSpecular
(ssgi_renderer.cpp:858), the upsample 14 (:1462).

- ssgi_trace_cs.hlsl after :40, inside the `UZ_GI_HIT_SPECULAR` block: `#ifdef UZ_GI_HIT_PERMESH uint
  g_hitMaterialExtSrv; uint g_hitMinRoughnessBits; #endif` -> indices 18, 19. ssgi_upsample_cs.hlsl
  after :23 -> indices 14, 15. Before the include, next to :47-48 / :27: `#define
  UZ_SSGI_HIT_MATERIAL_EXT_SRV g_hitMaterialExtSrv` and `UZ_SSGI_HIT_MIN_ROUGHNESS_BITS
  g_hitMinRoughnessBits`.
- ssgi_world_gather.hlsli: defaults next to :12-18 (`0xffffffffu` and `0u`: an unnamed table means
  "no per-mesh data", every hit keeps the scene constant); `BuildSurfaceScene` after :40:
  `scene.hitMaterialExtSrv = UZ_SSGI_HIT_MATERIAL_EXT_SRV; scene.hitMinRoughness =
  asfloat(UZ_SSGI_HIT_MIN_ROUGHNESS_BITS);` under the define.
- sdf_reflections.hlsli `SdfReflectionScene` after :64, still inside the HitSpecular block:
  `#ifdef UZ_GI_HIT_PERMESH uint hitMaterialExtSrv; float hitMinRoughness; #endif`. The deferred PS
  and the reflection passes never define it.
- CPU: `SSGITraceRC` (:812-832) += `uint32_t hitMaterialExtSrv, hitMinRoughnessBits;` (static_assert
  80, "14 legacy + 3 DXR + 1 HitSpecular + 2 PerMesh"); `UpsampleRC` (:1423-1439) += the same
  (64). Fill from `params.gatherMaterialExtSrv` and the CVar's float bits (the :856 memcpy pattern).
  Push counts `m_hitPerMesh ? 20 : (m_hitSpecular ? 18 : (m_gatherDxrMode ? 17 : 14))` at :858 and
  `m_hitPerMesh ? 16 : (m_hitSpecular ? 14 : (m_gatherDxrMode ? 13 : 10))` at :1462.
- The RC writer's root block (radiance_cache_trace_cs.hlsl:60-67, radiance_cache.cpp:1091-1095) is
  untouched. `RadianceCacheTraceConstants` and PerFrameCB untouched.

### 7.4 Effective state, selection, history - ssgi_renderer.cpp `Render()` after :503

```
bool hitPerMesh = params.worldGather && dxrMode != 0u && hitSpecular && HitMaterialPerMeshEnabled()
               && params.gatherMaterialExtSrv != UINT32_MAX;
if (hitPerMesh) {
    using V = PipelineLibrary::GiHitPerMeshVariant;
    const bool variantsValid = GetGiHitPerMesh(hitUvWrap ? V::SsgiDxrTraceWrapUv : V::SsgiDxrTrace).IsValid()
                            && GetGiHitPerMesh(hitUvWrap ? V::SsgiDxrUpsampleWrapUv : V::SsgiDxrUpsample).IsValid();
    if (!variantsValid) { log once (m_loggedHitPerMeshFallback); hitPerMesh = false; }
}
if (m_hitPerMesh != hitPerMesh) { m_hasHistory = false; m_restirHistoryValid = false; m_hitPerMesh = hitPerMesh; log; }
```

The both-variants-valid rule and the history invalidation are the HitSpecular / WrapUv precedents
(:458-478, :483-503); members next to ssgi_renderer.h:372-378. `DispatchTrace` (:786-791) and
`DispatchUpsample` (:1396-1402): after the WrapUv choice, `if (m_hitPerMesh) pso =
GetGiHitPerMesh(m_hitUvWrap ? ...WrapUv : ...)` - PerMesh supersedes, since its define set is a
superset.

### 7.5 OFF byte-identity and the layout test

- Shader text: every change is an addition inside `#ifdef UZ_GI_HIT_PERMESH` (5.1, 5.2, 6, 7.3) or a
  nested block whose `#else` is the current HitSpecular text verbatim (5.2 item 3). The default,
  HitSpecular and WrapUv pipelines of every reader of gi_hit_material.hlsli / gi_hit_specular.hlsli /
  sdf_reflections.hlsli / dxr_diffuse_gather.hlsli / ssgi_world_gather.hlsli preprocess to today's
  text: the 21 blobs of doc 06 section 12 plus the eight WrapUv variants, SHA-256 of stripped DXIL
  equal before/after (one recompile expected: the cache keys on source text).
- `GIMeshMaterialGPU` / `GIMeshMaterial`: untouched (the :483-484 Tier C entry keeps reporting 64).
- New Tier C entry after test_cbuffer_layouts.py:484: `layout_entry('gi/gi_hit_material.hlsli',
  'GIMeshMaterialExt', PASSES + 'pathtracing/path_tracer_pass.h', 'GIMeshMaterialExtGPU')`. The
  test's `declaration()` (:673-683) brace-matches the struct text after stripping comments, and the
  `#ifdef` lines sit outside the braces, so a struct declared inside the define block is still found
  and reflected (32 bytes expected); confirm on the first run.
- The SSGI root blocks are explicit `UNTRACKED` inventory entries ("runtime/ssgi/ssgi_trace_cs.hlsl",
  "RootConstants" and the upsample's), so two more DWORDs under a define change no inventory line.
- CPU with the CVar off: `m_hitPerMesh` false -> the existing pipeline handles and push counts; the
  ext buffer is filled but never bound.

## 8. SDF-only hits: what they can and cannot get

- Who: the plain SDF gather (`ShadeSdfReflectionHit`, sdf_reflections.hlsli:239-299 - the engine
  default when `r.GI.FinalGather.DXR` is 0), the radiance-cache probe writer
  (radiance_cache_trace_cs.hlsl:767-790), the DXR gather's mode-2 control and its no-raw-attributes
  fallback (dxr_diffuse_gather.hlsli:37-46, :62-68). A voxel is the min over meshes of a distance with
  one folded reflectance (global_sdf_composite_cs.hlsl:113); no mesh id, no UV, no roughness.
- What they keep: the scene-constant roughness, F0 = 0.04 and the `(1 - metallic)`-weighted albedo -
  a metal is black plus the dielectric lobe; a dielectric is as in doc 06. Unchanged by this spec,
  byte-identical, and never over-lit.
- Consequence inside the DXR gather: its probe term at a metal hit is `E_spec * E/pi` (section 6),
  where `E` comes from RC probes whose own rays shaded metals black. In metal-rich scenes the probe
  term under-estimates the metal-to-metal transport (consistent sign: under, never amplified); the
  showcase ROIs measure it against the PT (9.3).
- Options for later, ranked by cost: (a) Lumen's view-independent fold at composite time,
  `reflectance = albedo * (1 - m) + 0.45 * lerp(0.04, albedo, m)` (06:185-200 Design C) - one line in
  `DiffuseReflectance` but it changes every SDF consumer's OFF output, so it needs a composite
  permutation and its own lane; (b) a second volume per cascade with the per-voxel mean F0 and
  roughness (doubles clipmap memory; the RC writer could then evaluate the real lobe); (c) a TLAS
  mode for the RC writer (a DXR variant of radiance_cache_trace_cs.hlsl with the material + ext
  tables) - the largest change, the only one that gives probes the exact material. None in pass one.

## 9. Validation protocol

### 9.1 Gates

- `zig build -Doptimize=ReleaseFast` exit 0; shader integrity count unchanged (runtime variants only).
- `npm run test:layouts` (package.json:21): the new Tier C `GIMeshMaterialExt` entry PASS at 32,
  `GIMeshMaterial` still 64, inventory unchanged.
- `npm test`.

### 9.2 OFF is byte-identical; HitSpecular-only is unchanged

- Stripped-DXIL SHA-256 of every pre-existing pipeline that reads a touched header (7.5): equal.
- `_evidence/gi/indirect-underlit/matrix.py` (matrix.py:16-50, `python matrix.py <names>` reruns
  named conditions :72-84): `rt_full rt_dxr1 rt_dxr_k0_nosky rt_d4` at the default CVars and the doc
  06 ON conditions `rt_dxr_k0_nosky_hs rt_dxr1_hs` (HitSpecular 1, PerMesh 0): every ROI equals the
  archived value to 5 decimals (the launch-to-launch floor, 06:554-560; the HitSpecular permutations'
  text is verbatim so their results are unchanged, not merely close).

### 9.3 ON acceptance - metals (new fixture `_evidence/gi/per-mesh-hit-material/`)

Harness: `_evidence/gi/radiance-cache-stable/capture.py` already parameterises the project and camera
(`run(..., project=, camera=)`, capture.py:45-58; `--camera-pos/--camera-look`, `--region-dump`,
`--cvar`) and holds fixture copies of the showcase scenes
(`_evidence/gi/radiance-cache-stable/fixtures/content/scenes/material_showcase.uzscene`). Copy its
`run()` into the new directory with a `CAMERAS["M"]` for the showcase and the PT ladder of
indirect-underlit/matrix.py:12, :55-69 (`pt_b1`, `pt_b2`, `pt_b8`, each with `NOSKY`, 300 frames;
real-time frames 90-150).

Primary fixture: `sample/projects/material_showcase.uzproject` - five spheres of radius 90 on y = 400
(Glass), 200 (Diffuse: base 1,1,1 roughness 1 metallic 0, material_showcase.uzscene:54-58), 0
(Chrome: 1,1,1 / 0.0 / 1.0, :81-85), -200 (DarkMetal: 0.4 / 0.4 / 1.0, :108-112), -400
(GreenPlastic: roughness 0.05 metallic 0, :138-139), all at z = 100 with a 20-unit gap between
neighbours; sun and sky only, no local lights (:168-227). Camera: side-on so the facing limbs are
visible (the scene's own MainCamera at (1022, -19, 140), :148-158, or a closer pose; record the pose
in `command.json` as the harness does). No scene edit is needed. ROIs (world-described; the pixel
rects are picked from a screenshot and recorded with the run):

| ROI | what it measures |
|---|---|
| `diffuse_vs_chrome` | the Diffuse sphere's limb facing Chrome: a dielectric receiver whose gather hits land on a metal with base colour 1 |
| `plastic_vs_darkmetal` | GreenPlastic's limb facing DarkMetal: hits on a dark, rough metal |
| `darkmetal_vs_chrome` | DarkMetal's limb facing Chrome: metal-to-metal (the probe-term coupling plus the sun lobe) |
| `chrome_vs_diffuse` | Chrome's limb facing Diffuse: hits on a dielectric - a control that moves only by the roughness swap (1.0 authored vs 0.7 constant) |
| `diffuse_lit` | the Diffuse sphere's sun-lit crown: direct lighting, unchanged to 5 decimals |
| `black` | an unlit background patch under NOSKY: zero control |

Conditions (sky off = the first-bounce ladder; sky on = the full indirect): `rt_dxr_k0_nosky`
(HitSpecular 0, PerMesh 0), `rt_dxr_k0_nosky_hs` (1, 0), `rt_dxr_k0_nosky_hs_pm` (1, 1),
`rt_dxr_k0_nosky_hs_pm_r02` / `_r07` (MinRoughness 0.2 / 0.7), and the same four on `rt_dxr1` (cache
on, sky on). Oracle: `pt_b2_nosky - pt_b1_nosky` for the `k0_nosky` set, `pt_b8 - pt_b1` for the
`dxr1` set (06:562-578's pairing).

| condition | compares against | accept |
|---|---|---|
| `rt_dxr_k0_nosky_hs_pm` | `pt_b2_nosky - pt_b1_nosky` | on `diffuse_vs_chrome`, `plastic_vs_darkmetal`, `darkmetal_vs_chrome`: `abs(ON - PT) <= 0.6 * abs(HS - PT)` (the gap to the oracle closes by at least 40%; Chrome's invented white bounce is expected to be the largest OFF error and to DROP under ON) and `ON / PT` in [0.6, 1.25]; no ROI above `1.25 * PT` |
| `rt_dxr_k0_nosky_hs_pm` | `rt_dxr_k0_nosky_hs` | `chrome_vs_diffuse` within [0.95, 1.08] (the dielectric hit's roughness goes 0.7 -> 1.0 and the probe term gains at most 3%); `diffuse_lit` and `black` equal to 5 decimals |
| `rt_dxr1_hs_pm` | `pt_b8 - pt_b1` | the three metal-hit ROIs closer to the PT than `rt_dxr1_hs`; `chrome_vs_diffuse` within [0.95, 1.08] of `rt_dxr1_hs` |
| MinRoughness sweep 0.2 / 0.4 / 0.7 | each other | the three metal-hit ROI means within +-10% across the sweep (the floor spreads the glint at equal energy; a trend larger than that means the cap is clipping energy at the low end) and per-ray p99.9 (the `gather` fields run, 9.4) monotone in the floor; the default stays 0.4 if it passes 9.4, else record 0.7 |

Secondary fixture: `sample/projects/pbr_showcase.uzproject` - a checker `GroundPlane` (roughness 1,
pbr_showcase.uzscene:37-64) under a textured corroded-metal sphere and cube with separate
albedo/roughness/metallic maps (`Sphere_Metal` :200-224, `Cube_Metal_Center` :120-144; no ORM
packing). Make a fixture copy with the four `PointLight_*` entities (:307-394) removed - local lights
reach hits only through the surface cache (06:206-208), so the sun-only ladder needs them out of the
PT too - and keep `CubeRotator` dormant (`--editor`, capture.py:47-48). ROIs: the floor ring one
sphere-radius wide around `Sphere_Metal`'s contact (the metal's bounce footprint, large enough to
integrate the spread glint) and the floor strip beside `Cube_Metal_Center`; a floor patch beside
`Sphere_Stone` as the dielectric control. Same conditions and the same two gates as above: the metal
footprints move closer to the PT than `_hs`; the stone control within [0.95, 1.08] of `_hs`.

Sponza non-regression (`indirect-underlit/matrix.py`, Camera_A, 14 ROIs, no metals): add
`rt_dxr_k0_nosky_hs_pm`, `rt_dxr1_hs_pm` and `rt_dxr_k0_nosky_hs_wrap_pm` (WrapUv 1). Accept:
aggregate ratio to the PT sun-only first bounce within [0.97, 1.05] of the `_hs` run (per-mesh
roughness 0.6-0.8 replaces the 0.7 constant; the probe term is off in `k0`), per-ROI ratio spread
`<= 0.244 + 0.02` (doc 06 section 12's HS value); `rt_dxr1_hs_pm` within [0.98, 1.05] of `rt_dxr1_hs`
on every ROI (the +1-3% coupling); `lit_floor` direct unchanged.

### 9.4 Variance gate (both scenes)

`rt_dxr1_hs_pm_g` and `rt_dxr1_hs_g` with the `gather` fields (per-ray records,
sdf_diffuse_gather.hlsli:119-122 per 06:571): per-ray luminance p99.9 ratio PM/HS `<= 1.5` and max
ratio `<= 3` over all ROIs x frames; per-ROI temporal std of `diffuse_indirect` `<= 1.2x` HS; the
count of isolated pixels `> 8x` the ROI mean not increased (the H-68 sparkle precedent, 06:571).
At MinRoughness 0.4; if a metal ROI fails, the sweep row of 9.3 decides between 0.4 and 0.7.

Interpretation guard: the oracle's metal glints are near-mirror at Chrome's authored roughness 0 (the
PT floors at 0.045, path_tracer_ps.hlsl:325) while the gather's floor is 0.4, so a per-pixel
comparison inside a glint footprint is meaningless; the ROIs are sized to integrate the footprint,
and the energy (ROI mean) is what the gates compare.

## 10. Risks

1. Fireflies from low-roughness metals. One hit returns at most `min(D*Vis*NoL, 1) * F * E_sun *
   visibility <= E_sun` (the existing cap, gi_hit_specular.hlsli:38, :67-69), the same formal bound
   as today - but with F0 up to 1 the typical glint hit is 0.9 x `dv` instead of 0.04 x `dv`, up to ~3x
   a white Lambertian's `E_sun / pi`. The `MinRoughness` floor (0.4) keeps `dv` below the cap except in
   the grazing-mirror sliver (06 section 12); the stable temporal accumulator and the 8-ray mean do the
   rest; 9.4 is the gate and 0.7 the fallback (cap no-op). No new random dimension: the lobe is a
   function of the ray already traced.
2. HitSpecular interaction: PerMesh is a superset permutation (the `#error` guard, 5.2); either
   toggle invalidates the gather history (7.4); the ReSTIR exclusion is inherited (7.2). The
   HitSpecular-only path's text is verbatim, so doc 06's archived numbers hold under PerMesh 0 (9.2).
3. UV wrap: the roughness/metallic/ORM taps use the albedo tap's sampler macro and `safeUv` (5.1);
   the WrapUv x PerMesh variants are in the table (7.2) and `rt_dxr_k0_nosky_hs_wrap_pm` exercises
   them. Without this, a tiled ORM map would read an edge texel for roughness - doc 07's defect class.
4. RoughReconstruct: `r.RayTracing.Reflections.RoughReconstruct` reconstructs the RT reflection pass,
   whose hit shading is untouched here (06:497-502); the gather output is not its input. No
   interaction. (When RT reflections later get per-mesh metals at hits, section 11, its input changes
   and that lane re-validates it.)
5. SDF-only hit fallback quality: section 8 - metals stay black + 0.04 on the plain SDF gather and
   inside the RC probes; the DXR gather's metal probe term under-estimates in metal-rich scenes,
   never over. Mixed-path inconsistency bounded by `E_spec` of the metal (the term that is missing).
6. Graph materials with math between a texture and the PBR output: the resolved metadata is an
   approximation (scene_sync.cpp:568-574) - identical to what the SDF composite and the pipeline PT
   read, so the oracle and the gather agree with each other even where both differ from the G-buffer.
7. The pipeline PT's ORM convention (2.6 caveat): ORM-packed MaterialInterface materials are
   mis-read by the oracle itself (metallic = multiplier). The fixtures avoid ORM packing; a scene that
   uses it must not be used as an oracle until the PT lane fixes :439.
8. Shading models: Unlit and ThinTranslucent hits are shaded as opaque DefaultLit by the GI table
   today (Sphere_Glass, material_showcase.uzscene:6-33, is an opaque white Lambertian at a hit) and
   still are; `materialFlags` in the ext record (bits 24-27) makes a shading-model-aware hit a later
   zero-ABI change (section 11).
9. Before the IBL bake (LUT sentinel, a few frames) the identity path drops a metal's probe term to
   zero where today it is the invented Lambert; transient, and those frames have no lobe either. The
   split early-out of section 6 keeps the common "sun below the hit's horizon" case out of this path.
10. Record/ABI: no change to `GIMeshMaterialGPU`, `RadianceCacheTraceConstants`, PerFrameCB or any
    tracked cbuffer; one new 32-byte structured record with a Tier C entry; two root DWORDs under a
    define (24-DWORD budget: 20 used by the trace).

## 11. Out of scope / follow-ups (ranked)

1. The TLAS writers that still shade a metal as a base-colour Lambertian: the surface-cache feed /
   indirect (surface_cache_hit.hlsli:33-47), the DDGI probe writer (ddgi_probe_common.hlsli:270) and
   RT reflections (rt_reflection_cs.hlsl:328-349). Their caches are view-independent, so the right
   form there is `(1 - metallic) * albedo + 0.45 * F0` (Lumen's fold, 06:185-200), not the directional
   lobe; the ext table is already built for every mesh and needs only a root DWORD per writer.
2. SDF composite: section 8 options (a)-(c), so the RC writer and the plain SDF gather stop shading
   metals black.
3. Pipeline PT ORM convention (pipeline_path_tracer_pass.cpp:439 should use `aoSrvIndex` when bit 2
   is set) - oracle hygiene.
4. Shading-model-aware hits from `materialFlags` (Unlit = emission only; glass = continue the ray).
5. Collapse `EvalGiHitMaterialSpecular` into `EvalGiHitSunSpecular` once PerMesh is default-on (6).
6. ReSTIR GI variants (06:521-528).
7. Normal maps at the hit: neither the PT closest hit nor the gather applies them (parity today); a
   joint change if ever.

## 12. One-pass checklist for the implementation lane

1. path_tracer_pass.h: `GIMeshMaterialExtGPU` + static_asserts (4.1).
2. scene_renderer_impl.h: ext buffer members + accessor; frame_lighting.cpp `UpdateGIMaterialTable`:
   create/zero/fill/unmap the ext table in the existing loop (4.2); destroy it where the primary is.
3. ssgi_renderer.h: `SSGIRenderParams::gatherMaterialExtSrv`, `m_hitPerMesh`,
   `m_loggedHitPerMeshFallback`, the two statics; frame_gi_passes.cpp:158-197: bind the ext SRV (4.3).
4. gi_hit_material.hlsli: the `UZ_GI_HIT_PERMESH` block - `GIMeshMaterialExt`, the sampler macro,
   `GITrace_HitRoughnessMetallic` (5.1).
5. gi_hit_specular.hlsli: `GiHitMaterialSpecular` + `EvalGiHitMaterialSpecular` under the define with
   the split early-outs (6).
6. sdf_reflections.hlsli: the two scene fields under the define (7.3); ssgi_world_gather.hlsli:
   macro defaults + `BuildSurfaceScene` fields (7.3).
7. dxr_diffuse_gather.hlsli: the `#error` guard, the per-hit read in the table-hit branch, the nested
   shading block with the verbatim `#else` (5.2, 6).
8. ssgi_trace_cs.hlsl / ssgi_upsample_cs.hlsl: the two root DWORDs + the `#define`s before the
   include (7.3).
9. ssgi_renderer.cpp: the two CVars + statics (7.1); `SSGITraceRC` / `UpsampleRC` + push counts;
   the effective-state block, history invalidation, fallback log; pipeline choice in both dispatches
   (7.3, 7.4).
10. pipeline_gi.cpp / pipeline_library.h / pipeline_library_impl.h / pipeline_lifecycle.cpp: the
    four-entry table, enum, accessor, handles, destruction (7.2).
11. test_cbuffer_layouts.py: the Tier C entry (7.5).
12. Run 9.1, 9.2, 9.3 (both showcase fixtures + the Sponza conditions), 9.4; record the chosen
    `MinRoughness` and the measured ratios in a section 13 of this document.
