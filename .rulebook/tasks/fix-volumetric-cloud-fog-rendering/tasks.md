## 1. Cloud Shader — Raymarch Rewrite (clouds.ps.hlsl)

- [ ] 1.1 Replace fixed 32-step loop with quality-driven step count via `maxSteps` root constant (32/64/96/128)
- [ ] 1.2 Implement exponential step distribution: finer steps near ray entry, coarser toward ray exit (`stepSize = baseStep * pow(ratio, i)`) to concentrate samples where density changes most
- [ ] 1.3 Replace IGN-only jitter with R2 quasi-random sequence (Martin Roberts 2018) for better blue-noise distribution: `jitter = frac(0.5 + frameIndex * 0.7548776662 + pixelHash * 0.5698402910)`
- [ ] 1.4 Add spatial blue noise offset per-pixel using Bayer matrix fallback when no blue noise texture is bound
- [ ] 1.5 Fix transmittance integration to use proper energy-conserving form: `integScatter += (scatter - scatter * stepT) / max(extinction, 1e-6) * transmittance`
- [ ] 1.6 Ensure early-exit threshold accounts for accumulated color (not just transmittance < 0.01)

## 2. Cloud Shader — Improved Density Sampling

- [ ] 2.1 Increase FBM to 4 octaves for base shape (add octave 4 at 0.0625 amplitude, frequency ~8.3x)
- [ ] 2.2 Add Worley noise (cellular) as erosion channel blended with Perlin FBM: `shape = perlinFBM - worley * erosionWeight`
- [ ] 2.3 Implement smooth height gradient with cosine falloff at boundaries instead of hard smoothstep: `hGrad = sin(saturate(h) * PI) * smoothstep(0.0, 0.1, h) * smoothstep(1.0, 0.85, h)`
- [ ] 2.4 Add weather map concept: use 2D low-frequency noise to vary coverage spatially (not uniform coverage everywhere)
- [ ] 2.5 Implement detail noise at 2 octaves (up from 1) applied only to cloud edges for fine wispy detail
- [ ] 2.6 Add density remapping with smooth Hermite curve instead of linear remap: `d = smoothstep(0.0, 1.0, d)`

## 3. Cloud Shader — Lighting Improvements

- [ ] 3.1 Increase sun transmittance sample count from 6 to 8 steps with better distribution (logarithmic spacing)
- [ ] 3.2 Add multi-scattering approximation (Frostbite 2015): `msContrib = (1.0 - exp(-2.0 * opticalDepth)) * 0.25` added to ambient term
- [ ] 3.3 Fix powder effect to properly attenuate at cloud boundaries: `powder = 1.0 - exp(-extinction * stepSize * 4.0)` with depth-dependent falloff
- [ ] 3.4 Add silver lining effect: extra forward-scattering boost when cosTheta > 0.95 (sun directly behind cloud edges)
- [ ] 3.5 Improve ambient lighting with height-based gradient lerp between ground bounce and sky color

## 4. Temporal Accumulation Pass (NEW: clouds_temporal.ps.hlsl)

- [ ] 4.1 Create `clouds_temporal.ps.hlsl` — reads current frame cloud result + previous frame history
- [ ] 4.2 Implement motion vector reprojection: use velocity buffer to find previous-frame UV for each pixel
- [ ] 4.3 Implement neighborhood clamping (3x3 min/max of current frame) to reject stale history and prevent ghosting
- [ ] 4.4 Blend factor: 95% history + 5% current (configurable via root constant `temporalBlendFactor`)
- [ ] 4.5 Invalidate history on camera cut (blend factor = 0% history when `historyValid` flag is false)
- [ ] 4.6 Handle disocclusion: when reprojected UV is out of bounds or depth difference exceeds threshold, use current frame only
- [ ] 4.7 Register shader in `shader_library.cpp` and create PSO in renderer

## 5. Volumetric Fog Integration

- [ ] 5.1 Rewrite `volumetric_apply.ps.hlsl` to use analytical height fog integration along view ray (Frostbite 2014 formula): `fogAmount = (fogDensity / fogHeightFalloff) * exp(-cameraHeight * fogHeightFalloff) * (1.0 - exp(-dist * rayDirY * fogHeightFalloff)) / (rayDirY * fogHeightFalloff)`
- [ ] 5.2 Add fog inscattering with dual-lobe Henyey-Greenstein (g=0.8 forward, g=-0.2 back) matching cloud phase function
- [ ] 5.3 Integrate fog into cloud composite pass — fog applied to scene color BEFORE cloud alpha blend, not as separate flat layer
- [ ] 5.4 Add fog-sky blending: fog density smoothly transitions to zero at sky depth (no hard cutoff at depth >= 0.999)
- [ ] 5.5 Add aerial perspective: faint fog applied even to cloud layer itself at far distances
- [ ] 5.6 Wire fog pass dispatch in renderer.cpp with correct barrier transitions

## 6. C++ Renderer Changes (renderer.h/cpp)

- [ ] 6.1 Add `CloudQuality` enum: `LOW=0, MEDIUM=1, HIGH=2, CINEMATIC=3` with step counts 32/64/96/128
- [ ] 6.2 Add cloud quality root constant to cloud pass constant buffer (replace a padding slot)
- [ ] 6.3 Create temporal accumulation render targets: `cloudHistoryRT_` (RGBA16F, same resolution as HDR) and `cloudCurrentRT_` (intermediate)
- [ ] 6.4 Add temporal accumulation pass dispatch after cloud pass — reads cloudCurrentRT_ + cloudHistoryRT_, writes to cloudHistoryRT_
- [ ] 6.5 Add `historyValid_` flag — set false on first frame, camera teleport, or quality change; set true after first successful temporal resolve
- [ ] 6.6 Swap cloud history buffers each frame (double-buffer pattern)
- [ ] 6.7 Add `cloudQuality_` member variable with setter `setCloudQuality(CloudQuality q)` and IPC handler
- [ ] 6.8 Add `cloudTemporalEnabled_` flag with setter and IPC handler
- [ ] 6.9 Parse `cloud_quality` and `cloud_temporal` from .uzscene files in scene loader

## 7. Debug Visualization

- [ ] 7.1 Add `cloudDebugMode` root constant (0=off, 1=density, 2=stepCount, 3=transmittance, 4=lighting)
- [ ] 7.2 Implement density visualization: output raw density as grayscale in cloud shader when debugMode==1
- [ ] 7.3 Implement step count heatmap: color-code raymarch iterations (blue=few, red=many) when debugMode==2
- [ ] 7.4 Implement transmittance visualization: show transmittance as grayscale when debugMode==3
- [ ] 7.5 Implement lighting contribution: show scattered light only (no ambient) when debugMode==4
- [ ] 7.6 Add IPC handler `renderer.setCloudDebug` to toggle debug modes from editor

## 8. Test Scene

- [ ] 8.1 Create `assets/scenes/volumetric_test.uzscene` with ground plane, sky atmosphere, directional light
- [ ] 8.2 Configure cloud layer: coverage=0.55, density=0.12, base=800, top=2400, quality=HIGH, temporal=true
- [ ] 8.3 Configure fog: density=0.015, height_falloff=0.2, base_height=0.0, max_opacity=0.6
- [ ] 8.4 Add multiple camera positions to validate: looking up, looking at horizon, looking through thick clouds
- [ ] 8.5 Ensure clouds_enabled=true and fog integrated in the same scene

## 9. Performance Validation

- [ ] 9.1 Profile each quality level at 1080p: LOW < 2ms, MEDIUM < 3ms, HIGH < 5ms, CINEMATIC < 8ms GPU time
- [ ] 9.2 Verify temporal accumulation reduces visible noise compared to single-frame rendering
- [ ] 9.3 Verify no visible banding/slicing at MEDIUM quality or above
- [ ] 9.4 Verify fog blends smoothly with sky (no hard edges at depth boundary)
- [ ] 9.5 Verify empty-space skipping still works with new step distribution

## 10. Documentation

- [ ] 10.1 Update `docs/API.md` with new IPC commands: `renderer.setCloudQuality`, `renderer.setCloudDebug`, `renderer.setCloudTemporal`
- [ ] 10.2 Document cloud quality levels and their performance characteristics in `docs/specs/rendering/volumetric-clouds.md`
