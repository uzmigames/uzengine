# Lumen · Software RT vs Hardware RT

Lumen's tracing backend has two modes. Understanding the trade-offs matters
for UzEngine because we support GPUs that cannot do HWRT (DX11 / SM5 /
older discrete / integrated).

---

## 1 · Software Ray Tracing — mesh SDFs + global SDF

### 1.1 What

Every mesh has a **precomputed signed distance field** stored in a 3D
texture. Rays traverse a **global SDF** — a clipmap-merged aggregate of all
mesh SDFs within a volume around the camera — by sphere-tracing.

### 1.2 How rays trace

```
t = 0
while (t < maxDistance) {
    pos = origin + t * dir
    d   = SampleGlobalSDF(pos)     // signed distance to nearest surface
    if (d < 0.001) break            // hit
    t += max(d, minStep)
    if (iterations > maxIter) break  // miss
}
if (t < maxDistance) {
    // Reconstruct hit normal from SDF gradient
    N = normalize(grad(SampleGlobalSDF, pos))
    // Sample surface cache at (pos, dir)
    return SurfaceCacheLookup(pos, N, dir)
}
return SkyLUT(dir)
```

Sphere tracing is **O(iterations)** — in a cluttered scene, ~16–32 iterations
to converge.

### 1.3 Advantages

- **No BVH**: no RT-capable HW required. Runs on any SM6 GPU.
- **Natural soft shadows**: the distance field gives a continuous occlusion
  signal, not a hard ray hit, which translates nicely to soft-shadow
  effects (cone tracing).
- **Cheap temporal**: SDFs are static until mesh moves.

### 1.4 Disadvantages

- **Quantized surfaces**: an SDF at 4³/cm quantizes features smaller than
  a centimeter. Sharp edges become rounded.
- **Memory heavy**: a 2 m cube mesh at 4/cm resolution = 80³ volume = 512K
  texels × 2 B (R16F) = 1 MB per mesh. Ten thousand meshes = 10 GB. Lumen
  paginates, but this is a real cost.
- **Dynamic objects**: can't bake SDF at runtime cheaply. Skeletal meshes
  and destructibles don't participate in SDF tracing. Lumen's answer:
  screen-space traces + voxel lighting fill in the missing pieces.
- **Self-intersections**: SDFs from multiple meshes can overlap in the
  global SDF. The merge operation (`min` of distances) works but can
  produce incorrect "inside surface" results when meshes overlap.

### 1.5 Cone tracing

Lumen's SDF mode optionally does **cone tracing** instead of ray tracing
for soft shadows and glossy reflections. A cone is traced by sphere-tracing
with increasing sphere radius along the ray:

```
radius = tan(coneAngle) * t
d = SampleSDF(pos) - radius
```

This naturally integrates a volume of space into one ray, giving soft
shadows and rough reflections without stochastic sampling.

---

## 2 · Hardware Ray Tracing

### 2.1 What

Rays traverse the real scene's BVH via RT cores (NVIDIA RTX, AMD RDNA2+,
Intel Arc). Hit = exact triangle + barycentric.

### 2.2 How rays trace

```
RayQuery<RAY_FLAG_NONE> q;
q.TraceRayInline(TLAS, flags, mask, ray);
q.Proceed();
if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    // Exact hit location
    hitPos = q.CommittedWorldRayOrigin() + q.CommittedRayT() * q.CommittedWorldRayDirection()
    // Mesh + primitive + barycentric
    meshIdx = q.CommittedInstanceIndex()
    primIdx = q.CommittedPrimitiveIndex()
    bary    = q.CommittedTriangleBarycentrics()
    // Now look up surface cache at hitPos
    return SurfaceCacheLookup(meshIdx, hitPos)
}
return SkyLUT(dir)
```

### 2.3 Advantages

- **Exact geometry**: no SDF quantization. Sharp edges are sharp.
- **Dynamic meshes for free**: skeletal BLAS refit is cheap (~0.3 ms).
- **Better performance on simple geometry**: a single ray through a
  100-triangle mesh is O(log N) vs SDF's O(iterations).
- **Higher quality reflections**: mirror-like surfaces need exact hit
  locations that SDF can't provide.

### 2.4 Disadvantages

- **RT-capable HW required**: excludes older GPUs, consoles (PS4, XSS limited).
- **BVH build cost**: ~1 ms/frame for a dynamic scene. Not free.
- **Memory**: ~15% of VRAM used by BVHs in typical UE5 scenes.
- **Foliage and Nanite tricky**: see §3.

---

## 3 · The Nanite / RT collision

Nanite is UE5's virtual geometry system — meshes have cluster LOD that
swaps triangles dynamically. RT wants fixed geometry in the BLAS. These
are fundamentally at odds.

Lumen's solution (as of 5.3+):

- **Nanite meshes generate a proxy BLAS** at a fixed LOD (usually medium).
- The proxy is used for ray tracing regardless of what Nanite is
  rasterizing on screen.
- Reflections look slightly lower-detail than primary visibility, but the
  difference is rarely visible in final composite.

For UzEngine, if we ever adopt cluster LOD, this is a landmine. Plan for
proxy BLAS from day one.

---

## 4 · Which mode Lumen picks

Lumen can run:

- **SW only** (`r.Lumen.HardwareRayTracing 0`) — works on any HW
- **HWRT only** (`r.Lumen.HardwareRayTracing 1`, `...DynamicGeometryType 1`) — requires RT GPU
- **Hit lighting = HWRT but shading = surface cache** (hybrid, default on RTX)
- **Full HWRT with surface cache fallback** — highest quality

Most shipped titles use the hybrid (HWRT for visibility, surface cache for
shading). This is ~2× faster than full HWRT + material eval.

---

## 5 · Decision matrix

| Question | Pick SW | Pick HWRT |
|----------|---------|-----------|
| Target has RT HW? | — | Yes |
| Target has RT HW but you want broad compatibility? | Yes | — |
| Scene has many thin / small geometry (grass, leaves)? | — | HWRT |
| Scene has many dynamic objects? | — | HWRT |
| Memory-constrained (mobile, console XSS)? | Yes | — |
| Want highest quality reflections on mirror surfaces? | — | HWRT |
| Development phase? Fast iteration? | Yes | — |

---

## 6 · UzEngine relevance

We already have:

- `runtime/engine/renderer/gi/mesh_sdf_baker.{cpp,h}` — the SDF bake.
- `runtime/engine/renderer/gi/global_distance_field.{cpp,h}` — the clipmap merge.
- `runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl` — bake shader.
- `runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl` — clipmap composite.

We **do not yet** have:

- HWRT-capable trace shaders.
- A BVH manager / TLAS refit system.
- The decision logic to pick SW vs HWRT at runtime.

Our `raytracing/` directory is stubbed. Populating it with production-quality
HWRT dispatch is one of the big items in the AAA pipeline plan.

---

## 7 · Practical recommendation

For UzEngine's new default pipeline:

1. **Default to SDF tracing** (we have the pieces, it's HW-agnostic).
2. **Expose `r.RT.Mode = sdf | hybrid | hwrt`** CVar.
3. **Implement HWRT backend** as phase 2, with TLAS refit and proxy-BLAS for dynamic objects.
4. **Keep both paths simultaneously** — the frame graph picks based on CVar.

This matches Lumen's architecture closely enough that when we have HWRT
working, we can flip one switch in the pipeline to use it.

See [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md) §5.

---

## 8 · References

- Epic docs §"Lumen Software Ray Tracing", §"Hardware Ray Tracing".
- NVIDIA, "Unreal Engine 5.4 Ray Tracing Guide" (July 2024) — HWRT-specific tuning.
- Wright & Narkowicz, "Lumen: Software ray tracing" (blog posts, unreal.com).
- John Hable, "Distance Fields" blog series — SDF primer.
