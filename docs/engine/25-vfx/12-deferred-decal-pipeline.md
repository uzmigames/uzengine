# Deferred Decal Rendering Pipeline

> **Status**: Implemented (Phase 3.63.0)
> **Category**: Rendering, VFX
> **References**: [Decal System Overview](05-decals.md), [.uzdecal Format](11-uzdecal-format.md), [G-Buffer](../02-rendering/04-gbuffer.md)
> **Source**: `runtime/engine/vfx/decals/decal_component.h/.cpp`, `runtime/engine/vfx/decals/decal_manager.{h,cpp}`
> **Shaders**: `runtime/shaders/runtime/vfx/decal_deferred_vs.hlsl`, `decal_deferred_ps.hlsl`

## Purpose

This document describes how decals are rendered in the deferred pipeline. Unlike forward decal projection, deferred decals modify the G-Buffer (normals, roughness, albedo) post-geometry rendering, then lighting reads the modified buffers. This approach:

- **Reduces geometry**: Uses a unit cube mesh, not per-surface geometry
- **Scales well**: N decals × 1 cube = efficient batching
- **Decouples rendering**: Decals render after geometry, before lighting
- **Blends flexibly**: Supports multiple blend modes (Opaque, Additive, Multiply, Screen)

## Pipeline Overview

The deferred decal pass fits between G-Buffer rendering and lighting:

```
┌──────────────────────────────────────┐
│ 1. Geometry G-Buffer Rendering       │
│    - Render all opaque scene meshes  │
│    - Output: Albedo, Normal, PBR     │
│    - Output: Depth, Motion vectors   │
└────────────┬─────────────────────────┘
             │
             ▼
┌──────────────────────────────────────┐
│ 2. Deferred Decal Pass (THIS SECTION)│
│    - Sort decals by sort_order + dist│
│    - For each decal:                 │
│      ├─ Setup projection matrices    │
│      ├─ Render unit cube             │
│      ├─ PS: test depth & projection  │
│      ├─ PS: compute blend            │
│      └─ PS: write modified G-Buffer  │
└────────────┬─────────────────────────┘
             │
             ▼
┌──────────────────────────────────────┐
│ 3. Lighting Pass                     │
│    - Sample modified G-Buffer        │
│    - Apply lights with decal changes │
│    - Output: HDR radiance            │
└──────────────────────────────────────┘
```

## Rendering State & Setup

### Pipeline State Object (PSO)

The decal rendering uses a specialized PSO with these properties:

```cpp
rhi_graphics_pso_desc_t pso_desc = {};

// Vertex stage: Transform unit cube to decal OBB
pso_desc.vs = {shader_unit_cube_vs};
pso_desc.ps = {shader_decal_ps};

// Rasterizer: Front-face culling (we render from inside the box)
pso_desc.rasterizer_state = {
    .fill_mode = RHI_FILL_SOLID,
    .cull_mode = RHI_CULL_FRONT,    // Render back-faces (inside cube)
    .front_ccw = true,               // CCW = front-facing (standard)
};

// Depth: Read-only (don't modify depth)
pso_desc.depth_stencil_state = {
    .depth_test = RHI_COMPARISON_LESS,
    .depth_write = false,            // Read-only
};

// Color: Blend based on decal's blend_mode
pso_desc.blend_state = {
    .independent_blend = false,
    .rt_blend[0] = {
        .blend_enable = true,
        .src_blend = RHI_BLEND_ONE,
        .dst_blend = RHI_BLEND_ONE,  // Changes per blend_mode
        .blend_op = RHI_BLEND_OP_ADD,
        // ... (see Blend Modes section)
    }
};
```

### Constant Buffer Layout

Per-decal constant buffer (`register(b0)`):

```hlsl
cbuffer DecalConstants : register(b0) {
    float4x4    world_to_decal;         // Transform world space → decal local
    float3      decal_position;         // World position (redundant, for debug)
    float       normal_intensity;       // [0, 1] normal map contribution
    float3      decal_forward;          // Forward direction for backface rejection
    float       roughness_override;     // [-1, 1] roughness value (-1 = use surface)
    float4      blend_mode_params;      // Blend mode specific parameters
    float       fade_alpha;             // Current opacity [0, 1]
    float       backface_threshold;     // Dot product threshold for rejection
    uint        blend_mode;             // 0=Opaque, 1=Additive, 2=Multiply, 3=Screen
    float3      half_extents;           // Size of projection box
};
```

### Texture Bindings

Decal pass accesses two sets of textures:

**Decal Textures** (from material):
- `t0`: Decal albedo (RGBA8)
- `t1`: Decal normal (RG16F or BC5)
- `t2`: Decal roughness (R8)
- `s0`: Linear sampler with wrap or clamp (material-specific)

**G-Buffer Inputs** (read-only):
- `t3`: G-Buffer 0 (albedo)
- `t4`: G-Buffer 1 (normal, encoded as oct16 or R11G11B10)
- `t5`: G-Buffer 2 (PBR: roughness, metallic, ambient occlusion)
- `t6`: Depth buffer (D32_FLOAT or D24_UNORM_S8_UINT)
- `s1`: Point sampler (no filtering on depth)

## Vertex Shader

The vertex shader transforms a unit cube to the decal's oriented bounding box (OBB):

```hlsl
// decal_unit_cube_vs.hlsl

cbuffer PerFrame : register(b1) {
    float4x4 view;
    float4x4 projection;
};

cbuffer DecalConstants : register(b0) {
    float4x4    world_to_decal;
    float3      decal_position;
    float       normal_intensity;
    float3      decal_forward;
    float       roughness_override;
    // ... (rest of constants)
};

struct VS_Input {
    float3 position : POSITION;      // Unit cube vertex [-1, +1]
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct VS_Output {
    float4 position : SV_POSITION;   // Clip space
    float3 world_pos : TEXCOORD0;    // For depth reconstruction
    float3 local_pos : TEXCOORD1;    // Decal local space (for UV & rejection)
    float3 normal : TEXCOORD2;       // Surface normal (from G-Buffer)
};

VS_Output main(VS_Input input) {
    VS_Output output = (VS_Output)0;

    // Transform unit cube vertex to world space
    // Cube is in range [-1, +1], half_extents scales it
    float3 world_pos = decal_position + input.position * half_extents;

    // Transform to view space, then clip space
    float4 view_pos = mul(float4(world_pos, 1), view);
    output.position = mul(view_pos, projection);

    output.world_pos = world_pos;
    output.local_pos = input.position;  // Keep unit cube coordinates

    return output;
}
```

## Pixel Shader

The pixel shader performs the core decal projection logic:

```hlsl
// decal_deferred_ps.hlsl

cbuffer DecalConstants : register(b0) {
    float4x4    world_to_decal;
    float3      decal_position;
    float       normal_intensity;
    float3      decal_forward;
    float       roughness_override;
    float4      blend_mode_params;
    float       fade_alpha;
    float       backface_threshold;
    uint        blend_mode;
    float3      half_extents;
};

Texture2D decal_albedo : register(t0);
Texture2D decal_normal : register(t1);
Texture2D decal_roughness : register(t2);

Texture2D g_buffer_albedo : register(t3);
Texture2D g_buffer_normal : register(t4);
Texture2D g_buffer_pbr : register(t5);
Texture2D depth_buffer : register(t6);

SamplerState decal_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct PS_Input {
    float4 position : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 local_pos : TEXCOORD1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Depth Reconstruction — Convert depth buffer to world position
// ─────────────────────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 screen_uv, float depth) {
    // screen_uv in [0, 1] normalized coordinates
    // depth in [0, 1] from depth buffer

    // Reconstruct NDC coordinates
    float3 ndc = float3(screen_uv * 2.0 - 1.0, depth);
    ndc.y = -ndc.y;  // D3D convention

    // Inverse project to world space
    float4 world_pos = mul(float4(ndc, 1.0), inv_view_projection);
    return world_pos.xyz / world_pos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend Modes — Apply blend logic per mode
// ─────────────────────────────────────────────────────────────────────────────

float3 BlendDecalColor(float3 surface_color, float3 decal_color, uint mode) {
    switch (mode) {
        case 0:  // Opaque: Replace surface
            return decal_color;

        case 1:  // Additive: Add to surface
            return saturate(surface_color + decal_color);

        case 2:  // Multiply: Darken surface
            return surface_color * decal_color;

        case 3:  // Screen: Brighten surface
            return 1.0 - (1.0 - surface_color) * (1.0 - decal_color);

        default:
            return surface_color;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

float4 main(PS_Input input) : SV_TARGET {
    float2 screen_uv = input.position.xy / screen_size;

    // ─────────────────────────────────────────────────────────────────────────
    // Step 1: Reconstruct world position from depth
    // ─────────────────────────────────────────────────────────────────────────
    float depth = depth_buffer.Sample(point_sampler, screen_uv).r;
    float3 world_pos = ReconstructWorldPos(screen_uv, depth);

    // ─────────────────────────────────────────────────────────────────────────
    // Step 2: Transform to decal local space & test projection box
    // ─────────────────────────────────────────────────────────────────────────
    float4 decal_local = mul(float4(world_pos, 1.0), world_to_decal);
    decal_local.xyz /= decal_local.w;  // Perspective divide (if non-uniform scale)

    // Check if inside projection box
    if (any(abs(decal_local.xyz) > 1.0)) {
        discard;  // Outside decal box
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Step 3: Load surface normal and test backface rejection
    // ─────────────────────────────────────────────────────────────────────────
    float3 g_normal_encoded = g_buffer_normal.Sample(point_sampler, screen_uv).rgb;
    float3 surface_normal = DecodeNormal(g_normal_encoded);  // Oct16 or similar

    float facing = dot(surface_normal, decal_forward);
    if (facing < backface_threshold) {
        discard;  // Backfacing, reject decal
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Step 4: Compute UV coordinates for decal textures
    // ─────────────────────────────────────────────────────────────────────────
    // Map decal local [-1, +1] to UV [0, 1]
    float2 decal_uv = decal_local.xy * 0.5 + 0.5;
    decal_uv = saturate(decal_uv);  // Clamp to [0, 1] (avoid wrap artifacts)

    // ─────────────────────────────────────────────────────────────────────────
    // Step 5: Sample decal textures
    // ─────────────────────────────────────────────────────────────────────────
    float3 decal_color = decal_albedo.Sample(decal_sampler, decal_uv).rgb;
    float3 decal_normal_map = decal_normal.Sample(decal_sampler, decal_uv).rgb;
    float decal_roughness_val = decal_roughness.Sample(decal_sampler, decal_uv).r;

    // ─────────────────────────────────────────────────────────────────────────
    // Step 6: Load surface G-Buffer
    // ─────────────────────────────────────────────────────────────────────────
    float3 surface_albedo = g_buffer_albedo.Sample(point_sampler, screen_uv).rgb;
    float4 surface_pbr = g_buffer_pbr.Sample(point_sampler, screen_uv);
    float surface_roughness = surface_pbr.x;
    float surface_metallic = surface_pbr.y;
    float surface_ao = surface_pbr.z;

    // ─────────────────────────────────────────────────────────────────────────
    // Step 7: Blend albedo and normal based on blend mode
    // ─────────────────────────────────────────────────────────────────────────
    float3 blended_color = BlendDecalColor(surface_albedo, decal_color, blend_mode);

    // Blend normal: interpolate between surface and decal normal
    float3 blended_normal = normalize(
        lerp(surface_normal, decal_normal_map, normal_intensity)
    );

    // ─────────────────────────────────────────────────────────────────────────
    // Step 8: Handle roughness override
    // ─────────────────────────────────────────────────────────────────────────
    float final_roughness = surface_roughness;
    if (roughness_override >= 0.0) {
        // Override: use decal's roughness value
        final_roughness = roughness_override;
    } else {
        // Modulate: multiply surface roughness by decal's roughness
        final_roughness *= decal_roughness_val;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Step 9: Apply fade alpha
    // ─────────────────────────────────────────────────────────────────────────
    float final_alpha = fade_alpha;

    // Optional: Depth-based fade (if enabled in decal asset)
    if (depth_fade_enabled) {
        float dist_from_decal = abs(decal_local.z);  // Distance along decal Z
        float depth_fade_alpha = 1.0 - saturate((dist_from_decal - depth_fade_near)
                                                 / (depth_fade_far - depth_fade_near));
        final_alpha *= depth_fade_alpha;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Step 10: Output modified G-Buffer
    // ─────────────────────────────────────────────────────────────────────────
    // For Opaque blend mode, output is straightforward
    // For other modes, alpha is used by render target blend operations

    float4 output;
    output.rgb = blended_color;  // Modified albedo
    output.a = final_alpha;

    // Note: Normal and PBR are written to separate render targets
    // in the actual implementation with MRT (Multiple Render Targets)

    return output;
}
```

## Multi-Render-Target (MRT) Layout

The decal pass writes to multiple render targets simultaneously:

```cpp
// Render target bindings
RenderTarget rt_albedo(decal_ps_output, 0);  // RGB: blended color, A: fade alpha
RenderTarget rt_normal(decal_ps_output, 1);  // RG: encoded normal, BA: unused
RenderTarget rt_pbr(decal_ps_output, 2);     // R: roughness, G: metallic, BA: AO
```

The pixel shader outputs to all three:

```hlsl
struct PS_Output {
    float4 albedo : SV_TARGET0;      // Blended albedo + fade
    float4 normal : SV_TARGET1;      // Blended normal
    float4 pbr : SV_TARGET2;         // Blended PBR
};

PS_Output main(PS_Input input) {
    PS_Output output;
    output.albedo = float4(blended_color, final_alpha);
    output.normal = float4(EncodeNormal(blended_normal), 0, 0);
    output.pbr = float4(final_roughness, surface_metallic, surface_ao, 0);
    return output;
}
```

## Sorting & Rendering Order

Decals are sorted before rendering to ensure correct layering:

```cpp
struct DecalRenderCommand {
    DecalComponent* decal;
    float distance_to_camera;
    int sort_order;
};

std::vector<DecalRenderCommand> commands = /* populate from active decals */;

// Sort: by sort_order (ascending), then by distance (descending = back-to-front)
std::sort(commands.begin(), commands.end(),
    [](const DecalRenderCommand& a, const DecalRenderCommand& b) {
        if (a.sort_order != b.sort_order)
            return a.sort_order < b.sort_order;  // Primary sort
        return a.distance_to_camera > b.distance_to_camera;  // Secondary sort
    });

// Render in sorted order
for (const auto& cmd : commands) {
    SetConstantBuffer(cmd.decal->GetConstants());
    DrawIndexed(unit_cube_mesh.index_count);
}
```

## Performance Optimization

### Frustum Culling

Before rendering, cull decals outside the camera frustum:

```cpp
bool IsDecalInFrustum(const DecalComponent& decal, const Frustum& camera_frustum) {
    // Construct OBB from decal position, rotation, and half_extents
    OrientedBoundingBox obb = decal.GetOBB();
    return Intersects(camera_frustum, obb);
}

// Filter active decals
std::vector<DecalComponent*> visible_decals;
for (auto* decal : all_active_decals) {
    if (IsDecalInFrustum(*decal, camera_frustum)) {
        visible_decals.push_back(decal);
    }
}
```

### Early-Out on Depth

If a decal is entirely behind opaque geometry, it contributes nothing. Early GPU-side rejection:

```hlsl
// In pixel shader, after depth reconstruction:
float surface_depth_linear = LinearizeDepth(depth_buffer.Sample(...), near_far);
float decal_depth_linear = LinearizeDepth(decal_z, near_far);

if (surface_depth_linear < decal_depth_linear) {
    discard;  // Surface is in front of decal
}
```

### Atlasing & Batching

When multiple decals share the same material, batch them:

```cpp
std::unordered_map<MaterialHandle, std::vector<DecalRenderCommand>> batches;

for (const auto& cmd : sorted_commands) {
    batches[cmd.decal->GetMaterial()].push_back(cmd);
}

for (const auto& [material, batch] : batches) {
    SetMaterial(material);
    for (const auto& cmd : batch) {
        SetConstantBuffer(cmd.decal->GetConstants());
        DrawIndexed(unit_cube_mesh.index_count);
    }
}
```

## Blend Mode Implementation

The PSO blend state changes per blend mode:

```cpp
struct BlendStateConfig {
    uint32_t blend_mode;
    rhi_blend_state_t pso_blend_state;
};

static const BlendStateConfig blend_configs[] = {
    { 0, { .dst_blend = RHI_BLEND_ZERO, .blend_op = RHI_BLEND_OP_ADD } },      // Opaque
    { 1, { .dst_blend = RHI_BLEND_ONE, .blend_op = RHI_BLEND_OP_ADD } },       // Additive
    { 2, { .dst_blend = RHI_BLEND_DST_COLOR, .blend_op = RHI_BLEND_OP_ADD } }, // Multiply
    { 3, { .dst_blend = RHI_BLEND_INV_SRC_COLOR, .blend_op = RHI_BLEND_OP_ADD } }, // Screen
};
```

## See Also

- [Decal System Overview](05-decals.md) — Conceptual overview
- [.uzdecal Format](11-uzdecal-format.md) — Asset format specification
- [G-Buffer Rendering](../02-rendering/04-gbuffer.md) — Layout and encoding
