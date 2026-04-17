# Lumen (UE5) · Architecture Deep-Dive

**Source material**: Epic UE5 `Renderer` module (FLumenScene, FLumenSceneData, ScreenProbeGather.cpp, SurfaceCacheFeedback.cpp, RadianceCacheInternal.cpp), Krzysztof Narkowicz GDC 2022, Epic "Lumen Technical Details" docs page, SimLumen reimplementation (ShawnTSH1229), Epic's Fortnite BR Chapter 4 VSM post. All references at bottom.

Lumen is Unreal Engine 5's dynamic global-illumination and reflection system.
It is the most sophisticated real-time GI system deployed at scale. UzEngine
cannot reasonably port Lumen — it would take a team of 10 for 2 years — but
we can **borrow the architecture**: the passes, the data structures, the
fallback hierarchy. That is the point of this document.

---

## 1 · What Lumen is (and is not)

**Lumen is** a hybrid screen-space + world-space GI solution that unifies:

- Diffuse indirect lighting (skylight bounce, emissive bounce)
- Reflections (glossy and rough)
- Emissive surfaces as secondary light sources
- AO as a byproduct of GI rays

**Lumen is not** a path tracer. It does not trace rays from every pixel into
the full scene. It uses a **cached-surface approximation** so that most rays
hit a precomputed "surface cache" rather than doing full material evaluation.

Lumen's great insight is: **the cost of GI is not the rays — it is the
material evaluation at the hit.** By caching lit material attributes on every
mesh, the hit becomes a free texture tap. This is what makes Lumen fast.

---

## 2 · The four layers of cache

Lumen has four nested caches, each running at a different resolution and
update rate. Understanding this hierarchy is the key to understanding Lumen.

```text
┌──────────────────────────────────────────────────────────────────┐
│  Layer 4: Voxel Lighting    — scene-wide, coarse, multi-bounce   │
│  "What is the total radiance entering this voxel?"              │
│  Update: async, few voxels per frame                              │
│  Resolution: clipmap 64³ × N levels                              │
└──────────────────────────────────────────────────────────────────┘
                              ▲  inject
┌──────────────────────────────────────────────────────────────────┐
│  Layer 3: Surface Cache     — per-mesh, lit material attributes  │
│  "What does this point on this mesh look like when lit?"        │
│  Update: streaming, distance-prioritized                          │
│  Resolution: atlas 8192² with mesh cards (6 sides per mesh)      │
└──────────────────────────────────────────────────────────────────┘
                              ▲  direct-light trace
┌──────────────────────────────────────────────────────────────────┐
│  Layer 2: Radiance Cache    — world-space probes, distant gather │
│  "What is the distant radiance from this world position?"       │
│  Update: every frame, placed near screen probes                  │
│  Resolution: sparse 3D grid, octahedral hemisphere per probe     │
└──────────────────────────────────────────────────────────────────┘
                              ▲  final gather
┌──────────────────────────────────────────────────────────────────┐
│  Layer 1: Screen Probes     — screen-space, per-pixel gather     │
│  "What is the hemispherical radiance at this pixel?"            │
│  Update: every frame                                              │
│  Resolution: one probe per 16×16 pixel block, 32 rays each       │
└──────────────────────────────────────────────────────────────────┘
                              ▲  lookup per-pixel
┌──────────────────────────────────────────────────────────────────┐
│  Layer 0: Per-pixel integration — the G-Buffer shading           │
│  "Evaluate BRDF * incoming radiance for this pixel"             │
└──────────────────────────────────────────────────────────────────┘
```

Each upper layer feeds the lower layer via a single read. Each lower layer
is orders of magnitude cheaper than the upper one to update.

---

## 3 · Frame execution order

Lumen runs in this sequence each frame (approximate — subsystems are
asynchronous to squeeze GPU utilization):

| # | Pass | What it does |
|---|------|--------------|
| 1 | **Surface Cache feedback** | Determine which mesh cards are on-screen this frame; set their update priority. |
| 2 | **Surface Cache capture** | For cards whose update counters ticked, rasterize the mesh from 6 orthographic directions into a material atlas (albedo, normal, opacity, emissive). |
| 3 | **Surface Cache direct lighting** | Per surface-cache pixel, evaluate direct lights (shadow cast via mesh SDF or HWRT). Store lit albedo in another atlas. |
| 4 | **Voxel lighting inject** | Inject surface-cache lit-albedo into a sparse voxel clipmap. Multi-bounce emerges from repeated injection across frames. |
| 5 | **Screen probes adaptive placement** | Per 16×16 tile on screen, decide how many probes to place based on lighting variance estimate. |
| 6 | **Screen probes trace** | Each probe shoots 32 rays (near-field: mesh SDF or HWRT; far-field: voxel lighting or world probes). |
| 7 | **Radiance Cache update** | Sparse world-probe grid near the camera is updated from the scene (independent of screen probes). |
| 8 | **Screen probes composite** | Probes resample radiance cache for rays that missed or went far. |
| 9 | **Final gather** | Per-pixel, look up nearest screen probes and do hemispherical integration. |
| 10 | **Reflections** | Per-pixel reflection ray (screen trace first, then radiance-cache-backed fallback). |
| 11 | **Diffuse+reflection composite** | Combine final gather and reflections into the shading pass. |
| 12 | **Temporal / denoise** | Per-channel temporal filter with history + spatial denoise pass (disocclusion-aware). |

---

## 4 · Software RT vs Hardware RT

Lumen has two tracing backends:

- **Software Ray Tracing (Mesh SDF)**: Each mesh has a precomputed signed distance field baked into a 3D texture. Rays traverse a Global SDF (clipmap of SDFs merged) to find the nearest surface. Cheap, works on any SM6 GPU.
- **Hardware Ray Tracing (HWRT)**: Rays use the RT cores to traverse the real BVH. Higher quality (no SDF approximation), but requires RTX / RDNA2 and up.

Under HWRT, the **surface cache is still used** — the BVH ray returns a hit
location, then the surface cache is sampled for the "lit material" at that
location. This is the key difference vs a full path tracer: Lumen does NOT
evaluate the material inline at the hit, it reads a cache.

See [04-software-vs-hardware-rt.md](./04-software-vs-hardware-rt.md) for the
details.

---

## 5 · What you give up to get Lumen

Nothing is free. Lumen's compromises:

- **Dynamic geometry doesn't update surface cache in real time.** Destroyed meshes leave "ghost" lit surface in the cache for a few frames.
- **Mesh cards don't exist for very small or very thin meshes.** Leaves and thin grass often don't GI correctly.
- **Card-space lighting has low frequency.** High-frequency material detail on GI is blurry.
- **Light leaks** through thin walls (cards see through).
- **Distance fields are quantized.** An SDF is not the real mesh; sharp edges are rounded.

BlackSpace avoids all of these by tracing the real scene — at the cost of a
much higher ray budget. The right answer for UzEngine is somewhere in the
middle: a **Lumen-style hybrid** as the default, with an **optional RT path**
that mirrors BlackSpace's approach for HW that supports it.

---

## 6 · Module layout (as mirrored for UzEngine planning)

If UzEngine were to implement Lumen, this is the module structure (mirroring
Epic's layout):

```text
runtime/engine/renderer/
├── lumen/
│   ├── surface_cache.cpp         // card generation, feedback, atlas
│   ├── surface_cache_capture.cpp // the 6-axis capture
│   ├── surface_cache_lighting.cpp// direct lighting on surface cache
│   ├── voxel_lighting.cpp        // scene-wide voxel clipmap
│   ├── screen_probes.cpp         // placement, trace, composite
│   ├── radiance_cache.cpp        // world-probe gather
│   ├── final_gather.cpp          // per-pixel integration
│   ├── reflections.cpp           // stochastic reflections + integration
│   └── scene.cpp                 // manages tracking of meshes / cards
└── shaders/runtime/lumen/
    ├── surface_cache_capture_vs.hlsl
    ├── surface_cache_capture_ps.hlsl
    ├── surface_cache_direct_lighting_cs.hlsl
    ├── voxel_inject_cs.hlsl
    ├── screen_probes_trace_cs.hlsl
    ├── screen_probes_composite_cs.hlsl
    ├── radiance_cache_update_cs.hlsl
    ├── final_gather_ps.hlsl
    └── lumen_reflections_cs.hlsl
```

UzEngine has `runtime/engine/renderer/gi/` with `ddgi_system`,
`global_distance_field`, `irradiance_probe_grid`, `mesh_sdf_baker`,
`radiance_cache`, and `ssgi_renderer`. That is the **embryonic form** of
everything above — we have the building blocks, they just do not talk to
each other as one coherent pipeline.

---

## 7 · Pass-by-pass guides

Each of the following files is a standalone deep-dive on one part of Lumen:

- [01-surface-cache.md](./01-surface-cache.md) — Mesh cards, capture, atlas.
- [02-radiance-cache.md](./02-radiance-cache.md) — World probes + voxel clipmap.
- [03-screen-probe-gather.md](./03-screen-probe-gather.md) — Screen probes + final gather.
- [04-software-vs-hardware-rt.md](./04-software-vs-hardware-rt.md) — SDF vs HWRT tracing modes.
- [05-reflections.md](./05-reflections.md) — Lumen Reflections pipeline.
- [radiance-cache-compositing.md](./radiance-cache-compositing.md) *(moved — prior analysis)*.
- [radiance-cache-quick-ref.md](./radiance-cache-quick-ref.md) *(moved — quick-ref card)*.

---

## 8 · References

- Epic Games, "Lumen Technical Details in Unreal Engine", docs.unrealengine.com/5.7/en-US/lumen-technical-details/.
- Krzysztof Narkowicz, Daniel Wright, "Lumen: Real-time Global Illumination in Unreal Engine 5" (GDC 2022).
- Marcus Wassmer, "Nanite: A Deep Dive" (SIGGRAPH 2021) — Nanite × Lumen interactions.
- Epic, "Virtual Shadow Maps in Fortnite Battle Royale Chapter 4" (unrealengine.com/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4).
- ShawnTSH1229, "SimLumen: Simplified Unreal Lumen Implementation in MiniEngine", github.com/ShawnTSH1229/SimLumen — invaluable for the code-level detail.
- NVIDIA, "Unreal Engine 5.4 Ray Tracing Guide" — HWRT-specific tuning.
- Tom Looman, "Unreal Engine 5.6 Performance Highlights" — latest Lumen perf numbers.
- TGDF 2024, "Lumen with Arm Immortalis" — mobile Lumen detail.
