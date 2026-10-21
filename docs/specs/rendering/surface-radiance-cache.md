# Surface Radiance Cache - Design Spec (Lumen-parity track, the "surface cache" gap)

Status: **DESIGN / DECISION-COMPLETE, READY TO IMPLEMENT (stage 1)**. Author: autonomous lighting
series, 2026-10-21. Scope owner: whoever picks up the next GI-quality slice after the RT-reflections
stages 1-3 (`docs/specs/rendering/rt-reflections.md`).

This spec is cross-checked against the current tree (branch `feat/phase1-advanced-rendering`,
HEAD of 2026-10-21). Every file and symbol named below exists unless it is marked **new**. No code,
build or capture was produced for this document; it is a read-only design.

---

## 0. The gap, stated against the code as it is today

Lumen's Surface Cache is a persistent, per-mesh "card" atlas that stores lit material
(albedo, normal, emissive, direct lighting, indirect lighting, final radiance) so that a GI or
reflection ray hitting **off-screen** geometry reads detailed, temporally stable radiance in one
texture tap, and so that lighting and multi-bounce can be computed once per card texel and amortised.

What UzEngine does at an off-screen ray hit today (all verified in-tree):

| Consumer | Visibility | Hit material | Hit lighting | Where |
|---|---|---|---|---|
| HWRT reflections (`r.RayTracing.Reflections`, **default on** since Phase B) | TLAS `RayQuery` | **exact**: triangle UV albedo + interpolated vertex normal from the per-mesh `GIMeshMaterial` table (`gi_trace_common.hlsli::GITrace_HitSurface`), GDF fallback | sun x shadow ray (`ReflectionHitRadiance`) + radiance-cache L2 SH `E/pi` at the hit (`ReflectionProbeIrradianceOverPi`, `r.RayTracing.Reflections.Indirect`, default on) | `runtime/shaders/runtime/reflections/rt_reflection_cs.hlsl` |
| DDGI probe rays (`r.RayTracing` + DDGI) | TLAS `RayQuery` | GDF gradient normal + GDF per-mesh-average albedo (GBA) | sun x shadow ray + previous-frame DDGI irradiance at the hit | `gi/ddgi_probe_common.hlsli::TraceProbeRay`, `ddgi_probe_trace_cs.hlsl` |
| Radiance cache probes (SDF writer, the Sponza default) | GDF sphere trace (`TraceGDF`) | GDF normal + GDF average albedo | sun x GDF shadow trace + previous SH (`SamplePreviousDiffuse`) | `gi/radiance_cache_trace_cs.hlsl` |
| SDF final gather / ReSTIR GI / SDF reflections | GDF trace with the expanded-surface tracer (`TraceSdfRay`) | GDF normal + GDF average albedo | sun x GDF shadow trace + SH `E/pi` (`SdfReflectionIrradiance`) | `gi/sdf_reflections.hlsli::ShadeSdfReflectionHit`, used by `sdf_diffuse_gather.hlsli`, `ssgi/ssgi_restir_gi.hlsli`, `lighting/deferred_lighting_ps.hlsl:1226-1258` |
| DXR path tracer (the reference) | DXR pipeline | exact (same table) + light BVH NEE | full | `passes/pathtracing/` - **never reads any cache** |

So the engine already runs what UE calls Lumen's *hit-lighting* mode for its TLAS consumers
(materials evaluated inline at the hit - the BlackSpace/Crimson Desert pattern, see
`docs/analysis/lighting/crimson-desert/00-blackspace-engine.md`). The fidelity gap versus a surface
cache is therefore **not** material detail. It is:

1. **Lighting completeness at the hit.** Only the sun is evaluated. The frame's point/spot/rect
   lights (`LightingCB`, up to 512/128/16, `lighting/light_types.hlsli`) and mesh emissive
   (`DrawCommand::emissive*`, the path tracer's `LightBVHLight`) never reach a reflected or
   bounced surface. `docs/analysis/known-issues-and-partial-implementations.md:439` records this.
2. **Indirect detail at the hit.** The only indirect term is the 16^3 L2-SH grid
   (`RadianceCache::kProbesPerAxis`, spacing `2*extent/15` = 267 cm on Sponza, 667 cm at the default
   5000 cm extent): one cosine lobe per 2.7-6.7 m cell. A reflected wall in shadow is lit by one
   number.
3. **Material detail on the SDF side.** Every GDF consumer shades with the clipmap's GBA, which is
   the *nearest mesh's average* reflectance at 78 cm voxels (`GlobalDistanceField` 64^3 x 2500 cm),
   and the gradient normal. The SDF radiance cache, the final gather and ReSTIR GI never see a
   texture.
4. **No amortisation.** Every ray re-shades its hit (shadow ray + 8 probe reads); nothing is
   remembered between rays that hit the same patch, so glossy reflections and the final gather pay
   full cost and full variance per sample.

Closing 1-4 is what "surface cache parity" means for this engine. The rest of this document decides
how.

---

## 1. Approach decision

Three candidates were weighed against fidelity, memory, build/update cost, integration effort with
the existing SDF + radiance-cache + inline-RayQuery hit shading, and reuse of what already ships.

### 1.1 (a) Lumen-style card surface cache

Per-mesh orthographic cards (6+ directions), a physical-page atlas (albedo / normal / emissive /
depth / direct / indirect / final), a capture pass that rasterises meshes into cards, a direct
lighting pass over card texels, a radiosity pass for indirect, page streaming by feedback, and at a
hit: mesh id -> card -> project -> tap.

- Fidelity: card texel density (Lumen default ~1 texel / 2-4 cm near, far coarser) - **lower** than
  the per-texel material the TLAS consumers already evaluate. Direct shadows at card resolution.
- Memory: 4096^2 x 7 layers ~ 300-400 MB in UE; even a 2048^2 atlas is ~100 MB.
- Build/update: an offline/at-load card generator (ray-march the mesh SDF per direction), a card
  capture render path that must run every material (including material-graph PSOs,
  `DrawCommand::materialGraphPipeline`) under an orthographic camera into MRT card pages, an
  eviction scheduler. `docs/analysis/lighting/lumen/01-surface-cache.md` section 7 estimated this at
  3+ months and recommended against it.
- Integration: **a GDF hit has no mesh identity.** `global_sdf_composite_cs.hlsl` stores a min
  distance and the nearest mesh's average albedo; there is no mesh index channel, and the
  per-mesh SDFs are not traced individually at runtime. The SDF radiance cache, the final gather,
  ReSTIR GI and the SDF reflections (items 3 above) therefore cannot address a card at all without
  either adding a mesh-id channel to the composite (wrong at contacts at 78 cm voxels) or switching
  them to per-mesh SDF tracing (a different GI). Only the TLAS consumers could use cards, and they
  are the ones that need them least. Per-instance lighting also means per-instance pages: the
  material table is per `meshId` (`UpdateGIMaterialTable`, `tlas_manager.cpp:103` packs `cmd.meshId`
  as InstanceID), so cards would need a new instance-level identity on top.

Verdict: highest effort, worst fit, and it would *reduce* material fidelity on the TLAS path.
Rejected.

### 1.2 (b) Enriched radiance cache (world-space hash-grid surface radiance cache)

A world-space **spatial hash** keyed by (quantised hit position at a camera-distance level of
detail, surface-normal direction) whose entries accumulate the **outgoing diffuse radiance** that the
existing hit shading already computes, temporally resolved per frame, read back by any consumer that
lands in the same cell. This is the SHaRC design (NVIDIA RTXGI 2.0, 2024, after Binder/Fricke/Keller
2018 jittered spatial hashing): population is lazy and ray-driven, so there is no capture pipeline,
no atlas, no mesh identity, and no offline step.

- Fidelity: cell size 10 cm near the camera (CVar), doubling with distance; the stored value is the
  exact-material inline shading averaged over the cell and over time (hundreds of samples), i.e.
  the same estimator with far lower variance. Position-keyed, so GDF hits and TLAS hits share cells.
- Memory: 2^20 entries x 56 B = 56 MB (section 2); nothing allocated when off.
- Update: one resolve dispatch per frame (~0.1 ms over 1M entries); per hit, one hash + <= 8 probes
  + 7 atomics on the writer side, one hash + probe on the reader side.
- Integration: a single `SurfaceCacheQuery/Update` pair dropped into the three existing hit-shading
  functions (`rt_reflection_cs.hlsl` main, `TraceProbeRay`, `ShadeSdfReflectionHit`) and the
  radiance-cache trace; one root DWORD per consumer; the existing change-box invalidation
  (`GlobalDistanceField::GetChangedRegions`) reused as-is.
- Weakness: a purely lazy cache relights only where rays go. A light that changes (time of day, a
  switched lamp) is seen by a cell only when rays revisit it, and the cheap answer - clear the
  cache - throws away the multi-bounce history each time. Items 1 (local lights, emissive) are
  solved only in the sense that the writers can include them; nothing proactively re-lights.

### 1.3 (c) Hybrid: hash-grid storage + Lumen's separated lighting layers and scheduled relight

(b)'s storage and lazy population, plus what makes Lumen's cache robust: the cell keeps its
**material** (albedo, normal, mean position, emissive) and **two lighting layers** (direct, rest)
separately, so a scheduled **relight pass** can walk the live cells in slices and recompute direct
lighting (sun + local lights, shadow rays on the TLAS) without any ray having to revisit the cell,
and a **cell-indirect pass** can trace a few cosine rays from live cells into the cache itself for
true multi-bounce feedback at cell resolution. Cards without cards.

- Fidelity: (b) plus immediate direct-light response and multi-bounce that does not depend on
  consumer rays.
- Memory: (b) + 32 B/entry surface record = 88 MB at 2^20.
- Update: (b) + relight of 1/16 of live cells per frame (~20k cells x (1 + K) shadow rays) +
  cell-indirect rays (~20k x 4) - under 0.2 ms of RayQuery on the RTX 5080 at Sponza occupancy.
- Integration: identical to (b) at the consumers; the extra passes are self-contained.

### 1.4 Recommendation: **(c), staged so that stage 1 is exactly (b)**

**Recommended: the hybrid hash-grid surface radiance cache.** It keeps the one thing this engine
already does better than Lumen (exact material at TLAS hits), it is the only option the GDF
consumers can address (position-keyed, no mesh identity needed), it reuses the committed
infrastructure wholesale (the b2 trace constants, `GITrace_HitSurface`, the material table, the
TLAS, the change boxes, the SH cache as fallback), it allocates nothing when off, and its first
increment is small enough to prove with the series' existing harness (`test_rt_reflections_stage1.py`
shape: default byte-identical + path-tracer RMSE + an ROI metric). Pure (b) was rejected as the end
state because a lazy cache cannot answer "the sun moved" without discarding its history, which the
Sponza tour and any time-of-day scene will do continuously; (a) was rejected for the reasons in 1.1.
The stages are ordered so that each one is attributable: stage 1 proves the cache reproduces the
inline estimator (an identity proof), stage 2 adds the missing light terms (the first visible
fidelity gain, measured against the path tracer), stage 3 lets the SDF consumers read exact-material
radiance (the diffuse-GI gain), stage 4 adds the relight and cell-indirect passes (responsiveness and
multi-bounce). The path tracer never reads the cache at any stage; it stays the ground truth.

### 1.5 Decisions (binding for the implementation lane)

| # | Decision | Value |
|---|---|---|
| D1 | Storage | World-space spatial hash, open addressing, linear probing (<= 8), 64-bit verified keys stored as `uint2` |
| D2 | Key | level (4 bits, from camera distance) + normal direction (3 bits, 6 axis directions) + signed cell coords (18 bits per axis) + a valid bit; cell size = `CellSize * 2^level` |
| D3 | Level rule | `level = clamp(floor(log2(max(dist(hit, cameraPos) / LodDistance, 1))), 0, 15)` - the **camera** distance for every writer and reader, never the ray origin's |
| D4 | Stored quantity | Outgoing diffuse radiance `L_o` of the hit (albedo included, linear scene radiance, the units `ReflectionHitRadiance` returns), split into two layers: `sun` (sun direct) and `other` (local lights + emissive + indirect). Not `E/pi`. |
| D5 | Accumulation | Per-frame fixed-point `uint` atomics (scale 2^10, per-sample clamp `MaxRadiance`, per-cell per-frame cap 1024 samples); a resolve pass folds the frame's sum into a per-frame EMA (`AccumFrames`, default 32) and clears the accumulators |
| D6 | Validity | A cell is readable when `samples >= MinSamples` (default 8); otherwise the consumer shades inline exactly as today and writes the result |
| D7 | Reader policy | `DirectMode 0`: `L = sun + other` from the cell (Lumen's choice, cheapest); `DirectMode 1`: inline sun + shadow ray, `other` from the cell (sharp sun shadows, costs the shadow ray) |
| D8 | SDF consumers | Read-only by default (`SdfWrites 0`): their per-mesh-average albedo must not be averaged into cells the TLAS writers fill; hits are snapped to the raw surface before the lookup and use `SdfLodBias` coarser levels |
| D9 | Population | Lazy from the consumers' own rays, plus (stage 3) a dedicated feed trace on the TLAS with exact material; (stage 4) scheduled relight + cell-indirect |
| D10 | Binding | One `StructuredBuffer<SurfaceCacheParams>` (1 element, 64 B) whose SRV index is the only new root DWORD per consumer; sentinel = off. The shared b2 `RadianceCacheTraceConstants` (464 B, prefix-declared by three shader families) is **not** changed |
| D11 | Invalidation | GDF change boxes evict cells inside them (resolve pass); scene revision / TLAS instance hash change with no boxes -> clear; lighting hash change -> stage 1-3 clear, stage 4 relight |
| D12 | Gating | `r.GI.SurfaceCache` bool, default 0; OFF = nothing allocated, no pass registered, consumer flags clear, frame byte-identical |
| D13 | Reference | The DXR path tracer never binds the cache; all fidelity claims are RMSE vs its converged output |

---

## 2. Data structures, memory budget, build and update

### 2.1 GPU buffers (owned by **new** `runtime/engine/renderer/gi/surface_radiance_cache.{h,cpp}`, class `SurfaceRadianceCache`)

All sizes at the default `r.GI.SurfaceCache.EntriesLog2 = 20` (1,048,576 entries; range 16..22).

| Buffer | Element | Bytes/entry | Total | Access |
|---|---|---|---|---|
| `keys` | `uint2` (key lo, key hi; `0,0` = empty) | 8 | 8 MB | UAV (CAS insert, plain read) |
| `accum` | `uint[8]`: sun R,G,B, other R,G,B, sampleCount, writerTags | 32 | 32 MB | UAV (`InterlockedAdd`) |
| `resolved` | `uint4`: sun RGB fp16 (3 halves), other RGB fp16 (3 halves), `samples` u16, `frames` u8 \| `age` u8 | 16 | 16 MB | UAV in the resolve pass, SRV in consumers |
| `surface` (stage 4) | position fp32 x3, normal oct u32, albedo RGB8 + writerTag u8, emissive RGB fp16, pad | 32 | 32 MB | UAV (last writer wins), read by relight / cell-indirect |
| `params` | `SurfaceCacheParams` (section 2.2) | 64 | 64 B | SRV (one element, rewritten per frame via the CB ring or a tiny upload buffer) |
| `stats` | `uint[8]`: inserts, failedInserts, updates, queries, hits, evictions, live, maxProbe | 32 | 32 B | UAV + readback (the `RadianceCache::QueueDiagnosticReadback` pattern, 2 frames late) |

Stage 1 footprint: **56 MB**; stage 4: **88 MB**. For scale: the GDF is 4 x 64^3 x (8 + 2) B =
10.5 MB, the SH cache 0.45 MB, a 1080p RGBA16F target 16 MB. The cap at `EntriesLog2 = 22` is
224 MB / 352 MB and exists for large worlds; Sponza needs ~200-300k live cells (floor + walls
~2000 m^2 at 10-40 cm cells x up to 6 normal directions x 2-3 levels in view), load factor 0.2-0.3.
Every buffer is created on the first frame the CVar is on (the `SSGIRenderer::EnsureRestirResources`
pattern - OFF allocates nothing) and released in `DestroyResources()` (D3D12MA asserts on leaked
allocations at exit, exit code 127).

### 2.2 The shared contract (**new** `runtime/shaders/runtime/gi/surface_cache_contract.hlsli`)

Scalar C++/HLSL dual-compiled header exactly like `reflections/rt_reflection_contract.hlsli`
(`#ifdef __cplusplus` namespace `uz::surfacecache`), so the gtest evaluates the same expressions the
shaders run:

```
static const uint  kSurfaceCacheLevels     = 16;    // 4 bits
static const uint  kSurfaceCacheCoordBits  = 18;    // signed per axis: +-131072 cells (+-13 km at 10 cm)
static const uint  kSurfaceCacheProbeSteps = 8;
static const float kSurfaceCacheScale      = 1024.0; // fixed-point accumulator scale (2^10)
static const uint  kSurfaceCacheFrameCap   = 1024;   // samples per cell per frame (overflow-proof with MaxRadiance 4096)

uint  SurfaceCacheLevel(float cameraDistance, float lodDistance);
float SurfaceCacheCellSize(float cellSize, uint level);
uint  SurfaceCacheNormalIndex(float3 n);                       // 0..5: +-X, +-Y, +-Z by argmax |n|
uint2 SurfaceCacheKey(int3 cell, uint normalIndex, uint level); // valid bit set; never (0,0)
uint  SurfaceCacheSlot(uint2 key, uint entriesLog2);           // 64->32 mix (xxhash32 of both words)
uint  SurfaceCachePackSample(float v, float maxRadiance);      // clamp, scale, round
uint4 SurfaceCacheResolveRecord(...);                           // packing of the resolved record

struct SurfaceCacheParams {          // 64 bytes, 1-element StructuredBuffer; mirrored by uz::SurfaceCacheParamsGPU
    uint  keysUav, accumUav, resolvedSrv, surfaceUav;   // surfaceUav = sentinel before stage 4
    uint  entriesLog2, minSamples, flags, frame;        // flags: kSurfaceCacheFlag* (DirectMode, Jitter, Debug bits, SdfWrites)
    float cellSize, lodDistance, maxRadiance, _pad0;
    uint  sdfLodBias, statsUav, generation, _pad1;
};
```

Key layout (the lane pins the bit positions in the header and the gtest): `lo` = valid bit (31) |
x (18) | y low 13; `hi` = y high 5 | z (18) | normal (3) | level (4) | 2 spare. Both words are
non-zero by construction (the valid bit in `lo`; `hi` gets bit 31 set as a second valid bit).

### 2.3 The HLSL API (**new** `runtime/shaders/runtime/gi/surface_cache.hlsli`)

```
// Position jitter of +-0.5 cell seeded by (cell coords, params.frame) only - deterministic within
// a frame so two evaluations of the same ray agree (the ReSTIR same-surface shortcut,
// ssgi_restir_gi.hlsli::SsgiRestirSameSurface, assumes the trace is deterministic).
bool SurfaceCacheQuery(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                       out float3 sun, out float3 other);
void SurfaceCacheUpdate(SurfaceCacheParams p, float3 pos, float3 n, float3 cameraPos, uint lodBias,
                        float3 sun, float3 other, uint writerTag);
// Stage 4: also records position / normal / albedo / emissive into the surface record.
void SurfaceCacheUpdateSurface(SurfaceCacheParams p, ..., float3 albedo, float3 emissive);
```

Insertion (portable, 32-bit atomics only): compute `slot`; for `i < kSurfaceCacheProbeSteps`:
read `keys[s]`; if equal -> found; if `(0,0)` -> `InterlockedCompareExchange(keys[s].x, 0, key.x)`;
on success store `keys[s].y = key.y` and proceed (a reader that sees `x` match and `y` unwritten
treats the slot as foreign and keeps probing; the torn window is one frame and only wastes a slot);
on failure re-read and compare. After 8 steps: `stats.failedInserts++`, skip the update. Readers
stop at the first empty slot. If the RHI reports
`D3D12_FEATURE_D3D12_OPTIONS11::AtomicInt64OnDescriptorHeapResourceSupported`, a 64-bit CAS on a
`uint64_t` key buffer is an allowed optimisation, never a requirement.

Update: `InterlockedAdd` x 7 (six channels, scaled and clamped by `SurfaceCachePackSample`, plus the
count) on `accum[s]`, skipped when the cell's per-frame count already reached `kSurfaceCacheFrameCap`
(one load before the atomics). With `MaxRadiance` 4096, scale 2^10 and the cap 1024 the per-frame
channel sum is at most 2^32 - overflow-proof by construction.

### 2.4 The per-frame pipeline

```
RTManager::BeginFrame (TLAS)            existing
GDF composite (CompositeFromMeshSDFs)   existing - produces GetChangedRegions()
SurfaceCache_Resolve  [NEW]             resolve last frame's accum -> resolved; evict; clear; stats
RadianceCache_Trace / DDGI trace+update existing - stage 3: read + (DDGI) write the cache
SurfaceCache_Feed     [NEW, stage 3]    TLAS rays from the probe grid, exact material, write
SurfaceCache_Relight  [NEW, stage 4]    1/RelightSlices of live cells: sun + local lights, shadow rays
SurfaceCache_Indirect [NEW, stage 4]    IndirectRays cosine rays per live cell -> cache/SH -> other layer
SSGI gather / ReSTIR GI                 existing - stage 3: read the cache in ShadeSdfReflectionHit
SSR chain / RT_Reflections              existing - stage 1: read + write the cache
Deferred lighting (SDF reflections)     existing - stage 3: read the cache in ShadeSdfReflectionHit
```

`SurfaceCache_Resolve` (**new** `runtime/shaders/runtime/gi/surface_cache_resolve_cs.hlsl`, one
thread per entry, 64 per group, `entries / 64` groups; pipeline `PipelineLibrary::GetSurfaceCacheResolve()`
registered in `core/pipeline/pipeline_gi.cpp` next to `GetRtReflection`):

1. Empty key -> return.
2. Reconstruct the cell centre from the key (level + coords); if inside one of this frame's
   change boxes (`GlobalDistanceField::GetChangedRegions()`, up to 16 as `SSGIRestirHistory` carries
   them, uploaded in the pass's b2 block) -> evict (`keys = 0`, `resolved = 0`, `stats.evictions++`).
3. `k = accum.sampleCount`. If `k > 0`: `frames = min(frames + 1, AccumFrames)`,
   `alpha = 1 / frames`, `mean = lerp(mean, sum / k, alpha)` per layer (unscale by 2^10),
   `samples = min(samples + k, 65535)`, `age = 0`. Else `age++`; if `age >= StaleFrames` -> evict.
4. Clear `accum[entry]` (8 zeros). Count `stats.live`.

The resolve runs **before** the frame's consumers so they read the freshest mean (one frame of
latency, the same as `GetCompletedGridOrigin()`'s rule for the SH buffer). Within the frame, every
consumer pass that touches `keys`/`accum` must issue a UAV barrier before its dispatch (the buffers
are not render-graph resources; go through `RgStateRegistry::Transition` exactly as the RT
reflection pass does for the SSR textures, and remember the render-graph lesson that undeclared
reads mis-order passes).

### 2.5 Writers and readers per stage

| Stage | Writers (what they put in `sun` / `other`) | Readers |
|---|---|---|
| 1 | `rt_reflection_cs.hlsl`: `sun = albedo * sunRad / pi`, `other = albedo * E_SH/pi` (the two terms of `ReflectionHitRadiance`) | `rt_reflection_cs.hlsl` |
| 2 | + local lights into `sun` (K nearest point/spot/rect with shadow rays), emissive into `other` | same |
| 3 | + `SurfaceCache_Feed` (probe-grid rays on the TLAS, exact material); + DDGI `ddgi_probe_trace_cs.hlsl`; SDF consumers only if `SdfWrites 1` | + `ShadeSdfReflectionHit` (final gather, ReSTIR GI, SDF reflections), `radiance_cache_trace_cs.hlsl`, DDGI trace |
| 4 | + `SurfaceCache_Relight` rewrites `sun` of live cells directly (no accumulation); + `SurfaceCache_Indirect` EMA-writes the indirect part of `other` (emissive kept apart in the surface record) | unchanged |

Energy rule for every reader: a valid cell replaces the **whole** hit radiance (`sun + other` or,
in `DirectMode 1`, inline sun + `other`). Never add the SH term on top of a cached `other` - the
cell already contains it. Misses fall through to today's inline path, which also performs the
update, so a cold cache is never worse than the current frame.

### 2.6 Invalidation

| Event | Detection (existing) | Action |
|---|---|---|
| Mesh moved / appeared / material changed | `GlobalDistanceField::GetChangedRegions()` (block B.2 per-entity diff) | Evict cells inside the boxes in the resolve pass (step 2) |
| Whole-window rewrite / scene change | `GlobalDistanceField::GetSceneRevision()` changed | `InvalidateAll()` (clear dispatch over `keys` + `resolved` + `accum`) |
| Geometry change with the GDF off | `TLASManager` per-frame instance hash changed and no boxes available | `InvalidateAll()` |
| Lights changed | A lighting hash computed on the CPU each frame: sun dir/colour (`PerFrameCB::lightDir/lightColor`), skylight (`skyLightColor*`, `skyLightIntensity`), the point/spot/rect arrays of `LightingCB` (hash the bytes) | Stages 1-3: `InvalidateAll()` + `stats` note (a continuously animated sun defeats stages 1-3 and falls back to inline every frame - accepted, see 5); stage 4: bump `generation` only for the `sun` layer and let the relight catch up |
| Camera cut | nothing | nothing (world space; far cells age out) |
| `r.GI.SurfaceCache` toggled, `EntriesLog2`/`CellSize`/`LodDistance` changed | CVar poll | Recreate / `InvalidateAll()` |

---

## 3. Integration points (real symbols), gating and the byte-identical discipline

### 3.1 Where each consumer reads and writes

| Consumer | File / function | Change | Binding of the params SRV |
|---|---|---|---|
| HWRT reflections (stage 1) | `rt_reflection_cs.hlsl` main, after the attributes are resolved and before the sun block | `if (flags & kReflectionFlagSurfaceCache)`: query at `hitP + hitN * kReflectionSurfaceBias`, `hitN`, `cameraPos.xyz`; valid -> `radiance = sun + other` (or inline sun + `other`); miss -> current path, then `SurfaceCacheUpdate(...)` with the two terms | **new** `RTReflectionRootConstants::surfaceCacheParamsSrv` = DWORD 16 (`rt_reflection_pass.h`; block 16 -> 17 DWORDs, `static_assert <= 20` holds); flag `kReflectionFlagSurfaceCache = 16u` in `rt_reflection_contract.hlsli` + the matching `kRTReflectionFlagSurfaceCache` enum (the gtest pins them equal) |
| Local lights at the hit (stage 2) | same | K nearest point/spot/rect lights in range, each with a `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH` shadow ray, Lambert only; emissive from the table | the frame's lights as a `StructuredBuffer` (the `rectLightsSrv` precedent in `LightingCB`): **new** `SurfaceCacheParams::lightsSrv` + counts packed in the params record (no root DWORD) |
| Emissive at the hit (stage 2) | `gi_trace_common.hlsli::GIMeshMaterial` + `path_tracer_pass.h::GIMeshMaterialGPU` | Append `emissiveSrv` and `emissive RGB` (48 -> 64 B, both mirrors + static_asserts; the path tracer ignores the new fields and must stay byte-identical on `--path-trace`) | n/a |
| SDF consumers (stage 3) | `sdf_reflections.hlsli::ShadeSdfReflectionHit` - the single function behind `GatherSdfDiffuse` (final gather), `SsgiRestirRadiance` (ReSTIR GI) and `GatherSdfReflections` (deferred lighting) | Snap: `p' = position - normal * SampleSdfRayDistances(scene, position).x` (the raw, unexpanded distance), then query at `p'` with `lodBias = sdfLodBias`; valid -> return `sun + other`; else today's shading (and update only when `SdfWrites`) | `SdfReflectionScene` gains `uint surfaceCacheParamsSrv` set by `BuildSurfaceScene()` (ssgi_world_gather.hlsli) from: `SSGIRestirRootConstants::reserved0` (ReSTIR), a 14th root DWORD in `ssgi_trace_cs.hlsl` (13 today), and `LightingCB::_lightPad2[0]` -> `surfaceCacheParamsSrv` for the deferred lighting (the rect lights took the former pad at 43088 the same way; `PerFrameCB` is full at 1024 B - do not grow it) |
| SDF radiance cache (stage 3) | `radiance_cache_trace_cs.hlsl`, the `t >= 0` branch | Snap + query at the hit; valid -> `radiance = cached` (replaces `albedo * (sunRad/pi + indirect)`); the SH probes then inherit exact albedo, local lights and emissive, and the deferred lighting's indirect diffuse improves with **no change to `deferred_lighting_ps.hlsl`'s SH consumption** | 13th root DWORD `g_surfaceCacheParamsSrv` (12 today) |
| DDGI (stage 3) | `ddgi_probe_trace_cs.hlsl` after `TraceProbeRay` | Query at `hitP + hitN * 2 * kSurfaceEpsilon`; valid -> `rt.rgb = sun + other` (skip `SamplePreviousIrradiance`); else update with `albedo * sunRad/pi` and the atlas term; with `materialTableSrv` bound, `TraceProbeRay` gains the `GITrace_HitSurface` attribute path the reflection pass uses | 13th/14th root DWORDs `g_surfaceCacheParamsSrv`, `g_materialTableSrv` (12 today) |
| Feed (stage 3) | **new** `runtime/shaders/runtime/gi/surface_cache_feed_cs.hlsl` | `ProbeRayDirection(i, FeedRays, probe, frame)` from the radiance-cache probe positions (relocated origins where the side classifier moved them), inline `RayQuery` on the TLAS, `GITrace_HitSurface` material, sun + shadow ray + SH `E/pi` (+ stage-2 lights/emissive), update only | its own root block (b0) + b2 trace constants, `UpdateGIMaterialTable(ctx)` refreshed per frame while on |
| Relight / cell-indirect (stage 4) | **new** `surface_cache_relight_cs.hlsl`, `surface_cache_indirect_cs.hlsl` | Walk `entries / RelightSlices` entries per frame; live cells with a surface record: recompute `sun` (sun + K lights, shadow rays) and write `resolved.sun` directly; trace `IndirectRays` cosine rays from `position + normal * bias`, each hit -> query (`sun + other`) or SH fallback -> EMA into the indirect part of `other` | own root blocks |
| Path tracer | `passes/pathtracing/*` | **none, ever** | - |

### 3.2 Coexistence with the committed radiance cache

The SH radiance cache (`gi/radiance_cache.{h,cpp}`) keeps its two roles unchanged: the free-space
irradiance source of the deferred lighting (`PerFrameCB::rcProbeSHBufferSrv`, priority DDGI > RC >
LPV > sky) and the indirect fallback of every consumer when a cell is cold. The surface cache is a
surface-attached outgoing-radiance cache; the two are complementary exactly as Lumen's surface cache
and radiance cache are. From stage 3 the SH writer reads the surface cache at its hits, so the
probe grid inherits the detail; from stage 4 the cell-indirect pass reads the SH as its fallback.
`GetCompletedGridOrigin()` discipline is untouched; the cache has no grid.

### 3.3 Gating and the default-byte-identical discipline

- `r.GI.SurfaceCache` (`UZ_CVAR_BOOL`, default **0**) is the master switch. OFF:
  `SurfaceRadianceCache::IsActive()` is false, no buffer exists, `RegisterSurfaceCacheResolvePass`
  registers nothing, every consumer's params DWORD is `UINT32_MAX` and the reflection flag bit is
  clear. The frame must be byte-identical to the current default (which **includes**
  `r.RayTracing.Reflections 1` and `r.RayTracing.Reflections.Indirect 1`): frame-95 of
  `scripts/rendering/reflection-reference.uzproject` versus `E:/UzmiEngine/f005-after/frames/f_95.png`,
  `default_max_code_delta 0`, as `test_rt_reflections_stage1.py --case default-metrics` checks.
- Shader changes under a uniform flag are the series' norm (stage 2/3 of the reflections added
  branches the same way and proved `stage1_equivalence_max_code_delta 0`); the proof is the image,
  not the absence of code.
- The engine's CVar discipline from block A applies: every number above is a CVar, declared once
  in `surface_radiance_cache.cpp`, read at execution time (the PerFrameCB-populated-after-build
  trap in `path_tracer_pass.h`).
- The RT reflection root block stays <= 20 DWORDs (17 after this). DDGI/RC/SSGI root blocks gain
  one or two DWORDs each, all inside the 20/24 budget. No change to the shared graphics root, to
  `PerFrameCB`, or to the 464-byte `RadianceCacheTraceConstants`.

### 3.4 CVars (all `r.GI.SurfaceCache.*`)

| CVar | Type / default | Stage | Meaning |
|---|---|---|---|
| `r.GI.SurfaceCache` | bool 0 | 1 | master switch |
| `.EntriesLog2` | int 20 [16..22] | 1 | hash table size |
| `.CellSize` | float 10 cm [2..100] | 1 | level-0 cell |
| `.LodDistance` | float 400 cm [50..5000] | 1 | distance at which the cell size first doubles |
| `.MinSamples` | int 8 [1..256] | 1 | validity threshold |
| `.AccumFrames` | int 32 [1..255] | 1 | EMA horizon in frames |
| `.StaleFrames` | int 64 [1..1024] | 1 | eviction age |
| `.MaxRadiance` | float 4096 | 1 | per-sample clamp (firefly cap; also the overflow guard) |
| `.Jitter` | bool 1 | 1 | +-0.5 cell lookup jitter |
| `.DirectMode` | int 0 [0..1] | 1 | D7 |
| `.Stats` | bool 0 | 1 | readback + log every 60 frames |
| `.Debug` | int 0 [0..3] | 1 | 1 cell hash colour, 2 sample-count heat, 3 hit mask - written into the reflection buffer by the RT pass |
| `.LocalLights` | int 4 [0..16] | 2 | K lights with shadow rays at a hit |
| `.Emissive` | bool 1 | 2 | table emissive at a hit |
| `.FeedRays` | int 16 [0..64] | 3 | rays per probe per frame of the feed trace (0 = no feed) |
| `.SdfReads` | bool 1 | 3 | SDF consumers read |
| `.SdfLodBias` | int 2 [0..4] | 3 | coarser levels for SDF-traced hits |
| `.SdfWrites` | bool 0 | 3 | SDF consumers write (diagnostic) |
| `.DDGI` | bool 1 | 3 | DDGI trace reads/writes |
| `.RelightSlices` | int 16 [1..64] | 4 | frames to relight every live cell once |
| `.IndirectRays` | int 4 [0..16] | 4 | cosine rays per live cell per relight slice |

---

## 4. Staged plan with acceptance criteria

Ground truth for every fidelity claim: the converged `--path-trace` capture of the same project
(frames 255 and 511 via `--screenshot-frames 255-511:256`, the `test_rt_reflections_stage1.py`
recipe; `pt_256_to_512_rmse_codes` reports the reference's own convergence). Improvement metric:
the same capture with `r.GI.SurfaceCache 0` (the current GDF+SH shading). All metrics in 8-bit code
space on fixed ROIs (`scripts/rendering/flicker/runner.run_capture`, `clean=False`, `timeout <= 180`).
Serialize engine runs (single GPU lease); the first frames of a cold shader cache are not timing
evidence (the Sponza frame-60 spread).

### Stage 1 - the cache exists and is provably the same estimator (minimal increment)

Scope: `surface_cache_contract.hlsli`, `surface_cache.hlsli`, `surface_cache_resolve_cs.hlsl`,
`SurfaceRadianceCache` class + `RegisterSurfaceCacheResolvePass` (`frame/frame_gi_passes.cpp`,
registered right after the GDF composite), `GetSurfaceCacheResolve()` in `pipeline_gi.cpp`, the
RT-reflection read/write under `kReflectionFlagSurfaceCache` + root DWORD 16, CVars of stage 1,
the gtest, the capture script. Nothing else reads or writes the cache.

Acceptance:
- A1 **Default byte-identical**: `reflection-reference.uzproject`, frame 95, `max_code_delta 0`
  and PNG bytes identical to `E:/UzmiEngine/f005-after/frames/f_95.png`.
- A2 **Identity of the estimator**: `rt-reflections-glossy.uzproject`, 100 frames, shots 60-99,
  `r.RayTracing.Reflections.Denoise 0` (the denoiser would hide the signal), cache OFF vs ON
  (`DirectMode 0` and `1`): in the reflection ROI the mean over frames 60-99 differs by
  <= 2 codes per channel, and the per-pixel temporal standard deviation over those frames is lower
  with the cache ON (report the ratio; expected < 0.5 at `AccumFrames 32`).
- A3 **No regression vs the reference**: RMSE of frame 99 against the path-traced frame 511 in the
  same ROI: ON <= OFF + 1 code (both modes).
- A4 **Cost**: `SurfaceCache_Resolve` <= 0.25 ms at `EntriesLog2 20`; `RT_Reflections` on the
  standalone route grows by <= 15 % with the cache ON (PassInspector timings or
  `scripts/rendering/measure_gpu_cost.py`); `stats.failedInserts / inserts < 1 %` on Sponza
  (`_sponza-tour.uzproject`, 300 frames) with `live` reported.
- A5 **Tests**: build exit 0; `npm run test:layouts` exit 0 (the params record); gtests no
  regression (7892 + 3 pre-existing VfxEditorSlate; DX12DeviceTest transient-TDR 127/255 -> re-run
  once); **new** `tests/engine/renderer/test_surface_cache.cpp`: level/cell-size rule (D3) at
  the boundaries, key round trip and the never-(0,0) guarantee, normal index for the six axes and
  ties, `SurfaceCachePackSample` clamp/scale round trip, the frame-cap arithmetic (no overflow at
  the clamp), the flag-bit equality with the C++ enum.
- A6 **Debug view** `.Debug 1/2/3` renders (describe + read the PNG); `.Stats` logs live/insert
  counts.

### Stage 2 - lighting completeness at the hit (the first visible fidelity gain)

Scope: K local lights with shadow rays, emissive via the extended `GIMeshMaterial`, the lights
`StructuredBuffer` upload, `.LocalLights` / `.Emissive`.

Acceptance:
- B1 A1 again, plus `--path-trace` byte-identical before/after the table extension (the PT ignores
  the new fields): `test_layered_bsdf_stage1.py`-style `default_max_code_delta 0` on a PT capture.
- B2 **new** project `scripts/rendering/surface-cache-locallight.uzproject` (derived from
  `rt-reflections-offscreen.uzproject`: the off-screen block lit by a point light, plus an emissive
  quad, `light-bvh-emissive.uzproject` is the emissive precedent): RMSE of the reflection ROI
  against the PT reference drops with the cache ON versus OFF (report the delta; the OFF image
  cannot contain the lamp or the emitter at all, so the drop must be clear, > 5 codes expected).
- B3 Stage-1 equivalence: with `.LocalLights 0` and `.Emissive 0` the stage-2 build reproduces the
  stage-1 capture (`max_code_delta 0`).
- B4 Cost: `RT_Reflections` <= stage 1 + 0.3 ms at K = 4 on Sponza.

### Stage 3 - the SDF consumers and the probes read exact-material radiance

Scope: the feed trace, the `ShadeSdfReflectionHit` hook (snap + `SdfLodBias`), the radiance-cache
trace hook, the DDGI hook, the three bindings (`reserved0`, the 14th SSGI DWORD, the `LightingCB`
pad), `.FeedRays` / `.SdfReads` / `.SdfLodBias` / `.SdfWrites` / `.DDGI`.

Acceptance:
- C1 A1 again; `npm run test:layouts` for the `LightingCB` pad consumption; with every stage-3 CVar
  at its OFF value the stage-2 captures reproduce (`max_code_delta 0`).
- C2 **Diffuse GI vs the reference**: `gi-restir-indirect.uzproject` through
  `scripts/rendering/measure_restir_gi.py` (its one-bounce indirect rows, code-space means): the
  mean error of the real-time gather against the converged plain PT decreases or holds in every
  measured row with the cache ON (`r.GI.ReSTIR 0` and `1`), and no row gains more than 2 codes of
  bias.
- C3 **The expanded-surface bias is absorbed**: `stats` tagged per writer; the fraction of SDF-hit
  queries that land on a cell previously written by a TLAS writer (feed / reflections) in the Sponza
  atrium (`_sponza-tour.uzproject`, frames 100-300) is >= 50 % at `SdfLodBias 2`; a `SdfLodBias 0`
  run shows the fraction falling (documents why the bias exists).
- C4 **Indoor Sponza**: the sponza indoor reference pose used by `phase0_indoor-lighting-system`
  (frame 95 of `_sponza-tour` or the pose in `docs/analysis/lighting/indoor/sponza-test-handoff.md`)
  against a 4-bounce PT capture: full-frame RMSE ON <= OFF, and the shadowed-wall ROI RMSE improves
  (report both; the ON image must show texture-resolved bounce where OFF shows per-mesh flat
  colour - describe the PNGs).
- C5 Cost: feed <= 0.3 ms at 16 rays x 4096 probes; the resolve unchanged; total GI section
  growth <= 1 ms at 1080p.

### Stage 4 - scheduled relight and cell-indirect (the hybrid half)

Scope: the surface record, `surface_cache_relight_cs.hlsl`, `surface_cache_indirect_cs.hlsl`, the
lighting-hash policy switch (relight instead of clear), `.RelightSlices` / `.IndirectRays`.

Acceptance:
- D1 A1 again; stage-3 equivalence with `.RelightSlices 0`-equivalent (relight off) and
  `.IndirectRays 0`.
- D2 **Responsiveness**: a scripted sun-direction change at frame N (`_tour.lua` precedent) in
  `_sponza-tour`: the reflection ROI mean settles within 2 codes of the steady state (captured
  separately) in <= 2 x `RelightSlices` frames, and `stats.evictions` at frame N is 0 (no clear).
- D3 **Multi-bounce**: the enclosed fixture room / `probe-opening-core.uzproject` against a
  4-bounce PT reference: RMSE ON (stage 4) < ON (stage 3) in the room's floor ROI; the deferred
  lighting's indirect diffuse (through the SH writer's reads) within 3 codes of the PT mean in that
  ROI.
- D4 Cost: relight + indirect <= 0.4 ms combined at Sponza occupancy; memory `88 MB` reported by
  the stats log at `EntriesLog2 20`.

### Later (not scheduled, recorded so nobody re-derives them)

- A spatial reconstruction of cells (bilinear over neighbouring cells instead of jitter) if the
  blockiness survives the consumers' temporal filters.
- The reflection pass terminating glossy paths at the cache (a second bounce for rough lobes).
- A neural radiance cache (Muller 2021) as a drop-in behind the same `Query/Update` API - rejected
  for now (training cost, non-determinism against the byte-identical discipline).

---

## 5. Risks and traps

- **Memory blow-up.** Fixed by construction: the table never grows; a full table fails inserts
  (`stats.failedInserts`) and the writer falls back to inline. Watch the log; raise `EntriesLog2`
  only with evidence. Off allocates nothing. Destroy every buffer (D3D12MA leak assert = exit 127).
- **Update cost / atomic contention.** Hot cells (a mirror facing one wall) receive thousands of
  samples per frame; the per-frame cap (`kSurfaceCacheFrameCap`) bounds the atomics and the
  overflow. Measure with A4 before changing the cap.
- **Temporal lag.** The per-frame EMA (`AccumFrames`) makes every cell converge in ~32 frames
  regardless of its sample rate (unlike a sample-count EMA, which would take 1000 frames for a
  cell visited once per frame). Stages 1-3 clear on light change - a continuously animated sun
  keeps the cache cold and the frame reverts to inline shading (never worse than today, never
  better either). Stage 4 removes this. State it in the stage-1 report.
- **Blockiness / bias.** The cell average is a spatial blur at the cell size and a blur over the
  normal direction's 90-degree cone; the jitter converts it to noise the consumers' temporal passes
  average. The ReSTIR target function becomes a function of the cache state: seed the jitter from
  (cell, frame) only, never from the pixel, or the same-surface shortcut and the pairwise MIS
  compare different functions in one frame.
- **The SDF tracer's expanded-surface hit bias** (found in the GI-ReSTIR work:
  `ssgi_restir_gi.hlsli` header - "it registers hits on a distance-dependent expanded surface ...
  reconnection to it was measured biased by the footprint"). An SDF hit sits in front of the raw
  surface by up to the expansion (tens of cm at distance, bounded by `0.5 * t` for diffuse rays).
  Unsnapped, it keys into an air cell no TLAS writer ever fills. The snap along the gradient by
  the raw distance (`SampleSdfRayDistances(...).x`) plus `SdfLodBias` coarser cells is the
  mitigation; C3 measures it. SDF consumers stay read-only by default so their per-mesh-average
  albedo never contaminates exact-material cells.
- **Light leaking through thin geometry.** Two sides of a curtain are two keys (opposite normal
  directions) even when they share cell coordinates; a wall thinner than the cell with the *same*
  facing on both sides does not exist. Cells straddling a corner mix two walls - the jitter and
  the normal split limit it; the cell size (10 cm) is below Lumen's near-card texel density.
- **Root budget.** One DWORD per consumer; the RT block goes 16 -> 17 of 20. The `PerFrameCB` is
  full (1024 B, the tail was carved for the region dump); do not grow it - use the `LightingCB` pad
  for the deferred-lighting path and root DWORDs elsewhere. The shared 464-byte b2 layout is
  prefix-declared by `gi_trace_common.hlsli`, `ddgi_probe_common.hlsli`, `probe_reach_cs.hlsl` and
  `radiance_cache_trace_cs.hlsl`; touching it shifts every field - the params record exists so it is
  never touched.
- **TLAS transform convention.** Hit positions are world space already (`ray.Origin + R * t`);
  only normals cross the instance transform - keep the row-vector `CommittedWorldToObject3x4`
  multiply the reflection pass uses (the `56f65e6c` lesson applies to anything that goes the other
  way, e.g. a stage-4 relight that projects a cell into object space - it must not; relight works in
  world space from the surface record).
- **Material table identity.** `GIMeshMaterialGPU` is per `meshId`; two draws of one mesh with
  different materials overwrite each other (pre-existing). The cache is per world position and
  inherits whichever albedo the table had; a follow-up, not this task.
- **Only GI occluders are in the TLAS** (`IsGIOccluder(cmd)`, `tlas_manager.cpp:79`); non-occluder
  draws never populate cells and never receive cached radiance - consistent with today.
- **Render-graph ordering.** The buffers are not graph resources; every pass that uses them must
  transition/UAV-barrier through `m_rgStates` like the SSR textures, and the resolve must be
  registered before the first consumer in registration order (registration order is execution
  order for a shared resource, the note in `RegisterRTReflectionPass`).
- **Verification traps inherited from the series.** `--screenshot-frames` takes `N`, `A-B` or
  `A-B:step`, not a list; PowerShell redirects write UTF-16 logs; the DX12DeviceTest may transient-TDR
  (re-run once); bound every capture at 180 s and kill stragglers; stash-to-build-the-before only
  with the stash restored before the "after".

---

## 6. References

- Wright, D., Narkowicz, K., Kelly, P. **"Lumen: Real-time Global Illumination in Unreal Engine 5"**,
  SIGGRAPH 2022, Advances in Real-Time Rendering course - surface cache (mesh cards, capture,
  direct lighting, radiosity), radiance cache, the screen / SDF / HWRT trace ladder, and the
  "surface cache lighting" vs "hit lighting" modes of the hardware path
  (`r.Lumen.HardwareRayTracing.LightingMode`). The architecture this spec reaches parity with.
- Wright, D. **"Radiance Caching for Real-Time Global Illumination"**, SIGGRAPH 2021 Advances in
  Real-Time Rendering - Lumen's world radiance cache and screen probes (the role the engine's
  SH grid plays).
- NVIDIA **SHaRC - Spatially Hashed Radiance Cache**, RTXGI 2.0 SDK (2024), github.com/NVIDIA-RTX/SHARC -
  the hash-grid radiance cache this design follows: camera-distance levels, position + normal
  keys, CAS insertion, per-frame update/resolve with staleness eviction, sample-threshold queries.
- Binder, N., Fricke, S., Keller, A. **"Fast Path Space Filtering by Jittered Spatial Hashing"**,
  SIGGRAPH 2018 Talks; Keller, A., Wachter, C., Binder, N. **"Massively Parallel Path Space
  Filtering"**, MCQMC 2020 (arXiv 1902.05942) - jittered spatial hashing of path vertices, the
  blockiness-to-noise argument.
- Muller, T., Rousselle, F., Novak, J., Keller, A. **"Real-time Neural Radiance Caching for Path
  Tracing"**, SIGGRAPH 2021 - the neural alternative behind the same query/update interface
  (recorded, not chosen).
- Majercik, Z., Guertin, J.-P., Nowrouzezahrai, D., McGuire, M. **"Dynamic Diffuse Global
  Illumination with Ray-Traced Irradiance Fields"**, JCGT 2019; Majercik et al. **"Scaling Probe-Based
  Real-Time Dynamic Global Illumination for Production"**, JCGT 2021 - the probe model, hysteresis,
  relocation and classification the engine's writers already follow.
- McGuire, M. et al. **"Real-Time Global Illumination using Precomputed Light Field Probes"**, I3D
  2017 - the SH probe layout `radiance_cache.h` cites.
- Ouyang, Y. et al. **"ReSTIR GI: Path Resampling for Real-Time Path Tracing"**, HPG 2021; Lin, D.
  et al. **"Generalized Resampled Importance Sampling"**, SIGGRAPH 2022; Bitterli, B. et al.
  **"Spatiotemporal Reservoir Resampling"**, SIGGRAPH 2020 - the ReSTIR GI consumer and why its
  target function must be deterministic within a frame.
- Wright, D. **"Dynamic Occlusion with Signed Distance Fields"**, SIGGRAPH 2015 Advances - the mesh
  SDF / global distance field substrate and its surface bias.
- Heitz, E. **"Sampling the GGX Distribution of Visible Normals"**, JCGT 2018 - the glossy rays that
  populate the cells.
- Cigolle, Z. et al. **"A Survey of Efficient Representations for Independent Unit Vectors"**, JCGT
  2014 - the octahedral normal packing of the surface record.
- Microsoft, **DirectX Raytracing 1.1 (inline RayQuery)**; **HLSL Shader Model 6.6** (64-bit
  integer atomics; `D3D12_FEATURE_D3D12_OPTIONS11::AtomicInt64OnDescriptorHeapResourceSupported`).
- In-repo: `docs/analysis/lighting/lumen/01-surface-cache.md` (the card estimate and the
  recommendation to skip cards), `docs/analysis/lighting/crimson-desert/00-blackspace-engine.md`
  (inline material evaluation at hits), `docs/specs/rendering/rt-reflections.md` (the consumer this
  spec extends), `docs/specs/rendering/gi-system.md` (the `E/pi` exchange convention the cache
  deliberately does not use - it stores `L_o`).

---

## 7. Turnkey implementation task prompt (stage 1)

Paste as a rendering implementation task (deep-high lane) when the lane is free. Self-contained.

> Work in UzEngine at E:/UzmiEngine/uzengine, branch `feat/phase1-advanced-rendering`. The engine/GPU
> (RTX 5080, DXR) is AVAILABLE; serialize builds and captures (you hold the single GPU lease).
> Implement **STAGE 1 of the surface radiance cache** per
> `docs/specs/rendering/surface-radiance-cache.md` (READ IT FIRST - it is decision-complete and
> code-grounded; sections 1.5, 2, 3.1, 3.3, 4 "Stage 1" and 5 are binding). Summary: a world-space
> spatial-hash cache of outgoing diffuse radiance at ray hits (SHaRC-style: 64-bit keys from camera-
> distance level + 6-direction normal + cell coords, linear probing <= 8, per-frame fixed-point
> atomic accumulation, a resolve pass with per-frame EMA and staleness eviction), read and written
> ONLY by the hardware ray-traced reflection pass in this stage.
>
> Build: (1) `runtime/shaders/runtime/gi/surface_cache_contract.hlsli` (C++/HLSL dual, the
> `rt_reflection_contract.hlsli` pattern: level/cell/key/slot/pack math + the 64-byte
> `SurfaceCacheParams` record), (2) `runtime/shaders/runtime/gi/surface_cache.hlsli`
> (`SurfaceCacheQuery` / `SurfaceCacheUpdate`, jitter seeded by (cell, frame) only),
> (3) `runtime/shaders/runtime/gi/surface_cache_resolve_cs.hlsl` + `PipelineLibrary::GetSurfaceCacheResolve()`
> in `core/pipeline/pipeline_gi.cpp`, (4) `runtime/engine/renderer/gi/surface_radiance_cache.{h,cpp}`
> (class `SurfaceRadianceCache`: buffers created on first use, `Resolve()`, `InvalidateAll()`,
> change-box eviction from `GlobalDistanceField::GetChangedRegions()`, lighting-hash clear, stats
> readback, all `r.GI.SurfaceCache.*` CVars of stage 1 declared with `UZ_CVAR_*` and read at execute
> time), (5) `SceneRenderer::RegisterSurfaceCacheResolvePass` in `frame/frame_gi_passes.cpp`
> registered right after the GDF composite and before every GI consumer, state transitions / UAV
> barriers through `m_rgStates` like the SSR textures, (6) the reflection consumer:
> `RTReflectionRootConstants::surfaceCacheParamsSrv` as DWORD 16 (`passes/raytracing/rt_reflection_pass.h`,
> keep the `<= 20` static_assert), `kReflectionFlagSurfaceCache = 16u` in
> `rt_reflection_contract.hlsli` + the C++ enum, and in `rt_reflection_cs.hlsl` the query at
> `hitP + hitN * kReflectionSurfaceBias` replacing the whole hit radiance when valid (`DirectMode 0`:
> `sun + other`; `1`: inline sun + cached `other`), else the current shading followed by the update
> of its two terms (`albedo * sunRad / pi` -> sun, `albedo * E/pi` -> other), (7) `.Debug 1/2/3`
> views through the reflection buffer, (8) `tests/engine/renderer/test_surface_cache.cpp` per
> acceptance A5, (9) `scripts/rendering/test_surface_cache_stage1.py` modelled on
> `test_rt_reflections_stage1.py` with cases `default`, `default-metrics`, `off`, `on0`, `on1`, `pt`,
> `metrics` implementing A1-A4.
>
> CRITICAL safety: OPT-IN, default OFF (`r.GI.SurfaceCache 0`): nothing allocated, no pass
> registered, frame byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png` (frame 95 of
> `scripts/rendering/reflection-reference.uzproject`, `max_code_delta 0`, PNG bytes identical). Do
> NOT touch `PerFrameCB`, the shared graphics root, the 464-byte `RadianceCacheTraceConstants`, the
> base BRDF, or anything under `passes/pathtracing/` (the path tracer is the reference and never
> reads the cache). Root constants <= 20 DWORDs. 32-bit atomics only (the two-word key scheme in
> section 2.3). Destroy every buffer (D3D12MA leak assert = exit 127). Use the camera distance for
> the level in both the query and the update.
>
> PROVE IT (section 4, Stage 1): A1 byte-identical default; A2 estimator identity on
> `rt-reflections-glossy.uzproject` frames 60-99 with `r.RayTracing.Reflections.Denoise 0` - ROI mean
> within 2 codes OFF vs ON in both `DirectMode`s AND lower temporal std with the cache ON (report the
> ratio); A3 RMSE vs the `--path-trace` frame 511 (`--screenshot-frames 255-511:256`): ON <= OFF + 1
> code; A4 resolve <= 0.25 ms, `RT_Reflections` growth <= 15 %, `failedInserts/inserts < 1 %` and
> `live` on `_sponza-tour.uzproject` 300 frames with `.Stats 1`; A5 build exit 0, `npm run
> test:layouts` exit 0, gtests no regression vs 7892 + 3 pre-existing VfxEditorSlate (DX12DeviceTest
> transient TDR 127/255 -> re-run once), the new gtest passing; A6 the three debug views captured and
> described.
>
> BOUNDS: every engine run <= 180 s (`--screenshot-frames` syntax: `N`, `A-B`, `A-B:step`); kill and
> switch after 3 min; stop all engine/test/zig processes when done; do NOT commit. Keep the diff
> scoped to the files listed above plus the task's tasks.md; IGNORE unrelated WIP and untracked junk
> (`*.tmp.*`, `.agents/`, `.claude/`, capture dirs). Deliver: the complete changed-files list, the
> byte-match, the A2 means and std ratio, the A3 deltas, the A4 timings and stats, test results, and
> the debug-view PNG descriptions. If any acceptance number cannot be met, report the measured value
> and stop rather than loosening the criterion.

---

## 8. Where this lands

Phase 2 / 3a GI quality, immediately after the reflections stages 1-3 it builds on. Stage 1 is a
one-lane, one-week slice; stages 2-4 are each a further slice with their own byte-identical gate.
After stage 4 the engine has every layer of Lumen's architecture with a different, position-keyed
storage: screen trace -> hardware/SDF trace -> surface radiance cache -> SH radiance cache -> sky,
with exact materials at hardware hits and the path tracer as the standing reference.
