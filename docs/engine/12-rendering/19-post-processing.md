# Post-Processing System

> **Status**: Done (v0.5.0 — TAA, SSR, GTAO, bloom, tonemap, auto-exposure, DOF, motion blur, color grading, SSGI, SVGF)
> **Source**: `engine/post-process/`, `engine/subsystem/post_process_world.h`
> **References**: UE5 `PostProcessVolume` + `PostProcessSettings`, Godot `Environment` + `WorldEnvironment`

## Purpose

Post-processing applies image-space effects to the final rendered frame. These effects run after all geometry is shaded and are blended together in a fixed pipeline order. A **PostProcessVolume** system allows artists to customize effects per-level region — effects fade based on camera position (inside/outside volumes).

## PostProcessVolume System

A volume is an invisible axis-aligned bounding box that modifies post-process settings within its bounds:

```cpp
/// Defines a region where post-process effects are applied and blended.
struct PostProcessVolume {
    AABB     bounds;
    bool     is_infinite;  // if true, affects entire level (e.g., "default" volume)
    float    priority;     // higher = blends in first
    bool     enabled;

    PostProcessSettings settings;  // all effect parameters
};

/// Blend post-process settings by distance to camera.
/// If camera is inside a volume, blend_weight = 1.0
/// If outside, blend_weight = distance_to_box / blend_distance
PostProcessSettings BlendVolumes(
    const Vec3& camera_pos,
    const std::vector<PostProcessVolume>& volumes
);
```

**Why volumes?** Different areas of a level may need different aesthetics:
- Indoor office: cool white-point, low bloom, film grain off
- Desert exterior: warm color grade, high bloom, dust vignette
- Horror dungeon: desaturated, chromatic aberration, vignette sharp

## Effect Chain Order

All post-process effects execute in this fixed order. Each effect reads from the previous stage's output:

```
1. SSAO (Screen-Space Ambient Occlusion)
   └─ Darkens crevices (modulates ambient light)

2. SSR (Screen-Space Reflections)
   └─ Real-time reflections in screen space (denoised)

3. Lighting Composite
   └─ Combine direct + indirect lighting

4. Bloom (Threshold + Downsample + Upsample)
   └─ Bright areas glow, halo effect

5. Depth of Field
   └─ Blur out-of-focus areas

6. Motion Blur
   └─ Streaks trailing fast motion

7. Temporal Upscaling (TAA or TSR)
   └─ Resolve jittered samples over time with optional resolution scaling

8. Tone Mapping
   └─ Convert HDR to SDR display range

9. Color Grading
   └─ Apply 3D LUT (look-up table) for mood/style

10. Film Grain
    └─ Add noise for texture/age

11. Vignette
    └─ Darken screen edges

12. Final UI Composite
    └─ Render 2D UI on top
```

**Key invariant:** Each effect is optional and disabled by default. Effects only run if `enabled = true` in the active PostProcessVolume.

## Temporal Upscaling (TAA / TSR) — Phase 2+

UzEngine abstracts temporal upscaling behind a unified `ITemporalUpscaler` interface, allowing seamless switching between TAA (lightweight single-pass) and TSR (multi-pass super-resolution).

```cpp
/// Abstract temporal upscaler interface (runtime/engine/renderer/passes/postprocess/temporal_upscaler.h)
enum class UpscalerType : uint32_t {
    None = 0,  // No upscaling
    TAA  = 1,  // Single-pass TAA (aliasing only)
    TSR  = 2,  // Multi-pass TSR (aliasing + resolution scaling)
};

class ITemporalUpscaler {
    virtual void Execute(IRHICommandList* cmd, const UpscalerInput& input) = 0;
    virtual void OnResize(uint32_t displayWidth, uint32_t displayHeight) = 0;
    virtual const char* GetName() const = 0;
    virtual float GetMinScreenPercentage() const = 0;
    virtual float GetMaxScreenPercentage() const = 0;
};

// Factory: CreateTemporalUpscaler(UpscalerType type)
```

### TAA — Temporal Anti-Aliasing (Single-Pass, 100% Resolution Only)

**How TAA works:**
1. **Jitter camera projection** via Halton sequence (0.25–1.0 pixels, deterministic per-frame)
2. **Render scene** with jittered projection at display resolution
3. **Next frame**: reproject previous frame's history using **velocity buffer** (screen-space pixel motion)
   ```hlsl
   float2 velocity = velocityBuffer.Sample(sampler, uv).rg;
   float2 prevUV = uv - (velocity / displayDimensions);
   float4 history = historyBuffer.Sample(sampler, prevUV);
   ```
4. **Blend** current with reprojected history
   ```hlsl
   float blendWeight = 0.04;  // ~25-frame accumulation @source UE5 TemporalAA.cpp:41
   blendWeight *= lerp(0.04, 0.2, velocityLength);  // Adaptive: reduce weight at high motion
   output = lerp(current, history, blendWeight);
   ```
5. **Repeat** — over 8+ frames, jagged edges become smooth (equivalent to 2x–4x MSAA)

**Characteristics:**
- Single compute dispatch (~0.3ms)
- Smooth temporal accumulation over time
- No resolution scaling (screen percentage must be 100%)
- Velocity dilation + 5-tap bicubic history sampling built-in
- Good for stable rendering or when resolution scaling not needed

### TSR — Temporal Super Resolution (Multi-Pass, 50–200% Screen Percentage)

**How TSR works:**
1. **Velocity dilation (Pass 1)**: Search 3×3 neighborhood for closest depth, output dilated motion vectors. Prevents thin geometry from losing motion coverage.
2. **History rejection (Pass 2)**: Compare current frame luminance vs reprojected history in YCoCg color space. Compute neighborhood AABB to detect moire/flicker. Mark rejected pixels (>0.5 weight).
3. **History update (Pass 3)**:
   - Reproject history using dilated velocity
   - Clamp reprojected history to current 3×3 neighborhood (YCoCg AABB)
   - Adaptive blend: static pixels alpha~0.06, motion alpha~0.2, rejected alpha~1.0
   - Apply spatial anti-aliasing filter on rejected pixels (5-tap Gaussian)
   - Unjitter current frame by subtracting jitter offset
   - Optional sharpening via contrast-adaptive unsharp mask (RCAS)

**Characteristics:**
- 3-stage compute pipeline (~1.2ms total)
- Supports 50–200% screen percentage for resolution scaling
- Reduces temporal noise via multi-pass rejection and filtering
- Better handling of fast motion and disocclusion vs TAA
- Default upscaler (recommended for production)

### Screen Percentage System

Decouples render resolution from display resolution:

```
Screen Percentage = 100% → renderRes = displayRes (1:1)
Screen Percentage = 67%  → renderRes = 0.67 × displayRes (4K display, 1440p render)
Screen Percentage = 50%  → renderRes = 0.5 × displayRes (1440p display, 720p render)
```

All geometry (GBuffer, lighting, shadows) renders at `renderRes`. Only the final upscaler pass outputs at `displayRes`. Mipmap bias (`bias = log₂(screenPercentage/100)`) adjusts texture LOD to maintain visual quality.

**Velocity Buffer Integration**:
- Generated via depth reprojection in separate compute pass
- Stores screen-space motion vectors in pixels (RG16F)
- Sky pixels (depth ≈ 0 or ≈ 1) output zero velocity to prevent ghosting at horizon
- See [temporal-upscaling.md guide](../../guides/temporal-upscaling.md) for implementation details

### Dynamic Resolution Controller

Optional GPU-bound adaptive resolution scaling:

```cpp
DynamicResolutionController controller;
controller.SetTargetFPS(60.0);
controller.SetEnabled(true);

// Per frame:
float gpuFrameTime = rhi_query_gpu_time();
controller.Update(gpuFrameTime);
float screenPercentage = controller.GetScreenPercentage();  // 50-100%
```

- **Scale-down**: if gpuTime > targetTime × 0.95, reduce 5% immediately (instant response)
- **Scale-up**: if gpuTime < targetTime × 0.85 for 120+ frames, increase 5% (conservative hysteresis)
- Clamped to [minPct, maxPct] (default 50–100%)

For detailed API reference and configuration, see [temporal-upscaling.md guide](../../guides/temporal-upscaling.md).

## Screen-Space Ambient Occlusion (SSAO) — Phase 2

Darkens crevices and contact shadows using depth buffer:

```cpp
struct SSAOSettings {
    bool     enabled;
    float    radius;            // search radius (1-3 typical)
    int      sample_count;      // 16-32 samples per pixel
    float    intensity;         // darken amount
    float    bias;              // reduce acne (0.01-0.1)
    float    attenuation_distance; // fade out far occlusion
};
```

**Implementation (GTAO algorithm):**
1. Sample depth at radius offsets around each pixel
2. Measure angle to occluding horizon (how much depth increases)
3. Integrate horizon angles to estimate occlusion
4. Bilateral blur to denoise (half-res intermediate)
5. Upsample back to full resolution

**Why GTAO over HBAO?** More stable, fewer artifacts at depth discontinuities.

## Screen-Space Reflections (SSR) — Phase 2/3

Real-time reflections from on-screen geometry:

```cpp
struct SSRSettings {
    bool     enabled;
    int      max_steps;         // ray march iterations (32-64)
    float    step_size;         // march distance per step
    float    roughness_threshold; // only reflect if roughness < this
    float    fade_start;        // start fading at screen edge
    float    fade_width;        // fade width toward edge
    bool     use_hzb;           // use hierarchical z-buffer (faster)
};
```

**How SSR works:**
1. For each reflective pixel, compute reflection ray direction from normal + view
2. Step ray through screen space, checking depth buffer at each step
3. When step crosses surface (depth behind), record hit point
4. Sample color at hit point, apply falloff toward screen edge
5. Blend with pre-filtered reflection probes if no on-screen hit

**Limitations:** Only reflects on-screen geometry. Fallback to cubemap probes for off-screen reflections.

## Bloom — Phase 2

Extract bright pixels and blur them back onto screen for glow:

```cpp
struct BloomSettings {
    bool     enabled;
    float    threshold;         // luminance above which to bloom (1.0-10.0)
    float    threshold_knee;    // soft threshold transition (0.0-1.0)
    float    intensity;         // bloom brightness multiplier
    int      downsample_count;  // 4-5 levels (smaller = faster, softer bloom)
};
```

**Implementation:**
1. Downsample HDR framebuffer with Kawase filter (fast, quality), threshold bright pixels
2. Build mip chain (half-res, quarter-res, eighth-res, etc.)
3. Upsample from coarsest to finest, blurring at each level
4. Add upsampled bloom back to original frame

**Why Kawase over Gaussian?** Kawase is separable in a single pass, cheaper on bandwidth.

## Depth of Field (DoF) — Phase 2/3

Blur out-of-focus areas (shallow focus effect):

```cpp
struct DoFSettings {
    bool     enabled;
    float    focus_distance;    // distance in world units from camera
    float    focus_range;       // zone where focus is sharp
    float    max_blur_radius;   // max pixels to blur (10-50)
    bool     use_bokeh_shape;   // if true, bokeh = custom shape; else = circular
    ResourceHandle<Texture> bokeh_shape;  // texture for bokeh shape
};
```

**Implementation:**
1. Compute Circle of Confusion (CoC) from depth: `coc = abs(depth - focus_dist) / focus_range`
2. Separate passes: in-focus (CoC ≈ 0) sharper, out-of-focus blurred
3. Tile-based approach: divide screen into tiles, gather max CoC per tile
4. Shade with bokeh shape texture for realistic lens diffraction

## Motion Blur — Phase 2/3

Streak fast motion to convey speed:

```cpp
struct MotionBlurSettings {
    bool     enabled;
    float    max_blur_radius;   // max pixels per motion (clamped)
    int      sample_count;      // 8-16 samples along motion ray
    bool     per_object_blur;   // if false, use velocity buffer only (Phase 1.3.1+)
};
```

**How it works:**
1. **Sample velocity buffer** for each pixel (camera + object motion combined)
   ```hlsl
   float2 velocity = velocityBuffer.Sample(sampler, uv).rg;
   velocity = min(velocity, maxBlurRadius);  // Clamp to prevent extreme blur
   ```
2. **Step along motion vector** and sample color at multiple points
   ```hlsl
   float3 blurred = float3(0, 0, 0);
   for (int i = 0; i < SAMPLE_COUNT; ++i) {
       float t = i / (SAMPLE_COUNT - 1);
       float2 sampleUV = uv - (velocity / screenDimensions) * t;
       blurred += colorBuffer.Sample(sampler, sampleUV).rgb;
   }
   blurred /= SAMPLE_COUNT;
   ```
3. **Average samples** to produce blur streak
4. **Clamp velocity** to respect `max_blur_radius` (avoid ghosting on extremely fast motion)

**Velocity Buffer Integration** (Milestone 1.3.1+):
- Velocity is read directly from the RG16Float velocity buffer generated in the deferred pipeline
- Negative velocity = leftward/upward motion, positive = rightward/downward
- Sky pixels have zero velocity, naturally unblurred
- See [GBuffer Layout Guide](../../user/guides/rendering/gbuffer-layout.md#velocity-buffer-generation) for implementation details

## Color Grading — Phase 2

Apply artistic look-and-feel via **3D LUT** (Look-Up Table):

```cpp
struct ColorGradingSettings {
    bool     enabled;
    ResourceHandle<Texture> lut;  // 3D texture (neutral LUT + artist adjustments)
    float    intensity;           // blend LUT intensity

    // Alternative: per-channel adjustments (if no LUT)
    float    temperature;         // blue-shift / orange-shift (-50 to +50)
    float    tint;                // magenta-shift / green-shift (-50 to +50)

    struct Lift {
        Vec3 shadows;
        Vec3 midtones;
        Vec3 highlights;
    } lift;
};
```

**3D LUT workflow:**
1. Artist exports neutral LUT from engine (e.g., 16×16×16 RGB texture)
2. Apply adjustments in Photoshop / color grading tool (DaVinci Resolve, etc.)
3. Export back to game
4. Pixel shader: sample LUT at (R, G, B) coordinates, get graded output

## Film Grain — Phase 2

Add noise for analog film look:

```cpp
struct FilmGrainSettings {
    bool     enabled;
    float    intensity;         // grain strength (0.0-1.0)
    float    grain_size;        // pixel size of grain (0.5-2.0)
};
```

Grain is blue-noise based (perceptually smoother than white noise).

## Chromatic Aberration — Phase 3

Separate RGB channels slightly for lens distortion:

```cpp
struct ChromaticAberrationSettings {
    bool     enabled;
    float    intensity;  // channel separation amount
};
```

Offset R and B channels slightly outward from G, mimics cheap lens design.

## Vignette — Phase 2

Darken screen edges:

```cpp
struct VignetteSettings {
    bool     enabled;
    float    intensity;    // darkening strength
    float    radius;       // falloff radius from center
    float    smoothness;   // softness of transition
};
```

## Lens Flare — Phase 3

Sun/bright light creates lens flare (optional, expensive):

```cpp
struct LensFlareSettings {
    bool     enabled;
    Vec2     screen_position;  // position of bright source
    float    intensity;
    int      ghost_count;      // number of lens artifacts
};
```

## Quality Scalability — Phase 2/3

Each effect has a quality mode for performance tuning:

```cpp
enum class EffectQuality {
    Off,      // disabled
    Low,      // fewer samples, half-res intermediate
    Medium,   // normal samples, full-res
    High,     // extra samples, high-quality filters
    Ultra,    // max samples, multiple passes
};

struct PostProcessSettings {
    EffectQuality ssao_quality;
    EffectQuality ssr_quality;
    EffectQuality bloom_quality;
    EffectQuality dof_quality;
    EffectQuality motion_blur_quality;
    EffectQuality taa_quality;
    // ... etc
};
```

Game code can dynamically adjust quality per-effect based on GPU budget (frame time headroom).

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | TAA, SSAO (GTAO), SSR, Bloom, DoF, Motion Blur, Tone Mapping, Color Grading (3D LUT), Film Grain, Vignette. PostProcessVolume blending. Quality scalability per-effect. |
| **Phase 3** | Chromatic Aberration, Lens Flare, improved DoF (bokeh shapes), per-object motion blur velocity, Kuwahara filter stylization. |

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| Volume system | `PostProcessVolume` | `Environment` (global only) | **PostProcessVolume (per-region Phase 2)** |
| TAA | Yes (Nanite-aware) | Yes (basic) | **Yes Phase 2** |
| SSAO | DFAO (Lumen) or SSAO | SSAO (GTAO-like) | **GTAO Phase 2** |
| SSR | Yes (limited roughness) | Yes | **Yes Phase 2/3** |
| Bloom | Yes (dual filtering) | Yes | **Yes (Kawase) Phase 2** |
| DoF | Yes (gather-based) | Yes | **Yes Phase 2/3** |
| Motion Blur | Yes (per-object) | Yes | **Yes Phase 2/3** |
| Color Grading | LUT-based | LUT-based | **3D LUT Phase 2** |

## Rules

1. **NEVER** disable TAA in shipping — aliasing is too visible in motion
2. **ALWAYS** author color grades in 16-bit LUT texture (8-bit causes banding)
3. **NEVER** set bloom threshold to 0 — will brighten entire screen (disable instead)
4. **ALWAYS** dial quality modes at runtime based on frame time budget
5. **NEVER** enable all high-quality effects simultaneously — profile which are most impactful
