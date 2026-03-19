<!-- doc-meta
  title: Sky & Atmosphere Architecture for UzEngine
  description: Concrete directives for implementing sky rendering, IBL radiance baking, and atmosphere in UzEngine, adapted from Godot 4 reference analysis.
  status: complete
  tags: [uzengine, sky, atmosphere, ibl, architecture, directive]
  created: 2026-03-15
  by: engine-architect
-->

# Sky & Atmosphere Architecture for UzEngine

**Status**: Design + Direction (post-Godot reference analysis)

**Goal**: Implement sky rendering with real-time IBL radiance baking, using Godot's lean approach adapted to UzEngine's architecture.

---

## Executive Summary

**Problem**: UzEngine needs sky rendering, atmosphere, and real-time IBL radiance generation. UE5's SkyAtmosphereComponent is complex; Godot's simpler design is a better fit.

**Solution**: Implement a single `SkyPass` that:
1. Renders sky material to octahedral radiance texture (256×256)
2. Bakes one roughness layer per frame (7-frame cycle)
3. Integrates fixed fog directly in sky shader
4. Triggers IBL re-bind when material/position/time changes

**Key Decisions**:
- **Octahedral encoding** (not cubemap) — simpler, more efficient
- **Material-encapsulated variants** (not parent switch) — no RTTI
- **Snapshot dirty tracking** (not observers) — cheap, self-contained
- **Multi-frame baking** (not monolithic) — preserves 60 FPS

---

## 1. Core Abstractions

### 1.1 SkyMaterial Interface

**File**: `runtime/engine/renderer/core/sky_material.h`

```cpp
/// Base class for all sky material types. Each material decides its own shader variant
/// based on feature flags (debanding, textures, etc.). No parent switch-on-type.
class SkyMaterial {
public:
    virtual ~SkyMaterial() = default;

    /// Returns the shader variant RID (must be cached by subclass).
    /// Variants are selected via feature flags, e.g., shader_cache[has_texture ? 1 : 0].
    virtual RID GetShaderVariant() const = 0;

    /// Invalidates sky radiance (triggers re-bake next frame).
    /// Called when material parameters change.
    virtual void Invalidate() = 0;

    /// Returns a hash of all material parameters. Used for dirty tracking
    /// without dynamic_cast or RTTI.
    virtual uint64_t GetParameterHash() const = 0;

    /// True if sky depends on TIME (animate). Invalidates every frame if true.
    virtual bool UsesTIme() const { return false; }

    /// True if sky depends on CAMERA POSITION (parallax). Invalidates on camera move.
    virtual bool UsesPosition() const { return false; }

    /// Set a named parameter. Name strings are hashed to u32 for fast comparison.
    /// E.g., SetParameter(uz::StringId("sky_top_color"), Color(0.6f, 0.8f, 1.0f))
    virtual void SetParameter(uz::StringId name, const Variant& value) = 0;

    /// Get parameter by name. Returns Variant::null() if not found.
    virtual Variant GetParameter(uz::StringId name) const = 0;
};
```

### 1.2 ProceduralSkyMaterial

**File**: `runtime/engine/renderer/core/procedural_sky_material.cpp/.h`

```cpp
/// Simple gradient-based sky with optional clouds texture.
/// Variants: 4 (debanding yes/no × clouds yes/no)
class ProceduralSkyMaterial : public SkyMaterial {
public:
    struct Desc {
        Vec3 sky_top_color = Vec3(0.6f, 0.8f, 1.0f);
        Vec3 sky_horizon_color = Vec3(0.2f, 0.4f, 0.8f);
        float sky_curve = 1.0f;
        float sky_energy_multiplier = 1.0f;

        RHITextureHandle sky_cover = nullptr;  // Optional clouds texture
        Vec3 sky_cover_tint = Vec3(1.0f);

        Vec3 ground_bottom_color = Vec3(0.1f, 0.1f, 0.1f);
        Vec3 ground_horizon_color = Vec3(0.3f, 0.3f, 0.35f);
        float ground_curve = 1.0f;
        float ground_energy_multiplier = 1.0f;

        float sun_angle_max = 30.0f;  // Degrees
        float sun_curve = 1.0f;

        bool use_debanding = true;
    };

    explicit ProceduralSkyMaterial(const Desc& desc);
    ~ProceduralSkyMaterial();

    RID GetShaderVariant() const override;
    void Invalidate() override;
    uint64_t GetParameterHash() const override;
    void SetParameter(uz::StringId name, const Variant& value) override;
    Variant GetParameter(uz::StringId name) const override;

private:
    // Shader variant: shader_cache[int(use_debanding) + (has_sky_cover ? 2 : 0)]
    RID shader_cache_[4];  // Always allocate all 4, use different indices

    // Parameter storage
    Vec3 sky_top_color_;
    Vec3 sky_horizon_color_;
    float sky_curve_;
    float sky_energy_multiplier_;
    RHITextureHandle sky_cover_;  // Null if not used
    Vec3 sky_cover_tint_;
    // ... (ground, sun params)
    bool use_debanding_;

    uint64_t parameter_hash_ = 0;

    /// Called when feature flags change (e.g., sky_cover is set/unset).
    /// Re-compiles shader variant if needed.
    void UpdateShaderVariant_();

    /// Updates parameter_hash_ to reflect current state.
    void UpdateParameterHash_();
};
```

### 1.3 Sky Radiance Texture Hierarchy

**File**: `runtime/engine/renderer/core/sky_radiance.h`

```cpp
/// Octahedral radiance texture hierarchy for real-time IBL.
/// Stores base octahedral map (256×256) + downsampled + roughness mipmaps (7 layers).
/// Uses **octahedral encoding** (not cubemap) for memory efficiency and simpler filtering.
struct SkyRadiance {
    /// Base rendered sky texture (256×256 octahedral, RGBA16F HDR).
    RHITextureHandle base_octahedral;

    /// Downsampled source for fast filtering (128×128).
    RHITextureHandle downsampled_octahedral;

    /// Per-roughness layers (7 mipmaps for roughness 0.0→1.0).
    /// layers[0] = low roughness (mirror reflections)
    /// layers[6] = high roughness (diffuse-like)
    struct RoughnessLayer {
        RHITextureHandle mip0;  // 128×128
        RHITextureHandle mip1;  // 64×64
        RHITextureHandle mip2;  // 32×32
        RHITextureHandle mip3;  // 16×16
        RHITextureHandle mip4;  // 8×8
        RHITextureHandle mip5;  // 4×4
        RHITextureHandle mip6;  // 2×2
    };
    RoughnessLayer layers[7];

    /// Frame index for multi-frame baking (0..6, cycles every 7 frames).
    int processing_layer = 0;

    /// True if radiance needs to be re-rendered/re-baked this frame.
    bool dirty = true;

    /// Octahedral border size in UV space (prevents cross-edge sampling artifacts).
    float uv_border_size = 1.0f / 256.0f;

    /// Estimated exposure/EV from sky luminance (for post-processing).
    float baked_exposure = 1.0f;
};
```

---

## 2. SkyPass Rendering

**File**: `runtime/engine/renderer/passes/sky_pass.h/.cpp`

### 2.1 SkyPassDesc

```cpp
/// Configuration for sky rendering pass.
struct SkyPassDesc {
    rg::RgResourceId render_target;      ///< Target framebuffer (RenderTarget state)
    rg::RgResourceId depth_buffer;       ///< Depth buffer (DepthStencilWrite)
    RHITextureHandle depth_texture;      ///< Physical depth for D3D12 begin_render_pass

    const SkyMaterial* material;         ///< Sky material (ProceduralSkyMaterial, etc.)
    SkyRadiance* radiance;               ///< Radiance texture hierarchy (output)

    Vec3 camera_position;                ///< World space camera position
    Quat camera_orientation;             ///< Camera rotation (basis)
    float aspect_ratio;                  ///< Viewport width / height
    float fov_degrees;                   ///< Vertical FOV in degrees
    float time_total;                    ///< Elapsed time since start
    float exposure_ev;                   ///< Exposure value (EV) from auto-exposure
    float luminance_multiplier;          ///< Brightness scale for tone mapping
};

/// Fullscreen sky rendering pass.
class SkyPass {
public:
    void Execute(const RenderGraphExecutor& exec, const SkyPassDesc& desc);

private:
    // Push constants (96 bytes, fits in D3D12 128 limit)
    struct SkyPushConstant {
        float orientation[12];           // 3×3 camera basis matrix
        float projection[4];             // (scale_x, scale_y, offset_x, offset_y)
        float position[3];               // Camera world pos
        float time;                      // Total elapsed time
        float border_size[2];            // Octmap border in UV space
        float luminance_multiplier;      // HDR exposure
        float brightness_multiplier;     // Extra brightness control
    };

    RHIPipelineHandle sky_pipeline_;    ///< Compiled sky.hlsl shader PSO
};
```

### 2.2 SkyPass::Execute() Logic

```cpp
void SkyPass::Execute(const RenderGraphExecutor& exec, const SkyPassDesc& desc) {
    // Step 1: Check if radiance needs update
    bool needs_radiance_update = desc.radiance->dirty;
    if (desc.material->UsesTime()) {
        needs_radiance_update = true;  // Re-bake every frame
    }

    // Step 2: If dirty, render sky shader to base octahedral texture
    if (needs_radiance_update) {
        exec.SetRenderTarget(desc.radiance->base_octahedral, desc.depth_buffer);

        SkyPushConstant pc;
        pc.orientation = desc.camera_orientation.to_matrix3().to_float_array();
        pc.position = { desc.camera_position.x, desc.camera_position.y, desc.camera_position.z };
        pc.time = desc.time_total;
        pc.luminance_multiplier = desc.exposure_ev;
        pc.brightness_multiplier = desc.luminance_multiplier;
        pc.border_size[0] = pc.border_size[1] = desc.radiance->uv_border_size;

        // Compute projection: tan(fov/2) = 1 / projection.y
        float tan_half_fov = tanf(desc.fov_degrees * DEG_TO_RAD * 0.5f);
        pc.projection[0] = tan_half_fov;           // scale_x (uncorrected for aspect)
        pc.projection[1] = tan_half_fov;           // scale_y
        pc.projection[2] = 0.0f;                   // offset_x (no viewport offset)
        pc.projection[3] = 0.0f;                   // offset_y

        exec.BindPipeline(sky_pipeline_);
        exec.PushConstants(&pc, sizeof(pc));
        exec.BindDescriptors(0, desc.material->GetShaderVariant());  // Material UBO

        // Draw fullscreen triangle (no vertex buffer)
        exec.Draw(3, 0);

        desc.radiance->dirty = false;
    }

    // Step 3: Multi-frame baking — process one roughness layer this frame
    // (Amortized cost: 7 frames to fully bake all layers)
    {
        int layer_idx = desc.radiance->processing_layer;
        desc.radiance->processing_layer = (layer_idx + 1) % 7;

        // Downsample if layer == 0
        if (layer_idx == 0) {
            exec.Dispatch("Octmap Downsample",
                octmap_downsample_cs_,
                { desc.radiance->base_octahedral, desc.radiance->downsampled_octahedral },
                { 128, 128, 1 });
        }

        // Importance-sample (GGX) this layer
        // Input: downsampled_octahedral
        // Output: layers[layer_idx].mip0
        float roughness = float(layer_idx) / 6.0f;  // 0.0→1.0 across 7 layers
        exec.Dispatch("Octmap GGX Filter",
            octmap_ggx_filter_cs_,
            { desc.radiance->downsampled_octahedral, desc.radiance->layers[layer_idx].mip0 },
            { 128, 128, 1 },
            /* roughness = */ roughness);

        // Then downsample mips (1→6) via box filter
        for (int m = 0; m < 6; ++m) {
            RHITextureHandle src = (m == 0) ? desc.radiance->layers[layer_idx].mip0
                                             : desc.radiance->layers[layer_idx].mipmaps[m];
            RHITextureHandle dst = desc.radiance->layers[layer_idx].mipmaps[m + 1];
            Size2i size = { 128 >> (m + 1), 128 >> (m + 1) };

            exec.Dispatch("Octmap Downsample Mip",
                octmap_box_filter_cs_,
                { src, dst },
                { std::max(1, size.x / 8), std::max(1, size.y / 8), 1 });
        }
    }

    // Render sky also to main render target (after lighting pass)
    exec.SetRenderTarget(desc.render_target, desc.depth_buffer);
    // (repeat sky shader bind + draw)
}
```

---

## 3. Dirty Tracking

**File**: `runtime/engine/renderer/core/sky_dirty_tracker.h`

```cpp
/// Tracks when sky needs to be re-baked based on camera/material state.
/// Uses cheap snapshot comparison (no observers, no callbacks).
struct SkyDirtyTracker {
    /// Last known state
    Vec3 last_camera_position;
    float last_time;
    uint64_t last_material_hash;

    bool UpdateAndCheck(const Vec3& current_pos, float current_time,
                        const SkyMaterial* material) {
        bool dirty = false;

        // Check material parameters changed
        if (current_time > last_time + 0.001f && material->UsesTime()) {
            dirty = true;
        }

        // Check camera moved (threshold: 0.1 units)
        if (material->UsesPosition() &&
            !current_pos.IsEqual(last_camera_position, 0.1f)) {
            dirty = true;
        }

        // Check material parameters changed (via hash)
        uint64_t material_hash = material->GetParameterHash();
        if (material_hash != last_material_hash) {
            dirty = true;
        }

        // Update snapshots
        if (dirty) {
            last_camera_position = current_pos;
            last_time = current_time;
            last_material_hash = material_hash;
        }

        return dirty;
    }
};
```

---

## 4. Fog Integration

**File**: `runtime/shaders/runtime/environment/sky.hlsl`

```hlsl
// ─────────────────────────────────────────────────────────────────────────────
// Sky & Atmosphere with Fixed Fog
// ─────────────────────────────────────────────────────────────────────────────

cbuffer SkyMaterialCB : register(b0) {
    float3 sky_top_color;
    float sky_energy_multiplier;
    float3 sky_horizon_color;
    float sky_curve;
    // ... (ground, sun params)
};

cbuffer EnvironmentCB : register(b1) {
    float3 sun_direction;
    float sun_energy;
    float3 sun_color;
    float sun_disk_scale;

    // Fog parameters
    bool fog_enabled;
    float fog_density;
    float fog_sky_affect;
    float fog_sun_scatter;
    float3 fog_light_color;
    float fog_aerial_perspective;
};

// Main sky shader entry point
float4 PSMain(float2 uv : SV_Position) : SV_TARGET {
    // Unproject screen UV to view direction
    float3 view_dir = UnprojectScreenUV(uv);
    view_dir = normalize(mul((float3x3)orientation_matrix, view_dir));

    // Compute procedural sky (Rayleigh/Mie or gradient)
    float3 sky_color = ComputeProcedualSky(view_dir);

    // Apply fixed fog
    if (fog_enabled) {
        float3 fog_color = lerp(fog_light_color, sky_color, fog_aerial_perspective);

        // Sun scatter effect (Mie-like)
        float sun_dot = max(dot(view_dir, sun_direction), 0.0);
        float sun_scatter = pow(sun_dot, 8.0);  // Henyey-Greenstein approximation
        fog_color += sun_color * sun_energy * sun_scatter * fog_sun_scatter;

        // Blend fog with sky
        sky_color = lerp(sky_color, fog_color, fog_sky_affect);
    }

    return float4(sky_color, 1.0);
}

// Procedural sky (gradient + Rayleigh/Mie)
float3 ComputeProcedualSky(float3 view_dir) {
    // Simple gradient for MVP
    float h = normalize(view_dir).y;  // Horizon gradient (-1 to 1)
    h = saturate(h);

    float3 top = sky_top_color * sky_energy_multiplier;
    float3 horizon = sky_horizon_color * sky_energy_multiplier;

    // Apply curve (exponential falloff toward horizon)
    float curve_factor = pow(saturate(1.0 - abs(h)), sky_curve);
    return lerp(horizon, top, curve_factor);
}
```

---

## 5. Octahedral Encoding Helpers

**File**: `runtime/shaders/include/octahedral.hlsli`

```hlsl
/// Encode 3D direction to 2D octahedral UV (normalized to [0,1])
float2 EncodeOctahedral(float3 n) {
    // Fold space such that all octants map to (-1, 1)
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 uv = (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * sign(n.xy);
    return uv * 0.5 + 0.5;
}

/// Decode 2D octahedral UV to 3D direction
float3 DecodeOctahedral(float2 uv) {
    uv = uv * 2.0 - 1.0;
    float3 n = float3(uv, 1.0 - dot(abs(uv), 1.0.xx));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    }
    return normalize(n);
}

/// With border handling (prevents cross-edge artifacts)
float2 EncodeOctahedralWithBorder(float3 n, float border_size) {
    float2 uv = EncodeOctahedral(n);
    uv = lerp(border_size, 1.0 - border_size, uv);
    return uv;
}
```

---

## 6. Compute Shaders for Filtering

### 6.1 Octahedral Downsampling

**File**: `runtime/shaders/compute/octmap_downsample.hlsl`

```hlsl
// Simple 2×2 box filter for octahedral textures
Texture2D<float4> source : register(t0);
RWTexture2D<float4> dest : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID) {
    uint2 dst_coord = dispatch_id.xy;
    uint2 src_coord = dst_coord * 2;

    // 2×2 box filter
    float4 sample0 = source[src_coord];
    float4 sample1 = source[src_coord + uint2(1, 0)];
    float4 sample2 = source[src_coord + uint2(0, 1)];
    float4 sample3 = source[src_coord + uint2(1, 1)];

    float4 result = (sample0 + sample1 + sample2 + sample3) * 0.25;
    dest[dst_coord] = result;
}
```

### 6.2 GGX Importance Sampling

**File**: `runtime/shaders/compute/octmap_ggx_filter.hlsl`

```hlsl
// GGX importance sampling for roughness layer generation
Texture2D<float4> source : register(t0);  // Downsampled radiance
RWTexture2D<float4> dest : register(u0);   // Output roughness layer

cbuffer FilterParams : register(b0) {
    float roughness;
    uint num_samples;  // 64, 256, or 1024 depending on quality
    float2 pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID) {
    uint2 dst_coord = dispatch_id.xy;
    float2 uv = float2(dst_coord) / 128.0;  // Normalize to [0,1]

    float3 N = DecodeOctahedral(uv);
    float3 V = N;  // For diffuse-like sampling, V == N
    float3 L_sum = 0.0;
    float weight_sum = 0.0;

    // GGX importance sampling
    float alpha = roughness * roughness;

    for (uint i = 0; i < num_samples; ++i) {
        float2 xi = Hammersley(i, num_samples);

        // Convert to spherical coords via importance sampling
        float cos_theta = sqrt((1.0 - xi.x) / (1.0 + (alpha * alpha - 1.0) * xi.x));
        float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        float phi = 2.0 * PI * xi.y;

        // Convert to world space (tangent to normal)
        float3 H = SphericalToCartesian(sin_theta, cos_theta, phi);
        H = TangentToWorld(N, H);

        // Sample radiance at H direction
        float2 sample_uv = EncodeOctahedral(H);
        float3 sample_color = source.SampleLevel(linear_sampler, sample_uv, 0).rgb;

        // Weight by solid angle (for importance sampling)
        float solid_angle = 4.0 * PI / float(num_samples);
        L_sum += sample_color * solid_angle;
        weight_sum += solid_angle;
    }

    float3 result = L_sum / weight_sum;
    dest[dst_coord] = float4(result, 1.0);
}
```

---

## 7. Implementation Order (Roadmap)

### Step 1: SkyMaterial Interface + ProceduralSkyMaterial
- Create `sky_material.h` with interface
- Implement `procedural_sky_material.cpp` with 4 variants
- Shader variant selection logic
- Dirty tracking via parameter hash

**Deliverable**: Can create and modify sky material parameters.

### Step 2: SkyRadiance Texture Allocation
- Allocate octahedral textures (256×256 base, 128×128 downsampled, 7 layers)
- Create texture views for each mipmap
- Set up descriptor heap indices

**Deliverable**: Textures allocated, ready for rendering.

### Step 3: SkyPass Render-to-Texture
- Write sky.hlsl shader (simple gradient for MVP)
- Implement SkyPass::Execute() to render sky to base texture
- Push constants setup (orientation, projection, time)
- Integrate into render graph after lighting pass

**Deliverable**: See sky gradient rendered fullscreen; sky visible in viewport.

### Step 4: Multi-Frame Layer Baking
- Implement octmap_downsample compute shader
- Implement octmap_ggx_filter compute shader
- Process one roughness layer per frame (0..6 cycle)
- No frame stalls

**Deliverable**: 7-frame IBL generation cycle; smooth 60 FPS.

### Step 5: Dirty Tracking Integration
- Connect SkyDirtyTracker to scene renderer
- Check on each frame before rendering
- Set `radiance->dirty = true` when material/pos/time changes

**Deliverable**: Sky only re-bakes when needed (reduced redundant work).

### Step 6: Fog Integration
- Add fog parameters to EnvironmentCB
- Integrate fog_process() into sky.hlsl
- Wire up fog enable/density/color from scene

**Deliverable**: Fog visible in sky rendering; integrates seamlessly.

### Step 7: PhysicalSkyMaterial (Phase 2)
- Add Rayleigh/Mie computation to shader
- Expose parameters in PhysicalSkyMaterial
- Add night_sky texture support

**Deliverable**: Physically-plausible sky with time-of-day simulation.

---

## 8. Key Design Decisions Explained

### Why Octahedral, Not Cubemap?

| Factor | Octahedral | Cubemap |
|--------|-----------|---------|
| **Memory** | 1 texture, 2 coords | 6 faces, 3 coords + face index |
| **Filtering** | Pure 2D kernel | 2D kernel + edge wrap logic |
| **Cache efficiency** | Better (single texture) | Worse (face boundaries) |
| **Seam artifacts** | None (edges are continuous) | Cross-face seams |
| **Shader complexity** | Simpler (no face selection) | More complex |

**Godot's rationale**: "Octahedral because it's simpler and more efficient." We adopt this.

### Why Multi-Frame, Not Monolithic?

**Monolithic** (bad):
```cpp
// Process all 7 layers in one frame → GPU stall
for (int i = 1; i < 7; ++i) {
    importance_sample(i);
}
// Frame 30ms stall on GPU
```

**Multi-frame** (good):
```cpp
// Process 1 layer per frame, 7-frame cycle
importance_sample(processing_layer % 7);
processing_layer++;
// Frame 4ms increase distributed over 7 frames
// User sees smooth 60 FPS
```

### Why Snapshot Dirty Tracking, Not Observers?

**Observers** (bad):
- Every material parameter change fires callback
- Callback invalidates sky → may trigger re-renders
- Complex dependency graph (observer → dispatcher → sky)
- Cognitive overhead for developers

**Snapshots** (good):
- Each frame: `if (pos != last_pos) dirty = true;`
- 3 float comparisons (~0.01ms)
- No callbacks, no observers
- Godot's proven approach

---

## 9. Testing & Validation

### Unit Tests

```cpp
// tests/renderer/test_sky_material.cpp
TEST(ProceduralSkyMaterial, VariantSelectionWithoutDebanding) {
    ProceduralSkyMaterial::Desc desc;
    desc.use_debanding = false;
    desc.sky_cover = nullptr;

    ProceduralSkyMaterial mat(desc);
    EXPECT_EQ(mat.GetShaderVariant(), shader_cache[0]);
}

TEST(ProceduralSkyMaterial, VariantSelectionWithDebandingAndClouds) {
    ProceduralSkyMaterial::Desc desc;
    desc.use_debanding = true;
    desc.sky_cover = test_texture;

    ProceduralSkyMaterial mat(desc);
    EXPECT_EQ(mat.GetShaderVariant(), shader_cache[3]);
}

TEST(SkyRadiance, DirtyTrackingDetectsMaterialChange) {
    SkyDirtyTracker tracker;
    ProceduralSkyMaterial mat1, mat2;

    EXPECT_FALSE(tracker.UpdateAndCheck(pos, time, &mat1));  // First call
    EXPECT_FALSE(tracker.UpdateAndCheck(pos, time, &mat1));  // Same material

    EXPECT_TRUE(tracker.UpdateAndCheck(pos, time, &mat2));   // Different material
}
```

### Visual Tests (Screenshots)

```bash
# Render sky to screenshot
demo.exe --screenshot ./sky_gradient.png

# Should see:
# - Top: blue sky
# - Horizon: lighter blue
# - Ground: dark gray
# - Sun disk: sharp disk outline (if enabled)

# Compare against reference
compare_images sky_gradient.png expected_sky_gradient.png
```

---

## 10. Future Extensions

### Volumetric Fog (Phase 3)
- Separate compute shader: volumetric_fog.hlsl
- Renders into 3D texture (160×90×64 with temporal reprojection)
- Sky samples volumetric texture independently

### PhysicalSkyMaterial with Bruneton (Phase 3)
- Extend Rayleigh/Mie to multi-scattering lookup tables
- Pre-compute scattering coefficients
- Real-time atmosphere matching reference spectral sky

### Sky Cubemap Baking (Editor)
- Bake 6-face cubemap from octahedral at any resolution
- Save as .ktx2 for offline IBL in other engines
- Editor tool: "Export Sky as Cubemap..."

---

## References

- **Godot Reference Analysis**: `docs/analysis/rendering/godot-sky-atmosphere-architecture.md`
- **UzEngine Renderer Passes**: `docs/engine/12-rendering/20-renderer-passes.md`
- **Octahedral Mapping**: Cigolle et al., "A Study of Efficient Real-time 2D Rendering"
- **GGX Importance Sampling**: https://learnopengl.com/PBR/IBL/Specular-IBL

