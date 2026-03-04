# Render Pipeline Architecture — Directives

> **Status**: MANDATORY — applies to all rendering code from Phase 1 onwards.
> **Enforced by**: Code review, AGENTS.md rule §RENDER-PIPELINE.

---

## 1. Pipeline Overview

```
Game Thread                      Render Graph Thread
────────────                     ───────────────────
Scene (flat node array)
  └── ComponentStore
        ├── StaticMeshData   ──► StaticMeshProxy.collect_mesh_batches()
        ├── LightData        ──► LightProxy.collect_shadow_data()
        └── CameraData       ──► CameraProxy.build_view_constants()
                                      │
                                      ▼
                              RenderGraph (DAG)
                                ├── ShadowPass
                                ├── GBufferPass (Deferred: Albedo, Normal, Roughness, Metallic)
                                ├── LightingPass (BRDF + IBL)
                                ├── SSAOPass
                                ├── PostProcessPass (TAA, Bloom, Tonemap)
                                └── EditorOverlayPass
                                      │
                                      ▼
                              RHI (rhi.h vtable)
                                ├── rhi_dx12.c  (Windows)
                                ├── rhi_vulkan.c (Linux)
                                └── rhi_metal.c (macOS)
```

---

## 2. Data Flow Rules

### RULE PIPE-1: Scene data flows DOWN — never up

```
Scene → RenderProxy → RenderGraph → RHI
```

Data flows from scene to RHI, never in reverse. The RHI backend does NOT write back to
the scene. The RenderGraph does NOT modify ComponentStore data.

```cpp
// ✅ CORRECT — renderer reads from scene
void GBufferPass::execute(RhiCmdList cmd) {
    for (ISceneProxy* proxy : proxies_) {
        proxy->execute(cmd, frame_constants_);  // proxy reads its OWN copy of data
    }
}

// ❌ WRONG — renderer writes back to scene
void SomePass::execute(RhiCmdList cmd) {
    scene_->nodes[id].some_field = computed_value;  // FORBIDDEN
}
```

### RULE PIPE-2: Render passes communicate via RenderGraph resources

Render passes MUST NOT share data via C++ member variables or shared pointers.
Passes communicate by declaring read/write dependencies on `RgTextureHandle` /
`RgBufferHandle` in the RenderGraph.

```cpp
// ✅ CORRECT — passes communicate via render graph resources
RgTextureHandle gbuffer_albedo = rg.create_texture({ .name="GBuffer_Albedo", ... });
RgTextureHandle gbuffer_normal = rg.create_texture({ .name="GBuffer_Normal", ... });

rg.add_pass("GBufferPass", RgPassType::Graphics,
    [&](RgPassBuilder& b) {
        b.write(gbuffer_albedo).write(gbuffer_normal);
    },
    [=](RhiCmdList cmd) { /* draw geometry */ }
);

rg.add_pass("LightingPass", RgPassType::Graphics,
    [&](RgPassBuilder& b) {
        b.read(gbuffer_albedo).read(gbuffer_normal);  // explicit dependency
    },
    [=](RhiCmdList cmd) { /* apply lighting using gbuffer data */ }
);

// ❌ WRONG — passes share data via member variable
class LightingPass {
    // Reading from a member set by GBufferPass — no explicit dependency declared
    RhiTexture albedo_tex_;  // Shared mutable state — FORBIDDEN
};
```

### RULE PIPE-3: Frame constants are uploaded ONCE per frame

Per-frame data (view matrix, projection matrix, time, resolution) MUST be uploaded to
the GPU exactly once per frame into a dedicated constant buffer. All passes read from this
shared buffer; no pass re-uploads frame data.

```cpp
// ✅ CORRECT — frame constants buffer (uploaded once)
struct FrameConstants {
    Mat4    view;
    Mat4    proj;
    Mat4    view_proj;
    Mat4    inv_view_proj;
    Vec3    camera_world_pos;  float _pad;
    Vec2    resolution;        Vec2  inv_resolution;
    float   time;
    float   delta_time;
};

// At frame start:
FrameConstants fc = build_frame_constants(camera_proxy);
rhi()->buffer_upload(frame_constants_buf_, &fc, sizeof(fc));
```

---

## 3. Adding a New Render Pass

### RULE PIPE-4: New passes are self-contained units

A render pass MUST:
1. Declare its own `RgTextureHandle` / `RgBufferHandle` resources
2. Register `read()` / `write()` dependencies in its `setup_fn`
3. Execute only via `rhi()->cmd_*()` calls
4. NOT know what pass runs before or after it (the RenderGraph handles ordering)

```cpp
// ✅ CORRECT — self-contained pass
class SSAOPass {
public:
    void register_with(RenderGraph& rg, RgTextureHandle gbuffer_depth,
                       RgTextureHandle gbuffer_normal) {
        ao_texture_ = rg.create_texture({ .name="SSAO_AO", .format=RgFormat::R32F, ... });

        rg.add_pass("SSAO", RgPassType::Compute,
            [&](RgPassBuilder& b) {
                b.read(gbuffer_depth)
                 .read(gbuffer_normal)
                 .write(ao_texture_);
            },
            [this](RhiCmdList cmd) { execute_ssao(cmd); }
        );
    }

    RgTextureHandle ao_output() const { return ao_texture_; }

private:
    void execute_ssao(RhiCmdList cmd) {
        rhi()->cmd_set_pipeline(cmd, ssao_pipeline_);
        rhi()->cmd_dispatch(cmd, dispatch_x_, dispatch_y_, 1);
    }

    RgTextureHandle ao_texture_ = kRgHandleInvalid;
    RhiPipeline     ssao_pipeline_ = RHI_NULL;
};
```

### RULE PIPE-5: Pass ordering is determined by the RenderGraph, never by manual ordering

Do NOT hardcode pass execution order in `main.cpp` or the renderer. The RenderGraph
compiles the DAG and determines order from `read()` / `write()` dependencies.

```cpp
// ✅ CORRECT — order determined by dependencies
gbuffer_pass.register_with(rg);
lighting_pass.register_with(rg, gbuffer_pass.albedo(), gbuffer_pass.normal());
ssao_pass.register_with(rg, gbuffer_pass.depth(), gbuffer_pass.normal());
// RenderGraph will automatically order: GBuffer → SSAO, GBuffer → Lighting

// ❌ WRONG — manual ordering with explicit dependencies
rg.add_pass("SSAO", ...);   // Added before GBuffer — will produce wrong results
rg.add_pass("GBuffer", ...);
rg.add_pass("Lighting", ...);
rg.execute_in_order({gbuffer_idx, ssao_idx, lighting_idx}); // MANUAL ORDER — FORBIDDEN
```

---

## 4. Render Proxy Lifecycle

### RULE PIPE-6: Proxies own copies of all data they render

A proxy MUST copy all data it needs during `sync_from_component()`. It MUST NOT hold
references, raw pointers, or shared_ptrs to game-thread component data.

```cpp
// ✅ CORRECT — proxy owns its data
struct MeshProxy : ISceneProxy {
    RhiBuffer  vertex_buf;    // GPU handle (proxy owns it)
    RhiBuffer  index_buf;
    uint32_t   index_count;
    uz::Mat4   world_transform;   // Copied from scene
    uz::Aabb   bounds;            // Copied from component

    void sync_from_component(const void* data, const uz::Mat4& world_transform_) {
        const StaticMeshData* mesh = static_cast<const StaticMeshData*>(data);
        world_transform = world_transform_;
        bounds = mesh->local_bounds;  // COPY — not a reference
        // Do NOT store pointer to mesh — it may move
    }
};
```

### RULE PIPE-7: Proxy creation/destruction happens between frames

Proxies MUST NOT be created or destroyed during RenderGraph execution (i.e., inside
`execute_fn` callbacks). Proxy management happens between frames, in a sync phase.

```cpp
// ✅ CORRECT — proxy management in sync phase (between frames)
void Renderer::sync_scene(const Scene& scene) {
    // This runs between frame N and frame N+1, never during execute()
    for (NodeId id : scene.dirty_nodes()) {
        destroy_proxy(id);  // Remove old proxy
        create_proxy(id, scene);  // Create new proxy
    }
}

// ❌ WRONG — proxy management inside pass execution
void GBufferPass::execute(RhiCmdList cmd) {
    if (needs_new_proxy_) {
        proxies_.push_back(new MeshProxy());  // FORBIDDEN during execute
    }
}
```

---

## 5. Texture Format Conventions

| Purpose | Format | RgFormat |
|---------|--------|----------|
| GBuffer Albedo | `RGBA8_SRGB` | `RgFormat::RGBA8` |
| GBuffer Normal | `RGBA16F` | `RgFormat::RGBA16F` |
| GBuffer Material (Roughness/Metallic/AO) | `RGBA8_UNORM` | `RgFormat::RGBA8` |
| Depth Buffer | `D32_FLOAT` | `RgFormat::D32F` |
| HDR Color Buffer | `RGBA16F` | `RgFormat::RGBA16F` |
| LDR Output (post-tonemap) | `RGBA8_SRGB` | `RgFormat::RGBA8` |
| Ambient Occlusion | `R32F` | `RgFormat::R32F` |
| Shadow Maps | `D32_FLOAT` | `RgFormat::D32F` |

### RULE PIPE-8: HDR rendering required from GBuffer onwards

All intermediate render targets from GBuffer onwards MUST be `RGBA16F` or `R11G11B10F`.
Tonemapping happens as the final post-process step only.

```cpp
// ✅ CORRECT — HDR all the way to tonemap
RgTextureDesc hdr_desc = {
    .width=w, .height=h, .format=RgFormat::RGBA16F, .is_render_target=true,
    .name="HDR_Color"
};

// ❌ WRONG — LDR intermediate
RgTextureDesc ldr_desc = {
    .width=w, .height=h, .format=RgFormat::RGBA8, .is_render_target=true  // WRONG for HDR pipeline
};
```

---

## 6. Phase-Based Rendering Architecture

### Phase 1 (Current — Legacy Renderer)

The legacy `DX12Renderer` continues to function during migration. The `RenderGraph`
exists but is not yet driving all rendering.

**Phase 1 target**: All new rendering passes go through `RenderGraph`. Legacy DX12 calls
are confined to `rhi_dx12.c`. No new DX12 code above the RHI boundary.

### Phase 2 (PBR Renderer)

Full deferred rendering via RenderGraph:
- GBufferPass (geometry + material)
- ShadowPass (directional light cascaded shadow maps)
- LightingPass (PBR BRDF + IBL)
- SSAOPass (screen-space ambient occlusion)
- PostProcessPass (TAA, bloom, tonemap)

All passes registered via the proxy system. No switch-on-type in any pass.

### Phase 3 (Advanced)

- SSRPass (screen-space reflections)
- VolumetricFogPass
- AnimatedMeshPass (skinning compute shader)
- ParticlePass

---

## 7. Checklist for New Render Passes

```
□ Pass declares all read/write dependencies in setup_fn
□ Pass uses only rhi()->cmd_*() inside execute_fn
□ No DX12/Vulkan types in pass header or implementation
□ Pass is registered via RenderGraph::add_pass(), not manually ordered
□ Frame constants read from shared frame constants buffer, not re-uploaded
□ Proxy creation/destruction happens outside execute_fn
□ Intermediate render targets use HDR formats (RGBA16F / R11G11B10F)
□ Pass has tests (render-graph-engineer test pattern with RHI_NULL)
```
