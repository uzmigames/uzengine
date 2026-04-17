<!-- doc-meta
title: HDRI Pipeline Data Flow Review
category: analysis
created: 2026-03-24
-->

# HDRI → IBL → Reflection Pipeline: Data Flow Review

## Summary

Traced the full HDRI environment map pipeline from EXR loading through IBL baking to per-frame CB propagation. Identified **one critical bug** (missing SH projection for HDRI) and **one potential issue** (reflection probe using procedural sky SRV instead of HDRI).

---

## Stage 1: EXR/HDR Loading — `texture_loader.cpp:881`

**Status: CORRECT**

- `TextureLoader::LoadHDR()` loads via tinyexr (EXR) or stbi_loadf (HDR)
- Both produce float32 RGBA pixels — values > 1.0 are preserved
- `ConvertFloat32ToFloat16()` (line 870) converts to RGBA16F via `FloatToHalf()`
- GPU texture created as `RHIFormat::RGBA16Float`, single mip level
- Barrier: CopyDst → ShaderResource
- `WaitIdle()` ensures upload completes before return
- Returns `TextureLoadResult` with valid `srvIndex`

**HDR values are preserved end-to-end.** No clamping, no tonemapping at load time.

---

## Stage 2: SetHDRIEnvironment — `scene_lifecycle.cpp:1458`

**Status: CORRECT (with one missing step — see Finding #1)**

Flow:
1. `m_iblBaker->EquirectToCubemap(equirectSrvIndex, cubemapSize)` — converts equirect → 6-face cubemap (RGBA16F)
2. `m_iblBaker->BakeEnvironment(m_hdriCubemapSrv, m_hdriCubemap)` — bakes irradiance + prefiltered specular from cubemap
3. `m_iblResult = bakeResult` — stores irradianceSrvIndex + prefilteredSrvIndex
4. `m_hdriActive = true` — flags HDRI mode active
5. `m_skyIBLDirty = false` — suppresses procedural sky IBL re-bake
6. `m_skyCubemapSrv = m_hdriCubemapSrv` — skybox now renders HDRI

SRV indices at this point:
- `m_hdriCubemapSrv` = equirect→cubemap result SRV (used for skybox background)
- `m_iblResult.irradianceSrvIndex` = baked irradiance cubemap SRV
- `m_iblResult.prefilteredSrvIndex` = baked prefiltered specular cubemap SRV
- `m_skyCubemapSrv` = overwritten to `m_hdriCubemapSrv`

**No HDRI data is lost at this stage.** The `BakeEnvironment` correctly receives the HDRI cubemap.

---

## Stage 3: IBLBaker::BakeEnvironment — `ibl_baker.cpp:389`

**Status: CORRECT**

Flow:
1. Creates irradiance cubemap (32x32, RGBA16F, 6 slices)
2. Creates 6 per-mip temporary textures for prefiltered specular
3. Creates final prefiltered cubemap (256x256, 6 mips, RGBA16F)
4. **Irradiance dispatch**: root constants = {envCubemapSrvIndex, faceSize}, UAV = irradiance cubemap
   - SM5 path: `SetFragmentTexture(0, envCubemapTexture)` — binds HDRI cubemap at t0
   - Bindless path: uses `envCubemapSrvIndex` in root constants to index ResourceDescriptorHeap
5. **Prefilter dispatch** (per mip): root constants = {envCubemapSrvIndex, mipSize, roughness, sampleCount}
   - SM5 path: `SetFragmentTexture(0, envCubemapTexture)` — binds HDRI cubemap at t0
   - Per-mip texture → UAV dispatch → barrier → CopySrc
6. Copy per-mip textures into final mipmapped cubemap (CopyTextureRegion per face per mip)
7. Barrier: CopyDst → ShaderResource on final cubemap
8. Fence signal + WaitCPU
9. Returns SRV indices for irradiance and prefiltered textures

**Barrier ordering is correct:**
- Irradiance: dispatch → UAV→SRV barrier
- Each prefilter mip: dispatch → UAV→CopySrc barrier
- Final cubemap: Common→CopyDst → (copies) → CopyDst→SRV

**The prefiltered cubemap contains HDRI content.** The envCubemapSrvIndex passed in is the HDRI cubemap SRV.

---

## Stage 4: Per-Frame CB Propagation — `frame_setup.cpp:246`

**Status: CORRECT**

```cpp
perFrame.irradianceSrvIndex  = m_iblResult.irradianceSrvIndex;   // line 246
perFrame.prefilteredSrvIndex = m_iblResult.prefilteredSrvIndex;  // line 247
perFrame.brdfLutSrvIndex     = m_brdfLutSrvIndex;               // line 248
perFrame.prefilteredMipLevels = IBLBaker::kPrefilteredMipLevels; // line 249
```

These read directly from `m_iblResult`, which was set by `SetHDRIEnvironment` at line 1491.

**No code path overwrites `m_iblResult` after HDRI is set:**
- `PostFrameAtmosphereBake()` (line 476) checks `!*ctx.skyIBLDirty` — returns early because `m_skyIBLDirty = false`
- `m_skyIBLDirty` is only set to `true` as a class default (`scene_renderer.h:669`) — never re-set after `SetHDRIEnvironment` clears it

**HDRI IBL SRV indices reach the shader correctly.**

---

## Stage 5: Reflection Probes — `frame_setup.cpp:473`

**Status: POTENTIAL ISSUE**

```cpp
m_reflectionProbes.CaptureIfDirty(
    *ctx.world, captureDesc, m_skyCubemapSrv, m_skyCubemap);  // line 487-488

const auto probeResult = m_reflectionProbes.Update(
    *ctx.world, m_skyCubemapSrv, m_skyCubemap);               // line 490-491
```

After HDRI load, `m_skyCubemapSrv` points to the HDRI cubemap (set at line 1501), so probes capture the HDRI environment. **This is correct for skybox fallback.**

However, `m_skyCubemap` is the **procedural sky cubemap texture handle** — it was NOT overwritten by `SetHDRIEnvironment`. Only `m_skyCubemapSrv` was changed.

**If `ReflectionProbeRenderer` uses the texture handle (not the SRV index) for any purpose, it would be reading the wrong cubemap.** This needs verification — the probe capture uses `m_skyCubemapSrv` for bindless access, so it may be fine, but on SM5 paths the texture handle matters.

---

## Stage 6: SH Projection — `atmosphere_renderer.cpp:490`

### **FINDING #1: CRITICAL BUG — Missing SH Projection for HDRI**

When HDRI is active, `PostFrameAtmosphereBake()` returns early at line 476:
```cpp
if (!*ctx.skyIBLDirty || !ctx.iblBaker || ...) {
    return;  // skyIBLDirty = false when HDRI active → always returns
}
```

The SH projection code (lines 490-510) is inside `PostFrameAtmosphereBake` and only executes when `skyIBLDirty` is true. **Since `SetHDRIEnvironment` sets `m_skyIBLDirty = false`, SH projection from the HDRI cubemap NEVER happens.**

This means `m_skyIrradianceSH` retains whatever value it had before the HDRI was loaded:
- If procedural sky was baked first → SH contains procedural sky irradiance (stale data)
- If no sky was baked yet → SH is all zeros (default)

**Impact:**
- `frame_setup.cpp:285-290`: SH coefficients uploaded to CB are stale/wrong
- `deferred_lighting_ps.hlsl:360`: `GetSkySHDiffuse(N, skyIrradianceSH)` — used as diffuse fallback when `irradianceSrvIndex == 0` (SM5 or no IBL). Not a problem on bindless path since irradiance cubemap SRV is valid.
- `frame_lighting.cpp:941-943`: Sky fog inscattering color uses SH L0 — **wrong fog color when using HDRI**

**Fix needed in `SetHDRIEnvironment()`:** After baking IBL, project SH from the HDRI cubemap:
```cpp
auto shResult = ProjectCubemapToSH(m_device, cmdList, m_hdriCubemap, 64);
m_skyIrradianceSH = shResult;
```

---

## Stage 7: Shader IBL Sampling — `deferred_lighting_ps.hlsl:350-414`

**Status: CORRECT (bindless path)**

The shader's specular reflection pipeline:
1. R = reflect(-V, N) at line 370
2. mipLevel = roughness * (prefilteredMipLevels - 1) at line 371
3. prefilteredColor = prefTex.SampleLevel(clampSampler, WorldToCubemap(R), mipLevel) at line 378
4. envBRDF = brdfTex.SampleLevel(clampSampler, float2(NoV, roughness), 0) at line 382
5. specularAmbient = prefilteredColor * (F0 * envBRDF.x + F90 * envBRDF.y) at line 391

The `prefilteredSrvIndex` in the CB points to the HDRI-baked prefiltered cubemap — correct.

**Coordinate convention check:**
- `CubeFaceDirection()` in ibl.hlsli returns Y-up directions (DX cubemap convention)
- `DirectionToEquirectUV()` in equirect_to_cubemap_cs.hlsl uses Y-up (dir.y for latitude)
- `WorldToCubemap()` converts Z-up world → Y-up cubemap: `(X, -Z, Y)`
- All shaders use `WorldToCubemap()` before sampling IBL cubemaps

**Coordinate conventions are consistent.** The equirect conversion writes Y-up cubemaps, and the lighting shader converts world-space directions to Y-up before sampling.

**One edge case at lines 411-414:**
```hlsl
float iblEnergy = dot(prefilteredColor, float3(0.299, 0.587, 0.114));
if (iblEnergy < 0.001) {
    ambient = lerp(baseColor * 0.05, F0 * 0.15, metallic) * skyInt;
}
```
If the prefiltered cubemap is somehow empty (black), this fallback kicks in and produces flat/dim ambient. If the IBL bake succeeded but produced near-zero values (e.g., from a very dark HDRI), this threshold might trigger incorrectly. **This is not a bug per se, but could cause flat-looking reflections with very dark HDRIs.**

---

## Summary of Findings

### Critical (causes visible artifacts)

1. **Missing SH projection for HDRI** — `SetHDRIEnvironment()` does not project the HDRI cubemap into SH coefficients. `m_skyIrradianceSH` is stale (procedural sky values or zeros). Affects:
   - Fog inscattering color (line 941-943 in frame_lighting.cpp)
   - SH diffuse fallback on SM5 path (line 360 in deferred_lighting_ps.hlsl)
   - SH L0 logging is misleading (reports procedural sky values for HDRI scene)

### Minor / Needs Verification

2. **`m_skyCubemap` handle not updated for HDRI** — `SetHDRIEnvironment` overrides `m_skyCubemapSrv` but not `m_skyCubemap`. Reflection probe capture/update receives the old procedural sky texture handle. On bindless path this is fine (SRV index is used), but SM5 path may use the wrong texture handle for explicit binding.

### Not a Bug

3. **Prefiltered cubemap energy fallback** — The `iblEnergy < 0.001` check in the shader could mask an HDRI bake failure with a flat ambient color instead of black, making it harder to diagnose IBL issues. Consider logging when this fallback triggers.

---

## Data Flow Diagram

```
LoadHDR("scene.exr")
  │ tinyexr → float32 RGBA → ConvertFloat32ToFloat16 → RGBA16F texture
  │ SRV = device->GetTextureSRVIndex()
  ▼
SetHDRIEnvironment(equirectSrvIndex)
  │
  ├─► EquirectToCubemap(equirectSrvIndex, 512)
  │     equirect_to_cubemap_cs.hlsl: dispatch [64,64,6]
  │     result: cubemap RGBA16F 512x512x6, SRV = m_hdriCubemapSrv
  │
  ├─► BakeEnvironment(m_hdriCubemapSrv, m_hdriCubemap)
  │     irradiance_cs.hlsl:  32x32x6 cubemap → irradianceSrvIndex
  │     prefilter_cs.hlsl:   256x256x6, 6 mips → prefilteredSrvIndex
  │     m_iblResult = { irradianceSrvIndex, prefilteredSrvIndex }
  │
  ├─► m_hdriActive = true
  ├─► m_skyIBLDirty = false
  ├─► m_skyCubemapSrv = m_hdriCubemapSrv
  └─► *** MISSING: ProjectCubemapToSH(m_hdriCubemap) → m_skyIrradianceSH ***
       (stale SH data persists from procedural sky)

Per-Frame CB Update (frame_setup.cpp:246)
  │ perFrame.irradianceSrvIndex  = m_iblResult.irradianceSrvIndex   ✓
  │ perFrame.prefilteredSrvIndex = m_iblResult.prefilteredSrvIndex  ✓
  │ perFrame.brdfLutSrvIndex     = m_brdfLutSrvIndex                ✓
  │ perFrame.skyIrradianceSH[7]  = m_skyIrradianceSH * shScale     ✗ STALE
  ▼
Shader (deferred_lighting_ps.hlsl)
  │ Bindless: irradianceSrvIndex → TextureCube sample (HDRI data)   ✓
  │ Bindless: prefilteredSrvIndex → TextureCube sample (HDRI data)  ✓
  │ SM5: SH fallback → GetSkySHDiffuse(N, skyIrradianceSH)         ✗ STALE
  │ Fog: SH L0 for inscattering                                    ✗ STALE
```
