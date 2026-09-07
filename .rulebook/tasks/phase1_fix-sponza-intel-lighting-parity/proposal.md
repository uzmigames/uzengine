# Proposal: Sponza × Intel Reference Lighting Parity (phase1_fix-sponza-intel-lighting-parity)

## Why

The default Sponza scene does not match Intel's New Sponza reference renders
(`Render_Main_A–F`, package `main1_sponza`, cdrdv2.intel.com 830833). A full analysis
plus a three-model adversarial debate (GPT / GLM / Kimi, two rounds, adjudicated against
source) confirmed the root causes:

1. **Sun direction is wrong**: ours is elevation 75° due +X; Intel's sun is elevation ~56°,
   azimuth ~100° (decoded from the glTF SUN node quat, Y-up frame, -Z forward).
2. **Sun/sky scale split**: direct sun uses `light.intensity = 10`, but the Bruneton sky
   (and therefore the captured SH ambient) is driven by `SkyAtmosphereComponent.sunIlluminance`
   (~20) — independent scales (`atmosphere_renderer.cpp:148-167`), making shade ~2× too
   bright relative to sun and destroying the reference's high-contrast daylight.
3. **Shade floors**: `min_occlusion: 0.1` becomes a hard irradiance floor
   (`deferred_lighting_ps.hlsl:758`), 5–20× above the shader's own recommended 0.005–0.02,
   plus `kMinShadowedSkyFraction = 0.10`.
4. **Materials deviate from the authored asset**: all 28 sponza `.uzmat` files force
   `roughnessMultiplier 0.55`, zero metallic, and a warm `baseColorTint` — the asset IS
   Intel New Sponza (28/28 material-name match) but its PBR response is overridden.
5. **No GI is actually active**: SSGI is off (`r.SSGI` default false; the scene only sets
   `r.SSGI.Intensity` — dead knob), GTAO/SSR/TAA default off, Brixelizer disabled
   (commented out, crashes on non-AMD), DDGI requires DXR.
6. **Camera is non-deterministic**: `fly_camera.lua` overwrites the camera every frame
   from script-local state; the scene camera transform is dead config; FOV 75° vertical
   vs Intel's 36–58° PhysCameras.
7. **Latent renderer defects**: scene loader silently drops directional-light keys
   (`cast_shadows`, `atmosphere_sun_light`, `light_source_angle`); PCSS unreachable
   (hardcoded `filterMethod=1` Poisson + `shadow_pcss.hlsli` never included); DDGI/SSGI
   bind never-written textures; Radiance Cache SH buffer never cleared; sun disk uses
   full angle as radius; sRGB mipmaps filtered in gamma space; sky IBL never re-dirtied
   after runtime sun changes.

This supersedes the stale task `phase1_fix-sponza-lighting-calibration` (its premises —
sun intensity 80, warm sky color, dead tonemap CVars — no longer exist in the scene).

## What Changes

### Scene/config (sample/)
- `sponza.uzscene`: Intel-matched sun rotation, unified sun/sky scale, `min_occlusion`
  0.01, dead SSGI cvar removed, GTAO enabled, `light_source_angle` 0.53, PhysCamera
  A/C/E rig, fly-camera controller removed.
- 28 `materials/sponza/*.uzmat`: multipliers/tints reset to authored values.

### Runtime code (runtime/)
- `scene_loader.cpp`: parse all directional-light fields + warn on unknown light keys.
- `ssgi_renderer.*`: enabled default aligned with cvar; sentinel SRV until produced.
- `ddgi_system.cpp` / `frame_constants.cpp`: atlases cleared at creation; DDGI SRVs
  bound only after first successful update.
- `radiance_cache.*`: SH buffer cleared at creation; hysteresis bypass on first update.
- `atmosphere_renderer.cpp`: sun disk angular radius half-angle fix.
- Shadow distance CVar exposed and clamped for courtyard-scale scenes.

## Impact
- Affected specs: `/.rulebook/specs/` (new deltas: scene-loader, lighting-parity)
- Affected code: `sample/content/scenes/sponza.uzscene`, `sample/content/materials/sponza/*.uzmat`,
  `runtime/engine/ecs/loaders/scene_loader.cpp`, `runtime/engine/renderer/gi/ssgi_renderer.*`,
  `runtime/engine/renderer/gi/radiance_cache.*`, `runtime/engine/renderer/gi/ddgi_system.cpp`,
  `runtime/engine/renderer/frame/frame_constants.cpp`,
  `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp`
- Breaking change: NO — bug fixes + scene values; loader additions are backward-compatible
- User benefit: Sponza renders with the Intel reference's contrast, shadow placement, sky
  hue and GI bounce; deterministic reference cameras enable FLIP/pixel comparison
- Enables: GI calibration milestones (RC/DDGI), night/interior lamp milestone, PCSS task
