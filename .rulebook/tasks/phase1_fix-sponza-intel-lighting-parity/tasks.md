## 1. Implementation

### Phase 1 — Scene parity config (sample/, no engine code)
- [x] 1.1 Sun direction: set Sun transform rotation to match Intel reference (elevation ~56°, azimuth ~100°; initial guess pitch -56, yaw +100 — validate sign against reference shadows at capture)
- [x] 1.2 Unify sun/sky scale: set the SkyAtmosphere sun illuminance to the same scale as the directional light intensity so direct:ambient ratio is physical
- [x] 1.3 SkyLight min_occlusion: 0.1 → 0.01 (shader-recommended range 0.005–0.02)
- [x] 1.4 Remove dead `r.SSGI.Intensity` cvar; add `r.GTAO: 1` in scene cvars
- [x] 1.5 Reset all 28 `materials/sponza/*.uzmat` overrides to authored values (roughnessMultiplier 1.0, metallicMultiplier 1.0, baseColorTint [1,1,1])
- [x] 1.6 Add PhysCamera A/C/E camera entities (Intel glTF transforms converted to engine Z-up/cm + vertical FOV) and remove the Controller fly_camera entity

### Phase 2 — Scene loader directional-light keys (TDD)
- [x] 2.1 Write failing unit tests: directional light JSON parses castShadows, atmosphereSunLight, lightSourceAngle, shadowCascades, dynamicShadowDistance; defaults preserved when absent; unknown light keys produce a warning
- [x] 2.2 Implement `ApplyDirectionalLightFields` extension + unknown-key warning in scene_loader.cpp
- [x] 2.3 Set `light_source_angle: 0.53` in sponza.uzscene (now reachable)

### Phase 3 — GI resource validity (TDD)
- [x] 3.1 Write failing test: SSGI disabled → result SRV is sentinel / not bound; then align `SSGISettings.enabled` default with the `r.SSGI` cvar (false) and bind sentinel until first produced output
- [x] 3.2 Write failing test: DDGI atlases are zero after CreateResources and SRVs stay sentinel until first update; then implement clear-at-creation + bind-after-update gate (implemented as bind-after-first-update gate — no CPU-side texture clear exists without a cmdlist; DX12 zero-inits committed resources)
- [x] 3.3 Write failing test: Radiance Cache SH buffer is zeroed at creation; then implement clear + first-update hysteresis bypass (implemented as `firstWrite` full-replace on the first probe dispatch — blend is correct once history is defined)

### Phase 4 — Shadow honesty
- [x] 4.1 Fix sun disk angular radius half-angle in atmosphere_renderer.cpp (full angle currently used as radius)
- [x] 4.2 Expose CSM shadow max distance as a CVar and set a courtyard-appropriate value for Sponza (implemented as component-driven `dynamic_shadow_distance` plumbed scene JSON → loader → LightFrameData → SetupShadowFrame)

### Phase 5 — Build, run, visual comparison
- [x] 5.1 Install toolchain (zig 0.15.2 + cmake/ninja) and build the runtime for macOS/Metal (CMake path works; zig 0.15.2 can't link on macOS 27, zig 0.16 needed build.zig patches)
- [ ] 5.2 Run the Sponza project, settle temporal systems, capture screenshots at PhysCamera A/C/E — BLOCKED: uzengine tool + scene renderer need the Metal shader path (runtime HLSL compilation is Windows-only); triangle_demo runs on Metal with screenshots as proof-of-life
- [ ] 5.3 Compare captures against Intel Render_Main_A/C/E (framing, shadow direction, shade level/hue, contrast); calibrate exposure iteratively — blocked with 5.2 (run on Windows)
- [ ] 5.4 Pass-dump RT1 (direct) / RT2 (indirect) / RT3 (shadow+AO) and sanity-check the separation — blocked with 5.2

## 2. Tail (docs + tests — check or waive with tailWaiver)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
