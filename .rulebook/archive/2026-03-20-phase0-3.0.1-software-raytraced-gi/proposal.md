# Proposal: Software Ray-Traced Global Illumination (Phase 3a)

## Why

UzEngine's current GI (SSGI, task 3.52.0) only traces rays in screen space — it cannot see off-screen geometry, occluded surfaces, or anything behind the camera. In an outdoor scene with small objects, most rays miss because there's nothing to bounce off in screen space. This is why shadows remain black despite SkyLight SH irradiance being correct (L0=0.84) — the ambient contribution is ~0.1 HDR units while direct sun is ~5-10, and the auto-exposure compresses shadows to near-zero.

UE5's Lumen solves this with three tiers:
1. **Signed Distance Field (SDF) ray tracing** — traces rays against mesh distance fields for infinite-range bounces (not limited to screen)
2. **Radiance Cache** — 3D grid of irradiance probes updated each frame via SDF traces, providing stable low-frequency GI everywhere
3. **Screen-space traces** — short-range high-quality traces using the depth buffer (what our SSGI does)

Without tiers 1 and 2, shadows in any scene will be too dark. This is not a bug — it's a missing system. This task implements a complete software ray-traced GI pipeline equivalent to UE5 Lumen's core architecture.

## What Changes

### New Systems

1. **Mesh SDF Generator** — offline or runtime generation of per-mesh signed distance fields stored as 3D textures. Each mesh gets a low-resolution (32-64 voxel) SDF that represents its shape for ray tracing.

2. **Global Distance Field** — composites all mesh SDFs into a single coarse global SDF (clipmap or cascaded grid) centered on the camera. Used for long-range ray marching.

3. **SDF Ray Tracer** — compute shader that traces rays against the global distance field using sphere marching. Returns hit position, normal, and material ID for each ray.

4. **Radiance Cache** — 3D grid of irradiance probes (world-space). Each probe stores L2 SH coefficients updated by tracing rays from probe positions through the SDF. Probes near the camera are updated more frequently.

5. **Probe Interpolation** — deferred lighting reads the radiance cache via trilinear interpolation of the 8 nearest probes, with visibility weighting (Chebyshev) to prevent light leaking through walls.

6. **SSGI Integration** — existing SSGI (task 3.52.0) becomes the near-field tier, blended with radiance cache for mid/far-field.

### Refactoring

- `deferred_lighting_ps.hlsl` — replace current IBL-only ambient with 3-tier GI: SSGI (near) + RadianceCache (mid) + SkyLight SH (far)
- `frame_lighting.cpp` — add SDF update + radiance cache update passes before SSGI
- `scene_lifecycle.cpp` — create SDF textures and radiance cache buffers

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`, `docs/specs/rendering/screen-space-effects.md`
- Affected code: `runtime/engine/renderer/gi/` (new SDF + radiance cache), `runtime/shaders/runtime/gi/` (new compute shaders)
- Breaking change: NO (additive — new GI system augments existing ambient)
- User benefit: Shadowed areas have visible fill light matching UE5 quality
- Enables: Indoor scenes, color bleeding, dynamic GI for time-of-day
