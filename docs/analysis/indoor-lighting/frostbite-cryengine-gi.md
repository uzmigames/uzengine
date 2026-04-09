<!-- doc-meta
title: Indoor Lighting & Shadow Propagation — Frostbite vs CryEngine
author: Claude Code (Frostbite Architect)
date: 2026-04-06
status: research
keywords: indoor lighting, GI, light propagation, shadow mapping, light probes, DDGI, LPV
references: Kaplanyan 2009, O'Donnell 2018, Apers 2024, Jimenez 2016
-->

# Indoor Lighting & Shadow Propagation: Frostbite vs CryEngine Analysis

## Executive Summary

**The Core Problem**: Cascaded shadow maps (CSM) render from the sun's viewpoint — pixels inside buildings are behind walls/ceilings and receive full shadow (value 0). Without global illumination or fill lighting, interiors become pitch-black, killing visual quality.

**Frostbite's Solution**: Multi-layered approach combining:
1. Enlighten middleware for semi-dynamic GI
2. Light probe grids (irradiance volumes) for indirect ambient
3. Screen-space techniques (SSAO/GTAO + contact shadows)
4. DXR-accelerated real-time GI preview (Flux + DDGI)
5. Deferred shading to allow many fill lights

**CryEngine's Solution**: Real-time volumetric propagation:
1. Light Propagation Volumes (LPV) — spherical harmonics radiance grids
2. SVOGI (Sparse Voxel Octree GI) — voxel ray-tracing with occlusion
3. Light injection from RSM (Reflective Shadow Map) into 3D volume
4. Iterative radiance propagation for multi-bounce effects
5. Artist-controlled ambient light grids

**Production Reality**: Both engines supplement systems with artist-placed fill lights — the most practical "cheat" in AAA production. No system fully solves indoor GI; all hybrid combinations of bake + real-time + artists.

---

## Part 1: Frostbite's Indoor Lighting Architecture

### 1.1 Enlighten Middleware Integration (2011–2016)

@ref O'Donnell 2018 "Precomputed Global Illumination in Frostbite" — GDC 2018

Frostbite was an **early adopter of Geomerics Enlighten**, shipping in:
- Battlefield 3 (2011) — Frostbite 2
- Need for Speed: The Run (2011)
- FIFA, Madden series
- Star Wars Battlefront II (2017)

**Enlighten's Role**: Semi-dynamic indirect lighting with real-time updates.
- Precomputes light propagation on GPU
- Allows "dynamic radiosity" — moving lights cause instant, plausible GI changes
- Does NOT solve static indirect lighting perfectly — requires baking first
- Stores results as **lightmaps** + **irradiance volumes** (probe grids)

**Frostbite Integration**:
- Enlighten bakes to two data structures: Flux (static GI) + Enlighten runtime data (semi-dynamic)
- GiGrid subsystem streams irradiance volumes in the form of **probes**
- Probes store spherical harmonics (L2, 9 coefficients) at strategic grid positions
- CPU-driven: artist places probe volumes, engine bakes at level load or in editor

### 1.2 Irradiance Volume System (Probe Grids)

**Probe Representation** (@ref O'Donnell 2018):
```
struct IrradianceProbe {
    float3 position;        // world space grid position
    float3 sh_coeffs[9];    // L2 spherical harmonics (diffuse only)
};

struct IrradianceVolume {
    uint3 grid_dims;        // e.g., 10×10×10 probes per room
    float3 origin;
    float cell_size;        // spacing (1-2 meters typical)
    IrradianceProbe* probes;
};
```

**Rendering with Probes**:
- For each pixel, interpolate probes using trilinear blending
- Evaluate SH at pixel's surface normal → indirect diffuse color
- Result: smooth, directionally-aware ambient lighting (not just flat ambient)
- Energy conservation: probes are calibrated to Enlighten bake

**Why Probes Work Indoors**:
1. Captures first bounce of bounced light from sunlit surfaces
2. Avoids leaking (when setup correctly) — probes placed inside rooms only
3. Smooth gradients across transitions (window to shadow areas)
4. Very fast lookup — few interpolations per pixel

**Drawbacks**:
- Requires artist probe placement
- Baking time (5–30 min for level)
- Cannot capture fine high-frequency details (secondary bounce shadows)

### 1.3 Cascaded Shadow Maps + Ambient Fill

Battlefield 3 Lighting System (@ref "Lighting you up in Battlefield 3", GDC 2011):

**CSM Architecture for Frostbite 2**:
```
// 4 cascades: near, mid, mid-far, far
Cascade 0: [0m, 15m]      → 2048×2048, 0.01 shadow bias
Cascade 1: [15m, 60m]     → 2048×2048, 0.015 shadow bias
Cascade 2: [60m, 200m]    → 2048×2048, 0.025 shadow bias
Cascade 3: [200m, 500m]   → 1024×1024, 0.05 shadow bias
```

**Problem**: Interior pixels are behind walls/ceiling → CSM always returns "in shadow".

**Solution in Frostbite**:
1. **CSM Mask Generation** — separate pass computing which cascades are "valid" for each pixel
   - Checks if pixel is within cascade cuboid in world space
   - If outside cuboid, shadow contribution = 0 (fully lit)
   - Creates L8 mask texture for CSM validity

2. **Deferred Shading** — enables many fill lights
   ```hlsl
   // Deferred pass
   float csm_shadow = SampleCSM(world_pos, cascades);
   float3 sunlight = sun_color * sun_intensity * csm_shadow;
   
   // Multiple fill lights blended at full brightness
   float3 fill = 0;
   for (uint i = 0; i < fill_light_count; ++i) {
       float3 to_light = fill_lights[i].pos - world_pos;
       float dist = length(to_light);
       float attenuation = 1.0 / (1.0 + dist * dist * falloff);
       fill += fill_lights[i].color * attenuation;
   }
   
   // Indirect from probes
   float3 indirect = EvaluateProbes(world_pos, normal);
   
   float3 lit = (sunlight + fill) * albedo + indirect;
   ```

3. **Contact Shadows** — screen-space ray-march for fine detail
   - Adds local occlusion where surfaces touch
   - Supplements CSM for nearby geometry
   - 4–8 ray steps, 50cm–1m range

### 1.4 Screen-Space Techniques (SSAO/GTAO)

Frostbite implements both **SSAO** (Screen-Space Ambient Occlusion) and **GTAO** (Ground-Truth AO):

**GTAO** (@ref Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect Occlusion"):
- Separates hemisphere into **slices** (e.g., 16 directions)
- Per-slice: finds horizon angle by scanning depth buffer
- Radiometrically correct — matches Monte Carlo ground truth
- Cost: 1–2 ms on 1080p, 60fps targets
- Quality: excellent for indoor contact shadows, creases, crevices

**Integration with Probes**:
```glsl
float3 ambient = EvaluateProbes(world_pos, normal);
float ao = ComputeGTAO(depth_pyramid, screen_uv, world_normal);
float3 lit_ambient = ambient * (1.0 - 0.8 * ao);  // AO darkens probe result
```

**Why GTAO Matters Indoors**:
- CSM can't capture fine shadow details on indoor geometry
- Contact shadows + GTAO replaces missing detail
- Creates convincing "shadow on shadow" effects where creases meet walls
- No baking required

---

## Part 2: CryEngine's Real-Time Volumetric Propagation

### 2.1 Light Propagation Volumes (LPV) — 2009

@ref Kaplanyan et al. 2009 "Light Propagation Volumes in CryEngine 3"

**Motivation**: Real-time first-bounce GI without baking. Ship dynamic destruction + dynamic lights with global illumination.

**Core Concept**: Spherical Harmonics (SH) radiance stored in a 3D grid volume.

```cpp
struct LPVCell {
    float4 sh_red[3];       // SH coefficients for R channel (L0 = 1, L1m=3, L1=1, L1p=1, L2=5)
    float4 sh_green[3];     // SH for G
    float4 sh_blue[3];      // SH for B
};

struct LightPropagationVolume {
    uint3 dimensions;       // e.g., 64×32×32 cells
    float cell_size;        // 0.5m–1.0m
    LPVCell* data;          // 3D texture
};
```

**Algorithm Steps**:

#### Step 1: Light Injection (from RSM)
```hlsl
// Render Reflective Shadow Map (RSM): pos, normal, flux from light
// Then inject into LPV at world-space positions

void InjectLightIntoLPV(RSMData rsm_texel, float3 world_pos, LPV lpv)
{
    // Convert world pos to LPV grid coordinates
    uint3 grid_pos = WorldToLPVGrid(world_pos, lpv);
    
    // Incident light direction and radiance
    float3 light_dir = normalize(world_pos - sun_pos);
    float3 incident_radiance = rsm_texel.flux;
    
    // Project into SH: incident_radiance has directional bias
    // (light comes from direction = -light_dir)
    float4 sh_coeffs = ProjectToSH(incident_radiance, -light_dir);
    
    // Add to LPV cell (accumulate)
    lpv.data[grid_pos].sh_red += sh_coeffs * incident_radiance.r;
    lpv.data[grid_pos].sh_green += sh_coeffs * incident_radiance.g;
    lpv.data[grid_pos].sh_blue += sh_coeffs * incident_radiance.b;
}
```

#### Step 2: Iterative Propagation
```hlsl
// Propagation: blur radiance to neighboring cells
// Simulates light traveling through the volume

void PropagateLPV(LPV src, LPV dst, uint iteration)
{
    // For each cell
    for (uint3 pos : src.dimensions) {
        float4 propagated_sh = float4(0, 0, 0, 0);
        
        // Sample 6 neighbors (±X, ±Y, ±Z)
        propagated_sh += src.data[pos + uint3(1,0,0)].sh;
        propagated_sh += src.data[pos - uint3(1,0,0)].sh;
        propagated_sh += src.data[pos + uint3(0,1,0)].sh;
        propagated_sh += src.data[pos - uint3(0,1,0)].sh;
        propagated_sh += src.data[pos + uint3(0,0,1)].sh;
        propagated_sh += src.data[pos - uint3(0,0,1)].sh;
        
        propagated_sh /= 6.0;  // Average
        
        // Accumulate with damping (energy decay)
        dst.data[pos].sh = propagated_sh * 0.9;
    }
}
```

Multiple iterations (3–5) simulate light bouncing through the volume.

#### Step 3: Sampling for Final Render

```hlsl
float3 SampleLPV(float3 world_pos, float3 world_normal, LPV lpv)
{
    // Trilinear interpolation in LPV grid
    float3 grid_pos = WorldToLPVGrid(world_pos, lpv);
    float4 sh_coeffs = TrilinearSampleSH(lpv, grid_pos);
    
    // Evaluate SH at surface normal
    float3 indirect = EvaluateSH(sh_coeffs, world_normal);
    
    return indirect;
}
```

**Key Strengths**:
1. **Fully Dynamic** — no baking required
2. **Directional Information** — SH captures light direction, not just brightness
3. **Multi-Bounce** — propagation iterations simulate secondary bounces
4. **Hardware-Efficient** — runs on console hardware (PS3, X360)

**Key Weaknesses**:
1. **Aliasing** — grid resolution causes blockiness in sharp shadows
2. **Light Leaking** — geometry doesn't block propagation; requires artist masking
3. **Low Frequency** — SH captures only diffuse, misses specular bounces
4. **Injection Complexity** — RSM generation adds overhead

**CryEngine 3 Tuning** (@ref Kaplanyan 2009):
- LPV grid: 64×32×32 cells covering view frustum
- Cell size: 0.5m (for medium-sized levels)
- Propagation iterations: 5
- Damping factor: 0.90 per iteration (90% energy retention)
- Injection: 32×32 RSM at sun resolution

### 2.2 SVOGI (Sparse Voxel Octree GI) — 2012+

@ref CryEngine 5 Documentation "Voxel-Based Global Illumination (SVOGI)"

**Evolution**: LPV is wasteful of grid space. SVOGI uses **sparse octree** — only voxelize geometry that matters.

**Architecture**:
```cpp
struct VoxelOctree {
    Octree* root;                    // Root octree node
    float voxel_size;                // Smallest voxel size (0.25m typical)
    std::vector<uint64_t> brickpool; // Brick storage (8×8×8 voxels per brick)
};

struct Voxel {
    float4 diffuse_radiance;         // RGB radiance + alpha (validity)
    float normal_x, normal_y;        // Compressed normal
};
```

**Voxelization Process**:
1. Render geometry into sparse voxel tree (conservative rasterization)
2. Each voxel stores surface normal + albedo
3. Build octree mips automatically
4. Only allocate bricks where geometry exists (avoids empty space)

**Ray Tracing in Voxels**:
```hlsl
float3 TraceVoxelCone(float3 ray_start, float3 ray_dir, VoxelOctree octree)
{
    float3 color = 0;
    float alpha = 0;
    float dist = 0.1;  // Start near surface
    float cone_angle = 0.15;  // Cone aperture in radians
    
    while (dist < MAX_DIST && alpha < 0.99) {
        float3 pos = ray_start + ray_dir * dist;
        float cone_radius = dist * tan(cone_angle / 2);
        
        // Sample voxel at this position with cone radius
        // Selects appropriate mip level based on cone_radius
        float4 voxel_color = SampleVoxelCone(octree, pos, cone_radius);
        
        // Front-to-back compositing
        color += voxel_color.rgb * (1 - alpha) * voxel_color.a;
        alpha += (1 - alpha) * voxel_color.a;
        
        dist += cone_radius * 0.5;  // Step by cone radius
    }
    
    return color;
}
```

**Advantages over LPV**:
- Sparse — only stores voxels where geometry exists
- Higher resolution — voxel cone tracing captures fine detail
- Multi-directional — traces 6 cones for indirect specular
- Handles secondary bounces naturally
- Better occlusion — voxels encode actual geometry

**Limitations**:
- More expensive than LPV (multiple cone traces per pixel)
- Voxelization every frame cost (shadows + diffuse)
- Artifacts at material transitions (insufficient mip precision)

**CryEngine SVOGI Tuning** (from official docs):
```
r_SVOGI = 1                    // Enable SVOGI
r_SVOGI_Voxels_Diffuse_Bounces = 2  // Num diffuse bounces (lighting passes)
r_SVOGI_Voxels_Diffuse_Cone_Angle = 0.8  // Diffuse cone angle (radians)
r_SVOGI_Voxels_Specular_Cone_Angle = 0.4 // Specular cone angle
r_SVOGI_Cascade_Num = 2       // Multiple cascades (detailed near, coarse far)
r_SVOGI_Voxel_Pool_Frame_Threshold = 10  // Frames before octree realloc
```

---

## Part 3: DDGI (Dynamic Diffuse Global Illumination) — Modern Unified Approach

@ref Morgan 2019 "Dynamic Diffuse Global Illumination" (SIGGRAPH/NVIDIA RTXGI)

**Motivation**: Frostbite + CryEngine approaches are complex. Can we simplify?

**Key Insight**: Don't bake or propagate. Use **ray tracing** to update a simple probe grid in real-time.

### 3.1 DDGI Architecture

```cpp
struct DDGIProbe {
    float3 position;                // World position
    float3 irradiance[6];           // Irradiance in 6 cardinal directions
    float distance[6];              // Distance to surface in 6 directions
    uint probes_updated;            // Validity flag
};

struct DDGIVolume {
    uint3 probe_dims;               // e.g., 16×16×16 = 4096 probes
    float3 origin;
    float probe_spacing;            // Typically 0.5–1.0m
    std::vector<DDGIProbe> probes;
};
```

### 3.2 Probe Update (GPU Ray Tracing)

```hlsl
// CS: One thread per probe
[numthreads(8, 8, 8)]
void UpdateDDGIProbes(uint3 DTid : SV_DispatchThreadID)
{
    uint probe_idx = DTid.x + DTid.y * PROBES_X + DTid.z * PROBES_X * PROBES_Y;
    if (probe_idx >= probe_count) return;
    
    DDGIProbe probe = probes[probe_idx];
    float3 new_irradiance[6] = { 0, 0, 0, 0, 0, 0 };
    float new_distance[6] = { 0, 0, 0, 0, 0, 0 };
    
    // 6 cone directions (±X, ±Y, ±Z)
    float3 directions[6] = {
        float3(1, 0, 0), float3(-1, 0, 0),
        float3(0, 1, 0), float3(0, -1, 0),
        float3(0, 0, 1), float3(0, 0, -1)
    };
    
    for (int dir_idx = 0; dir_idx < 6; ++dir_idx) {
        // Trace N rays in cone around this direction
        uint ray_count = 64;  // Rays per probe per frame
        
        for (uint ray = 0; ray < ray_count; ++ray) {
            float3 ray_dir = GenerateConeDirection(directions[dir_idx], cone_angle, ray);
            
            // Ray trace
            RayDesc ray_desc;
            ray_desc.Origin = probe.position;
            ray_desc.Direction = ray_dir;
            ray_desc.TMin = 0.01;
            ray_desc.TMax = 100.0;
            
            RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES> q;
            q.TraceRayInline(tlas, 0, 0, ray_desc);
            q.Proceed();
            
            if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                // Hit geometry — fetch its albedo and lighting
                float3 hit_pos = probe.position + ray_dir * q.CommittedRayT();
                float3 hit_normal = q.CommittedTriangleGetWorldNormal();
                float3 hit_albedo = FetchAlbedo(q.CommittedTriangleIndex(), q.CommittedTriangleBarycentrics());
                
                // Evaluate direct + indirect lighting at hit point
                float3 hit_radiance = EvaluateLighting(hit_pos, hit_normal, hit_albedo);
                
                new_irradiance[dir_idx] += hit_radiance;
                new_distance[dir_idx] += q.CommittedRayT();
            }
        }
        
        new_irradiance[dir_idx] /= ray_count;
        new_distance[dir_idx] /= ray_count;
    }
    
    // Temporal blend for stability
    float blend_rate = 0.1;  // Update 10% per frame (convergence ~300 frames)
    probe.irradiance = lerp(probe.irradiance, new_irradiance, blend_rate);
    probe.distance = lerp(probe.distance, new_distance, blend_rate);
    
    probes[probe_idx] = probe;
}
```

### 3.3 Probe Sampling

```hlsl
float3 SampleDDGI(float3 world_pos, float3 world_normal, DDGIVolume volume)
{
    // Find nearest 8 probes (trilinear weights)
    float3 grid_pos = WorldToGridPos(world_pos, volume);
    uint3 base = uint3(floor(grid_pos));
    float3 weights = frac(grid_pos);
    
    float3 irradiance = 0;
    float total_weight = 0;
    
    // Interpolate irradiance from 8 surrounding probes
    for (int i = 0; i < 8; ++i) {
        uint3 probe_pos = base + uint3((i & 1), (i >> 1) & 1, (i >> 2) & 1);
        DDGIProbe probe = volume.probes[ProbeIndex(probe_pos, volume)];
        
        // Sample 6-directional irradiance at world_normal
        float3 probe_irradiance = SampleIrradiance6D(probe, world_normal);
        
        // Trilinear weight
        float3 w = i & 1 ? weights : (1 - weights);
        float weight = w.x * w.y * w.z;
        
        irradiance += probe_irradiance * weight;
        total_weight += weight;
    }
    
    return irradiance / total_weight;
}

float3 SampleIrradiance6D(DDGIProbe probe, float3 direction)
{
    // Which 2 of 6 directions are closest to 'direction'?
    // Cosine weights them
    
    float3 dir_x = direction.x > 0 ? probe.irradiance[0] : probe.irradiance[1];
    float3 dir_y = direction.y > 0 ? probe.irradiance[2] : probe.irradiance[3];
    float3 dir_z = direction.z > 0 ? probe.irradiance[4] : probe.irradiance[5];
    
    float3 weights = abs(direction);  // Cosine-like weighting
    weights /= (weights.x + weights.y + weights.z);
    
    return dir_x * weights.x + dir_y * weights.y + dir_z * weights.z;
}
```

### 3.4 DDGI Advantages & Limitations

**Advantages**:
- **Fully Dynamic** — updates every frame (no baking)
- **No Geometry Approximation** — uses actual ray tracing
- **Artist-Friendly** — just tune probe spacing and blend rate
- **Inherent Occlusion** — rays blocked by geometry are zero
- **Multi-Bounce** — indirect lighting at hit points includes GI

**Limitations**:
- **Expensive** — requires hardware RT (DXR/VK_KHR_ray_query)
- **Low Frequency** — 6D irradiance inherently smooth, no sharp shadows
- **Temporal Artifacts** — convergence requires ~300 frames
- **Probe Count Scale** — 4096 probes = 4096 rays per frame minimum
- **Doesn't Capture Specular** — diffuse-only (can add specular cones with cost)

**NVIDIA RTXGI Parameters**:
```cpp
ddgi_config.probeGridDimensions = uint3(16, 8, 16);  // 2048 probes
ddgi_config.probeSpacing = 1.0f;                      // 1m between probes
ddgi_config.probeMaxRayDistance = 100.0f;             // 100m max trace
ddgi_config.probeRaysPerProbe = 128;                  // Rays/probe/frame (shared across updates)
ddgi_config.probeIrradianceEncodingGamma = 1.0f;      // Linear storage
ddgi_config.probeBlendingRate = 0.1f;                 // Convergence speed
ddgi_config.normalBias = 0.15f;                       // Offset against surface for probe rays
ddgi_config.viewBias = 0.15f;                         // Camera-relative offset
```

---

## Part 4: Production Hybrid Approaches

### 4.1 The Reality: Fill Lights Are Still Essential

All three techniques above are expensive and have limitations. **AAA production still relies on artist-placed fill lights.**

**Why**:
1. Fill lights are cheap — just deferred shading samples
2. Deterministic — artists have full control
3. Responsive — no convergence, no baking, no update lag
4. Scene-specific — artists can optimize for gameplay

**Typical Interior Setup** (from GDC lighting talks):

```cpp
// Sun CSM
DirectionalLight sun {
    intensity = 1.0f,
    shadow_cascade_count = 4,
    shadow_bias = {0.01f, 0.015f, 0.025f, 0.05f}
};

// Irradiance volume or DDGI probes
IrradianceVolume ambient_probes {
    grid_dims = {8, 8, 8},
    cell_size = 2.0f  // Coarse grid
};

// Fill lights (artist-placed)
PointLight fill_1 {
    pos = {10, 2, 5},
    color = {0.8, 0.75, 0.6},      // Slightly warm
    intensity = 0.5f,
    range = 15.0f
};

PointLight fill_2 {
    pos = {-10, 1.5, 8},
    color = {0.6, 0.7, 0.8},       // Slightly cool (shadow side)
    intensity = 0.3f,
    range = 12.0f
};

// Local occlusion
ScreenSpaceAO {
    intensity = 0.8f,
    radius = 0.5f,
    power = 2.0f
};

// Contact shadows
ContactShadows {
    thickness = 0.05f,           // 5cm ray thickness
    max_distance = 1.0f,         // 1m ray length
    samples = 6,
    fade_distance = 2.0f
};
```

### 4.2 Frostbite's Hybrid in Shipped Titles

**Battlefield 3 & Franchise** (2011–2021):
1. **Outdoor**: CSM (4 cascades) + Enlighten GI
2. **Indoor**: 
   - CSM sun (low bias for exterior windows)
   - Irradiance volumes (coarse 2m grid)
   - Many fill lights (5–15 per room)
   - Contact shadows for ledges/crevices
   - SSAO for depth
3. **Dynamic Objects**: Per-object GI probes (small grid around moving entity)

**Star Wars Battlefront II** (2017):
1. CSM for sun
2. Flux (precomputed static GI)
3. Enlighten semi-dynamic GI for moved objects
4. Many fill lights for cinematics
5. DXR preview (real-time GI for art iteration, disabled in shipping)

### 4.3 CryEngine's Hybrid in Shipped Titles

**Crysis 2 & 3** (2011–2013):
1. SVOGI for dynamic GI
2. Ambient light "maps" (sparse grid of precomputed ambient per region)
3. Many fill lights
4. No contact shadows (too expensive for console)

**CryEngine 5+ Projects**:
1. SVOGI with 2–3 cascades
2. Artist-controlled ambient grid modulation
3. Voxel-based shadow occlusion (VXAO)
4. Fill lights as needed

---

## Part 5: Shadow Propagation Details

### 5.1 The CSM Problem Indoors

**Issue**: Cascade frustum is a cuboid in world space. Interior pixels are outside all cuboids.

**Visualization**:
```
World space:
┌─────────────────────────────┐
│        Sun Cascade 3        │  ← Covers 200–500m distance
│   ┌─────────────────────┐   │
│   │   Sun Cascade 2     │   │  ← Covers 60–200m distance
│   │   ┌──────────────┐  │   │
│   │   │ Sun Cascade1 │  │   │  ← Covers 15–60m distance
│   │   │ ┌────────┐   │  │   │
│   │   │ │Cascade0│   │  │   │  ← Covers 0–15m distance (detail)
│   │   │ └────────┘   │  │   │
│   │   └──────────────┘  │   │
│   └─────────────────────┘   │
└─────────────────────────────┘

Building:    ───────────────────
             │                 │
             │ Interior pixel  │  ← OUTSIDE all cascades
             │ (X, Y, Z)       │
             │                 │
             ───────────────────
```

**Result**: When sampling CSM for interior pixel, all cascades are "out of bounds" → defaults to fully lit (shadow = 1.0). But pixel is indoors, should be dark.

### 5.2 Frostbite's Solution: Cascade Culling Mask

Instead of discarding out-of-bounds samples, Frostbite generates an **L8 validity mask**:

```hlsl
// Pass 1: Generate mask
void GenerateCSMCullingMask(float4 world_pos, Texture2D culling_mask_out)
{
    // For each cascade
    uint mask = 0;
    for (uint cas = 0; cas < 4; ++cas) {
        float4 cascade_min = cascade_bounds[cas].min_bounds;
        float4 cascade_max = cascade_bounds[cas].max_bounds;
        
        bool in_bounds = all(world_pos >= cascade_min && world_pos <= cascade_max);
        if (in_bounds) {
            mask |= (1 << cas);
        }
    }
    
    // Write to texture
    uint2 screen_pos = (uint2)input.screen_pos;
    culling_mask_out[screen_pos] = mask;
}

// Pass 2: Sample CSM with mask
float SampleCSMWithCulling(float4 world_pos, Texture2D culling_mask)
{
    uint2 screen_pos = GetScreenPos();
    uint mask = culling_mask[screen_pos].r;
    
    float shadow = 1.0;
    
    // Only sample cascades that are valid
    for (uint cas = 0; cas < 4; ++cas) {
        if ((mask & (1 << cas)) != 0) {
            shadow = min(shadow, SampleCascade(world_pos, cas));
            break;  // Use finest valid cascade
        }
    }
    
    return shadow;
}
```

**Result**: Interior pixels either (a) use a valid cascade if geometry protrudes, or (b) ignore CSM entirely and rely on fill lights + GI probes.

### 5.3 CryEngine's Voxel Occlusion

Instead of RSM/CSM, SVOGI uses **voxelized geometry** for occlusion:

```hlsl
float GetOcclusionFromVoxels(float3 world_pos, VoxelOctree voxels)
{
    // Trace upward ray (toward sky)
    RayDesc ray;
    ray.Origin = world_pos + float3(0, 0.1, 0);  // Slight offset
    ray.Direction = float3(0, 1, 0);
    ray.TMin = 0.01;
    ray.TMax = 10.0;  // Check up to 10m
    
    // Cone trace through voxel octree
    float3 occlusion = TraceVoxelCone(ray, voxels);
    
    return occlusion.a;  // Alpha = occlusion amount
}
```

**Advantage**: Occlusion is based on actual geometry (voxels), not RSM shadow resolution.

---

## Part 6: Energy Balance & Parameter Tuning

### 6.1 The Problem: Why Interiors Are Dark

Simplified radiosity equation:
```
L_out(x, ω_out) = L_emit(x, ω_out) 
                + ∫_hemisphere L_in(x, ω_in) · f_r(ω_in, ω_out) · cos(θ) dω_in
```

**Outdoor (sunny)**: L_in includes direct sun (~65,000 lux), sky (~10,000 lux).
**Indoor (window)**: L_in is only what enters through window (~200–500 lux).

**With CSM shadow inside**: CSM returns 0 (fully shadowed) → only ambient remains.

### 6.2 Ambient Fill Calculation

**Goal**: Make interior match sun-lit outdoor brightness without looking flat.

**Technique**: Multi-layer approach:
1. **Direct Sun** (CSM): 0.0–1.0 (0 inside buildings)
2. **Probe-Based Indirect**: 0.2–0.4 (from irradiance volumes)
3. **Fill Light Ambient**: 0.3–0.6 (from placed lights)
4. **SSAO Darkening**: ×(1 − AO × 0.6) (prevent flatness)

**Example Shader** (Frostbite-style):

```hlsl
float3 ComputeIndirectLighting(float3 world_pos, float3 normal, 
                               IrradianceVolume probes,
                               ScreenSpaceAO ssao)
{
    // Probe-based diffuse
    float3 probe_irradiance = EvaluateProbes(world_pos, normal, probes);
    
    // SSAO modulation
    float ao = SampleSSAO(GetScreenPos());
    float ao_effect = mix(1.0, ao, 0.6);  // 60% darkening from AO
    
    // Final indirect
    float3 indirect = probe_irradiance * ao_effect;
    
    return indirect;
}

float3 ComputeFinalLighting(float3 world_pos, float3 normal, 
                           float3 albedo, float3 normal_map,
                           float roughness, float metallic,
                           DirectionalLight sun, IrradianceVolume probes)
{
    // Direct lighting
    float sun_shadow = SampleCSMWithCulling(world_pos);
    float3 direct = albedo * sun.color * sun.intensity * sun_shadow;
    
    // Indirect: combination of probes + fill lights
    float3 indirect = ComputeIndirectLighting(world_pos, normal, probes, ssao);
    
    // For interior pixels, sun_shadow ≈ 0, so direct ≈ 0
    // Indirect must be strong enough to light the scene
    
    return direct + indirect;
}
```

### 6.3 Parameter Tuning for Interiors

**Frostbite Enlighten tuning**:
```cpp
enlighten_config.max_ambient = 0.4f;          // Max ambient from probes
enlighten_config.ambient_power = 1.2f;        // Gamma adjust (brighten)
enlighten_config.probe_validity_threshold = 0.05f;  // Discard dark probes
enlighten_config.probe_smoothing_radius = 1.5f;     // Blur probes
```

**CryEngine SVOGI tuning**:
```
r_SVOGI_VoxelOpacityTarget = 0.2        // How opaque voxels become
r_SVOGI_TracingAmbient = 0.5            // Ambient fallback when no hit
r_SVOGI_Denoise_Radius = 2              // Spatial blur (reduce noise)
r_SVOGI_DiffuseBounces = 2              // Num propagation iterations
```

**DDGI tuning**:
```cpp
ddgi_config.probeBlendingRate = 0.1f;  // Slower = more stable but sluggish
ddgi_config.normalBias = 0.15f;        // Offset rays away from surface (anti-leak)
ddgi_config.viewBias = 0.15f;          // Camera-relative offset
ddgi_config.probeGridDimensions = {16, 8, 16};  // Denser grid = higher quality
```

---

## Part 7: Comparison Table

| Aspect | Frostbite | CryEngine | DDGI |
|--------|-----------|----------|------|
| **GI Type** | Baked (Enlighten) + semi-dynamic | Real-time volumetric | Real-time ray-traced |
| **Baking Required** | Yes (5–30 min) | No | No |
| **Dynamic Objects** | Limited (per-object probes) | Full (SVOGI updates every frame) | Full (probes update every frame) |
| **Light Leaking** | Moderate (probe grid can leak) | Moderate (voxel aliasing) | Minimal (ray occlusion) |
| **Resolution** | Medium (probe spacing) | High (voxel cone tracing) | Medium (6D irradiance) |
| **Specular Support** | IBL only | Limited (6 cone directions) | No (diffuse only) |
| **Memory** | Low (SH coeffs + lightmaps) | Medium (octree bricks) | Low (irradiance + distance) |
| **GPU Cost** | Low (2–3 ms probe sampling) | Medium (5–8 ms voxel tracing) | High (15–25 ms, 64 rays/probe) |
| **Console Support** | Yes (PS4, Xbox One) | Yes (PS3/X360 for LPV; SVOGI for current gen) | No (DXR/RT core required) |
| **Artist Control** | Good (place probes, tune bake) | Good (cascade count, voxel size) | Medium (probe spacing, blend rate) |
| **Production Use** | BF3, Battlefront, FIFA, Madden | Crysis 2/3, CryEngine 5+ projects | Emerging (NVIDIA tech demos) |

---

## Part 8: Practical Implementation Strategy for UzEngine

### 8.1 Recommended Hybrid

For a game engine targeting PC/console:

**Phase 1 (Minimal)**:
1. CSM with cascade culling mask (prevent leaking indoors)
2. Artist-placed fill lights (5–10 per interior space)
3. GTAO for contact shadows + depth
4. Manual light probe grids (L2 SH coefficients)

**Phase 2 (Medium)**:
1. Enlighten or Flux-style lightmap baking
2. Irradiance volumes (coarse 2m spacing)
3. Contact shadows for crevices
4. Per-light per-object shadow caching

**Phase 3 (Advanced)**:
1. DDGI (if DXR available)
2. Voxel-based secondary occlusion
3. Temporal stability (frame blending)
4. Cascade-based quality LOD

### 8.2 Critical Implementation Details

**CSM Culling Mask** (required for interiors):
```cpp
// src/engine/renderer/passes/shadow_pass.cpp

void GenerateCSMCullingMask(const FrameContext& ctx) {
    auto cmd = ctx.gfx->BeginPass("CSMCullingMask");
    cmd->SetPipeline(culling_mask_pso);
    
    for (uint cas = 0; cas < 4; ++cas) {
        cmd->SetConstant("cascade_bounds_min", cascade_bounds[cas].min);
        cmd->SetConstant("cascade_bounds_max", cascade_bounds[cas].max);
        cmd->SetConstant("cascade_index", cas);
    }
    
    cmd->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
    cmd->EndPass();
    
    // Subsequent CSM sampling respects this mask
}
```

**Probe Grid Evaluation**:
```hlsl
// shaders/runtime/include/gi_common.hlsli

float3 EvaluateProbes(float3 world_pos, float3 world_normal, 
                     StructuredBuffer<float4> probe_sh_data,
                     uint3 probe_dims, float3 probe_origin, float probe_spacing)
{
    // Trilinear interpolation
    float3 grid_pos = (world_pos - probe_origin) / probe_spacing;
    uint3 base = uint3(floor(grid_pos));
    float3 frac = frac(grid_pos);
    
    float3 irradiance = 0;
    float total_weight = 0;
    
    for (int i = 0; i < 8; ++i) {
        uint3 probe_coord = base + uint3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        
        // Bounds check
        if (any(probe_coord >= probe_dims)) continue;
        
        uint probe_idx = probe_coord.x + probe_coord.y * probe_dims.x + 
                        probe_coord.z * probe_dims.x * probe_dims.y;
        
        // Fetch 9 SH coefficients (L0 + L1) for this probe
        float4 sh0 = probe_sh_data[probe_idx * 3 + 0];  // L0 + L1m
        float4 sh1 = probe_sh_data[probe_idx * 3 + 1];  // L1 + L1p
        float4 sh2 = probe_sh_data[probe_idx * 3 + 2];  // L2
        
        // Evaluate SH at world_normal
        float3 probe_irradiance = EvaluateSH(sh0, sh1, sh2, world_normal);
        
        // Trilinear weight
        float3 w = i & 1 ? frac : (1 - frac);
        float weight = w.x * w.y * w.z;
        
        irradiance += probe_irradiance * weight;
        total_weight += weight;
    }
    
    return irradiance / max(total_weight, 0.001);
}
```

### 8.3 References for Implementation

- **Enlighten integration**: See [Frostbite Enlighten docs](https://www.ea.com/frostbite/news/precomputed-global-illumination-in-frostbite)
- **DDGI**: [NVIDIA RTXGI GitHub](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- **Contact shadows**: [Sterna 2018 contact-hardening soft shadows paper](https://wojtsterna.com/wp-content/uploads/2023/02/contact_hardening_soft_shadows.pdf)
- **GTAO**: [Jimenez et al. 2016](https://www.researchgate.net/publication/301997301_Practical_Real-Time_Strategies_for_Accurate_Indirect_Occlusion)
- **SVOGI**: [CryEngine 5 SVOGI documentation](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535599)

---

## Summary

**Indoor lighting is solved by combining THREE layers**:

1. **Direct Sun** — CSM with proper culling (no contribution indoors)
2. **Indirect GI** — Probes/SVOGI/DDGI (must be computed, not guessed)
3. **Fill Lights** — Artist-placed point lights (deterministic, cheap)

**No single system is sufficient.** Frostbite combines Enlighten + many fill lights. CryEngine combines SVOGI + many fill lights. DDGI is newer and doesn't require baking, but needs DXR and still benefits from fill lights for efficiency.

**For UzEngine**, recommend starting with **Phase 1**: CSM culling + fill lights + GTAO. This is industry-proven, CPU-efficient, and scales to console hardware.

---

## Sources

- [O'Donnell 2018 "Precomputed Global Illumination in Frostbite"](https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc2018-precomputedgiobalilluminationinfrostbite.pdf) — GDC 2018
- [Kaplanyan et al. 2009 "Light Propagation Volumes in CryEngine 3"](https://advances.realtimerendering.com/s2009/Light_Propagation_Volumes.pdf) — SIGGRAPH 2009 Advances
- [Apers 2024 "Shipping Dynamic GI"](https://www.advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf) — SIGGRAPH 2024 (GIBS)
- [Jimenez et al. 2016 "Practical Real-Time Strategies for Accurate Indirect Occlusion"](https://www.researchgate.net/publication/301997301_Practical_Real-Time_Strategies_for_Accurate_Indirect_Occlusion) — ACM SIGGRAPH
- [Morgan 2019 "Dynamic Diffuse Global Illumination"](https://morgan3d.github.io/articles/2019-04-01-ddgi/) — NVIDIA Technical Blog / Morgan3D
- [Sterna 2018 "Contact-Hardening Soft Shadows Made Fast"](https://wojtsterna.com/wp-content/uploads/2023/02/contact_hardening_soft_shadows.pdf)
- [CryEngine Documentation: Voxel-Based Global Illumination (SVOGI)](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/25535599)
- [NVIDIA RTXGI-DDGI GitHub](https://github.com/NVIDIAGameWorks/RTXGI-DDGI)
- [Frostbite: Lighting you up in Battlefield 3](https://www.ea.com/frostbite/news/lighting-you-up-in-battlefield-3) — GDC 2011
