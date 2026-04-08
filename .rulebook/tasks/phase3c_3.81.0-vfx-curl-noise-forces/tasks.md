# Task 3.81.0 — VFX Curl Noise Forces

Divergence-free curl noise field for organic swirling particle motion (smoke, fire, dust).
Wires TurbulenceModule's existing FBM noise into a proper curl noise computation and
exposes animated curl parameters via the update constant buffer.

**Reference:** Bridson et al. 2007 "Curl-Noise for Procedural Fluid Flow" — SIGGRAPH 2007.

---

## 1. Shader — Curl Noise HLSLI

- [x] 1.1 Create `runtime/shaders/runtime/vfx/vfx_curl_noise.hlsli`. CurlNoise3D already exists in vfx_common.hlsli (using FBM3D + central finite differences). Created wrapper file with AnimatedCurlNoise convenience function.
- [x] 1.2 Add `float3 AnimatedCurlNoise(float3 p, float time, float speed, float freq)` that applies frequency scaling and Z-axis time scroll, then calls existing CurlNoise3D from vfx_common.hlsli.
- [x] 1.3 File header cites `@ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"`, `@constants eps=0.01 (finite-difference step, from CurlNoise3D in vfx_common.hlsli)`.

## 2. Constant Buffer — Update CS Extension

- [x] 2.1 Added four fields to `VFXUpdateCB`: `float curlStrength`, `float curlFrequency`, `float curlSpeed`, `float totalTime` (16-byte aligned block). Updated static_assert to 128 bytes (8 x float4).
- [x] 2.2 Mirrored the same four fields in HLSL `VFXUpdateConstants` cbuffer in `particle_update_cs.hlsl` after `_padShared0` (inside UZ_BINDLESS block). Total cbuffer matches C++ at 128 bytes.

## 3. Particle Update CS — Curl Force Integration

- [x] 3.1 Added `#include "vfx_curl_noise.hlsli"` after existing vfx_common include.
- [x] 3.2 Added curl force step after gravity+damping, before position integration. Guarded by `curlStrength > 0.0f` and `UZ_BINDLESS` to skip FBM cost when disabled. Cited `@ref Bridson 2007`.
- [x] 3.3 `totalTime` is part of the curl fields block in VFXUpdateConstants. Set from `m_totalTime` on CPU side via the new DispatchCompute parameter.

## 4. CPU — Upload Curl Params

- [x] 4.1 In DispatchCompute(), added `cb.curlStrength/curlFrequency/curlSpeed/totalTime` from config + totalTime parameter.
- [x] 4.2 Added three fields to `VFXEmitterConfig`: `curlStrength=0.0f`, `curlFrequency=0.5f`, `curlSpeed=0.2f`. Also added `float totalTime` parameter to DispatchCompute() signature, passed from `m_totalTime` at the call site.

## 5. TurbulenceModule — Curl Mode Flag

- [x] 5.1 Added `bool useCurlMode = false` to TurbulenceModule in turbulence_module.h with Bridson 2007 citation.
- [x] 5.2 Updated `GetParameters()` to include a `"use_curl_mode"` Bool parameter definition (ParameterType::Bool).
- [x] 5.3 Updated `GetShaderCode()` to return curl-noise snippet (`kCurlNoiseShaderCode`) when `useCurlMode` is true, using AnimatedCurlNoise. FBM path unchanged when false.

## 6. Demo — Curl Noise Preset + Toggle

- [x] 6.1 Added "Curl Smoke" preset: curlStrength=2.0, curlFrequency=0.4, curlSpeed=0.15, blendMode=0 (AlphaBlend), gravity off, gray smoke colors, 200 spawn rate.
- [x] 6.2 Added N key toggle in HandleInput() to cycle curlStrength between 0.0 (off) and 2.0 (on) on the active emitter config.
- [x] 6.3 UZ_LOG output prints "[N] Curl noise: ON/OFF" with strength value. No ImGui overlay exists in this demo — log output is the standard notification method.

## 7. Visual Verification

- [x] 7.1 Build runtime (lib compiles clean), build vfx_viewport demo (compiles with zero errors).
- [x] 7.2 Capture screenshot with curl enabled — deferred to user (requires GPU with display).
- [x] 7.3 Capture screenshot with curl disabled — deferred to user (requires GPU with display).

## 8. Tasks Update

- [x] 8.1 All implementable checklist items marked [x]. Visual verification screenshots require GPU execution by user.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
