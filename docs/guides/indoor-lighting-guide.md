<!-- doc-meta
title: Indoor Lighting Guide
status: stable
owner: rendering
last-updated: 2026-04-06
-->

# Indoor Lighting Guide

UzEngine ships three layered systems for lighting interior scenes. Each builds
on the previous and can be combined freely.

## 1. SkyLight tuning (cheapest)

The first lever for any indoor space is the `SkyLightComponent`. The renderer
already pulls these values into `PerFrameCB` through `frame_constants.cpp` and
the deferred lighting shader honours them directly.

| Property | Effect | Indoor starting point |
|---|---|---|
| `intensity` | Multiplies the IBL specular and SH irradiance | `1.0` outdoors, `1.4`–`2.5` indoors to lift dark corners |
| `indirectLightingIntensity` | Diffuse-only multiplier on the SH coefficients (applied CPU-side before upload) | `1.0`–`2.0` indoors to brighten ambient bounce without blowing out specular |
| `minOcclusion` | Floor on the AO term (`max(ao, minOcclusion)`) | `0.05`–`0.15` indoors so deep crevices still receive a bit of light |
| `lowerHemisphereIsBlack` | When `false`, blends `lowerHemisphereColor` into the L0 + L1_Z bands | `false` for warm-floor / cool-ceiling tints |

CVars:

- `r.SkyLight.Intensity` — overrides component intensity
- `r.SkyLight.MinAmbient` — clamps the AO floor at runtime
- `r.SkyLight.IndirectIntensity` — independent diffuse multiplier

## 2. Light Probe Volume (author-placed irradiance grid)

Add a `LightProbeVolumeComponent` to a node and place its AABB around the
indoor space. The renderer allocates an `IrradianceProbeGrid` (one
`StructuredBuffer<float4>` of L2 SH probes), seeds every probe from the current
sky cubemap, and trilinearly samples 8 surrounding probes per pixel during
deferred lighting.

```cpp
LightProbeVolumeComponent vol;
vol.gridDimensionsX = 8;     // 8 × 4 × 8 = 256 probes (default)
vol.gridDimensionsY = 4;
vol.gridDimensionsZ = 8;
vol.extentMin = Vec3{-500, 0,    -500};  // cm — corner of the room
vol.extentMax = Vec3{ 500, 400,   500};
vol.enabled   = true;
vol.needsRebuild = true;     // forces initial sky-SH fill on first frame
```

Tips:

- For a single rectangular room, 8×4×8 is enough. Multi-floor scenes deserve
  one probe volume per floor.
- The volume's extents must enclose the geometry you want to sample. Pixels
  outside the AABB clamp to the nearest probe.
- Set `vol.needsRebuild = true` whenever the dimensions or extents change.
- Probe count is capped at `IrradianceProbeGrid::kMaxProbes` (16384).

The probe volume contribution is gated by `r.GI.Intensity` (also drives the
radiance cache).

## 3. Bounce light (radiance cache, DDGI, Brixelizer)

For real coloured bounce light — warm walls reflecting onto floors, sky-blue
spilling through windows — enable one of the existing GI subsystems on top of
the probe volume:

- **Radiance cache** (`m_radianceCache`, world-space SDF probe grid). Toggle
  via `r.GI.SDF=1`. Best for fast iteration; updates probes from the global
  distance field each frame.
- **Brixelizer GI** (AMD FidelityFX). Toggle via `r.GI.Brixelizer=1`. Best
  visual quality; uses sparse SDF voxel ray tracing.

The deferred lighting shader composites these on top of the SkyLight SH and
the LPV trilinear blend, in that order, before applying `minOcclusion`.

## Diagnostics

Use the standard diagnostic tools when an interior scene looks wrong:

```bash
# Inspect every pass output as PNGs
uzengine.exe --pass-dump --frames 200 --project <project> --editor

# Pixel readback (13 GBuffer values + HDR)
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 \
             --project <project> --editor

# Visualize SkyLight indirect / shadow contribution
uzengine.exe --view-mode lighting --screenshot-stdout --frames 8 \
             --project <project> --editor
```

If `--vis hdr` is uniformly dark, raise `r.SkyLight.IndirectIntensity` first;
if only the corners are dark, raise `r.SkyLight.MinAmbient`. If the corridor
has flat ambient with no spatial variation, you almost certainly need a
`LightProbeVolumeComponent` covering the area.
