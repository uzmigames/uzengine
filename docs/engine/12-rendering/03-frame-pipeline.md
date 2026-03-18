# Rendering — Frame Pipeline

> **Status**: Implemented
> **Source**: `runtime/engine/renderer/scene/` (SceneRenderer + frame orchestration)
> **Depends on**: Render World (§12-01), Visibility (§12-02), RHI (§03), Shader Pipeline (§05)
>
> **Directory layout** (v0.2.35):
> ```
> renderer/
> ├── scene/               — SceneRenderer class + split implementation
> │   ├── scene_renderer.h — Main class definition
> │   ├── scene_lifecycle.cpp
> │   ├── scene_render_frame.cpp
> │   ├── scene_sync.cpp
> │   ├── scene_assets.cpp
> │   └── scene_debug.cpp
> ├── core/                — Renderer infrastructure
> │   ├── params/          — CB structs (constant_buffers, per_frame_params, shadow_data_params, ...)
> │   ├── gbuffer/         — GBuffer, draw list, render path, debug view mode
> │   ├── shader/          — Shader manager + parameter system
> │   ├── pipeline/        — PipelineLibrary split (lifecycle, geometry, shadow, atmosphere, postprocess)
> │   └── (root)           — cb_ring_allocator, frame_pacer, instance_batcher, LOD, render_world
> ├── passes/              — Render pass implementations
> │   ├── geometry/        — depth_prepass, gbuffer_pass, pbr_pass
> │   ├── lighting/        — shadow_pass, lighting_pass
> │   ├── atmosphere/      — sky_atmosphere, transmittance_lut, sky_cubemap, height_fog
> │   ├── postprocess/     — auto_exposure, bloom, gtao, hiz, velocity, motion blur, DOF
> │   └── debug/           — gbuffer_debug, debug_view
> ├── shadows/             — Shadow atlas, cascade, cache, SDF, local light shadows
> ├── lighting/            — Light renderer, BRDF math
> ├── atmosphere/          — Atmosphere renderer
> ├── fog/                 — Fog renderer
> ├── ibl/                 — IBL baker, SH projection
> ├── render_graph/        — Render graph builder + resources
> └── subsystems.h         — Umbrella header for all subsystem renderers
> ```

## Purpose

The complete per-frame rendering pipeline, from input polling through GPU submission to present. Defines the ordered sequence of all engine systems within a single frame.

## Full Frame Pipeline

```
Frame N:
    │
    ├── 1. Input Tick
    │      OS events → modifier chains → trigger evaluation → callbacks
    │
    ├── 2. Game Tick
    │      PrePhysics → Physics → PostPhysics → Update → PostUpdate
    │      Behaviors run, components update, events fire
    │
    ├── 3. Transform Propagation
    │      Dirty SceneComponents → recompute world matrices (parent→child)
    │
    ├── 4. Sync Point
    │      PrimitiveComponent → RenderProxy (create/update/destroy)
    │      CameraComponent → ViewState
    │
    ├── 5. Visibility
    │      Frustum cull (CPU) → GPU occlusion cull → LOD select
    │
    ├── 6. Shadow Pass
    │      Per shadow-casting light: frustum cull → shadow map render
    │
    ├── 7. Depth Prepass
    │      Opaque geometry → depth-only, no color output
    │      Fills depth buffer for early-Z rejection in GBuffer pass
    │      Toggle: SceneRenderer::SetDepthPrepassEnabled(bool)
    │
    ├── 7b. Hi-Z Pyramid
    │      Compute pass: per-mip min-depth downsample (12 mip levels)
    │      Produces Hi-Z mip chain for occlusion culling, SSAO, SSR, contact shadows
    │
    ├── 8. GBuffer Pass (Deferred)
    │      Opaque geometry → Albedo, Normal, Metallic/Roughness, Emissive, Depth
    │      With depth prepass active: uses EQUAL depth test for early-Z rejection
    │
    ├── 9. Lighting Pass
    │      Read GBuffer + shadow maps → lit result
    │      GI: light probes / screen-space / DXR (future)
    │
    ├── 10. Transparent Pass (Forward)
    │       Back-to-front sorted, forward shading
    │
    ├── 11. Post-Processing
    │       TAA → SSAO (consumes Hi-Z) → SSR (consumes Hi-Z) → Bloom → DoF → Motion Blur → Tonemap → DLSS/FSR
    │
    └── 12. Present
            Swapchain present → start Frame N+1
```

## Deferred Rendering — GBuffer Layout

| Target | Format | Content | Bytes/Pixel |
|--------|--------|---------|-------------|
| GBuffer 0 (Albedo) | RGBA8Unorm | RGB = linear base color, A = ambient occlusion | 4 |
| GBuffer 1 (Normal) | RGBA8Unorm | RG = octahedral-encoded normal, B = metallic, A = roughness | 4 |
| GBuffer 2 (Velocity) | RG16Float | RG = screen-space motion vectors (pixels) for TAA/motion blur | 4 |
| Depth | D32Float | Standard depth (0.0 = far, 1.0 = near) | 4 |
| **Total** | — | — | **16 B/px** (without velocity) or **20 B/px** (with temporal effects) |

**Optimization** (Milestone 1.3.1): Migrated from 3 color RTs (20 bytes) to 2 color + velocity (20 bytes, but with temporal support):
- **Albedo**: Moved AO from separate RT to alpha channel
- **Normal**: Changed from RGBA16F (8 bytes) to RGBA8 with octahedral encoding (4 bytes, <0.5° error)
- **Velocity**: Added for TAA and motion blur via depth reprojection

**See**: [11-deferred-pipeline.md](11-deferred-pipeline.md) for complete implementation and [GBuffer Layout Guide](../../user/guides/rendering/gbuffer-layout.md) for encoding details.

### Why Deferred

- Decouples geometry complexity from light count — 100 point lights cost almost the same as 1 for opaque geometry
- GBuffer data is reused by SSAO, SSR, and other post-processing effects
- Forward shading is reserved for transparents only (which require sorted back-to-front blending)
- Single geometry pass writes all material data; lighting pass(es) can vary without geometry re-rendering

## Post-Processing Chain

Each effect reads from the previous output and writes to the next:

1. **TAA (Temporal Anti-Aliasing)** — jittered samples accumulated over frames, ghosting rejection
2. **SSAO (Screen-Space Ambient Occlusion)** — half-res compute, bilateral blur
3. **SSR (Screen-Space Reflections)** — hierarchical ray march against HZB
4. **Bloom** — downsample chain (mip pyramid) + Gaussian blur + upsample composite
5. **Depth of Field** — circle-of-confusion from depth, separated near/far blur
6. **Motion Blur** — per-pixel velocity from motion vectors
7. **Tonemap** — ACES filmic or customizable LUT, gamma correction
8. **Upscaling** — DLSS / FSR / native (passthrough)

## Per-Frame Constant Buffer Builder

The `PerFrameCB` structure holds global frame data (camera matrices, time, lighting) used by all render passes. A helper function constructs it in one call instead of duplicating setup logic across demos.

### PerFrameCB Structure

```cpp
/// Per-frame data pushed to GPU via constant buffer.
/// Used by all passes: forward, deferred, post-processing, shaders.
struct PerFrameCB {
    Mat4 view_matrix;              // Camera view (world-to-camera)
    Mat4 projection_matrix;         // Camera projection (camera-to-clip)
    Mat4 view_projection;           // view * projection (world-to-clip)
    Mat4 inv_view_projection;       // Inverse for ray reconstruction

    Vec3 camera_position;           // World-space camera position
    float total_time;               // Total elapsed time in seconds

    Vec3 camera_forward;            // Camera forward vector
    float delta_time;               // Frame time delta

    Vec4 ambint_color;              // Global ambient light (RGB + intensity)
    uint32_t frame_index;           // Frame counter for temporal effects
    Vec3 directional_light_dir;     // Normalized direction to sun/key light
    float directional_light_intensity;

    Vec4 directional_light_color;   // RGB color + 1.0 (for alignment)
    // ... additional point lights array, shadow data, etc.
};
```

### BuildPerFrameCB Helper

```cpp
/// Construct PerFrameCB from camera, timing, and lighting.
///
/// This is the single source of truth for how frame data is assembled.
/// Eliminates manual matrix operations and ensures all passes receive
/// identical frame constants.
///
/// # Arguments
/// * camera - Camera with view/projection matrices already set
/// * total_time - Total elapsed time (seconds since start)
/// * delta_time - Frame delta (seconds since last frame)
/// * frame_index - Frame counter (0, 1, 2, ... for temporal TAA/dithering)
/// * lighting - Scene lighting (ambient, directional, point lights)
///
/// # Returns
/// Fully populated PerFrameCB ready for GPU constant buffer upload.
///
/// # Example
/// ```cpp
/// SceneLighting lighting;
/// lighting.ambient_color = {0.2f, 0.2f, 0.2f};
/// lighting.directional_dir = normalize(Vec3(1, 1, -1));
/// lighting.directional_color = {1, 1, 1};
/// lighting.directional_intensity = 1.5f;
///
/// PerFrameCB frame_cb = BuildPerFrameCB(camera, total_time, delta_time,
///                                        frame_idx++, lighting);
/// cmd_list->UploadConstantBuffer(frame_cb_handle, &frame_cb, sizeof(frame_cb));
/// ```
PerFrameCB BuildPerFrameCB(
    const Camera& camera,
    float total_time,
    float delta_time,
    uint32_t frame_index,
    const SceneLighting& lighting
);
```

### SceneLighting Structure

```cpp
/// Global scene lighting parameters.
struct SceneLighting {
    // Ambient light
    Vec3 ambient_color = {0.1f, 0.1f, 0.1f};

    // Directional light (sun, key light)
    Vec3 directional_dir = {0, -1, 0};              // Normalized world direction
    Vec3 directional_color = {1, 1, 1};
    float directional_intensity = 1.0f;

    // Point lights (up to 8 for deferred)
    static constexpr size_t MAX_POINT_LIGHTS = 8;
    struct PointLight {
        Vec3 position;
        float radius;
        Vec3 color;
        float intensity;
    };
    std::array<PointLight, MAX_POINT_LIGHTS> point_lights;
    uint32_t point_light_count = 0;
};
```

### Current Duplication Problem

Today, `PerFrameCB` is manually assembled in each demo with identical boilerplate:

```cpp
// In pbr_test_demo.cpp
PerFrameCB frame_cb;
frame_cb.view_matrix = camera.GetViewMatrix();
frame_cb.projection_matrix = camera.GetProjectionMatrix();
frame_cb.view_projection = camera.GetViewProjectionMatrix();
frame_cb.inv_view_projection = glm::inverse(frame_cb.view_projection);
frame_cb.camera_position = camera_pos;
frame_cb.total_time = total_time;
frame_cb.camera_forward = camera.GetForward();
frame_cb.delta_time = frame_delta;
frame_cb.ambient_color = {0.2f, 0.2f, 0.2f, 1.0f};
frame_cb.frame_index = frame_count++;
frame_cb.directional_light_dir = sun_dir;
frame_cb.directional_light_intensity = 1.5f;
// ... 20+ lines identical to deferred_test_demo.cpp ...

// In deferred_test_demo.cpp (identical)
PerFrameCB frame_cb;
frame_cb.view_matrix = camera.GetViewMatrix();
// ... same 20+ lines ...
```

**Problems**:
- Setup logic duplicated identically across demos
- Adding a new frame constant (e.g., `frame_jitter_offset` for TAA) requires updating all demos
- Easy to introduce inconsistencies: one demo sets `ambient_color`, another forgets it
- Maintenance burden when lighting system changes

### Usage Examples

### Before (Manual Assembly)

```cpp
// In pbr_test_demo.cpp — 20 lines of boilerplate per frame
void OnRender() {
    PerFrameCB frame_cb{};
    frame_cb.view_matrix = camera.GetViewMatrix();
    frame_cb.projection_matrix = camera.GetProjectionMatrix();
    frame_cb.view_projection = camera.GetViewProjectionMatrix();
    frame_cb.inv_view_projection = glm::inverse(frame_cb.view_projection);
    frame_cb.camera_position = camera.GetPosition();
    frame_cb.total_time = elapsed_time;
    frame_cb.camera_forward = camera.GetForward();
    frame_cb.delta_time = frame_delta;
    frame_cb.ambient_color = Vec4(0.2f, 0.2f, 0.2f, 1.0f);
    frame_cb.frame_index = frame_count++;
    frame_cb.directional_light_dir = sun_direction;
    frame_cb.directional_light_intensity = 1.5f;
    frame_cb.directional_light_color = Vec4(1, 1, 1, 1);

    cmd_list->UpdateConstantBuffer(frame_cb_handle, &frame_cb);
    RenderScene();
}
```

### After (BuildPerFrameCB)

```cpp
// In pbr_test_demo.cpp — 5 lines, single function call
void OnRender() {
    SceneLighting lighting;
    lighting.ambient_color = {0.2f, 0.2f, 0.2f};
    lighting.directional_dir = sun_direction;
    lighting.directional_color = {1, 1, 1};
    lighting.directional_intensity = 1.5f;

    PerFrameCB frame_cb = BuildPerFrameCB(camera, elapsed_time, frame_delta,
                                          frame_count++, lighting);

    cmd_list->UpdateConstantBuffer(frame_cb_handle, &frame_cb);
    RenderScene();
}
```

## Implementation Phases

| Phase | What Renders |
|-------|-------------|
| **Phase 1 (MVP)** | Forward shading, single light, no post-processing |
| **Phase 2** | Deferred GBuffer, multiple lights, shadow maps, depth prepass, Hi-Z pyramid, basic post (bloom + tonemap) |
| **Phase 3** | Full post chain (TAA, SSAO, SSR using Hi-Z), GPU occlusion culling (using Hi-Z), contact shadows |
| **Phase 4+** | Virtual geometry, visibility buffer, DLSS/FSR, DXR GI |

## Rules

1. **ALWAYS use deferred for opaque, forward for transparent** — mixing causes double-lighting bugs
2. **ALWAYS use reversed-Z depth** — better precision at distance (1.0 = near, 0.0 = far)
3. **NEVER add post-processing effects without LOD/quality settings** — every effect must have Off/Low/Medium/High/Ultra
