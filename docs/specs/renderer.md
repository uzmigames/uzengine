# Renderer Spec

## Overview

UzEngine uses **DirectX 12** as the primary render API. The render pipeline is **Forward+** (forward rendering with a light list built by a compute pre-pass), supporting PBR materials, cascaded shadow maps, and a post-process stack. The renderer outputs frames via the existing JPEG+WebSocket streaming pipeline for display in the VSCode viewport.

---

## Pipeline Overview

```
Per Frame:
────────────────────────────────────────────────────────────────────
1. Frustum Cull          CPU — reject nodes outside camera frustum
2. Build Draw Lists      CPU — sort opaque / transparent / shadow
3. Shadow Pass           GPU — render shadow cascades (3 cascades)
4. Depth Prepass         GPU — fill depth buffer, no color output
5. Light Culling         GPU compute — tile-based light list (16x16 tiles)
6. G-Buffer Pass         GPU — albedo, normal, roughness/metallic
7. Deferred Lighting     GPU — PBR shading using G-Buffer + light list
8. Forward Transparent   GPU — alpha-blended objects (sorted back-to-front)
9. Skybox                GPU — cubemap sky rendered last (depth test = EQUAL)
10. Post-Process         GPU — bloom, SSAO, tonemapping (ACEFilm)
11. Readback             GPU→CPU — copy back buffer to CPU for JPEG encode
12. JPEG Encode          CPU — libjpeg-turbo
13. WebSocket Broadcast  CPU — send to VSCode extension
```

---

## Frame Graph (Simplified)

```
[Scene Nodes]
     │
     ▼
[Frustum Cull] → DrawList{opaque, transparent, shadow}
     │
     ├──▶ [Shadow Pass]          ShadowAtlas (2048×2048, 3 cascades)
     │         │
     ├──▶ [Depth Prepass]        DepthTarget (1280×720, D32_FLOAT)
     │         │
     ├──▶ [Light Cull Compute]   LightListBuffer
     │         │
     ├──▶ [G-Buffer Pass]        ─── GBuffer0: RGBA8 (albedo+ao)
     │                           ─── GBuffer1: RGBA16F (normal)
     │                           ─── GBuffer2: RGBA8 (roughness/metallic)
     │
     ├──▶ [Deferred Lighting]    HDR RenderTarget (R11G11B10F)
     │
     ├──▶ [Transparent Pass]     blended into HDR target
     │
     ├──▶ [Skybox]               into HDR target
     │
     ├──▶ [Post-Process]         → Bloom → SSAO → Tonemapping → LDR (RGBA8)
     │
     └──▶ [Readback]             → JPEG Encode → WebSocket
```

---

## Resource Layout

### Descriptor Heaps

| Heap | Type | Size | Contents |
|------|------|------|----------|
| `cbv_srv_uav` | CBV_SRV_UAV | 4096 | Textures, CBVs, UAVs |
| `dsv` | DSV | 16 | Depth targets |
| `rtv` | RTV | 64 | Color render targets |
| `sampler` | SAMPLER | 32 | Texture samplers |

### Root Signature Layout

Slot 0: `b0` — Per-Object CBV (model matrix, material index)
Slot 1: `b1` — Per-Frame CBV (view, proj, camera pos, time, light count)
Slot 2: Descriptor table — SRVs t0..t7 (albedo, normal, roughness, metallic, shadow, etc.)
Slot 3: Descriptor table — Samplers s0..s3

---

## Material System (PBR)

### PBR Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `albedo` | `float3` | `[1,1,1]` | Base color (linear) |
| `albedo_texture` | `Texture2D` | white | Multiplied with `albedo` |
| `roughness` | `float` | `0.5` | 0=mirror, 1=fully rough |
| `metallic` | `float` | `0.0` | 0=dielectric, 1=metal |
| `normal_texture` | `Texture2D` | flat | Tangent-space normal map |
| `ao_texture` | `Texture2D` | white | Ambient occlusion |
| `emission` | `float3` | `[0,0,0]` | Emissive color (HDR) |
| `emission_texture` | `Texture2D` | black | Emissive mask |
| `alpha_mode` | enum | `OPAQUE` | OPAQUE / MASK / BLEND |
| `alpha_cutoff` | `float` | `0.5` | Used for MASK mode |

### BRDF: GGX Cook-Torrance

```hlsl
float3 PBR_DirectLight(float3 N, float3 V, float3 L, float3 radiance,
                       float3 albedo, float roughness, float metallic)
{
    float3 H  = normalize(V + L);
    float  a  = roughness * roughness;

    // Distribution (GGX Trowbridge-Reitz)
    float NdH = saturate(dot(N, H));
    float D   = a*a / (PI * pow(NdH*NdH*(a*a - 1.0) + 1.0, 2.0));

    // Geometry (Smith GGX)
    float k   = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G   = G_SchlickGGX(dot(N,V), k) * G_SchlickGGX(dot(N,L), k);

    // Fresnel (Schlick)
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F  = F0 + (1.0 - F0) * pow(1.0 - saturate(dot(H,V)), 5.0);

    // Specular
    float3 spec = D * G * F / max(4.0 * dot(N,V) * dot(N,L), 0.001);

    // Diffuse (Lambertian, metals have no diffuse)
    float3 kD   = (1.0 - F) * (1.0 - metallic);
    float3 diff = kD * albedo / PI;

    return (diff + spec) * radiance * saturate(dot(N, L));
}
```

---

## Shadow System (CSM)

3 cascades covering:
- Cascade 0: 0–20m (high resolution)
- Cascade 1: 20–80m (medium resolution)
- Cascade 2: 80–300m (low resolution)

Each cascade is a 2048×2048 shadow map tile within a 2048×6144 atlas. PCF 3×3 kernel for soft shadows.

---

## Post-Process Stack

| Effect | Method | Toggle |
|--------|--------|--------|
| SSAO | Screen-Space Ambient Occlusion (16 samples) | `renderer.ssao` |
| Bloom | Dual-threshold downsample + additive upsample | `renderer.bloom` |
| Tonemapping | ACES filmic | always on |
| Vignette | Analytical, edge darkening | `renderer.vignette` |
| Color grading | 16×16×16 LUT texture | `renderer.color_grading` |
| TAA | Temporal Anti-Aliasing (Phase 2) | `renderer.taa` |

---

## IPC Handlers (renderer.*)

```json
renderer.setResolution  { "width": 1280, "height": 720 }
renderer.setMSAA        { "samples": 4 }
renderer.setSSAO        { "enabled": true, "radius": 0.5 }
renderer.setBloom       { "enabled": true, "threshold": 1.0, "strength": 0.5 }
renderer.setShadows     { "enabled": true, "atlas_size": 2048, "cascades": 3 }
renderer.screenshot     {}  → JPEG binary
renderer.stats          {}  → { "fps": 60, "draw_calls": 128, "triangles": 240000 }
```

---

## Streaming (JPEG → WebSocket)

Same pipeline as current PoC, unchanged:

```
DX12 Readback Buffer (RGBA8, 1280×720)
    ↓
RingBuffer<FrameData, 8>  (lock-free SPSC)
    ↓
Encode Thread: libjpeg-turbo  (quality=85, 4:2:0, FastDCT)
    ↓
WebSocket Broadcast (binary frame = raw JPEG bytes)
    ↓
VSCode Webview: <canvas> drawImage on blob URL
```

This is already implemented and working. The renderer upgrade adds the deferred pipeline, PBR, and shadows on top.
