## 1. Implementation

Assessment (2026-11-17, `E:/UzmiEngine/_evidence/lighting/hdri-skybox/report.md`): the proposal's premise
("HDRISkyboxPass exists as a stub") was wrong at f03ec96b. The pass, its pipeline, the `cubemap_path`
auto-load, the equirect-to-cubemap conversion, the IBL bake and the procedural-sky override were all
implemented and render correctly on the GPU. The real gaps were the CVar, the visible sky being the
128-texel IBL capture cube, the diagnostics target, docs and tests.

- [x] 1.1 Complete `HDRISkyboxPass::Execute()` — bind equirectangular-to-cubemap converted texture (or raw cubemap SRV), draw fullscreen quad at depth=1.0 (behind all geometry). — Already implemented at f03ec96b (`hdri_skybox_pass.cpp`: depth SRV + cubemap SRV root constants, fullscreen triangle, writes only reversed-Z far-plane pixels); verified on the RTX 5080 (37 fixtures, spheres occlude the sky cleanly, 0 NaN/Inf). No change.
- [x] 1.2 Wire `SkyLight::cubemap_path` to trigger HDRI skybox pass instead of procedural sky pass in `SceneRendererInit`. — Already wired (`frame_constants.cpp` auto-load -> `SetHDRIEnvironment` -> `m_hdriActive` -> `AddAtmospherePasses` HDRI branch); verified with a `sky_atmosphere` component present in every fixture: the HDRI replaces it. No change.
- [x] 1.3 Verify `skybox_cubemap_ps.hlsl` correctly samples the cubemap with proper view direction reconstruction from screen UV + inverse view-projection. — Verified: same two-point `invViewProj` unproject as `sky_atmosphere_ps.hlsl`, PerFrameCB prefix at the right offsets (pinned by `test_hdri_skybox.cpp`); labeled panorama: 8 yaws land within 0.026 hue of the expected azimuth, u increases to the camera's right (unmirrored F glyph at world +X), up is up, chrome ball = sky behind the camera to 1-3 %.
- [x] 1.4 Apply exposure to HDRI skybox output (multiply by scene exposure so HDRI matches lit scene brightness). — Verified, no change: `tonemap_ps.hlsl` multiplies the whole HDR buffer by `exposure` after the sky is in it (the procedural sky works the same way); a second multiply in the pass would double-expose. Pinned by test. A white diffuse sphere lit only by the HDRI reads 0.51 (synthetic) / 0.63 (sunset) of the mean sky radiance at exposure 1.
- [x] 1.5 Add CVar `r.HDRISkybox` (0=off, 1=on, default=1) for toggling. — Added (`frame_constants.cpp`, `HdriSkyboxMode` in `hdri_skybox_pass.h`): 0 = skip the pass (procedural sky, HDRI IBL kept), 1 = the shipped behaviour, 2 = convert the panorama at its source resolution (new, opt-in). **Default 1**: the only value that keeps every `cubemap_path` scene byte-identical to f03ec96b (37/37 fixtures, 0 differing pixels); the lane brief asked for a default of 0 — see the report's decision section. USER DECISION: keep 1 (= spec), set 0 (hides the HDRI sky of user-authored scenes; no shipped scene uses `cubemap_path`), or 2 (sharper sky + reflections, 16x cube memory at load).

## 2. Testing

### 2.1 Visual Regression (Tier 2)
- [x] 2.1.1 Load a scene with `cubemap_path` pointing to an HDR environment. Verify skybox renders correctly behind geometry. — `grasslands_sunset_4k.hdr` + a synthetic labeled panorama, 37 fixtures (8 yaws, 4 pitch-60 views, zenith, nadir, chrome / mirror / diffuse views, sun + ground, 512 cube). Correct behind the spheres and the ground plane. Finding: at the default `cubemap_resolution` 128 the 4K sun is a 4-texel block and the horizon smears (montage_baseline_res128_vs_512.png); `r.HDRISkybox 2` or `cubemap_resolution: 512` fixes it.
- [x] 2.1.2 Verify IBL reflections match the HDRI skybox (same cubemap used for both). — One cubemap feeds the pass, the bake, the SH and env-MIS; chrome ball centre vs the sky behind the camera: ratio 0.98-1.00 (synthetic), 1.03-1.06 at 18 deg elevation (sunset); the band ring on the mirror ball is the panorama's azimuth order.
- [x] 2.1.3 Rotate camera 360 degrees — verify no seams, no flipping, correct orientation. — 8 yaws at 45 deg (cube vertical edges and the u=0/1 seam at the frame centre): max adjacent-pixel hue jump 0.003, value jump 1.1 %; pitch 60 at 4 yaws + zenith (top-face edges and pole): no seam; orientation per 1.3.
- [x] 2.1.4 Verify HDRI exposure matches scene lighting (no blown-out or too-dark sky). — Per 1.4; `grass-sun-ground` (sun 10 + checker ground) reads naturally at exposure 1.0 with auto exposure off; no clipping in the linear dumps except the HDR's own sun.

### 2.2 Diagnostics
- [x] 2.2.1 Run `--pass-dump` and verify HDRI skybox appears correctly in the pass output. — `hdr` shows the HDRI sky. `atmo.sky_cubemap` was the never-filled procedural capture (all zero); the cube the pass draws is now registered as `atmo.hdri_cubemap` (`frame_lighting.cpp`) and dumps at its face size (128 / 512).
- [x] 2.2.2 Run `--diag` and verify no NaN/Inf in the sky pixels. — `hdr` and `atmo.hdri_cubemap` OK, no NaN/Inf in any of the 27 targets (modes 1 and 2); 0 NaN/Inf in every linear region dump of the 37 fixtures x 3 modes.

### 2.3 Coverage
- [ ] 2.3.1 Verify 95%+ coverage on new C++ code. — Not measured: the zig build has no coverage instrumentation and the repo has no coverage gate to run. The new C++ is 1 constexpr helper (7 cases pinned), 1 CVar read in `AddAtmospherePasses`, 1 branch in the auto-load, 1 inspector registration; the GPU runs exercise all three CVar values.

## 3. Documentation
- [x] 3.1 Update `docs/specs/rendering/renderer.md` with HDRI skybox pass description. — That file does not exist; the pass is described in `docs/specs/rendering/atmosphere.md` ("HDRI Skybox (SkyLight `cubemap_path`)", key files, status row).
- [x] 3.2 Update `docs/guides/scene-authoring.md` with how to set up HDRI skybox via SkyLight cubemap_path. — That file does not exist; the how-to is in `docs/guides/cvar-reference.md` ("HDRI skybox (SkyLight `cubemap_path`)") next to the existing `cubemap_path` formats table, plus the `r.HDRISkybox` row.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation — 3.1 / 3.2.
- [x] 4.2 Write tests covering the new behavior — `runtime/tests/engine/renderer/test_hdri_skybox.cpp` (7): CVar default / range, `HdriSkyboxCubeResolution`, PerFrameCB prefix offsets, depth discard + two-point unproject + no second exposure, the converter's mapping and D3D face table, world axes -> panorama columns.
- [x] 4.3 Run tests and confirm they pass — 7/7; full suite 8261 run, 8248 passed, 13 skipped (Metal / DXBC / H264 platform skips), 0 failed; `npm run test:layouts` Tier A 105/105.
