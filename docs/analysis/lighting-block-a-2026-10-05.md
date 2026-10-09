# Lighting block A — cheap, certain corrections (2026-10-05)

Companion to the lighting assessment of 2026-10-05 (parecer, published as a page; sections 3 "O que
está implementado" and 4 "O que pode melhorar"). Block A is the list of defects that could be fixed
with evidence in days. Branch `feat/sponza-gi-parity`, after the merge of `origin/main` (31b5c14a).
Every item carries what was measured before and after; nothing here changes the GI defaults.

## A.1 Local (spot / point) light shadows were dead

**Defect.** The local shadow atlas pass cleared the atlas to 0.0 ("reversed-Z far") and drew it with
the CSM pipeline (`depthFunc = Less`, standard-Z), so no caster could ever pass the test; the
lighting shader then compared the receiver's NDC depth from a `Camera`-built (reversed-Z) spot
projection against that atlas with the LESS comparison sampler and mapped NDC straight into the
atlas as if it were UV (no `*0.5+0.5`, no y flip). Net effect measured on a reference scene
(`scenes/local-shadow-reference.uzscene`: ground, the 60 cm wall `solid_near_out_east`, a spot above
it, a point light beside it, sun intensity 0): spot lit/shadow ratio **1.07** — the spot lit both
sides of the wall; point light ratio **4.2** = distance attenuation only — `pl.shadowIndex` was
written on the CPU (six cube-face tiles rendered per light) and never read by the shader.

**Fix.** Local tiles store **linear distance / radius** on `SV_Depth`
(`shadow_depth_vs.hlsl` compiled with `UZ_SHADOW_LINEAR=1` + new `shadow_depth_linear_ps.hlsl`,
`PipelineLibrary::GetShadowDepthLinear`), so the convention of the light's projection no longer
matters: the atlas is cleared to 1.0, tested with `Less` and sampled with the existing LESS comparison
sampler, exactly like the CSM. `LocalLightShadowData` carries the light position and radius (80 → 96
bytes; `ShadowDataCB` stays 2304 with `_pad3` removed in both `constant_buffers.h` and
`shadow_data_params.h`); `ShadowCB` of the shadow passes carries them too (80 bytes). The lighting
shader has one `SampleLocalShadow(entry, worldPos, geometricNormal)`: NDC → UV with the CSM's y flip,
reference = (distance − `shadowBias` cm) / radius, receiver pushed 0.5 % of its distance along the
geometric normal. Spots call it; point lights pick the cube face from the major axis of light → pixel
(+X, −X, +Y, −Y, +Z, −Z, the allocation order in `frame_constants.cpp`).

**Verification.** `scripts/test_local_light_shadows.py` (world-space ROIs on the ground from the
frame's region dump, pre-registered): before 4/6 (ratios 1.07 / 4.2), after **6/6** — lit regions
0.358 / 0.385, shadow regions 0.0 (ratio ~1e11), no acne (0 % of lit pixels below a tenth of the
mean). Sponza (no local lights): Camera C ROI means bit-identical (rule 0 and rule 3), loading 3/3.
Rasterizer depth bias does not apply to a depth written by the pixel shader, so the bias lives in
the shader, in centimetres (`shadowBias` of the light JSON, default 0.5).

**Found on the way.** A scene without a directional-light entity was lit by `LightFrameData`'s
defaults (sun direction 0.5/0.3/0.8, colour 3/2.85/2.7); the test scenes carry a sun of intensity 0.
Fixed the same day, see A.10.
A spot's direction is `rotation * Forward` and the cone tests `dot(L, -direction)`, so a spot pointing
down needs pitch **+90**, while the sun entity's rotation points *towards* the sun.

## A.2 `r.SkyLight.MinAmbient` floored every scene at 0.25

The CVar default 0.25 was applied as `max(component, cvar)` to the AO clamp and the no-GI irradiance
floor, so the Sponza authors' `min_occlusion 0.01` became 0.25 and the shader's own recommendation
(0.005–0.02) was unreachable. Default is now **0** (the component value). Sponza Camera C, project
configuration: `lit_wall` −1.6 % (the AO floor no longer masks the gather's AO), `shadow_wall` and
`centre` bit-identical.

## A.3 Two latent energy errors in the ambient term

- Environment MIS (`env_mis_temporal_cs.hlsl`, metallic > 0.3 and roughness < 0.7, only when SDF
  reflections are off) was **added** on top of the full split-sum specular: environment specular
  counted twice on metals. It now **replaces** the split-sum specular on the pixels it covers
  (alpha = 1 there).
- The sky light colour multiplied the diffuse irradiance even when a GI source (radiance cache, DDGI)
  supplied it, although the probe writers already sample the sky with `skyLightColor * intensity`.
  The tint now applies to the sky-SH fallback only.

Neither changes Sponza (white sky, reflections on): ROI means bit-identical.

## A.4 CVars registered twice with different defaults

`r.AerialPerspective` was registered with default `true` in `atmosphere_renderer.cpp` and `false` in
`frame_constants.cpp`; the registry keeps the first registration, so the effective default depended
on static-initialisation order. Single registration now (default false, as known-issues records the
blend as wrong). The unused second registrations of `r.SSR.Intensity` (range 0–2 vs 0–5) and
`r.SSR.CopyPrev` (bool vs float) were removed; `scene_renderer_init.cpp` fallbacks aligned
(`r.SSR` false, `aerial_persp` false, `r.CAS.Sharpness` 0.2).

## A.5 Sky IBL never followed the sun

`m_skyIBLDirty` was set once at construction and only ever cleared: the sky cubemap, its SH and the
prefiltered maps were baked on the first frame and never again. The sky pass now keeps the sun
direction, the sky sun colour and the angular radius the IBL was baked from
(`AtmosphereFrameContext::skyBakeSignature`) and marks the IBL dirty when they change by more than
0.25° / 1 %, at most once per 30 passes. Each bake still stalls on `WaitIdle` plus an SH readback; an
asynchronous bake is the follow-up. Verification: `scripts/test_sky_ibl_rebake.py` (scene
`sky-rebake-reference`: sun intensity 0 so only the IBL lights the ground, `sun_rotate_test.lua`
turns the sun to the horizon at frame 100) — before: ground luminance identical at frames 90 and
200 (0.00 % change, one bake); after: see the results table below.

## A.7 `r.DFS` dispatched empty shaders by default

The three `dfs_*_cs.hlsl` are one-line scaffolds and the lighting shader never reads
`dfsOutputSrvIndex`; `r.DFS` defaulted to `true` and the TechPanel offered the toggle. Default
`false`, help text says so, row removed from the TechPanel.

## A.8 Dead knobs (subset)

- Auto exposure: `r.AutoExposure.MidGrayTarget`, `SpeedUp`, `SpeedDown`, `LogLuminanceMin/Max` were
  registered and never read (the shader hard-coded 0.18, 3, 1, 10 %, 90 %). They are root constants
  now (13 DWORDs of 16). Defaults reproduce the previous behaviour: Sponza auto exposure 55.3754
  before — see the results table.
- `r.ContactShadow.Length` only switched the effect on or off (the ray was fixed at 1 % of the far
  plane). It is now the ray length as a fraction of the far plane, default 0.01 = the previous
  length (A.10 simplified the first version, which scaled the old literal).
- Not done: the remaining ~35 unread CVars (PCSS.*, ESM, Bloom.Knee, tonemap curve, quality-preset
  keys) and the 15 unpolled `r.Show.*` flags — listed in the parecer, each needs a decision (wire or
  delete).

## A.9 The intermittent hang (mode 3 + relocation)

Not reproduced: 28 consecutive runs of the exact S1 configuration (14 from a copied executable, 14
earlier) completed in 33–36 s each. The one occurrence of 2026-09-25 stays unexplained; the suspects
(the G-buffer position read declared on the radiance-cache pass, the diagnostic readback's
`WaitIdle`) are unchanged.

## A.6 Alpha-masked shadow casters — not done

The shadow depth pipelines are vertex-only, so masked materials cast solid shadows. A fix needs a
masked VS/PS variant for both the instanced and the single-draw paths (the per-instance albedo SRV
exists in the instance buffer; the single path can use `PerObjectCB.albedoIndex`,
`opacitySrvIndex`, `alphaClipThreshold`). Sponza has no masked mesh (only `dirt_decal`), so there is
no visible gain on the current scenes; deferred with this plan.

## A.10 Defaults out of the code (follow-up, same day)

**The phantom sun.** `LightFrameData` defaulted to a sun that exists in no scene file (direction
0.5/0.3/0.8, colour 3/2.85/2.7), so a world without a `DirectionalLightComponent` was lit and
CSM-shadowed by it. Now `sunColor` defaults to zero and `hasDirectionalLight` records whether a sun
was found; without one `frame_shadows.cpp` skips the cascade pass and `frame_constants.cpp` publishes
`cascadeCount = 0` (the shader returns full visibility). The sky still needs a direction and owns it:
`SkyAtmosphereComponent::sunDirection` (scene key `sun_direction`, default the former value, Lua
`uz.scene.set_atmosphere_sun_direction`). A linked light overrides it as before.

**Verification.** `scripts/test_no_sun_lighting.py`, scene `local-shadow-nosun` (= the local-shadow
reference without its Sun entity) plus `local-shadow-nosun-sundown` (same, `sun_direction` below the
horizon). Pre-registered: N1 `direct_light` on the ground outside both local lights <= 1e-5; N2 sun
shadow visibility 1 on the whole ground; N3 the sky light still lights that region; N4 the sky
follows `sun_direction` (less than half the indirect light with the sun below the horizon); N5 the
local lights are unchanged (lit > 1e-3, lit / shadow >= 10). Before: **2/6** - the dark region
received 0.252 of direct light, the sun visibility reached 0 (a CSM was rendered for the phantom
sun), and the local ratios were 1.4 (spot) and 2.5 (point) because the sun floored every region.
After: 6/6 (dark region direct light 0.0, sun shadow visibility 1.0 on the whole ground, sky light 0.080 -> 0.004 with the sky sun below the horizon, local lights lit 0.358 / 0.385 with shadow 0.0).

**Literals that became data** (defaults reproduce the previous behaviour; the reference harnesses
and the Sponza ROIs are unchanged, see the table): `r.Shadow.Local.NormalOffset` (0.005, carried in
`ShadowDataCB`), `r.Shadow.Local.NearPlane` (1.0 cm for spot and point tiles; spots used 0.1 cm),
`r.SkyLight.RebakeAngleDeg` / `RebakeIntensityDelta` / `RebakeMinInterval` (0.25 / 0.01 / 30),
`r.AutoExposure.AdaptationLinearRange` (1.5, 14th root constant), `r.ContactShadow.Length` as a plain
fraction of the far plane (0.01). **Registered-but-unread CVars wired:** `r.Shadow.AtlasResolution`
(now default 4096 = the constant it replaces; `ShadowAtlas::GetResolution()` replaces
`kShadowResolution` at every use), `r.Shadow.LocalAtlasSize` / `LocalTileSize` (read by
`LocalLightShadowAtlas::Init`, tiles capped at 16), `r.Material.MinGGXAlpha` (published; default
0.0016 = the 0.04 roughness floor squared) and `r.Material.DielectricF0` (the shader used a literal
0.08 x specular; now 2 x F0 x specular). The dead `r.ContactShadow.Steps` / `Thickness` registrations
were removed (the live knobs are `r.Shadow.ContactShadowSteps` / `Thickness`). Full list with the
before/after of each item: `docs/analysis/hardcoded/01-rendering-defaults.md`, re-audit 2026-10-05.

## Results table

| Check | Before | After |
|---|---|---|
| `test_local_light_shadows.py` | 4/6 (spot ratio 1.07, point 4.2) | 6/6 (ratios ~1e11, no acne) |
| `test_sky_ibl_rebake.py` | 1/3 (0.00 % change, 1 bake) | 3/3 (ground luminance 0.685 → 0.048 after the sun moved, 92.98 %; 1 re-bake queued, 2 bakes) |
| Sponza Camera C ROIs (S2, rule 0 / rule 3) | — | lit_wall −1.6 % (A.2), others bit-identical |
| Sponza auto exposure (project defaults, frame 120) | 56.5726 / 57.0363 (pre-block-A exe, two runs) | 56.7676 — inside the ±0.8 % run-to-run floor; the value adapts with `dt` and is not converged at frame 120 |
| `verify_sponza_loading.ps1` | 3/3 | 3/3 |
| `test_region_capture.py` | 23/23 (merge build) | 23/23 (one 22/23 run while two other captures shared the GPU: contention, not a regression) |
| `test_no_sun_lighting.py` (A.10) | 2/6 (dark region direct 0.252, sun visibility min 0, local ratios 1.4 / 2.5) | 6/6 (dark region direct light 0.0, sun shadow visibility 1.0 on the whole ground, sky light 0.080 -> 0.004 with the sky sun below the horizon, local lights lit 0.358 / 0.385 with shadow 0.0) |
| gtests (`*GI*:*Radiance*:*Gdf*:*MeshSdf*:*DistanceField*:*Ddgi*:*Shadow*:*Exposure*:*Light*:TaaJitter.*`) | pass | 381 passed, 0 failed |

### Re-run after A.10 (same build as the commit)

`test_local_light_shadows.py` 6/6, `test_sky_ibl_rebake.py` 3/3, `verify_sponza_loading.ps1` 3/3,
`test_region_capture.py` 23/23; Sponza Camera C ROI means (S2, rule 0 and rule 3, probe classification
3 with relocation) equal to the block-A values to within run-to-run float noise (4e-9 relative, the
same digits that differ between two block-A runs); gtests 438 passed, 1 skipped, 3 failures that
predate this work (`RenderingParity.HeightFogDefaults_PrimaryLayer` and `_DirectionalInscattering`
expect the pre-merge fog defaults, `SkyAtmosphereComponent.FallbackSunDiskIntensityIsOne` expects 1.0
while the component default is 0.1). One `test_region_capture.py` run gave 22/23 on "a 16x exposure
change does not move the linear records beyond run-to-run noise": that run's repeat pair was
bit-identical (noise 0) while the exposure pair differed by 1e-3; re-run on both the pre-change and
the new executable the check reports exposure delta 0 against noise 0.0038 (identical statistics),
so the failure is the noise-relative threshold of the check, not the records.
