# Proposal: phase0_sponza-parity-01-core-fixes

## Why

`E:\SponzaTest` (branch `test/windows-sponza-parity`, HEAD `25cd0c5f`, merge base with us `5f6b8ce7`)
diagnosed a set of **pure correctness defects** that are still present in our tree and are independent
of any GI feature work. Each one is small, has a published reference, and ships with a regression
harness on their side. They are the cheapest wins of the port and unblock everything after them:

1. `Quat::FromEuler` composes yaw before pitch (DirectXMath returns Q2*Q1). The authored Sun
   `[-56,79,0]` yields 9.1 deg solar elevation instead of 56 deg. Affects every scene, not only Sponza.
   Our scene now authors the post-fix value `[-56,79,0]` (manual calibration removed 2026-09-07),
   so the sun is wrong until this lands.
2. Environment BRDF approximation in `deferred_lighting_ps.hlsl` is inlined twice and deviates from
   the Karis 2014 fit (uses `r.z` instead of `r.y`, misses the `(-1.04, 1.04)` scale).
3. `ssr_resolve_cs.hlsl` applies inverse Karis after an already-normalised weighted average
   (constant RGB=1 input can reach ~1000) and accumulates alpha without the Karis weight.
4. CSM bias uses `tan(acos(NdotL))` capped at 10 plus a hard 0.0005 depth-bias floor; the cascade
   blend uses a fixed 60% smoothstep fade. Produced ~1616 near-white ornament pixels.
5. `sky_cubemap_cs.hlsl` divides radiance by pi at generation while diffuse consumers divide again.
6. `ddgi_probe_update_cs.hlsl` declares `float _gdfN[4]` inside `TraceConstants`; HLSL packs each
   array element in its own 16-byte register, shifting every field after them by 180 bytes past the
   uploaded 176-byte struct (garbage sky/sun colours for every probe ray). SponzaTest has this as an
   uncommitted working-tree fix; port it from their diff, not from a commit.
7. `decals_1st_floor1_dirt_decal_0.uzmesh` has normals pointing down on a vertical surface (the
   large white patch visible with indirect lighting active).

## What Changes

- `runtime/engine/math/quaternion.cpp`: `XMQuaternionMultiply(XMQuaternionMultiply(qRoll, qPitch), qYaw)`.
- `runtime/shaders/runtime/lighting/ibl.hlsli`: add `EnvironmentBRDFApprox(roughness, NoV)`; use it
  at both call sites in `deferred_lighting_ps.hlsl`.
- `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl`: drop inverse Karis, Karis-weight alpha,
  mirror pass-through at `roughness <= 0.04`.
- `runtime/shaders/runtime/lighting/shadow.hlsli`: sin/tan bias capped at 2, no depth-bias floor, and
  `GetCascadeFadeStart(sel)` instead of `splitDist * 0.6` + smoothstep. Shader-only — verified
  2026-09-07 that `ShadowDataParams::cascadeFadeStarts[8]` is already produced on our side and that
  `shadow_pcss.hlsli` already consumes it. (The `shadow_renderer.cpp` / `frame_shadows.cpp` /
  `shadow_pass.cpp` hunks in `97acd374` are translucent shadow-caster exclusion, which belongs to
  task 02, not here.)
- `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl`: write `L` (radiance), not `L * M_1_PI`.
- `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`: scalarise the GDF clipmap fields.
- `sample/content/meshes/sponza/decals_1st_floor1_dirt_decal_0.uzmesh`: replace with the repaired
  mesh (positions/UVs/indices/LODs/bounds byte-identical; normals and tangents recomputed).
- Port harnesses: `scripts/test_quaternion.ps1`, `test_environment_brdf.{cpp,ps1}`,
  `test_ssr_resolve_filter.{cpp,ps1}`, `test_shadow_bias.ps1`, `repair_sponza_decal_normals.ps1`.

Source of truth for every hunk: `E:\SponzaTest` commits `33b0f934`, `97acd374`, `25cd0c5f` and the
uncommitted `ddgi_probe_update_cs.hlsl` diff. Do NOT copy files wholesale: our
`deferred_lighting_ps.hlsl` carries local-light shadow-atlas sampling (`a29168da`) that SponzaTest
never had and that must survive.

## Impact
- Affected specs: none (bug fixes against published references)
- Affected code: math, lighting/shadow/SSR/sky/DDGI shaders, one sample mesh, test scripts
- Breaking change: YES for rotations. Every scene/Lua caller that compensated the old Euler
  order will rotate. `sample/content/scenes/*.uzscene` (34 files remapped in `593c6304`) and
  `uz.scene.set_rotation` callers must be audited (item 2.3).
- User benefit: sun where the scene says it is; physically consistent specular/SSR/sky energy;
  shadow acne and cascade seams fixed; DDGI probes receive real sun/sky colours.
