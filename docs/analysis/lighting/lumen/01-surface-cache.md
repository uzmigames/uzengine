# Lumen · Surface Cache

The surface cache is Lumen's signature invention. It is *why* Lumen is fast.
Understanding this one data structure explains 60% of Lumen.

---

## 1 · The big idea

> "Cache lit material attributes per-mesh in a 2D atlas, so that a ray hit
> becomes a texture tap instead of a material evaluation."

Rendering a GI ray normally requires:

1. Trace ray → find mesh + triangle + barycentric
2. Interpolate vertex attributes (normal, UV, tangent)
3. Sample material textures (albedo, normal, roughness, emissive)
4. Evaluate BRDF × incoming radiance from light sources
5. Return shaded radiance to caller

Steps 2–4 are expensive (texture taps, BRDF math) and redundant if many
rays hit the same surface patch. The surface cache precomputes **the lit
result** — meaning step 2+3+4 done once, stored in an atlas, reused forever.

---

## 2 · Mesh cards

For every static mesh, Lumen generates ~6 **mesh cards** at cook time. A
card is a 2D view of the mesh from one axis-aligned direction:

- +X, -X, +Y, -Y, +Z, -Z (6 orthographic projections)

Cards are of variable resolution: a 10 m barn gets 512×512 cards, a 20 cm
doorknob gets 32×32. Card resolution is computed from mesh bounds and a
target texel-per-world-unit density.

For complex meshes (e.g. a hollow structure), Lumen may place **multiple
cards per direction** at different depth slices. The card generator tries
to ensure every visible surface point on the mesh has at least one card
texel covering it.

### 2.1 Card generation

At cook time (UE5's pre-process step):

1. Voxelize the mesh at high resolution (~1024³).
2. For each axis direction, ray-march along the axis for every texel.
3. Where a texel hits surface, record `(position, normal, uv, material)`.
4. Pack all 6 cards into a single atlas slot for this mesh.

Card generation is expensive (~seconds per complex mesh) but offline.
Runtime just uses the precomputed card descriptor.

### 2.2 Card atlas

All meshes' cards live in a **global card atlas** — typically 8192×8192
containing:

- Position layer (fp16×3 — world-space position of the texel's surface point)
- Normal layer (octahedral 16-bit)
- Albedo layer (BC3/BC7 compressed)
- Opacity layer (R8)
- Emissive layer (BC6H for HDR)
- Depth layer (R16 — distance from card projection to surface)

---

## 3 · Runtime streaming

The card atlas at 8K² is ~600 MB raw. Lumen cannot afford to upload every
mesh's cards. Instead:

### 3.1 Feedback

Every frame, a compute pass over the visible G-Buffer determines which
cards are referenced by on-screen pixels. It writes "card usage counters".
Counters decay over time.

### 3.2 Prioritized streaming

Cards with high counters are streamed in with high priority, displacing
low-counter cards. A few hundred cards are evicted/added per frame.

This is exactly the **virtual texture streaming** model, but applied to
per-mesh lit atlases rather than diffuse albedo.

---

## 4 · Direct lighting on the surface cache

Once a card is streamed in, its **lit color** is computed by a single
compute-shader pass over all texels in the atlas:

```hlsl
// Per-card-texel pseudocode
float3 pos    = Cards.PositionAt(uv);
float3 N      = Cards.NormalAt(uv);
float3 albedo = Cards.AlbedoAt(uv);

float3 sumL = 0;
for (each directional light) {
    float3 L = -lightDir;
    float  shadow = TraceShadow(pos, L);   // SDF or HWRT
    sumL += shadow * saturate(dot(N, L)) * lightColor;
}
for (each point light nearby) {
    // similar
}
sumL += SkyLight(pos, N);

// Cache the lit result
LitAtlas[uv] = albedo * sumL;
```

This runs once per card texel. Not every frame. Cards are relit on a
rotating schedule (maybe every 8 frames for close cards, every 64 frames
for distant ones) unless an event forces a relight (light moved, day/night
cycle tick, etc.).

Multi-bounce emerges naturally: the surface cache is re-lit *using the
voxel lighting that was built from the PREVIOUS frame's surface cache*.
Each frame the cache incorporates one more bounce of energy from around
the scene.

---

## 5 · Ray hit → card lookup

When a Lumen ray hits a mesh:

1. SDF or HWRT returns `(meshIndex, worldHitPos)`.
2. Pick the card direction closest to the hit surface normal.
3. Project `worldHitPos` into card UV space using the card's orthographic transform.
4. Sample `LitAtlas` at the card UV.
5. Return the sampled lit radiance.

This is ~1 texture tap + ~4 simple math ops. Compared to evaluating a full
material inline (~20 texture taps + BRDF), it's ~10× cheaper.

---

## 6 · Failure modes

The surface cache is an approximation. It fails visibly when:

- **Card resolution too low**: GI bleeds across material boundaries. Looks like "soft wash" on walls.
- **Card not yet streamed in**: First few frames of a new area show GI boil. Lumen warms up slowly.
- **Mesh can't be carded**: Thin meshes (grass blades, chain-link fence) have no valid card direction; Lumen falls back to voxel lighting, which is even coarser.
- **Dynamic lighting change**: Moving the sun causes a wave of card relights propagating through the scene.

These are why BlackSpace's per-pixel RT looks cleaner — it has no surface
cache to go stale.

---

## 7 · UzEngine relevance

We do not currently have a surface cache. If we implement a Lumen-style
hybrid GI, the surface cache is the hardest piece — it requires:

1. An offline card generator in the asset pipeline.
2. A runtime atlas streaming system (~same complexity as virtual texturing).
3. A per-card direct-lighting compute pass with SDF/HWRT shadows.
4. A runtime card-eviction scheduler.

For a team our size this is 3+ months of work. An alternative to consider:

**Skip the surface cache and use DDGI or SDFGI directly for the "distant
radiance" layer.** These are simpler (volumetric probes, not mesh-specific
atlases) and produce GI of comparable perceived quality for 20% of the
engineering effort.

See [../modern-techniques/ddgi-sdfgi.md](../modern-techniques/ddgi-sdfgi.md)
and [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md)
for our recommended path.

---

## 8 · References

- SimLumen repo (ShawnTSH1229) — clean surface cache implementation with source comments.
- Epic Lumen Technical Details docs — §"Surface Cache".
- Narkowicz & Wright GDC 2022 — §"Mesh Cards" and §"Surface Cache Streaming".
- Krzysztof Narkowicz, "Lumen: Software ray-tracing and more" (blog posts).
