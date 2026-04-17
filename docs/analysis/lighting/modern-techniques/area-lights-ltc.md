# Area Lights — Linearly Transformed Cosines (LTC)

The single biggest visual upgrade any classical-lighting engine can make
for "interior realism" is **proper area lights**. Point lights are wrong:
real lights have **shape** (rectangles for windows and monitors, tubes for
fluorescent, disks for downlights, spheres for bulbs). Shape determines:

- Where light comes from (parallax).
- How highlights stretch on glossy surfaces.
- How penumbra softens with distance.

LTC (Heitz/Hill 2016–2017) is the **analytical** solution: compute exact
integration of a GGX lobe over an arbitrary polygonal or disc area light
in ~20 ALU ops, no stochastic sampling, no ringing, no approximation
visible artifacts.

---

## 1 · The problem with point lights

For a point light, lighting integration is trivial: one `NdotL * (1/r²)
* shadow * BRDF` per pixel per light. But the real world has **extended
sources**, and approximating them as points produces:

1. **Sharp highlights** on a glossy kitchen counter under a fluorescent
   tube — should be a long streak, renders as a dot.
2. **Missing penumbra softening** — a recessed downlight should cast a
   soft edge; a point cast a hard one.
3. **No parallax as the surface moves** — walking under a ceiling tile
   light, the highlight should slide smoothly, but with point lights it
   "jumps" as you move between point light samples.
4. **Wrong GI** — the light falling through a window-shaped opening
   should have that shape's signature color pattern on the floor, not
   a uniform cone.

Sponza has many opportunities for area lights: the atrium's windows,
the torch fixtures, the ceiling recesses.

---

## 2 · What LTC does

Heitz et al. define a family of spherical distributions that can be
integrated analytically over polygons. Key insight:

> "To integrate GGX × polygon is hard. But GGX can be approximated as a
> Linearly Transformed Cosine, and a Linearly Transformed Cosine
> integrated over a polygon equals a regular Cosine integrated over the
> polygon transformed by the inverse linear transformation. Regular
> cosine integration over a polygon has a simple closed form."

So:

1. At runtime: look up the LTC's 3×3 transformation matrix for
   (roughness, NdotV). This is ~4 floats per material sample, stored in
   a precomputed 32×32 texture.
2. Transform the polygon's vertices by the inverse LTC matrix.
3. Integrate the transformed polygon using the closed-form polygonal
   cosine integration (Lambert's formula).

Result: exact GGX integration over the light shape. Approximately
20–30 ALU ops per vertex per light. 5–10× more expensive than point
light but still very cheap.

---

## 3 · Supported light shapes

### 3.1 Rectangle / polygon

The "primary" shape. Windows, TV screens, doors, monitor bezels, billboards.

```hlsl
float3 LTC_EvaluateRectangle(
    float3 N, float3 V, float3 pos,
    float3x3 Minv,    // LTC inverse matrix
    float3 p0, p1, p2, p3)  // rectangle corners
{
    // Transform polygon into LTC space
    float3 tp0 = mul(Minv, p0 - pos);
    float3 tp1 = mul(Minv, p1 - pos);
    float3 tp2 = mul(Minv, p2 - pos);
    float3 tp3 = mul(Minv, p3 - pos);

    // Lambert polygon integration
    float I = IntegrateEdge(tp0, tp1)
            + IntegrateEdge(tp1, tp2)
            + IntegrateEdge(tp2, tp3)
            + IntegrateEdge(tp3, tp0);
    return max(0, I);
}
```

### 3.2 Disk

Circles: recessed downlights, spherical lampshades seen head-on.

```hlsl
float LTC_EvaluateDisk(..., float3 center, float3 axisT, float3 axisB, float radius)
{
    // Transform an analytic ellipse (disk under LTC transform becomes an ellipse)
    // Use Hill/Heitz 2017 closed-form for ellipse irradiance
    ...
}
```

### 3.3 Sphere (ball)

Omnidirectional sources of finite radius.

```hlsl
float LTC_EvaluateSphere(..., float3 center, float radius)
{
    // Horizon clipping + disk approximation
    ...
}
```

### 3.4 Tube / line / capsule

Long thin sources: fluorescent tubes, light sabers, neon signs.

```hlsl
float LTC_EvaluateTube(..., float3 p0, float3 p1, float radius)
{
    // Integrate over tube — use the "capped cylinder" closed form
    ...
}
```

---

## 4 · Precomputed tables

LTC requires two precomputed textures:

### 4.1 LTC matrix table

32×32 RGBA16F. Indexed by (roughness, NdotV):
- RG channels: two elements of the inverse matrix (the others are fixed or derived).
- BA channels: remaining elements.

### 4.2 LTC magnitude table

32×32 RG16F. Indexed by (roughness, NdotV):
- R: magnitude of the specular contribution (keeps things energy-consistent).
- G: Fresnel factor magnitude (for tint).

Tables are **free** (published alongside the paper), ~12 KB total,
loaded once.

---

## 5 · Shader integration

LTC plugs into a classical deferred renderer:

```hlsl
// Per pixel, per area light
float3 N     = gbuffer.normal;
float3 V     = normalize(camPos - pos);
float  NdotV = saturate(dot(N, V));
float  r     = gbuffer.roughness;

float2 uv = float2(r, sqrt(1 - NdotV));
float4 t1 = LTC_Matrix.SampleLevel(sampler, uv, 0);
float4 t2 = LTC_Magnitude.SampleLevel(sampler, uv, 0);

float3x3 Minv = /* reconstruct from t1 */;

float3 spec = LTC_EvaluateRectangle(N, V, pos, Minv, light.p0, light.p1, light.p2, light.p3);
spec *= t2.x;  // magnitude

// Diffuse: same with identity matrix (Lambert over polygon)
float3 diff = LTC_EvaluateRectangle(N, V, pos, float3x3(1,0,0, 0,1,0, 0,0,1), ...);

float3 shading = light.color * (diff * gbuffer.diffuseAlbedo + spec * specularColor);
```

Cost per pixel per light: ~40 ALU ops + 2 texture taps. Very cheap.

---

## 6 · Shadowing

LTC gives unshadowed integration. Shadowing requires:

- **Analytic shadows** via a shadow map (cast from area light centroid or via stochastic sampling across the surface).
- **Ray-traced shadows** via a shadow ray to a random point on the light surface (stochastic, denoiser cleans up).
- **PCF + PCSS** extending the shadow map softness to match the area size.

Most engines use stochastic ray-traced shadows for area lights when RT
is available, otherwise PCSS matched to the area light size.

---

## 7 · Anisotropic extension (2022)

Heitz et al., "Bringing Linearly Transformed Cosines to Anisotropic GGX"
(2022). Extends the method to anisotropic GGX (brushed metals, hair,
film grain).

- Adds one more table (anisotropy × roughness × NdotV).
- Negligible runtime cost increase.
- Enables realistic brushed-metal shading under area lights.

---

## 8 · Adoption

Major engines shipping LTC area lights:
- **Frostbite** (since Battlefront 2017).
- **UE5** (since UE4.21 — RectLight actor).
- **Unity HDRP** (since HDRP 5.0).
- **Godot 4** (RectLightInstance, limited).
- **Wicked Engine** (open source reference implementation).

Not shipping: older engines with backward-compat pipelines. UzEngine is
in this category today.

---

## 9 · Implementation plan for UzEngine

### Phase 1 — Basic rect + disk + sphere (2 weeks)

1. Add `CRectLight`, `CDiskLight`, `CSphereLight` ECS components.
2. Include LTC tables in `runtime/assets/lighting/ltc_matrices.dds`.
3. Extend G-Buffer → lighting pass with LTC code path.
4. Integrate with existing point-light shadow atlas (use centroid sampling).
5. Expose in UI / Lua API.

Deliverable: Sponza atrium gets rect lights for windows. Visible quality jump.

### Phase 2 — Tube + line + capsule (1 week)

6. Add `CTubeLight` ECS component.
7. Add tube shader path.
8. Shipping test with a fluorescent tube scene.

### Phase 3 — Ray-traced shadows for area lights (2 weeks, requires HWRT)

9. Stochastic shadow ray sampling on area light surface.
10. Ray-traced penumbra (instead of PCSS fake).
11. NRD-style shadow denoise.

### Phase 4 — Anisotropic LTC (1 week)

12. Add anisotropic GGX table.
13. Extend LTC eval for CMaterial with anisotropy.
14. Brushed-metal test scene.

---

## 10 · References

- Eric Heitz, Jonathan Dupuy, Stephen Hill, David Neubelt, "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines" (SIGGRAPH 2016). eheitzresearch.wordpress.com/415-2/
- Stephen Hill & Eric Heitz, "Real-Time Line- and Disk-Light Shading with Linearly Transformed Cosines" (SIGGRAPH 2017 course). blog.selfshadow.com/publications/s2017-shading-course/heitz/s2017_pbs_ltc_lines_disks.pdf
- Hill & Heitz, "Bringing Linearly Transformed Cosines to Anisotropic GGX" (PACMCGIT 2022). dl.acm.org/doi/10.1145/3522612
- Reference implementation: github.com/guiqi134/LTC-Area-Lights
- advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf — "LTC: A Journey from Research to Production".
- Michal Drobot, "Physically Based Area Lights" (GPU Pro 5 chapter 4).
- Wicked Engine reference: wickedengine.net/2017/09/area-lights/
- Magnum Engine guest post: blog.magnum.graphics/guest-posts/area-lights-with-ltcs/
