# Energy and Bias Corrections (Sponza parity, step 1)

<!-- doc-meta
title: Energy and Bias Corrections
category: reference
engine: uzengine
theme: rendering
created: 2026-09-07
updated: 2026-09-07
status: active
author: sponza-parity-01
-->

## Overview

Six correctness fixes landed together as `phase0_sponza-parity-01-core-fixes`, ported from the
`test/windows-sponza-parity` investigation. Each one changes rendered output, so this page records
what the correct formulation is and how to re-verify it. None of them is a tuning knob: every value
here follows a published reference, and every one has a regression harness under `scripts/`.

| Area | File | Harness |
|---|---|---|
| Euler composition order | `runtime/engine/math/quaternion.cpp` | `scripts/test_quaternion.ps1` |
| Environment BRDF (DFG) | `runtime/shaders/runtime/lighting/ibl.hlsli` | `scripts/test_environment_brdf.ps1` |
| SSR resolve filter | `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl` | `scripts/test_ssr_resolve_filter.ps1` |
| Shadow bias and cascade fade | `runtime/shaders/runtime/lighting/shadow.hlsli` | `scripts/test_shadow_bias.ps1` |
| Sky cubemap energy | `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl` | (see "Sky energy" below) |
| DDGI constant packing | `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl` | (offset check, see below) |

## Euler composition order

`Quat::FromEuler(pitch, yaw, roll)` maps pitch to Y, yaw to Z and roll to X, and documents the
application order Roll → Pitch → Yaw. `XMQuaternionMultiply(Q1, Q2)` returns `Q2*Q1`, i.e. it applies
`Q1` first — so the documented order is `Multiply(Multiply(qRoll, qPitch), qYaw)`. The previous
nesting applied yaw before pitch and let roll steer the result about the fixed world X axis.

Consequences of the old form, all now gone:

- A directional light's roll changed its direction. `Sun [-30, 0, 60]` produced a 14.5° elevation
  instead of the authored 30°, because roll was applied after pitch about world X.
- Sponza's `Sun [-56, 79, 0]` resolved to `(0.1067, 0.9816, 0.1582)` — 9.1° of elevation instead of
  56° — so the scene was lit from near the horizon regardless of what the file said.

The correct direction for that authored pair is `(0.106699, 0.548919, 0.829038)`, asserted by
`QuatTest.FromEulerCombinedPitchYawPreservesElevation`.

**Scene files were not re-authored.** The old values were compensations for the defect; after the fix
every scene gets the elevation and bearing its file states. Two eye-tuned entries are worth a visual
check when touched: `material_showcase.uzscene` `MainCamera [68.2, -90, 0]` and the animated
`day_night_cycle.lua`.

## Environment BRDF (DFG)

`EnvironmentBRDFApprox(roughness, NoV)` in `ibl.hlsli` is the published Karis 2014 mobile fit:

```hlsl
float4 r = roughness * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
return float2(-1.04, 1.04) * a004 + r.zw;
```

Two inline copies in `deferred_lighting_ps.hlsl` had drifted from it — they added `r.z` where the fit
adds `r.y` and dropped the `(-1.04, 1.04)` scale, which inflated the A term and therefore all
image-based specular energy. Both call sites now use the shared helper (the diffuse compensation term
evaluates it at `NoV = 1`).

No clamping is applied. A and B leave `[0,1]` only marginally at grazing incidence
(`NoV → 0` gives A ≈ -0.044, B ≈ 1.044) and their sum stays ≈ 1, which is what the multi-scatter
terms downstream rely on. Clamping would break that identity.

## SSR resolve filter

The spatial filter weights samples by the Karis luminance weight `1/(1+L)` and divides by the
accumulated weight. That division already undoes the compression, so the trailing "inverse Karis"
step was a second decompression: a uniform `RGB = 1` reflection resolved to roughly 1000. Alpha (hit
confidence) is normalised by the same weight sum, so it now carries the Karis weight too, and the
mirror pass-through is `roughness <= 0.04` to match the trace pass boundary.

## Shadow bias and cascade fade

Normal offset uses `sin(theta)` and slope-scaled depth bias uses `tan(theta)` capped at 2:

```hlsl
float cosine      = saturate(NdotL);
float normalScale = sqrt(max(0.0, 1.0 - cosine * cosine));
float slopeScale  = min(2.0, normalScale / max(cosine, 1e-6));
float3 biasedPos  = worldPos + N * normalBias * normalScale;
float  depthBias  = max(GetCascadeDepthBias(cascadeIdx), 0.0);
```

The old form used `tan(acos(NdotL))` capped at 10 for *both* offsets, pushing the world-space offset
to ten times the cascade normal bias at grazing angles, and forced a 0.0005 depth-bias floor that
over-biased the near cascades and peeled contact shadows off ornaments.

Cascade blending now uses the producer's `GetCascadeFadeStart(sel)` (from
`ShadowDataParams::cascadeFadeStarts[8]`, the un-extended far split) instead of a hardcoded
`splitDist * 0.6` with a smoothstep. `shadow_pcss.hlsli` already used the producer value, so the two
shadow paths now agree.

## Sky energy

`sky_cubemap_cs.hlsl` stores radiance. It previously wrote `L * (1/PI)`, which double-counted the
Lambertian divide: `PackSHToCompactFormat()` (`sh_projection.cpp`) bakes the UE5 cosine-convolution
constants — `c0 = 1/(2*sqrt(PI)) = (A0/PI) * Y00 = 0.282095`, `c1 = 0.325735`, `c2 = 0.273137` — so
`EvalSH_L2` already returns `E/PI`. The old divide darkened both the SH diffuse and the prefiltered
specular by a factor of PI.

Verify by capturing Sponza with the procedural sky and reading the startup log: `SkyLight SH L0`
should be on the order of 1.5 for the default atmosphere at `intensity = 1.0`, not ~0.2.

## DDGI constant packing

`RadianceCacheTraceConstants` is 176 bytes and stores the four GDF clipmaps as 16 scalar floats.
`ddgi_probe_update_cs.hlsl` declared them as `float _gdf0[4] … _gdf3[4]`; HLSL gives every array
element its own 16-byte register, so those four arrays occupied 256 bytes where C++ had 64. Every
field after them — sky colour, sun direction, sun colour, ground albedo — was read 192 bytes past the
uploaded struct, i.e. garbage, for every probe ray. They are scalars now, mirroring the C++ layout
(`radiance_cache_trace_cs.hlsl` was already correct).

## Re-verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_quaternion.ps1          # 38/38
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_environment_brdf.ps1    # 200/200
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_ssr_resolve_filter.ps1  # 21/21
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_shadow_bias.ps1         # 10/10
```

Shader syntax without an engine run:

```bash
cd runtime/shaders
../thirdparty/dxc/bin/x64/dxc.exe -T ps_6_6 -E main -D UZ_BINDLESS=1 \
    runtime/lighting/deferred_lighting_ps.hlsl -Fo nul
```

Scene capture (the `--screenshot-frame` is required until the readiness fix in
`phase0_sponza-parity-05-loading-exposure-cli` lands, otherwise the capture fires while meshes are
still streaming):

```
runtime/build/bin/uzengine.exe --project sample/projects/sponza.uzproject \
    --screenshot .screenshots/sponza-01-core-fixes.png --screenshot-frame 600 --frames 700
```

Measured on 2026-09-07 at 1920x1080, Camera A, sampling every 4th pixel: mean luminance 61.2,
channel means R/G/B 62/61/59, 0.3 % of pixels below luminance 16, none above 240. The same scene data
on the pre-fix binary gave 68.7 / 71-68-65 / 2.0 % — seven times more crushed shadow.

## Related

- `docs/engine/12-rendering/17-lighting.md`, `18-shadow-system.md`, `23-ibl-lighting.md`,
  `27-sky-atmosphere.md`
- `.rulebook/tasks/phase0_sponza-parity-01-core-fixes/` — task, evidence and the rotation audit
