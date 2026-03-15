<!-- doc-meta
title: UE5 Sky Atmosphere → UzEngine Adaptation Patterns
description: Concrete patterns and data structures from UE5 sky atmosphere that can be adapted for UzEngine C++/HLSL implementation.
date: 2026-03-15
status: complete
tags: [sky, atmosphere, adaptation, c++, hlsl, render-graph, lut]
-->

# UE5 Sky Atmosphere → UzEngine Adaptation Patterns

This document extracts concrete architectural patterns from UE5's sky atmosphere system and proposes UzEngine equivalents.

---

## 1. Scene Proxy Pattern: Component → Renderer Decoupling

### UE5 Pattern

```cpp
// Engine/Classes/Components/SkyAtmosphereComponent.h
UCLASS(ClassGroup = Rendering, editinlinenew, meta = (BlueprintSpawnableComponent))
class USkyAtmosphereComponent : public USceneComponent
{
  float BottomRadius;          // User-editable (main thread)
  FLinearColor RayleighScattering;
  float MultiScatteringFactor;
  // ... 30+ parameters
};

// Renderer/Private/SceneProxies/SkyAtmosphereSceneProxy.h
class FSkyAtmosphereSceneProxy : public FPrimitiveSceneProxy
{
public:
  FSkyAtmosphereSceneProxy(const USkyAtmosphereComponent& InComponent);
  virtual void GetDynamicMeshElements(...) override final;

private:
  // Immutable copies of component state (set at proxy creation)
  FVector PlanetCenterKm;
  float BottomRadiusKm;
  float TopRadiusKm;
  float MultiScatteringFactor;
  // ... rest of parameters
};

// Renderer/Private/SkyAtmosphereRendering.h
class FSkyAtmosphereRenderSceneInfo
{
public:
  explicit FSkyAtmosphereRenderSceneInfo(FSkyAtmosphereSceneProxy& Proxy);

  // GPU-ready uniform buffer (regenerated on proxy changes)
  TUniformBufferRef<FAtmosphereUniformShaderParameters>& GetAtmosphereUniformBuffer()
  { return AtmosphereUniformBuffer; }

private:
  FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy;
  FAtmosphereUniformShaderParameters AtmosphereUniformShaderParameters;
  TUniformBufferRef<FAtmosphereUniformShaderParameters> AtmosphereUniformBuffer;

  TRefCountPtr<IPooledRenderTarget> TransmittanceLutTexture;
  TRefCountPtr<IPooledRenderTarget> MultiScatteredLuminanceLutTexture;
};
```

**Why this matters**: Component changes are rare. By using a proxy, UE5 can:
- Update CPU-side parameters without touching GPU
- Batch changes for render thread
- Invalidate LUTs only when atmosphere setup changes
- Keep renderer thread-safe (never reads Component directly)

### UzEngine Adaptation

UzEngine already has `SceneEnvironment` gatherer. Extend it with:

```cpp
// runtime/engine/renderer/core/sky_atmosphere.h
struct SkyAtmosphereParams
{
  // Planet
  Vec3 planet_center_km;
  float bottom_radius_km = 6371.0f;   // Earth
  float top_radius_km = 6471.0f;      // +100 km atmosphere
  float atmosphere_height_km = 100.0f;

  // Rayleigh (molecules, ~8.5 km scale height)
  float rayleigh_scale_height_km = 8.5f;
  Vec3 rayleigh_scattering_1_per_km;  // Precomputed from RGB wavelength
  float rayleigh_scale = 1.0f;

  // Mie (particles, ~1.2 km scale height)
  float mie_scale_height_km = 1.2f;
  Vec3 mie_scattering_1_per_km;
  Vec3 mie_extinction_1_per_km;
  Vec3 mie_absorption_1_per_km;
  float mie_anisotropy_g = 0.8f;  // Forward scattering bias
  float mie_scale = 1.0f;

  // Absorption (ozone)
  float absorption_scale = 1.0f;
  Vec3 absorption_extinction_1_per_km;
  struct TentDist {
    float peak_altitude_km;
    float peak_value;
    float width_km;
  } absorption_tent;

  // Art direction
  Vec4 ground_albedo;
  Vec4 sky_luminance_factor = Vec4(1.0f);
  float multi_scattering_factor = 2.0f;
  float aerial_perspective_start_depth_km = 0.0f;
  float height_fog_contribution = 1.0f;

  // Derived (computed at upload time)
  float transmittance_min_elevation_angle_rad = 0.0f;

  // Hashing: if these change, invalidate LUTs
  uint64_t hash() const;
};

// Persistent during scene lifetime
class SkyAtmosphere
{
  SkyAtmosphereParams params;
  uint64_t params_hash = 0;

  // GPU resources (regenerated on hash change)
  Handle<Texture> transmittance_lut;      // 256×64
  Handle<Texture> multiscatter_lut;       // 32×32
  Handle<Texture> sky_view_lut;           // 192×104
  Handle<Texture> aerial_perspective_vol; // 32×32×16

  // Uniform buffer (direct copy of params)
  Handle<ConstantBuffer> atmosphere_cb;

  void on_params_changed(const SkyAtmosphereParams& new_params);
  void regenerate_luts(RenderGraph& graph);
};
```

**Key insight**: Don't regenerate LUTs every frame. Hash component params, regenerate only on change.

---

## 2. Lookup Table Architecture

### UE5 Pattern: Four-Tier LUT Hierarchy

```
Transmittance LUT (256×64)
├─ Precomputed once per atmosphere setup
├─ Input: (ViewHeight [km], ViewZenithCosAngle)
├─ Output: Transmittance factor (RGBA16F)
└─ Used in: MultiScatter generation, runtime sampling

MultiScattering LUT (32×32)
├─ Dual-scattering approximation (ground bounce + secondary)
├─ Input: (LightZenithCosAngle, ViewHeight)
├─ Output: Luminance (RGBA16F)
├─ Factor applied at runtime: 1.0 + MultiScatteringFactor * LUT
└─ Used in: Sky rendering, IBL capture

Sky-View LUT (192×104, per-view)
├─ Pre-marched full hemisphere
├─ Input: (ViewZenithCosAngle, AzimuthAngle)
├─ Output: Sky luminance (RGB10A2 or RGBA16F)
├─ Replaces 32-sample ray-march per pixel
└─ Enable/disable via r.SkyAtmosphere.FastSkyLUT

Aerial Perspective Volume (32×32×16 per-view)
├─ Froxel grid in view-frustum space
├─ Depth non-linear: sqrt(z/zmax) emphasizes near
├─ Input: (ScreenUV, DepthSlice)
├─ Output: (Luminance RGB, Transmittance A)
└─ Applied in BasePass before lighting
```

### UzEngine Adaptation: Compute-Pass Based LUT Generation

```cpp
// runtime/engine/renderer/passes/sky_atmosphere_pass.h

class SkyAtmosphereLutPass : public RenderPass
{
  // Compute passes (one per LUT)
  class TransmittanceLutCS : public ComputeShader { /* ... */ };
  class MultiScatteringLutCS : public ComputeShader { /* ... */ };
  class SkyViewLutCS : public ComputeShader { /* ... */ };
  class AerialPerspectiveVolumeCS : public ComputeShader { /* ... */ };

public:
  struct Input {
    const SkyAtmosphere* sky_atmosphere;
    Handle<Texture> transmittance_lut_out;  // 256×64, RG16F
    Handle<Texture> multiscatter_lut_out;   // 32×32, RGBA16F
    Handle<Texture> sky_view_lut_out;       // 192×104, RGB10A2
    Handle<Texture> aerial_perspective_out; // 32×32×16, RGBA16F
  };

  virtual void execute(RenderGraph& graph, const Input& input) override;
};

// Dispatch sequence (in RenderGraph)
void render_sky_atmosphere_luts(
  RenderGraph& graph,
  SkyAtmosphere* sky_atm,
  bool force_regenerate = false)
{
  // Only regenerate if params changed or force flag
  if (!force_regenerate && sky_atm->params_hash == sky_atm->cached_hash)
  {
    return;  // LUTs already valid
  }

  // Compute passes in order (each depends on previous)
  {
    auto pass = graph.add_compute_pass("transmittance_lut");
    pass->add_output(sky_atm->transmittance_lut);
    pass->execute([&](ComputeContext& ctx) {
      // Dispatch 256×64 threads, 1 per output pixel
      // Sample atmosphere density at many points along ray
      ctx.dispatch(Vec3(16, 4, 1));  // 256/16 × 64/4 = 16×16 groups
    });
  }

  {
    auto pass = graph.add_compute_pass("multiscatter_lut");
    pass->add_input(sky_atm->transmittance_lut);  // Dependency
    pass->add_output(sky_atm->multiscatter_lut);
    pass->execute([&](ComputeContext& ctx) {
      ctx.dispatch(Vec3(2, 2, 1));  // 32×32 with 16×16 threads
    });
  }

  // ... similar for sky_view_lut, aerial_perspective_vol

  sky_atm->cached_hash = sky_atm->params_hash;
}
```

### Key Insight: LUT Dimensions

| LUT | Dimensions | Purpose | Precision | Typical Size |
|-----|-----------|---------|-----------|---------|
| Transmittance | 256×64 | View height + zenith angle lookup | RGBA16F | 256 KB |
| MultiScatter | 32×32 | Simple 2-bounce ground bounce | RGBA16F | 16 KB |
| SkyView | 192×104 (per-view) | Full sky hemisphere | RGB10A2 | 80 KB |
| AerialPerspective | 32×32×16 (per-view) | 3D fog in frustum | RGBA16F | 1 MB per-view |

**Total per-view**: ~1.4 MB (acceptable for UzEngine with typical 1-4 views)

---

## 3. Aerial Perspective Volume (3D Fog)

### UE5 Pattern

```cpp
struct FSkyAtmosphereRenderContext
{
  // Input camera position, viewport
  FViewMatrices* ViewMatrices;
  FIntRect Viewport;

  // Output volumes
  FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolume;     // Main 3D fog
  FRDGTextureRef SkyAtmosphereCameraAerialPerspectiveVolumeMieOnly;
  FRDGTextureRef SkyAtmosphereViewLutTexture;
};

// In shader, sample per-pixel
float4 GetAerialPerspectiveLuminanceTransmittance(
  float4 NDC,                                    // Normalized device coords
  float3 WorldPositionRelativeToCamera,
  Texture3D<float4> APVolume,
  float VolumeDepthResolution,       // 16 slices
  float VolumeDepthSliceLengthKm,    // 4 km/slice = 64 km total
  float StartDepthKm)
{
  // Non-linear depth for near-focus
  float tDepth = max(0.0, length(WorldPositionRelativeToCamera) - StartDepthKm);
  float LinearW = tDepth / (16.0 * 4.0);           // Linear in [0, 1]
  float NonLinW = sqrt(LinearW);                   // sqrt for detail near camera
  float NonLinSlice = NonLinW * 16.0;              // Slice index [0, 16]

  // Froxel coordinates
  float2 ScreenUv = NDC.xy / NDC.w * 0.5 + 0.5;
  float3 FroxelCoord = float3(ScreenUv, NonLinW);

  // Sample with near-fade
  float4 AP = Texture3DSample(APVolume, Sampler, FroxelCoord);
  float Weight = NonLinSlice < sqrt(0.5) ? NonLinSlice*NonLinSlice*2.0 : 1.0;
  AP.rgb *= Weight;
  AP.a = 1.0 - Weight*(1.0 - AP.a);

  return AP;
}
```

### UzEngine Adaptation

```cpp
// runtime/engine/renderer/core/aerial_perspective.h

/// 3D froxel volume for accumulated atmospheric fog.
///
/// Layout: screen 32×32 XY, depth 16 slices Z with sqrt distribution.
/// Each froxel stores integrated light scattering + transmittance from
/// camera to that cell.
///
/// Depth mapping: non-linear to emphasize near distances
/// - Linear depth = actual_distance_km / total_depth_km
/// - Froxel depth W = sqrt(LinearDepth)  → detail near camera
/// - Total range: 0 to 64 km (16 slices × 4 km/slice)
///
/// Sampling: in any shader, read froxel at (screen uv, NonLinW) and blend
/// with scene color using transmittance alpha.
struct AerialPerspectiveVolume
{
  static constexpr uint32_t GRID_X = 32;
  static constexpr uint32_t GRID_Y = 32;
  static constexpr uint32_t GRID_Z = 16;
  static constexpr float SLICE_LENGTH_KM = 4.0f;
  static constexpr float TOTAL_DEPTH_KM = GRID_Z * SLICE_LENGTH_KM;  // 64 km

  /// Upload per-frame for this view
  struct FrameData {
    Vec2 start_depth_km;           // (AP start, fade region size)
    float _pad[2];
  } frame_data;

  Handle<Texture> volume_tex;     // Texture3D, 32×32×16, RGBA16F

  /// Calculate non-linear depth coordinate for given world distance
  static inline float calc_froxel_depth_w(float distance_km, float start_depth_km)
  {
    float linear_depth = max(0.0f, distance_km - start_depth_km) / TOTAL_DEPTH_KM;
    return sqrt(saturate(linear_depth));  // Sqrt for detail compression
  }

  /// Sample froxel at screen position and depth
  static inline Vec4 sample(
    const AerialPerspectiveVolume& vol,
    Vec2 screen_uv,     // [0, 1]
    float distance_km,
    const Sampler& sampler)
  {
    float w = calc_froxel_depth_w(distance_km, vol.frame_data.start_depth_km.x);
    return sample_texture(vol.volume_tex, Vec3(screen_uv, w), sampler);
  }
};
```

**In-shader usage** (pixel shader, deferred):

```hlsl
// Deferred lighting pass
float4 ps_deferred_with_aerial_perspective(
  float4 ndc : SV_Position,
  float4 world_pos : TEXCOORD0) : SV_Target
{
  // ... standard deferred lighting ...
  float3 scene_color = compute_lighting(...);

  // Sample aerial perspective
  Vec2 screen_uv = ndc.xy / viewport_size;
  float distance_km = length(world_pos.xyz - camera_pos) * 0.00001;  // cm→km
  Vec4 ap = aerial_perspective_volume.sample(screen_uv, distance_km, linear_sampler);

  // Blend: AP as additive light with transmittance
  return float4(
    scene_color * ap.a + ap.rgb,  // Multiply by transmittance, add luminance
    1.0
  );
}
```

---

## 4. Rendering Integration Points

### UE5 Pattern: Where Sky Renders

```cpp
// In DeferredShadingRenderer
void FSceneRenderer::Render(FRDGBuilder& GraphBuilder)
{
  // 1. Pre-pass (optional async compute)
  if (GetSkyAtmospherePassLocation() == BeforePrePass)
  {
    AddSkyAtmosphereLookUpTablePass(GraphBuilder);
  }

  // 2. Nanite rasterization (can run in parallel with sky LUT compute)
  RenderNaniteEarlyZ(...);

  // 3. Base pass (BasePass has depth, renders geometry)
  RenderBasePass(...);

  // 4. Post-process (may render sky if !bRenderInMainPass)
  if (GetSkyAtmospherePassLocation() == BeforeBasePass)
  {
    AddSkyAtmosphereLookUpTablePass(GraphBuilder);
  }
  RenderPostProcess(...);
}
```

**Key**: Sky LUTs are async compute (can overlap). Sky pixels render before BasePass (depth not yet written where sky is).

### UzEngine Adaptation

UzEngine uses render graph. Sky atmosphere should integrate as:

```cpp
// runtime/engine/renderer/scene_renderer.cpp

void SceneRenderer::render_sky_atmosphere(RenderGraph& graph)
{
  // Skip if no sky atmosphere in scene
  if (!scene_->sky_atmosphere) return;
  if (!should_render_sky_atmosphere()) return;

  const SkyAtmosphere& sky = *scene_->sky_atmosphere;

  // 1. Regenerate LUTs if atmosphere params changed
  render_sky_atmosphere_luts(graph, const_cast<SkyAtmosphere*>(&sky));

  // 2. Render aerial perspective volume (per-view, before base-pass)
  {
    auto pass = graph.add_compute_pass("aerial_perspective_volume");
    for (const View& view : views_)
    {
      render_aerial_perspective_for_view(pass, sky, view);
    }
  }

  // 3. Sky pixels rendered as quad in post-process
  //    (or in base-pass if bRenderInMainPass, but that requires custom material)
}
```

---

## 5. Height Fog Integration

### UE5 Pattern

```cpp
// FogRendering.h
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters,)
  // Original exponential height fog parameters
  SHADER_PARAMETER(FVector4f, ExponentialFogParameters)
  // ...

  // Sky atmosphere contribution
  SHADER_PARAMETER(FVector4f, SkyAtmosphereAmbientContributionColorScale)

  // Light inscattered from sky
  SHADER_PARAMETER(FVector4f, InscatteringLightDirection)
  SHADER_PARAMETER(FVector4f, DirectionalInscatteringColor)
  SHADER_PARAMETER_TEXTURE(TextureCube, FogInscatteringColorCubemap)  // Sky capture
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// In SetupFogUniformParameters():
void SetupFogUniformParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View,
  FFogUniformParameters& OutParameters, bool bForRealtimeSkyCapture)
{
  // If SkyAtmosphere exists and HeightFogContribution > 0:
  if (Scene->GetSkyAtmosphereSceneInfo())
  {
    // Sample transmittance along light direction
    float3 SunTransmittance = GetSkyAtmosphereTransmittanceAtView(...);

    // Tint fog color by atmosphere
    OutParameters.SkyAtmosphereAmbientContributionColorScale =
      float4(SkyAtmosphereColor.rgb, HeightFogContribution);

    // Use sky cubemap as inscattering source
    OutParameters.FogInscatteringColorCubemap = SkyEnvironmentCubemap;
  }
}
```

### UzEngine Adaptation

```cpp
// runtime/engine/renderer/core/fog_parameters.h

struct FogParameters
{
  // Height fog (standard exponential)
  float density = 0.01f;
  float height_falloff = 0.2f;
  Vec4 fog_color = Vec4(0.5f, 0.6f, 0.7f, 1.0f);
  float inscattering_brightness = 1.0f;
  float distance_scale = 1.0f;

  // Sky atmosphere modulation (if SkyAtmosphere present)
  float sky_atmosphere_contribution = 1.0f;  // 0-1, blend factor
  Handle<Texture> sky_cubemap;               // Sky IBL cubemap
  Vec3 sky_ambient_color;                    // Sampled from SkyAtmosphere
};

// In upload_fog_parameters():
void upload_fog_parameters(
  const SceneEnvironment& env,
  const SkyAtmosphere* sky_atm,
  const Vec3& camera_pos,
  const Vec3& light_direction,
  FogParameters& out_params)
{
  // Fetch from environment
  if (env.height_fog_component)
  {
    out_params.density = env.height_fog_component->density;
    out_params.fog_color = env.height_fog_component->fog_color;
    // ...
  }

  // Modulate by sky atmosphere if present
  if (sky_atm && out_params.sky_atmosphere_contribution > 0.0f)
  {
    // Get transmittance from camera to light
    float3 transmittance = sample_transmittance_lut(
      sky_atm,
      camera_height_km,
      dot(light_direction, Vec3::UP));

    // Tint fog by transmission color
    float3 sky_color = sample_sky_color(sky_atm->sky_view_lut, light_direction);
    out_params.fog_color.rgb = mix(
      out_params.fog_color.rgb,
      sky_color * transmittance,
      out_params.sky_atmosphere_contribution);

    out_params.sky_cubemap = sky_atm->ibl_cubemap;
    out_params.sky_ambient_color = transmittance;
  }
}
```

**In fog shader**:

```hlsl
float3 apply_fog(float3 scene_color, float distance, FogParameters fog)
{
  // Standard exponential fog
  float fog_factor = exp(-fog.density * distance);

  // Add inscattering from sky
  float3 sky_light = sample_cubemap(fog.sky_cubemap, world_pos_norm);
  float3 inscatter = sky_light * (1.0 - fog_factor) * fog.inscattering_brightness;

  return scene_color * fog_factor + fog.fog_color.rgb * (1.0 - fog_factor) + inscatter;
}
```

---

## 6. Real-Time IBL Capture

### UE5 Pattern

```cpp
// SkyAtmosphereRendering.cpp: lines 1467+

if (Scene->SkyLight && Scene->SkyLight->bRealTimeCaptureEnabled)
{
  // Render sky atmosphere to panorama (360° view-independent)
  const FRDGTextureRef ReflectionCaptureSkyAtmosphereViewLutTexture =
    GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(
      FIntPoint(SkyViewLutWidth, SkyViewLutHeight),
      PF_R10G10B10A2,  // RGB10A2
      FClearValueBinding::Black,
      TexCreate_ShaderResource | TexCreate_UAV));

  // Shader: RenderSkyAtmosphereViewLutCS with View.RenderingReflectionCaptureMask != 0
  // Output: Panorama format (theta, phi) → (RGB color)

  // Store for reflection capture system
  Scene->RealTimeReflectionCaptureSkyAtmosphereViewLutTexture =
    ConvertToExternalAccessTexture(GraphBuilder, ReflectionCaptureSkyAtmosphereViewLutTexture);
}

// Separately: if scene has volumetric cloud, also capture aerial perspective
// as 360° projection for reflections
```

### UzEngine Adaptation

```cpp
// runtime/engine/renderer/core/ibl_capture.h

/// Real-time IBL capture of sky atmosphere
class SkyAtmosphereIBLCapture
{
public:
  /// Render panorama (equirect format) of current sky atmosphere
  /// Output: panorama_out (size 512×256 recommended)
  static void render_panorama(
    RenderGraph& graph,
    const SkyAtmosphere& sky,
    Handle<Texture> panorama_out);

  /// Bake panorama into cubemap (6×256 faces)
  /// Typical workflow: render_panorama() → bake_to_cubemap() → use in IBL
  static void bake_to_cubemap(
    RenderGraph& graph,
    Handle<Texture> panorama_in,
    Handle<Texture> cubemap_out);

  /// Generate BRDF lookup table (one-time, integrates panorama with GGX)
  /// Output: 256×256 texture (RGB = specular, A = for view-dep fallback)
  static void generate_specular_brdf_lut(
    RenderGraph& graph,
    Handle<Texture> panorama,
    Handle<Texture> brdf_lut_out);
};
```

---

## 7. CPU-Side Parameter Hashing

### Pattern: Change Detection Without Regenerating Every Frame

```cpp
// runtime/engine/renderer/core/sky_atmosphere.h

uint64_t SkyAtmosphereParams::hash() const
{
  // Hash all mutable fields
  uz::Hasher h;
  h.update(&bottom_radius_km, sizeof(bottom_radius_km));
  h.update(&atmosphere_height_km, sizeof(atmosphere_height_km));
  h.update(&rayleigh_scattering_1_per_km, sizeof(rayleigh_scattering_1_per_km));
  h.update(&rayleigh_scale_height_km, sizeof(rayleigh_scale_height_km));
  h.update(&mie_scattering_1_per_km, sizeof(mie_scattering_1_per_km));
  h.update(&mie_extinction_1_per_km, sizeof(mie_extinction_1_per_km));
  h.update(&mie_anisotropy_g, sizeof(mie_anisotropy_g));
  h.update(&absorption_extinction_1_per_km, sizeof(absorption_extinction_1_per_km));
  h.update(&ground_albedo, sizeof(ground_albedo));
  h.update(&multi_scattering_factor, sizeof(multi_scattering_factor));
  return h.finalize();
}

// In render loop:
void SceneRenderer::render_sky_atmosphere(RenderGraph& graph)
{
  const SkyAtmosphere& sky = *scene_->sky_atmosphere;
  uint64_t current_hash = sky.params.hash();

  if (current_hash != sky.cached_hash)
  {
    UZ_LOG(Renderer, Info, "Sky atmosphere params changed, regenerating LUTs");
    regenerate_luts(graph);
    sky.cached_hash = current_hash;
  }
}
```

**Cost**: One hash call per frame (~200 bytes) = negligible. Saves massive GPU work (LUT regens).

---

## 8. Summary: Key Takeaways for UzEngine

| Aspect | UE5 Pattern | UzEngine Adaptation |
|--------|------------|-------------------|
| **Component → GPU** | SceneProxy + cached uniform buffer | SkyAtmosphere singleton + param hash |
| **Lookup Tables** | 4-tier: Transmittance, MultiScatter, SkyView, AP | Same structure, compute-based generation |
| **Pass Placement** | Optional async compute, depth-test optimization | Async compute via RenderGraph, early exits |
| **Aerial Perspective** | 3D froxel volume (32×32×16), non-linear depth | Same, but precomputed frame-wise |
| **Height Fog Integration** | Unified FFogUniformParameters | FogParameters struct with sky modulation |
| **IBL Capture** | Real-time reflection capture support | SkyAtmosphereIBLCapture static helpers |
| **Performance** | Cvars for all quality tiers | Cvars: FastSky, AsyncCompute, SampleCounts |

---

## 9. Implementation Checklist for UzEngine

- [ ] Define `SkyAtmosphereParams` struct with all ~30 properties
- [ ] Implement `params.hash()` for change detection
- [ ] Create `SkyAtmosphere` resource class (persistent per-scene)
- [ ] Write compute shaders for 4 LUT passes (HLSL SM6.6)
- [ ] Add to SceneEnvironment gatherer (singleton pattern)
- [ ] Render graph integration: `render_sky_atmosphere_luts()` pass
- [ ] Aerial perspective volume: per-view texture allocation + compute
- [ ] Height fog modulation: integrate with existing fog system
- [ ] IBL cubemap export for reflection captures
- [ ] Lua API: allow script-side tweaking of parameters
- [ ] Console variables: FastSkyLUT, SampleCounts, AsyncCompute toggles
- [ ] Unit tests: LUT generation correctness, hash collision rate
- [ ] Demo: sky scene with day/night cycle, visual validation

