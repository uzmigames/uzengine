# Post-Processing System

> **Status**: Planned (Phases 2–3)
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

7. Temporal Anti-Aliasing (TAA)
   └─ Resolve jittered samples over time

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

## Temporal Anti-Aliasing (TAA) — Phase 2

Smooth jagged edges by rendering at jittered sub-pixels and averaging over time:

```cpp
/// TAA resolves aliasing via temporal history.
struct TAASettings {
    bool     enabled;
    float    jitter_scale;      // offset sub-pixel amount (0.25-1.0 pixels)
    float    feedback_min;      // minimum history weight (0.8 typical)
    float    feedback_max;      // maximum history weight (0.95 typical)
    float    ghosting_reduction; // reject old history if color changes too much
    bool     use_velocity;       // enable velocity-based rejection (Phase 1.3.1+)
};
```

**How TAA works:**
1. **Jitter camera projection** by random offset (0.25-1.0 pixels) each frame
2. **Render scene** with jittered projection, save frame to history texture
3. **Next frame**: reproject history using **velocity buffer** (from depth reprojection)
   ```hlsl
   float2 velocity = velocityBuffer.Sample(sampler, uv).rg;
   float2 prevUV = uv - (velocity / screenDimensions);
   float3 history = historyBuffer.Sample(sampler, prevUV).rgb;
   ```
4. **Blend** jittered frame with reprojected history
   ```hlsl
   float weight = mix(feedback_min, feedback_max, velocityLength);  // Reduce history weight for motion
   output = mix(current, history, weight);
   ```
5. **Repeat** — over 8+ frames, jagged edges become smooth (equivalent to 2x–4x MSAA)

**Challenge:** Fast camera movement or object motion can cause ghosting.
**Solution:** Velocity-based history rejection — if reprojected history differs significantly from current frame OR velocity is large, reduce history weight dynamically.

**Velocity Buffer Integration** (Milestone 1.3.1+):
- The velocity buffer (RG16Float) is generated via depth reprojection in a separate fullscreen pass
- Stores screen-space motion vectors in pixels (positive X = rightward, positive Y = downward)
- Sky pixels (depth ≈ 0 or ≈ 1) output zero velocity to prevent ghosting at horizon
- See [GBuffer Layout Guide](../../user/guides/rendering/gbuffer-layout.md#velocity-buffer-generation) for detailed velocity generation

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
