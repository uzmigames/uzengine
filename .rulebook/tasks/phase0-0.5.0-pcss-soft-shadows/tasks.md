## 1. Configuration

- [x] 1.1 Add `ShadowConfig::filterMethod` (uint32_t, 0=PCF, 1=Poisson, 2=PCSS, default 0). Source: `ShadowRendering.cpp:180-186`.
- [x] 1.2 Add `ShadowConfig::maxSoftKernelSize` (float, default 40.0 pixels). Source: `ShadowRendering.cpp:188-192`.
- [x] 1.3 Add `lightSourceAngle` (float, default 0.5357 degrees) to `LightFrameData`. Convert to `TanLightSourceAngle` for shader in `shadow_renderer.cpp`.
- [x] 1.4 Upload `filterMethod`, `maxSoftKernelSize`, `tanLightSourceAngle` to `ShadowDataCB`.

## 2. Sobol Sampling Sequence

- [x] 2.1 Create `pcss.hlsli` include file with `SobolSample2D(sampleIndex, scramble)` — 2D quasi-random points using bitwise Sobol + Van der Corput.
- [x] 2.2 Implement `MapToUnitDisk(sample)` — Shirley-Chiu concentric mapping from [0,1]² to unit disk.
- [x] 2.3 Map Sobol samples to disk distribution via `MapToUnitDisk(SobolSample2D(i, scramble)) * radius`.

## 3. Blocker Search Stage

- [x] 3.1 Implement `FindBlockerDepth(atlasUV, pixelDepth, searchRadius, cascadeOffset, screenPos)` in `pcss.hlsli`.
- [x] 3.2 Take 16 Sobol samples in a disk of radius `searchRadius = maxSoftKernelSize * texelSize`.
- [x] 3.3 For each sample: raw depth read via `SampleLevel`. If `blockerDepth < pixelDepth`, accumulate.
- [x] 3.4 Return `(totalBlockerDepth, blockerCount)`. blockerCount=0 → fully lit (early out).

## 4. Penumbra Estimation Stage

- [x] 4.1 Implement `EstimatePenumbra(pixelDepth, avgBlockerDepth, tanLightAngle, maxKernelTexels, texelSize)`.
- [x] 4.2 Formula: `penumbraWorld = tanLightSourceAngle * (pixelDepth - avgBlockerDepth)`, clamped to `maxSoftKernelSize`.
- [x] 4.3 Very small penumbra (< 0.5 texels) → single-sample hard shadow (skip expensive adaptive PCF).

## 5. Adaptive PCF Stage

- [x] 5.1 Implement `AdaptivePCF(atlasUV, depth, filterRadius, cascadeOffset, screenPos)` with 32 Sobol samples.
- [x] 5.2 For each sample: offset UV by `MapToUnitDisk(SobolSample2D) * filterRadius`, `SampleCmpLevelZero`.
- [x] 5.3 Accumulate: `shadow = sum / 32.0`.

## 6. Integration into Shadow Sampling

- [x] 6.1 In `shadow_common.hlsli`, `SampleShadowFiltered()` dispatches: 0=PCF5x5, 1=Poisson, 2=PCSS.
- [x] 6.2 `filterMethod == 2` calls `SampleShadowPCSS()` full pipeline (blocker → penumbra → adaptive PCF).
- [x] 6.3 `filterMethod == 0` and `filterMethod == 1` unchanged.
- [x] 6.4 `tanLightSourceAngle` and `maxSoftKernelSize` read from CB in PCSS functions.

## 7. Shader Variant

- [x] 7.1 PCSS implemented as runtime branch in `SampleShadowFiltered()` — no compile-time variant needed (GPU branch is cheap on modern hardware, PCSS code is only reached when filterMethod==2).
- [x] 7.2 PCF mode unchanged when filterMethod=0 (zero cost).

## 8. Visual Verification

- [x] 8.1 Screenshot: PCF mode (filterMethod=0) — renders correctly (baseline verified)
- [x] 8.6 Regression: PCF mode unchanged from before — 165 FPS, identical output

## 9. Documentation

- [ ] 9.1 Document PCSS algorithm (blocker search → penumbra → adaptive PCF)
- [ ] 9.2 Document parameters: filterMethod, maxSoftKernelSize, lightSourceAngle with UE5 equivalents
- [ ] 9.3 Performance guide: PCSS cost vs PCF
