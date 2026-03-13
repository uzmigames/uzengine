# G-Buffer Optimization + Velocity Buffer (UE5 Parity)

## Priority: P0 — Blocker for TAA, Motion Blur, and bandwidth optimization

## Summary

Optimize UzEngine's G-Buffer layout to match UE5's efficient packing and add a velocity buffer. Currently UzEngine wastes 46 bytes/pixel on normals (R16G16B16A16_FLOAT for world normals) when UE5 uses only 2 bytes (octahedral encoding). Also add RG16F velocity buffer required by TAA (3.14.0) and motion blur (3.18.0).

## Current UzEngine G-Buffer (WASTEFUL)

| Target | Format | Size/px | Content |
|--------|--------|---------|---------|
| RT0 | R8G8B8A8_UNORM | 4B | RGB=albedo, A=unused |
| RT1 | **R16G16B16A16_FLOAT** | **8B** | RGB=world normal, A=unused |
| RT2 | R8G8B8A8_UNORM | 4B | R=metallic, G=roughness, B=AO, A=unused |
| Depth | D32_FLOAT | 4B | Hardware depth |
| **Total** | | **20B/px** | |

**Problem**: RT1 uses 8 bytes for normals when 2 bytes suffice. At 1080p = 15.8MB wasted bandwidth per frame.

## UE5 G-Buffer (TARGET)

| Target | Format | Size/px | Content |
|--------|--------|---------|---------|
| RT0 | R8G8B8A8_UNORM | 4B | RGB=albedo (sRGB decoded), A=AO |
| RT1 | R8G8B8A8_UNORM | 4B | RG=octahedral normal, B=metallic, A=roughness |
| RT2 | R8G8B8A8_UNORM | 4B | Custom data (emissive, subsurface, shading model ID) |
| Velocity | RG16F | 4B | Per-pixel motion vectors |
| Depth | D32_FLOAT | 4B | Hardware depth |
| **Total** | | **20B/px** | But with velocity + custom data! |

## Key Changes

### 1. Octahedral Normal Encoding (8B → 2B)
UE5 encodes unit normals as 2 bytes using octahedral mapping:
- `encode(n)`: project unit sphere onto octahedron, unfold to [0,1]² → pack as RG8_UNORM
- `decode(rg)`: reverse mapping → reconstruct unit normal
- Quality: ±0.002 error (imperceptible), 75% bandwidth savings on normals
- Reference: "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle 2014)

### 2. Velocity Buffer (NEW — blocker for TAA + motion blur)
- Format: RG16F (4 bytes/pixel)
- Content: screen-space motion vectors in pixels
- Generation: depth reprojection using current/previous ViewProj matrices
- Per-object: vertex shader outputs prevWorldPos for dynamic/skinned objects
- Written during G-Buffer pass (extra MRT) or separate fullscreen pass

### 3. Packed Material Data
Move AO from RT2.B to RT0.A (pack with albedo like UE5). Free up RT2 for future custom data (emissive intensity, shading model ID, subsurface).

## Impact

- **Bandwidth**: -15.8MB/frame at 1080p (normal compression)
- **VRAM**: +8MB for velocity buffer, -15.8MB for normal compression = net -7.8MB
- **Performance**: ~0.2ms faster lighting pass (less bandwidth)
- **Unblocks**: TAA (3.14.0), Motion Blur (3.18.0), SSR temporal (3.16.0)
- **Breaking**: G-Buffer format change requires updating gbuffer_ps, deferred_lighting_ps, all G-Buffer readers

## Acceptance Criteria

1. Normals encoded as octahedral RG8 — lighting pass produces identical visual result
2. Velocity buffer correctly shows motion vectors (debug visualization: color = velocity direction)
3. Static camera = zero velocity everywhere
4. Camera rotation = radial velocity pattern
5. Bandwidth reduced (GPU profiler shows less RT memory)
6. All existing deferred demos still work (no visual regression)
