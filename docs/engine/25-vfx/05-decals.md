# Decal System — Deferred Decals, Projection, Atlasing, Pooling

> **Depends on**: VFX system (§25), Rendering (§02), G-Buffer (§02)
> **Rendering**: Deferred pass (post-G-Buffer, pre-lighting)
> **Used by**: Bullet holes, blood stains, scorch marks, impact marks

## Purpose

Decals are projected geometry that modifies surface appearance without altering the underlying mesh. Typical uses:

- **Bullet holes**: Bullet impact creates a decal on the wall
- **Blood stains**: Spray pattern on surfaces
- **Scorch marks**: Explosion damage marks on terrain
- **Footprints**: Wet ground shows footstep decals
- **Claw marks**: Creature damage on surfaces

A deferred decal system is efficient: decals modify the G-Buffer (normals, roughness, albedo) post-render, then lighting is applied.

## Decal Projection

Decals are defined by:

1. **Position & Rotation**: Where the decal is placed in world space
2. **Size (half-extents)**: Width × Height × Depth of projection box
3. **Texture**: Material with albedo, normal, roughness
4. **Fade curve**: How opacity changes over time

### Projection Matrix

Each decal has a box-shaped projection volume. Surface pixels are tested:

```
if (pixel is inside decal box && facing decal forward) {
    Sample decal texture using projected UV
    Blend with G-Buffer surface
}
```

Projection:

```cpp
namespace uz::VFX {
    struct DecalProjection {
        Vec3 position;           // World position
        Quat rotation;           // Orientation
        Vec3 half_extents;       // Width, height, depth of projection box

        /// Get projection matrix (world to decal local space).
        Mat4 GetProjectionMatrix() const {
            Mat4 world_to_local = Mat4::Inverse(Mat4::FromPositionRotation(position, rotation));
            // All pixels in decal box are projected onto decal texture
            return world_to_local;
        }

        /// Check if point is inside projection box.
        bool ContainsPoint(const Vec3& world_pos) const {
            Vec3 local_pos = GetProjectionMatrix() * world_pos;
            return all(abs(local_pos) <= half_extents);
        }
    };
}
```

## Deferred Decal Rendering

Decals are rendered in a separate pass after the G-Buffer, modifying surface properties:

```
┌─────────────────────────────────────┐
│ Main Forward/Deferred Pass          │
│  - Render scene geometry            │
│  - Output G-Buffer (albedo, normal) │
└──────────────┬──────────────────────┘
               │
               ▼
┌──────────────────────────────────────┐
│ Deferred Decal Pass (per-decal)      │
│  1. For each decal:                  │
│     ├─ Render projection box         │
│     ├─ PS: check if pixel in box     │
│     ├─ PS: sample decal texture      │
│     └─ PS: blend with G-Buffer       │
└──────────────┬──────────────────────┘
               │
               ▼
┌──────────────────────────────────────┐
│ Lighting Pass                        │
│  - Use modified G-Buffer             │
│  - Apply lights with decal changes   │
└──────────────────────────────────────┘
```

## DecalComponent

```cpp
namespace uz::VFX {
    /// Deferred decal placed in world.
    class DecalComponent : public uz::PrimitiveComponent {
    public:
        UZ_COMPONENT(DecalComponent);

        /// Decal asset (texture + material).
        uz::ResourceHandle<DecalAsset> asset;

        /// Size of projection box (width, height, depth).
        Vec3 half_extents = {1.0f, 1.0f, 0.5f};

        /// Normal map intensity (0–1).
        float normal_intensity = 1.0f;

        /// Roughness override (0–1, or -1 for no override).
        float roughness_override = -1.0f;

        /// Fade lifetime (seconds before auto-destroy).
        float lifetime = 10.0f;

        /// Current age (incremented per frame).
        float age = 0.0f;

        /// Fade curve (opacity over lifetime).
        uz::ResourceHandle<Curve> fade_curve;

        // Methods
        void FadeOut(float duration = 0.5f);
        void Destroy();

    private:
        void OnTick(float dt) override;
        void OnRender() override;  // Called during deferred pass
    };
}
```

## DecalAsset Format

```cpp
namespace uz::VFX {
    /// Decal asset: texture + blend mode + curves.
    struct DecalAsset {
        /// Decal material (contains albedo, normal, roughness textures).
        uz::ResourceHandle<Material> material;

        /// How to blend decal with surface:
        /// Opaque: decal_color
        /// Additive: decal_color + surface_color
        /// Multiply: decal_color * surface_color
        enum BlendMode { Opaque, Additive, Multiply, Screen };
        BlendMode blend_mode = Opaque;

        /// Lifetime before auto-fade.
        float lifetime = 10.0f;

        /// Fade curve (alpha over normalized lifetime [0,1]).
        uz::ResourceHandle<Curve> fade_curve;

        /// Normal intensity (how much decal normal affects surface normal).
        float normal_intensity = 0.8f;

        /// Sort order (higher = rendered later = on top).
        int sort_order = 0;
    };
}
```

## Decal Pixel Shader

```hlsl
// decal_deferred.hlsl
cbuffer DecalData : register(b0) {
    float4x4 world_to_decal;
    float3 decal_pos;
    float half_width;
    float3 decal_forward;
    float half_height;
    float normal_intensity;
    float roughness_override;
    float fade_alpha;
    int blend_mode;
};

Texture2D decal_albedo : register(t0);
Texture2D decal_normal : register(t1);
Texture2D decal_roughness : register(t2);

// G-Buffer inputs
Texture2D g_albedo : register(t3);
Texture2D g_normal : register(t4);
Texture2D g_roughness : register(t5);
Texture2D g_depth : register(t6);

SamplerState decal_sampler : register(s0);
SamplerState gbuffer_sampler : register(s1);

struct PS_Input {
    float4 position : SV_POSITION;
    float3 world_pos : TEXCOORD0;
};

float4 main(PS_Input input) : SV_TARGET {
    // Reconstruct world position from depth
    float depth = g_depth.Sample(gbuffer_sampler, input.position.xy / viewport_size).r;
    float3 world_pos = ReconstructWorldPos(input.position.xy, depth);

    // Transform to decal local space
    float4 decal_local = mul(float4(world_pos, 1), world_to_decal);

    // Check if inside projection box
    if (abs(decal_local.x) > half_width ||
        abs(decal_local.y) > half_height ||
        abs(decal_local.z) > 1.0) {
        discard;  // Outside box
    }

    // Check if facing decal forward (not backward)
    float3 surface_normal = g_normal.Sample(gbuffer_sampler, input.position.xy / viewport_size).rgb;
    if (dot(surface_normal, decal_forward) < 0.1) {
        discard;  // Backfacing
    }

    // Project UV: decal_local.xy maps to [0, 1]
    float2 uv = decal_local.xy * 0.5 + 0.5;
    uv = clamp(uv, 0.01, 0.99);  // Avoid edge artifacts

    // Sample decal textures
    float3 decal_color = decal_albedo.Sample(decal_sampler, uv).rgb;
    float3 decal_normal_map = decal_normal.Sample(decal_sampler, uv).rgb;
    float decal_roughness = decal_roughness.Sample(decal_sampler, uv).r;

    // Load G-Buffer
    float3 surface_albedo = g_albedo.Sample(gbuffer_sampler, input.position.xy / viewport_size).rgb;
    float surface_roughness = g_roughness.Sample(gbuffer_sampler, input.position.xy / viewport_size).r;

    // Blend based on mode
    float3 final_color;
    if (blend_mode == 0) {  // Opaque
        final_color = decal_color;
    } else if (blend_mode == 1) {  // Additive
        final_color = surface_albedo + decal_color;
    } else if (blend_mode == 2) {  // Multiply
        final_color = surface_albedo * decal_color;
    } else {  // Screen
        final_color = 1.0 - (1.0 - surface_albedo) * (1.0 - decal_color);
    }

    // Blend normal
    float3 blended_normal = normalize(
        lerp(surface_normal, decal_normal_map, normal_intensity)
    );

    // Override roughness if specified
    float final_roughness = (roughness_override >= 0) ? roughness_override : surface_roughness;

    // Apply fade alpha
    float4 output;
    output.rgb = lerp(surface_albedo, final_color, fade_alpha);
    output.a = fade_alpha;

    return output;
}
```

## Decal Atlasing

Multiple decals share a texture atlas to reduce state changes:

```
┌─────────────────────────────────────────┐
│ Decal Texture Atlas (2048×2048)         │
├──────────────┬────────────┬─────────────┤
│ Bullet hole  │ Blood      │ Scorch mark │
│ 512×512      │ 512×512    │ 512×512     │
├──────────────┼────────────┼─────────────┤
│ Footprint    │ Claw marks │ Rust stain  │
│ 512×512      │ 512×512    │ 512×512     │
└──────────────┴────────────┴─────────────┘
```

Implementation:

```cpp
namespace uz::VFX {
    /// Manages decal texture atlasing.
    class DecalAtlas {
    public:
        struct Entry {
            Vec2 uv_min;        // Top-left in atlas
            Vec2 uv_max;        // Bottom-right in atlas
            uint32_t width;
            uint32_t height;
        };

        /// Register decal texture, returns UV bounds in atlas.
        Entry AddDecal(const uz::Path& texture_path);

        /// Get GPU texture atlas.
        uz::ResourceHandle<TextureAsset> GetAtlasTexture() const;

    private:
        uz::ResourceHandle<TextureAsset> atlas_;
        std::map<std::string, Entry> entries_;
    };
}
```

## Decal Pooling

Decals are pooled for efficiency:

```cpp
namespace uz::VFX {
    class DecalPool {
    public:
        /// Get available decal from pool (or create if none).
        DecalComponent* AcquireDecal(const DecalAsset& asset);

        /// Return decal to pool (reset and mark available).
        void ReturnDecal(DecalComponent* decal);

        /// Clear entire pool.
        void Clear();

    private:
        std::vector<DecalComponent*> available_;
        std::vector<DecalComponent*> in_use_;
        uint32_t pool_size_ = 1000;
    };
}

// Usage:
uz::VFX::DecalPool pool;

// On bullet hit
DecalComponent* decal = pool.AcquireDecal(bullet_hole_asset);
decal->SetPosition(impact_pos);
decal->SetRotation(impact_normal);

// After fade-out
pool.ReturnDecal(decal);
```

## Example: Bullet Hole Decal

```cpp
void OnBulletImpact(const HitInfo& hit) {
    // Get decal pool
    auto pool = world->GetSystem<DecalPool>();

    // Acquire decal from pool
    auto decal = pool->AcquireDecal(bullet_hole_asset);

    // Position at impact
    decal->SetTransform(
        Transform{
            hit.position,
            Quat::LookAt(hit.normal),  // Face outward from surface
            Vec3::One()
        }
    );

    // Add to world
    world->AddEntity(decal);

    // Fade out over 10 seconds
    decal->FadeOut(10.0f);

    // Play sound and particle effect at impact
    uz::Audio::PlaySound("sfx/bullet_hit.ogg", hit.position);
    auto particles = SpawnEffect("vfx/bullet_impact.uzvfx", hit.position);
}
```

## Decal Manager

Centralized decal rendering and lifecycle:

```cpp
namespace uz::VFX {
    class DecalManager {
    public:
        /// Render all active decals (deferred pass).
        void RenderDecals(uz::Renderer& renderer);

        /// Update decal lifetimes and fade.
        void Update(float dt);

        /// Clean up expired decals.
        void Cleanup();

    private:
        std::vector<DecalComponent*> active_decals_;
        uz::VFX::DecalPool pool_;
    };
}
```

## Performance Considerations

Per-frame overhead:

```
Deferred decal pass: O(num_decals × num_pixels_in_box)

For 100 active decals (512×512 each):
  ~100 × 262K pixels = 26M pixels processed
  ~0.5 ms on modern GPU
```

Optimization:

- **Frustum cull decals**: Don't render decals outside camera view
- **Depth test**: Skip decals entirely behind opaque surfaces
- **Atlasing**: Minimize texture switches
- **Pooling**: Reuse decal objects, avoid alloc/free

## Comparison

| Feature | Unreal 5 | Godot 4 | Quake 3 | **UzEngine** |
|---------|----------|---------|---------|------------|
| Decal rendering | Deferred + forward | Limited | Stencil shadows | **Deferred only** |
| Projection | Box projection | No | No | **Box projection** |
| Atlasing | Yes | Limited | No | **Yes** |
| Pooling | Yes | No | Manual | **Yes (pool manager)** |
| Fade curve | Via material | No | No | **Curve-based** |
| Max decals | 100+ | ~10 | ~10 | **500+ (pooled)** |
| Blend modes | Many | Limited | No | **Opaque/Additive/Multiply/Screen** |

## Implementation Phase

| Phase | Status |
|-------|--------|
| **Phase 3.63.0** | **IMPLEMENTED** — DecalComponent, box projection, deferred pass, pooling, atlasing, fade curves, blend modes |
| **Phase 3.64.0** | Trail system (sibling feature, independently complete) |
