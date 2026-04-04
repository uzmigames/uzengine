## Phase 1: ScalabilityManager Core

- [ ] 1.1 Create `ScalabilityManager` class (`engine/config/scalability_manager.h/.cpp`): `SetQualityLevel(level)`, `SetGroupQuality(group, level)`, `GetOverallQuality()`, `GetGroupQuality(group)`.
- [ ] 1.2 Define `QualityLevel` enum: Low=0, Medium=1, High=2, Ultra=3, Cinematic=4.
- [ ] 1.3 Define `ScalabilityGroup` struct: name, array of CVar overrides per level.
- [ ] 1.4 Wire `ScalabilityManager` into `SceneRenderer` — call `ApplyQualityLevel()` at init and on change.
- [ ] 1.5 Add CVar `r.QualityLevel` (int, 0-4, default 3=Ultra) — global quality preset.
- [ ] 1.6 Add per-group CVars: `sg.ShadowQuality`, `sg.TextureQuality`, `sg.PostProcessQuality`, `sg.EffectsQuality`, `sg.ViewDistanceQuality`, `sg.GIQuality`, `sg.ScreenQuality`.

## Phase 2: Shadow Quality Group

- [ ] 2.1 Add `r.Shadow.MaxResolution` CVar (default 4096, range 256-8192). Wire to shadow atlas creation.
- [ ] 2.2 Add `r.Shadow.CSM.MaxCascades` CVar (default 4, range 1-4). Wire to cascade setup.
- [ ] 2.3 Add `r.Shadow.DistanceScale` CVar (default 1.0, range 0.1-2.0). Scale shadow distance.
- [ ] 2.4 Add `r.Shadow.PCF.Taps` CVar (default 16, options 4/8/16/32). Wire to shadow.hlsli.
- [ ] 2.5 Add `r.Shadow.ContactShadows` CVar (default 1). Enable/disable contact shadows.
- [ ] 2.6 Add `r.Shadow.CSM.TransitionScale` CVar (default 1.0). Cascade blend zone size.
- [ ] 2.7 Define ShadowQuality levels 0-4 with the CVars above.
- [ ] 2.8 Build + verify: switching shadow quality changes resolution, cascade count, PCF taps visually.

## Phase 3: Texture Quality Group

- [ ] 3.1 Add `r.Texture.MaxMipBias` CVar (default 0). Wire to PerFrameCB mipBias.
- [ ] 3.2 Add `r.Texture.MaxAniso` CVar (default 16, options 1/2/4/8/16). Wire to sampler creation.
- [ ] 3.3 Define TextureQuality levels 0-4.
- [ ] 3.4 Build + verify: low texture quality shows blurrier textures.

## Phase 4: Post-Process Quality Group

- [ ] 4.1 Wire existing CVars (`r.TAA`, `r.Bloom`, `r.GTAO`, `r.SSR`, `r.MotionBlur`, `r.DOF`, `r.LensFlare`) into PostProcessQuality levels.
- [ ] 4.2 Add `r.Bloom.Quality` CVar (default 3, range 0-4). Affects downsample chain length.
- [ ] 4.3 Add `r.GTAO.Quality` CVar (default 2, range 0-3). Affects sample count + radius.
- [ ] 4.4 Define PostProcessQuality levels 0-4.
- [ ] 4.5 Build + verify: Low disables TAA/bloom/GTAO, Ultra enables everything.

## Phase 5: Effects Quality Group

- [ ] 5.1 Add `r.VFX.MaxParticles` CVar (default 65536). Wire to VFX system budget.
- [ ] 5.2 Wire `r.VolumetricClouds` CVar and `r.Cloud.PrimarySteps` into effects levels.
- [ ] 5.3 Define EffectsQuality levels 0-4.

## Phase 6: View Distance + GI + Screen Quality Groups

- [ ] 6.1 Add `r.ViewDistanceScale` CVar (default 1.0). Wire to draw distance culling.
- [ ] 6.2 Add `r.LOD.DistanceScale` CVar (default 1.0). Wire to LOD transition distances.
- [ ] 6.3 Wire GI CVars (`r.SSGI`, `r.RadianceCache`) into GI quality levels.
- [ ] 6.4 Wire screen percentage + upscaler selection into screen quality levels.
- [ ] 6.5 Define ViewDistance, GI, and Screen quality levels 0-4.

## Phase 7: Config File + Lua API

- [ ] 7.1 Load quality presets from `.uzquality` INI file (optional override from default C++ values).
- [ ] 7.2 Save current quality settings to `.uzproject` or user config.
- [ ] 7.3 Add Lua API: `uz.settings.set_quality(level)`, `uz.settings.get_quality()`, `uz.settings.set_shadow_quality(level)`, etc.
- [ ] 7.4 Add `--quality` CLI flag: `uzengine.exe --quality low/medium/high/ultra/cinematic`.

## Phase 8: Auto-Detection

- [ ] 8.1 GPU benchmark pass at first launch: render a test scene for 60 frames, measure avg frame time.
- [ ] 8.2 Select quality level based on measured performance: target 60 FPS = pick highest level that achieves it.
- [ ] 8.3 Store detected quality in user config for subsequent launches.

## Testing

- [ ] T.1 Visual test: screenshot at Low vs Ultra — visible quality difference.
- [ ] T.2 Performance test: measure frame time at each quality level.
- [ ] T.3 Verify all CVars apply correctly (no stale values after quality change).
- [ ] T.4 Verify Lua API works for runtime quality switching.
