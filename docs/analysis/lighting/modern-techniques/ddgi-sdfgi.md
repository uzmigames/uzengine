# DDGI & SDFGI — Probe-Based Dynamic GI

The two dominant probe-based GI systems outside Lumen. Both place a **3D
grid of radiance probes** around the camera and update them with ray
tracing. Simpler than Lumen's surface cache, smaller engineering effort,
perceived quality close enough to Lumen for most scenes.

**Recommendation for UzEngine**: DDGI-style is our best candidate for the
distant-GI layer of the new default pipeline.

---

## 1 · DDGI (NVIDIA, 2019)

Paper: Majercik, Müller, McGuire, Nowrouzezahrai, "Dynamic Diffuse Global
Illumination with Ray-Traced Irradiance Probes" (JCGT 2019). Open-source
reference: github.com/NVIDIAGameWorks/RTXGI.

### 1.1 Data structure

- 3D grid of probes (e.g. 16³ = 4096 probes) around camera.
- Each probe stores:
  - Octahedral-mapped **irradiance** (8×8 or 16×16 texels, BC6H or fp16)
  - Octahedral-mapped **visibility** (mean + variance of ray distance, used for probe occlusion)
- Probes snap to a fixed world grid, scroll with camera.

### 1.2 Update

Per frame per probe (rotating schedule — not every probe every frame):

1. Shoot ~256 rays in random uniform directions.
2. Trace via BVH (HWRT) or SDF (SW).
3. At hit, evaluate direct lighting (analytic lights + skylight) using
   shadow rays.
4. Accumulate contributions into octahedral irradiance texels.
5. Also accumulate ray distance into visibility octahedral.

Octahedral texel update uses a hysteresis blend (exponential moving
average), giving temporal stability.

### 1.3 Per-pixel sampling

```hlsl
// For each shading pixel
float3 irradiance = 0;
for (int i = 0; i < 8; ++i) {
    // 8 nearest probes around the pixel's world-space position
    float3 probePos = GetProbeWorldPos(i);
    float3 L = normalize(probePos - pixelPos);
    float visibility = OctahedralSample(probeVisibility[i], L);
    float w = max(0, dot(N, L)) * ChebyshevTest(visibility, length(probePos - pixelPos));
    irradiance += w * OctahedralSample(probeIrradiance[i], N);
}
irradiance /= sumWeights;
```

The **visibility Chebyshev test** is the DDGI secret sauce: it uses the
variance of ray distances to weight each probe by its likelihood of being
"visible" from the pixel. This is what kills light leaks in DDGI vs
classical irradiance volumes.

### 1.4 Multi-bounce

Automatic via the "relight from self" trick: when rays hit geometry,
instead of sampling a full surface-cache-style lit result, the shader
samples the PREVIOUS frame's probe irradiance at the hit point (as if the
hit geometry were itself lit by probes). Over ~10 frames, energy
propagates.

### 1.5 Strengths

- Small implementation (~1000 lines of shader code).
- No per-mesh preprocessing.
- Works on any RT HW (or SDF with software tracing).
- Dynamic: probes update every frame.

### 1.6 Weaknesses

- **Leaks** through thin walls (Chebyshev test helps but not perfect).
- **Resolution-limited**: 16³ probes over a 64 m volume = 4 m probe
  spacing. Large rooms have blocky GI.
- **No per-material response**: GI is irradiance (integrated), not
  radiance, so BRDF variation across a surface is not captured (you get
  the cosine-weighted incoming light, not the reflected-out light from
  the surface).
- **Probes inside geometry**: probes that fall inside walls are useless
  but still consume memory. DDGI has a "classifier" to deactivate them.

---

## 2 · SDFGI (Godot, 2020)

Introduced in Godot 4. Similar spirit to DDGI but tracing via SDFs instead
of BVH.

### 2.1 What's different

- Traces against a **signed distance field clipmap** (same idea as Lumen's
  Software RT, Epic shipped first but Godot built it independently).
- Cascaded probe grids (multiple resolutions) for near and far GI.
- Probes are irradiance octahedral + visibility (like DDGI).
- Adds **reflections** via longer SDF traces, not just diffuse.

### 2.2 Cascades

Godot's SDFGI uses a **clipmap of probe grids**:

- Cascade 0: 4 m × 16³ probes, fine detail near camera.
- Cascade 1: 16 m × 16³ probes, mid-range.
- Cascade 2: 64 m × 16³ probes, far range.

Per pixel, sample the cascade whose probe spacing matches the pixel's
footprint. This handles open-world scale without exploding probe count.

### 2.3 SDF bake

Every static mesh has its SDF baked at import time. At runtime, a
clipmap-sized volume around the camera is filled by compositing the mesh
SDFs (same as Lumen SW RT).

### 2.4 Strengths / weaknesses

Broadly similar to DDGI. SDFGI is slightly slower per probe (SDF tracing
is more expensive than HWRT) but runs on any GPU with compute. Leaks
slightly worse than DDGI's Chebyshev because cascades quantize visibility.

---

## 3 · Comparison table

| Aspect | DDGI (HWRT) | DDGI (SW SDF) | SDFGI (Godot) |
|--------|-------------|---------------|---------------|
| Trace HW | RTX / RDNA2 | SM6 | SM6 |
| Tracing cost | Low | Medium | Medium |
| Leak prevention | Chebyshev | Chebyshev | Cascade boundaries |
| Multi-bounce | Self-relighting | Same | Same |
| Reflections | Separate pass | Separate pass | Integrated (long trace) |
| Integration in UE5 | Via RTXGI plugin | Deprecated path | N/A |
| Open-source reference | RTXGI | N/A | godotengine/godot |

---

## 4 · Implementation for UzEngine

We have the pieces:

- `runtime/engine/renderer/gi/ddgi_system.{cpp,h}` — stub DDGI
- `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl` — probe update
- `runtime/engine/renderer/gi/irradiance_probe_grid.{cpp,h}`
- `runtime/engine/renderer/gi/global_distance_field.{cpp,h}`
- `runtime/engine/renderer/gi/mesh_sdf_baker.{cpp,h}`

Wire these into a DDGI path:

1. Probe grid snaps to camera, scrolls per frame.
2. 1/8th of probes update each frame (rotating schedule).
3. Each updating probe shoots 256 rays via SDF.
4. At hit, evaluate direct lighting (existing code) + skylight.
5. Accumulate into octahedral irradiance + visibility.
6. Sample in lighting pass: 8 nearest probes, Chebyshev-weighted.

This is a **2-3 week implementation** (mostly wiring, since the primitives
exist). Delivers a dramatic Sponza improvement: currently Sponza looks
flat because we have no indirect light from the bright sun bouncing off
the walls. A probe grid captures exactly this.

---

## 5 · Probe placement strategies

Uniform 3D grid is simplest but wasteful. Advanced:

- **Adaptive grid**: detect geometry boundaries and snap probes to them.
- **Sparse allocation**: only allocate probes where rays hit surfaces
  (Lumen's adaptive world probes).
- **Cascades**: multiple grid resolutions (SDFGI's approach, also
  Lumen's voxel clipmap).

For v1, go uniform + scrolling. Optimize later.

---

## 6 · What DDGI won't fix

- High-frequency contact GI (where a small object shadows nearby surface)
  — probe spacing is too coarse. Use AO + SSGI for this.
- Directional reflection detail — DDGI is diffuse only. Reflections need
  separate pass.
- Emissive VFX lighting scenes — DDGI is too slow to update for a 1-frame
  flash of an explosion. Analytic light + separate emissive logic needed.

DDGI is the **distant gather** layer. It does NOT replace direct lights,
SSGI, SSR, or area-light shadowing. It fills in the "everything else that
used to be black" category.

---

## 7 · References

- Majercik, Müller, McGuire, Nowrouzezahrai, "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes" (JCGT 2019). jcgt.org/published/0008/02/01/
- NVIDIA RTXGI — github.com/NVIDIAGameWorks/RTXGI.
- Godot SDFGI source — github.com/godotengine/godot/tree/master/servers/rendering/renderer_rd/effects
- Juan Linietsky, "SDFGI Introduction" (Godot blog post, 2020).
- McGuire, "Ray-Traced Irradiance Probes" (SIGGRAPH 2019 course).
