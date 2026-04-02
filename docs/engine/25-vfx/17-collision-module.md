# Depth-Buffer Collision Module

> **Status**: Implemented (Phase 3.65.0)
> **References**: [VFX Modules](03-vfx-modules.md), [Module API Reference](04-module-api.md), [.uzvfx Format](07-uzvfx-format.md)
> **Source**: `runtime/engine/vfx/modules/collision/`, `runtime/shaders/runtime/vfx/vfx_collision.hlsli`

## Purpose

The Collision Module detects when GPU particles penetrate the scene depth buffer and applies a configurable response: Kill (particle dies), Bounce (velocity reflects), or Slide (velocity glides along surface). This allows particles to realistically interact with static geometry without expensive CPU-side spatial queries.

The module is implemented as an HLSL snippet that the module compositor stitches into the particle update compute shader. It runs entirely on the GPU each frame with no CPU cost beyond configuration.

## How It Works

### Detection Pipeline

Each frame, for every alive particle:

1. **Project to screen space** — Multiply particle world position by `cbViewProj` to get NDC coordinates
2. **Sample depth buffer** — Bilinear-filtered depth texture lookup at the resulting screen UV
3. **Reconstruct surface position** — Use inverse view-projection to recover world-space geometry position
4. **Compare depths** — If particle is at or behind the surface (within `collisionOffset` standoff), collision detected
5. **Estimate surface normal** — Sample depth buffer at 4 neighboring pixels and compute cross-product of reconstructed tangents
6. **Apply response** — Based on `response` type:
   - **Kill**: Set age to lifespan (particle recycled next frame)
   - **Bounce**: Reflect velocity off normal with `bounceFactor` energy loss, check `minBounceSpeed`
   - **Slide**: Project velocity onto surface plane (tangential component only)

### Surface Normal Estimation

Because the depth buffer contains no explicit normals, the module reconstructs them geometrically:

```
Sample depth at 5 points (center + 4 cardinal neighbors in UV space)
Reconstruct world positions from each depth sample
Compute tangent directions: right = (rightPos - leftPos), up = (upPos - downPos)
Normal = normalize(cross(right, up))
```

This is a best-effort estimate; the precision improves with screen resolution. For high-quality normal data, use a separate screen-space normal GBuffer pass.

## Configuration

### CollisionModuleConfig Fields

| Field | Default | Range | Description |
|-------|---------|-------|-------------|
| `response` | Bounce | Kill / Bounce / Slide | How particles react on contact |
| `bounceFactor` | 0.6 | [0, 1] | Velocity retention after bounce (1.0 = elastic, 0.0 = inelastic) |
| `depthBufferCollision` | true | bool | Enable/disable collision testing (for debugging) |
| `collisionOffset` | 0.01 m | > 0 | World-space standoff distance to prevent z-fighting |
| `minBounceSpeed` | 0.1 m/s | >= 0 | Speed threshold below which Bounce falls back to Kill |

### Response Types Explained

#### Kill
Particle dies immediately on contact:
```cpp
cfg.response = CollisionResponse::Kill;
// In HLSL: age = lifespan
```
**Use case**: Rain hitting a window, mist settling on ground.

#### Bounce
Velocity reflects off the surface normal, scaled by `bounceFactor`:
```cpp
cfg.response = CollisionResponse::Bounce;
cfg.bounceFactor = 0.6f;  // 60% energy retention
cfg.minBounceSpeed = 0.1f;  // Kill if speed drops below this
```
**Use case**: Sparks bouncing off metal, pebbles ricocheting.

**HLSL formula**: `v_reflected = (v - 2*(v·n)*n) * bounceFactor`

#### Slide
Velocity projects onto the surface plane (removes normal component):
```cpp
cfg.response = CollisionResponse::Slide;
```
**Use case**: Snow accumulating on surfaces, water running down walls.

**HLSL formula**: `v_tangential = v - (v·n)*n`

## HLSL Integration

The module returns an HLSL snippet for the module compositor. The snippet assumes these are in scope:

### Required Buffers (RWStructuredBuffer)
```hlsl
RWStructuredBuffer<float3> Positions;
RWStructuredBuffer<float3> Velocities;
RWStructuredBuffer<float>  Ages;
StructuredBuffer<float>    Lifespans;  // read-only
```

### Required Constants (cbuffer)
```hlsl
cbuffer CollisionConstants {
    float4x4 cbViewProj;          // View-projection matrix
    float4x4 cbInvViewProj;       // Inverse view-projection
    float2   cbScreenDimensions;  // Render target size (width, height)
    uint     cbCollisionResponse; // 0=Kill, 1=Bounce, 2=Slide
    float    cbBounceFactor;
    float    cbMinBounceSpeed;
    float    cbCollisionOffset;
    uint     cbCollisionEnabled;  // 0=disabled, 1=enabled
};
```

### Required Textures & Samplers
```hlsl
Texture2D<float>  DepthBuffer;
SamplerState      LinearClampSampler;  // or point-clamp
```

### Shader Utilities (vfx_collision.hlsli)

The module includes these helper functions from `vfx_collision.hlsli`:

```hlsl
/// Estimates surface normal from depth buffer gradient (5 samples)
float3 EstimateSurfaceNormal(
    Texture2D<float> depthTexture,
    SamplerState     depthSampler,
    float2           screenUV,
    float2           texelSize,
    float4x4         invViewProj
);

/// Tests depth-buffer collision and computes surface normal
bool ScreenSpaceCollision(
    float3           worldPos,
    float4x4         viewProj,
    Texture2D<float> depthTexture,
    SamplerState     depthSampler,
    float2           screenDimensions,
    float            collisionOffset,
    out float3       surfaceNormal
);

/// Reflects velocity off a surface normal
float3 ReflectVelocity(float3 velocity, float3 normal, float bounceFactor);

/// Projects velocity onto a plane (removes normal component)
float3 ProjectOntoPlane(float3 velocity, float3 normal);
```

## Usage Examples

### Basic Rain Effect

Rain that kills on contact:

```json
{
  "modules": [
    {
      "type": "collision",
      "config": {
        "response": "kill",
        "collision_offset": 0.01,
        "depth_buffer_collision": true
      }
    }
  ]
}
```

### Sparks Bouncing Off Metal

Sparks that lose energy with each bounce:

```json
{
  "modules": [
    {
      "type": "collision",
      "config": {
        "response": "bounce",
        "bounce_factor": 0.7,
        "min_bounce_speed": 0.05,
        "collision_offset": 0.001
      }
    },
    {
      "type": "decay",
      "config": {
        "lifetime": 3.0
      }
    }
  ]
}
```

### Snow Sliding Down Slopes

Snow particles that accumulate:

```json
{
  "modules": [
    {
      "type": "collision",
      "config": {
        "response": "slide",
        "collision_offset": 0.01
      }
    },
    {
      "type": "gravity",
      "config": {
        "strength": 0.5
      }
    }
  ]
}
```

### Debug (Collision Disabled)

Disable collision to verify particle paths before enabling:

```json
{
  "modules": [
    {
      "type": "collision",
      "config": {
        "depth_buffer_collision": false
      }
    }
  ]
}
```

## Performance Considerations

### GPU Cost

- **Per-particle depth sample**: 1 texture lookup (bilinear)
- **Per-collision normal estimate**: 4 additional depth samples + 2 cross products
- **Typical cost**: 20–30 GPU cycles per collision particle at 1080p

For **100k particles/frame**: Expect 5–10% GPU time on modern hardware (RTX 3080+).

### Bandwidth

- Depth buffer reads dominate memory traffic
- Bilinear filtering reduces cache misses; point-clamp sampling is fastest but may show artifacts at depth discontinuities
- **Recommendation**: Use bilinear sampler for 100k+ particles; switch to point-clamp if VRAM-bound

### Optimization Tips

1. **Minimize colliding particles** — Use LOD to cull distant emitters
2. **Increase `collisionOffset`** — Coarser detection = fewer false positives
3. **Set `response=Kill` for most particles** — Avoids normal estimation on bounces
4. **Lower `minBounceSpeed`** — Reduces the time spent in bouncing state
5. **Disable for transparent passes** — Collision is most visible on opaque surfaces
6. **Use screen-space normal buffer for high-quality needs** — Replaces 4-sample estimation with 1 texture read

## Common Issues

### Particles Jitter at Surface

**Problem**: Particles hover just on/off the surface, repeatedly colliding.

**Solution**: Increase `collisionOffset` to 0.02–0.05. This adds a standoff distance preventing micro-penetrations.

### Collision Detection Misses

**Problem**: Fast-moving particles pass through geometry.

**Solution**: This is a frame-rate issue, not a collision bug. Particle velocity can exceed depth-buffer sampling granularity. Mitigate by:
- Increasing particle count (LOD up) to make effect denser
- Capping particle speed in the VFX asset
- Using smaller particles so single-frame movement < 1 texel

### Bounces Seem Weak

**Problem**: Bouncing particles barely move after impact.

**Solution**: Check `bounceFactor` (should be 0.5–0.8 for realistic bounces) and verify gravity isn't immediately pulling them back down. Use `minBounceSpeed` to prevent endless micro-bounces.

### Normal Estimation Looks Blocky

**Problem**: Surface normals appear jagged, especially on diagonal surfaces.

**Solution**: This is a side effect of 4-sample normal reconstruction. For smoother results, author a proper screen-space normal GBuffer pass.

## Related Documentation

- [VFX Modules Overview](03-vfx-modules.md) — Module architecture and composition
- [Module API Reference](04-module-api.md) — IVFXModule interface and registry
- [.uzvfx Format](07-uzvfx-format.md) — Asset format for particle effects
- [LOD & Budget System](18-lod-budget-system.md) — Managing particle count

## Phase Roadmap

| Phase | Milestone | Status |
|-------|-----------|--------|
| 3.65.0 | Collision module implementation | Complete |
| 3.66.0 | Screen-space normal GBuffer optimization | Planned |
| 3.67.0 | Collision events & Lua API | Planned |
