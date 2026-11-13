<!-- doc-meta
title: Local Shadow Shader Functions
category: rendering
status: current
updated: 2026-11-13
-->

# Point and spot PCSS shader functions

The general [shader library](../shader-library.md) describes the runtime shader
tree. These local shadow functions live in
`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`; both the clustered
and full light loops reach them through `ShadePointLight` / `ShadeSpotLight`.

| Shipped symbol | Contract |
|---|---|
| `LocalPcssRadius(softShadowRadius, sourceRadius)` | Select the per-light radius when nonzero, otherwise positive point `sourceRadius`, otherwise `localPcssSourceRadius`. Spots pass zero for `sourceRadius`. Units are cm; a negative result requests the hard path. |
| `LocalShadow(entry, cubeBase, worldPos, N, sourceRadius)` | Call `SampleLocalShadowSoft` only when `softPCSS`, `localSoftPCSS` and a positive selected radius agree; otherwise call `SampleLocalShadow`. |
| `SampleLocalShadow(entry, worldPos, geometricNormal)` | Original comparison-sampler path using linear radial depth, distance-scaled normal offset and bias in cm. |
| `SampleLocalShadowSoft(entry, cubeBase, worldPos, N, sourceRadius)` | Search 16 radial-depth samples, estimate the penumbra, then filter with 16 comparison taps when blockers exist. No blockers returns visibility 1. |
| `LocalShadowTapUV(ray, entry, cubeBase, atlasDims)` | Project a ray to the selected tile and clamp to its half-texel inset. Point taps select a cube face by major axis; spots keep their supplied entry. |
| `PcssLocalPenumbraRadius(receiverDistance, blockerDistance, sourceRadius)` | `max(receiverDistance - blockerDistance, 0) * max(sourceRadius, 0) / max(blockerDistance, 1e-3)`, the physical radius in cm. CPU mirror: `engine/renderer/shadows/shadow_pcss.h`. |

For points, `entry` is the receiver's face and `cubeBase` is the first of six
consecutive entries, ordered +X, -X, +Y, -Y, +Z, -Z. For spots,
`cubeBase = 0xFFFFFFFFu`. Texture access happens after face selection. Each entry
occupies six `float4` records in `localShadowRaw`: view-projection rows, atlas
rectangle plus bias, then light position plus range.

The soft sampler intersects each tap ray with the receiver plane to obtain its
comparison distance. It projects blocker distances onto the centre ray before
averaging, converts the estimated physical penumbra to texels at the receiver,
and clamps each filter axis through `PcssClampRadiusTexels`. The search uses the
maximum clamped radius and includes the centre. `kPoissonDisk`,
`RotateShadowTap`, `ShadowKernelRotationNoise` and the clamp are shared with
`shadow.hlsli`. Local rotation is seeded by the centre atlas texel coordinates,
without a frame term. The non-bindless local samplers return visibility 1.

`lighting/shadow_pcss.hlsli` separately defines
`BlockerSearchPCSS(atlasUV, receiverDepth, searchRadius, atlas, atlasSize,
atlasMin, atlasMax, rotAngle)`, which clamps raw depth loads to supplied bounds,
and `EstimatePenumbraPCSS`. That standalone extension is not included by any
shipped pass and is not the local radial-depth implementation above. There are
no shipped `SampleShadowPCSS_PointLight` or `SampleShadowPCSS_SpotLight` symbols.

See [point and spot light shadows](point-spot-light-shadows.md) for controls,
data layout and CPU test coverage. The algorithm follows Fernando 2005,
"Percentage-Closer Soft Shadows"; receiver-plane comparisons follow Isidoro
2006, "Shadow Mapping: GPU-based Tips and Techniques".
