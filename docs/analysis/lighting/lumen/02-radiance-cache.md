# Lumen · Radiance Cache & Voxel Lighting

The middle two layers of Lumen's hierarchy: **voxel lighting** (scene-wide
coarse radiance) and the **radiance cache** (sparse world-space probes for
distant gather).

---

## 1 · Voxel lighting (Layer 4)

A sparse 3D clipmap centered on the camera. Each voxel stores the total
radiance entering it from all directions (packed as spherical harmonics L2
or as an octahedral direction map).

### 1.1 Structure

- Clipmap with ~4 levels (e.g. 1 m, 4 m, 16 m, 64 m voxel sizes).
- Each level 64³ voxels.
- Only voxels near surfaces are allocated (sparse).
- Total allocated voxels ~64K–256K in a typical scene.

### 1.2 Update

Each frame, a subset of voxels is re-evaluated:

1. Pick ~10% of allocated voxels per frame (distance-prioritized).
2. For each chosen voxel, trace 6 rays (±X, ±Y, ±Z).
3. Each ray hits the scene (SDF or HWRT).
4. At the hit, sample the **surface cache's lit atlas**.
5. Accumulate into the voxel's SH coefficients.

Because the voxel lighting samples the *lit* surface cache, each frame's
update integrates **one more bounce** of energy. Over ~10 frames, energy
propagates to equilibrium — that's why Lumen has ~10-frame warm-up.

### 1.3 Usage

Voxel lighting is used as:

- A source for **distant rays** in the screen-probe gather.
- A source for **missing surface cache** regions (cards that haven't streamed yet).
- A fallback for **reflections** on rough surfaces.

Think of it as "the scene's average radiance, cheap and everywhere."

---

## 2 · Radiance Cache (Layer 2)

Sparse world-space probes placed near the camera. Similar in spirit to DDGI
(NVIDIA) or SDFGI (Godot), but **adaptive**: probes are only placed where
screen pixels will use them.

### 2.1 Placement

Per frame:

1. Run a compute over all screen probes.
2. Each screen probe computes its "far-trace" direction (directions that
   need distant radiance).
3. Project those directions into world-space and place world probes where
   they land.
4. De-duplicate (snap to grid, reject duplicates).

This means: only regions that are being *looked at by screen probes* get
world probes. A hidden closet gets no probes. This is a big win over DDGI,
which places probes uniformly in a grid even in areas nobody ever sees.

### 2.2 Shape

Each world probe stores **hemispherical radiance** in an octahedral 8×8 or
16×16 layout, encoded in BC6H. Unlike DDGI's SH-compressed irradiance, this
is **raw radiance** — higher frequency, usable for both diffuse and glossy
reflections.

### 2.3 Update

Per probe per frame:

1. Shoot 32–64 rays into the hemisphere.
2. SDF or HWRT trace into scene.
3. At hit, sample surface cache → lit radiance.
4. Write into octahedral.

World probes are cheaper to update than screen probes because there are far
fewer of them (~10K total vs ~10K screen probes/pixel block at 1080p).

### 2.4 Usage

World probes are the **far-field radiance** for screen probes. When a
screen probe's ray travels more than ~2 m, instead of continuing to trace
(expensive), it samples the nearest world probe's octahedral in the ray's
direction (cheap, and already lit from all its sources).

---

## 3 · Why two separate world-space caches?

Voxel lighting and radiance cache overlap. Why have both?

| Voxel lighting | Radiance cache |
|----------------|----------------|
| Uniform 3D grid | Sparse world-space probes |
| Coarse (1–64 m voxels) | Fine (octahedral 16² per probe) |
| SH2 coefficients | Raw radiance |
| Used for: second-bounce injection to surface cache | Used for: primary far-field sampling by screen probes |
| Updated slowly | Updated every frame near camera |

Voxel lighting is primarily a **multi-bounce propagation** mechanism —
it's how energy spreads through the scene over multiple frames.

Radiance cache is primarily a **primary far-field gather** mechanism — it
provides one-hop radiance lookup for screen probes that are looking past
2 m.

A sufficiently smart engine could merge these into one structure, but Epic
chose to keep them separate because they have different update cadences
and different usage patterns.

---

## 4 · Sparse allocation mechanics

Both voxel lighting and radiance cache are **sparse 3D structures**.
Implementation in UE5 uses a **brick-based hash grid**:

- World space is divided into 4³ bricks of voxels.
- A hash table maps world position → brick memory slot.
- Only allocated bricks consume memory.
- Eviction when memory pressure is high (distance-prioritized).

This is the same pattern as sparse virtual textures (SVT) but in 3D.

---

## 5 · The "compositing" pass

Our existing doc `radiance-cache-compositing.md` (moved from `gi/lumen-...`)
describes the final compositing step. Briefly:

- Screen probes gather radiance via a mix of: mesh SDF hits returning
  surface cache, HWRT hits returning surface cache, and world-probe
  lookups.
- Each contribution is weighted by:
  - Ray length (short = prefer surface cache, long = prefer probe)
  - Surface normal similarity (reject probes with misaligned normals)
  - Distance to probe (bilinear between neighbors)
- The output is the per-probe hemispherical irradiance.

---

## 6 · 2026 optimizations

Per the web search results (epic dev community + Tom Looman UE5.6 highlights):

- **Adaptive probe placement** (new in 5.6) — probes placed where lighting variance is high, reducing total probe count.
- **Downsampled integration** — the per-pixel integration step runs at lower resolution and is upsampled, making the whole pass ~3× faster.
- **Direct sky sampling in reflections** — reflections now skip the radiance cache for sky hits, sampling the sky LUT directly. Better quality for reflective roofs.

These bring Lumen 5.6's GI cost to approximately 4.5 ms at 1440p on a 3080,
down from 7 ms in 5.0.

---

## 7 · UzEngine relevance

We have the primitives:

- `runtime/engine/renderer/gi/global_distance_field.{cpp,h}` — the SDF clipmap infrastructure.
- `runtime/engine/renderer/gi/radiance_cache.{cpp,h}` — stub radiance cache.
- `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl` — the trace shader.
- `runtime/engine/renderer/gi/irradiance_probe_grid.{cpp,h}` — probe grid.
- `runtime/engine/renderer/gi/ddgi_system.{cpp,h}` — DDGI-style setup.

None of these are integrated end-to-end into the lighting pass. We have
the LEGO bricks but no assembled Lumen.

A pragmatic path forward:

1. Implement a **simple volumetric radiance cache** (like DDGI) as the "Layer 2" distant gather.
2. Skip the voxel-lighting (Layer 4) for now — it's mostly a multi-bounce mechanism, and we can approximate multi-bounce via temporal accumulation in the screen probe gather.
3. Skip the surface cache (Layer 3) for now — instead, directly evaluate material at SDF hit points. Expensive, but correct, and we only do it for short-distance screen-probe rays.

This "Lumen Lite" is achievable for our team. See
[../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md).

---

## 8 · References

- `radiance-cache-compositing.md` (this directory) — prior analysis.
- SimLumen voxel lighting and radiance cache source.
- NVIDIA DDGI paper (Majercik et al. 2019) — for the volumetric probe approach.
- Godot SDFGI source — for a self-contained alternative reference implementation.
