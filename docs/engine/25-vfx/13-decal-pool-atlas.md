# Decal Pool & Atlas Usage Guide

> **Status**: Planned (Phase 3.63.0)
> **Category**: Performance, VFX, Memory Management
> **References**: [Decal System Overview](05-decals.md), [Deferred Decal Pipeline](12-deferred-decal-pipeline.md), [.uzdecal Format](11-uzdecal-format.md)

## Purpose

This document describes the performance optimization systems for decals:

- **DecalPool**: Pre-allocated object pool to avoid dynamic allocations during gameplay
- **DecalAtlas**: Shelf-packing texture atlas to minimize texture state changes
- **DecalManager**: High-level API tying pool and atlas together

These systems enable hundreds of decals to render efficiently, with predictable memory usage and minimal GPU state changes.

## DecalPool — Object Pooling

### Overview

The DecalPool pre-allocates `DecalComponent` objects at load time, then reuses them via acquire/return pattern. This avoids expensive allocations and deallocations during gameplay (e.g., every bullet impact).

### API

```cpp
namespace uz::VFX {
    /// Pool for reusing DecalComponent objects.
    /// Pre-allocates a fixed number of decal instances at construction.
    /// Thread-safe for acquire/return operations.
    class DecalPool {
    public:
        /// Constructor: pre-allocate pool_size decal components.
        /// decal_asset: Reference decal asset (blueprint for pooled instances).
        /// pool_size: Number of decal objects to pre-allocate. Default: 500.
        explicit DecalPool(const DecalAsset& decal_asset, uint32_t pool_size = 500);

        /// Acquire an available decal from the pool.
        /// Returns nullptr if pool is exhausted (all decals in use).
        /// Caller is responsible for setting position/rotation/lifetime.
        /// Acquired decals are marked "in-use" and not returned until ReturnDecal() called.
        DecalComponent* AcquireDecal();

        /// Acquire a decal with initial transform and lifetime.
        /// Convenience method: calls AcquireDecal() then sets transform + lifetime.
        /// Returns nullptr if pool is exhausted.
        DecalComponent* AcquireDecal(const Transform& transform, float lifetime);

        /// Return decal to pool after use.
        /// Resets decal state (position, rotation, lifetime, age).
        /// Decal becomes available for future acquire calls.
        /// Safe to call multiple times (idempotent).
        void ReturnDecal(DecalComponent* decal);

        /// Returns number of available decals ready to acquire.
        uint32_t GetAvailableCount() const;

        /// Returns number of decals currently in use.
        uint32_t GetInUseCount() const;

        /// Returns total pool capacity.
        uint32_t GetCapacity() const;

        /// Clears entire pool (destroys all decal objects).
        /// Called on level unload or engine shutdown.
        void Clear();

        /// Logs pool statistics (available, in-use, capacity).
        void LogStats() const;

    private:
        std::vector<DecalComponent*> available_;  // Ready to acquire
        std::vector<DecalComponent*> in_use_;     // Currently active
        uint32_t capacity_;
    };
}
```

### Usage Pattern

**Acquiring a decal:**

```cpp
auto pool = world->GetSystem<DecalPool>();

// Method 1: Acquire and configure manually
auto decal = pool->AcquireDecal();
if (decal) {
    decal->SetTransform(impact_transform);
    decal->SetLifetime(10.0f);
    world->AddEntity(decal);
} else {
    UZ_LOG(VFX, Warning, "Decal pool exhausted, discarding impact");
}

// Method 2: Acquire with transform (convenience)
auto decal = pool->AcquireDecal(impact_transform, 10.0f);
if (decal) {
    world->AddEntity(decal);
}
```

**Returning a decal:**

The DecalManager automatically returns expired decals. Manual return is only needed for early destruction:

```cpp
// Manual early return
pool->ReturnDecal(decal);

// Or let DecalManager handle it automatically via lifetime expiry
```

### Pool Exhaustion Handling

When the pool is exhausted (all pre-allocated decals in use), `AcquireDecal()` returns `nullptr`. Strategies:

1. **Gracefully degrade**: Skip the effect (most common)
   ```cpp
   auto decal = pool->AcquireDecal();
   if (!decal) {
       UZ_LOG(VFX, Warning, "Decal pool full, discarding impact");
       return;  // Skip effect
   }
   ```

2. **Reduce pool on demand** (if supported by memory budget)
   ```cpp
   auto decal = pool->AcquireDecal();
   if (!decal && pool->GetCapacity() < max_pool_size) {
       pool->ExpandCapacity(100);  // Add 100 more decals
       decal = pool->AcquireDecal();
   }
   ```

3. **Kill oldest active decal** (aggressive, not recommended)
   ```cpp
   auto decal = pool->AcquireDecal();
   if (!decal) {
       // Force-return the oldest in-use decal
       auto oldest = *std::min_element(in_use_.begin(), in_use_.end(),
           [](DecalComponent* a, DecalComponent* b) {
               return a->GetAge() > b->GetAge();  // Greatest age = oldest
           });
       ReturnDecal(oldest);
       decal = AcquireDecal();
   }
   ```

### Memory Footprint

Per-decal memory:

```
DecalComponent object:     ~256 bytes
  - Position, rotation:    ~32 bytes
  - Material handle:       ~8 bytes
  - Constants buffer:      ~256 bytes (GPU-side)

Pool overhead:
  - Vector pointers:       ~16 bytes per decal (available) + ~16 bytes (in-use)

Total per decal: ~300 bytes CPU + ~256 bytes GPU (constant buffer)

Example: Pool of 500 decals
  ≈ 150 KB CPU + 128 KB GPU per pool
  Multiple pools (different decal types) = multiplicative cost
```

## DecalAtlas — Texture Atlasing

### Overview

The DecalAtlas packs multiple decal textures into a single large texture using shelf-packing algorithm. This:

- **Reduces texture bindings**: 1 atlas texture instead of N separate textures
- **Reduces state changes**: Fewer `SetTexture()` calls during rendering
- **Improves cache efficiency**: Better GPU memory locality

### Architecture

```
┌────────────────────────────────────────────────┐
│ Decal Texture Atlas (2048×2048)                │
├──────────────┬──────────────┬──────────────────┤
│ Bullet hole  │ Blood splat  │ Scorch mark      │
│ 512×512      │ 512×256      │ 512×512          │
│ UV: [0, 0.5] │ UV: [0.5, 0] │ UV: [0.5, 0.5]   │
├──────────────┼──────────────┼──────────────────┤
│ Footprint    │ Claw marks   │ Rust stain       │
│ 256×256      │ 512×512      │ 256×512          │
│ UV: [0, 0.5] │ UV: [0.5, 0] │ UV: [0.75, 0.5]  │
└──────────────┴──────────────┴──────────────────┘
```

### API

```cpp
namespace uz::VFX {
    /// Entry in the decal atlas.
    struct DecalAtlasEntry {
        Vec2 uv_min;            // Top-left UV in atlas [0, 1]
        Vec2 uv_max;            // Bottom-right UV in atlas [0, 1]
        uint32_t width;         // Original texture width (pixels)
        uint32_t height;        // Original texture height (pixels)
    };

    /// Manages decal texture atlasing via shelf-packing.
    /// Multiple atlases are created if needed (overflow).
    class DecalAtlas {
    public:
        /// Constructor: create atlas with given dimensions.
        /// atlas_width: Width in pixels (default: 2048)
        /// atlas_height: Height in pixels (default: 2048)
        /// format: Texture format for all entries (default: RGBA8)
        DecalAtlas(uint32_t atlas_width = 2048, uint32_t atlas_height = 2048,
                   rhi_texture_format_t format = RHI_FORMAT_RGBA8);

        /// Add a decal texture to the atlas.
        /// texture_path: Path to texture asset (e.g., "textures/bullet_hole.png").
        /// Returns DecalAtlasEntry with UV bounds on success, or empty entry on failure.
        /// Logs warning if texture not found.
        /// If atlas is full, creates a new atlas internally.
        DecalAtlasEntry AddDecal(const std::string& texture_path);

        /// Add pre-loaded texture data to atlas.
        /// Returns DecalAtlasEntry with UV bounds, or empty entry if no space.
        DecalAtlasEntry AddDecalData(const uint8_t* pixel_data, uint32_t width, uint32_t height);

        /// Get the GPU texture atlas (may be one of multiple atlases).
        /// Returns handle to first atlas. If textures overflow, returns handle to latest atlas.
        rhi_texture_handle_t GetAtlasTexture(uint32_t atlas_index = 0) const;

        /// Get number of atlases created (if overflowed, > 1).
        uint32_t GetAtlasCount() const;

        /// Get occupancy of a specific atlas (0.0 = empty, 1.0 = full).
        float GetOccupancy(uint32_t atlas_index = 0) const;

        /// Clear all atlases (on level unload).
        void Clear();

        /// Log atlas statistics (occupancy, entry count, overflow info).
        void LogStats() const;

    private:
        struct Shelf {
            uint32_t x;         // Shelf x-offset
            uint32_t y;         // Shelf y-offset
            uint32_t height;    // Shelf height (tallest entry in shelf)
            uint32_t width_used; // Current width used in shelf
        };

        std::vector<rhi_texture_handle_t> atlases_;
        std::vector<std::vector<Shelf>> shelves_;  // One per atlas
        std::vector<std::map<std::string, DecalAtlasEntry>> entries_;
        uint32_t atlas_width_, atlas_height_;
        rhi_texture_format_t format_;
    };
}
```

### Shelf-Packing Algorithm

The atlas uses shelf-packing (similar to texture atlasing in game engines):

```
1. Start with empty shelf at y=0, width=0, height=0
2. For each texture to add:
   a. If texture fits in current shelf: pack it, advance x
   b. Else, create new shelf below current: set y += prev_shelf.height
   c. If still doesn't fit (height too tall): create new atlas
3. Track UV bounds for each packed texture
```

**Example packing sequence:**

```
Atlas before:     │ Empty 2048×2048         │

Add 512×512:      │ [████████]   (shelf 1)  │
                  │           Avail: 1536×512
                  │
                  │              (shelf 2)
                  │              Avail: 2048×1536

Add 512×256:      │ [████████][████] (shelf 1)
                  │           [████]  Avail: 1024×256
                  │
                  │              (shelf 2)
                  │              Avail: 2048×1280

Add 512×512:      │ [████████][████]
                  │ [████████████████] (shelf 2)
                  │           Avail: 1536×512

Utilization: ~75% (1.5 MB / 2.0 MB used)
```

## DecalManager — High-Level API

### Overview

DecalManager ties together the pool and atlas, providing the main API for spawning and rendering decals.

### API

```cpp
namespace uz::VFX {
    /// High-level decal management system.
    /// Handles pooling, atlasing, sorting, and rendering.
    class DecalManager {
    public:
        /// Initialize manager with decal asset library.
        /// assets: Map of asset names → DecalAsset definitions.
        void Initialize(const std::map<std::string, DecalAsset>& assets);

        /// Spawn a decal instance at position with surface normal alignment.
        /// asset_name: Name of decal asset (e.g., "bullet_hole_9mm").
        /// position: World position (center of decal box).
        /// surface_normal: Surface normal (used to orient decal forward direction).
        /// Returns handle to spawned decal, or invalid handle if pool exhausted.
        DecalHandle SpawnDecal(const std::string& asset_name,
                               const Vec3& position,
                               const Vec3& surface_normal);

        /// Spawn a decal with custom rotation.
        DecalHandle SpawnDecal(const std::string& asset_name,
                               const Transform& transform);

        /// Remove a decal immediately (returns to pool).
        void DestroyDecal(DecalHandle handle);

        /// Update all active decals (advance lifetime, fade alpha).
        /// Called once per frame by the renderer.
        void Update(float delta_time);

        /// Render all visible decals (deferred pass).
        /// Called by renderer between G-Buffer and lighting.
        void RenderDecals(rhi_cmd_buffer_t cmd_buffer);

        /// Get active decal count.
        uint32_t GetActiveCount() const;

        /// Get pool statistics.
        struct PoolStats {
            uint32_t available;
            uint32_t in_use;
            uint32_t capacity;
        };
        PoolStats GetPoolStats(const std::string& asset_name) const;

        /// Get atlas statistics.
        struct AtlasStats {
            uint32_t atlas_count;
            float avg_occupancy;  // 0.0–1.0
        };
        AtlasStats GetAtlasStats() const;

        /// Shutdown and clear all resources.
        void Shutdown();

    private:
        std::map<std::string, DecalPool*> pools_;     // Pool per asset
        DecalAtlas atlas_;
        std::vector<DecalComponent*> active_decals_;
        Frustum camera_frustum_;
        bool dirty_;  // Need re-sort
    };
}
```

### Usage Example

**Spawning decals:**

```cpp
// Get manager from engine
auto* decal_mgr = engine->GetSystem<DecalManager>();

// Spawn at bullet impact
void OnBulletImpact(const HitInfo& hit) {
    decal_mgr->SpawnDecal(
        "bullet_hole_9mm",
        hit.position,
        hit.surface_normal
    );

    // Optional: Play impact sound, particle effect
    AudioEngine::PlaySound("sfx/bullet_hit.wav", hit.position);
}

// Spawn blood splatter (larger, multiply blend)
void OnCharacterBleed(const Vec3& position, const Vec3& normal) {
    decal_mgr->SpawnDecal(
        "blood_splatter",
        position,
        normal
    );
}

// Spawn scorch mark (explosion damage)
void OnExplosion(const Vec3& center, float radius) {
    const int num_marks = 8;
    for (int i = 0; i < num_marks; ++i) {
        float angle = (2.0f * M_PI * i) / num_marks;
        Vec3 pos = center + Vec3(cos(angle), 0, sin(angle)) * radius * 0.8f;
        Vec3 normal = (pos - center).Normalized();
        decal_mgr->SpawnDecal("scorch_mark", pos, normal);
    }
}
```

**Monitoring pools:**

```cpp
void DebugLogDecalStats() {
    auto* decal_mgr = engine->GetSystem<DecalManager>();

    auto pool_stats = decal_mgr->GetPoolStats("bullet_hole_9mm");
    UZ_LOG(VFX, Info, "Bullet hole pool: {}/{} available/capacity",
           pool_stats.available, pool_stats.capacity);

    auto atlas_stats = decal_mgr->GetAtlasStats();
    UZ_LOG(VFX, Info, "Decal atlas: {} atlases, {:.1f}% avg occupancy",
           atlas_stats.atlas_count, atlas_stats.avg_occupancy * 100.0f);
}
```

## Best Practices

### Pool Sizing

Choose pool size based on gameplay:

| Game Type | Typical Active | Recommended Pool |
|-----------|--|--|
| Puzzle game | 0–10 | 50 |
| Action game | 10–50 | 200 |
| Shooter (single-player) | 50–200 | 500 |
| Shooter (multiplayer) | 200–500 | 1000 |
| Open-world action RPG | 100–300 | 500–1000 |

**Rule of thumb**: `pool_size ≈ max_concurrent_decals × 2` to allow headroom.

Monitor pool exhaustion:

```cpp
void DebugDecalPoolHealth() {
    auto stats = decal_mgr->GetPoolStats("bullet_hole");
    float utilization = (float)stats.in_use / stats.capacity;
    if (utilization > 0.9f) {
        UZ_LOG(VFX, Warning, "Decal pool 90%+ full!");
    }
}
```

### Lifetime Management

Set appropriate lifetimes based on visibility and gameplay:

```cpp
// Short lifetime: quick impacts (bullet holes, footsteps)
"lifetime": 5.0,      // Fades after 5 seconds
"fade_duration": 1.0, // 1 second fade-out

// Medium lifetime: blood, dirt (persists for a while)
"lifetime": 30.0,     // Visible for 30 seconds
"fade_duration": 3.0, // 3 second fade-out

// Long lifetime: scorch marks, permanent damage
"lifetime": 999999,   // Essentially permanent
"fade_duration": 0.0, // No fade

// Fast fade: visual feedback only
"lifetime": 0.5,      // Quick sparkle/hit effect
"fade_duration": 0.3, // 0.3 second fade-out
```

### Atlas Sizing

Choose atlas size based on decal count and quality:

| Atlas Size | Typical Atlases | Max Decals @ 512×512 | Use Case |
|-----------|--|--|--|
| 512×512 | 10+ | ~4 | Mobile, low-end |
| 1024×1024 | 3–5 | ~16 | Mid-range |
| 2048×2048 | 1–2 | ~64 | High-end, AAA |
| 4096×4096 | 1 | ~256 | Highest quality |

Monitor occupancy:

```cpp
auto atlas_stats = decal_mgr->GetAtlasStats();
if (atlas_stats.atlas_count > 2) {
    UZ_LOG(VFX, Warning, "Decal atlas overflow: {} atlases created",
           atlas_stats.atlas_count);
    // Consider: larger atlas size, fewer unique decal types
}
```

### Fade Curves

Use curves strategically:

```json
{
  "fade_curve": "curves/bullet_hole_fade.uzcurve"
}
```

**Linear fade** (simplest, most common):
```json
{
  "format": "uzcurve",
  "keyframes": [
    { "time": 0.0, "value": 1.0 },
    { "time": 1.0, "value": 0.0 }
  ]
}
```

**Ease-out fade** (natural, less jarring):
```json
{
  "format": "uzcurve",
  "keyframes": [
    { "time": 0.0, "value": 1.0, "tangent_out": -0.5 },
    { "time": 1.0, "value": 0.0, "tangent_in": 0.0 }
  ]
}
```

**Hold-then-fade** (visual feedback persists):
```json
{
  "format": "uzcurve",
  "keyframes": [
    { "time": 0.0, "value": 1.0 },
    { "time": 0.8, "value": 1.0 },      // Plateau
    { "time": 1.0, "value": 0.0 }
  ]
}
```

## Performance Tuning

### Frustum Culling

Decals outside camera view are not rendered:

```cpp
void DecalManager::RenderDecals(rhi_cmd_buffer_t cmd) {
    std::vector<DecalComponent*> visible_decals;

    for (auto* decal : active_decals_) {
        if (camera_frustum_.Intersects(decal->GetOBB())) {
            visible_decals.push_back(decal);
        }
    }

    // Render only visible decals
    for (auto* decal : visible_decals) {
        // ... render
    }
}
```

### Batching by Material

Reduce state changes by grouping decals by material:

```cpp
std::unordered_map<MaterialHandle, std::vector<DecalComponent*>> batches;
for (auto* decal : visible_decals) {
    batches[decal->material].push_back(decal);
}

for (const auto& [material, decals] : batches) {
    SetMaterial(material);  // Once per batch
    for (auto* decal : decals) {
        SetConstantBuffer(decal->GetConstants());
        DrawIndexed(unit_cube_mesh.index_count);
    }
}
```

### Profiling

Use engine profiling to find bottlenecks:

```cpp
{
    UZ_PROFILE_SCOPE("Decal Rendering");

    {
        UZ_PROFILE_SCOPE("Decal Frustum Cull");
        // ... frustum cull code
    }

    {
        UZ_PROFILE_SCOPE("Decal Sort");
        // ... sort code
    }

    {
        UZ_PROFILE_SCOPE("Decal Batch & Draw");
        // ... render code
    }
}
```

## See Also

- [Decal System Overview](05-decals.md) — Conceptual overview
- [.uzdecal Format](11-uzdecal-format.md) — Asset format specification
- [Deferred Decal Pipeline](12-deferred-decal-pipeline.md) — Rendering implementation
