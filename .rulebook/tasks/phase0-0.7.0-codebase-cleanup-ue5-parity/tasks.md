## 1. P0 — UE5 Parity Violations (CLAUDE.md forbids these explicitly)

- [ ] 1.1 **Remove analytical SH fallback** in `atmosphere_renderer.cpp:234-254` — invented values `0.90f, 0.92f, 0.05f, 0.3f, 0.7f` have no UE5 source. Fix GPU readback instead of falling back to invented approximation. UE5 ref: cubemap SH projection, never analytical fallback
- [ ] 1.2 **Remove forbidden minimum clamp** in `deferred_lighting_ps.hlsl:398` — `max(skyInscatter, float3(0.15f, 0.2f, 0.35f))`. CLAUDE.md: "NEVER add minimum clamps unless UE5 does the same"
- [ ] 1.3 **Remove forbidden compensation factor** in `deferred_lighting_ps.hlsl:386` — `* 2.0f` to "match UE5 intensity". CLAUDE.md: "NEVER add compensation factors"
- [ ] 1.4 **Replace aerial perspective approximation** in `deferred_lighting_ps.hlsl:371-403` — entire block is simplified per-pixel Rayleigh. UE5 uses `AerialPerspectiveLUT` (3D volume texture in `SkyAtmosphere.usf`). Either implement proper LUT or disable the feature behind a cvar until LUT is ready
- [ ] 1.5 **Replace empirical multi-scatter `*0.5`** in `sky_atmosphere_ps.hlsl:354` — UE5 uses precomputed `MultipleScatteringTexture` LUT (`SkyAtmosphere.usf`). Either implement multi-scatter LUT or remove the approximation

## 2. P1 — Shader UE5 Inconsistencies

- [ ] 2.1 **Fix forward light attenuation** in `lighting.hlsli:44-52` — uses `(1-(d/r)^2)^2`, should be `(1-(d/r)^4)^2` to match UE5 `DeferredLightingCommon.ush` and deferred path consistency
- [ ] 2.2 **Fix k-remapping for direct lights** in `pbr_common.hlsli:84` — uses `alpha/2` (IBL formula). Should be `(roughness+1)^2/8` per UE5 `ShadingModels.ush` (Karis 2013 direct light variant)
- [ ] 2.3 **Replace ACES tonemap** in `pbr_common.hlsli:36-43` — Narkowicz 2015 polynomial is not UE5's ACES. Replace with UE5's `FilmToneMap` from `ACES.ush`/`PostProcessCombineLUTs.usf` with file/line citation
- [ ] 2.4 **Fix kShadowTransitionScale** in `shadow_common.hlsli:27` — value is 512.0, UE5 uses 60.0. Implement UE5's shadow VS depth bias first, then set scale to 60.0. Cite `ShadowRendering.cpp` line
- [ ] 2.5 **Replace FXAA implementation** in `fxaa_ps.hlsl` — current is a 3-sample box blur. Replace with FXAA 3.11 (Nvidia/Lottes) or UE5's implementation from `FXAAShader.usf`
- [ ] 2.6 **Fix GROUND_ALBEDO** in `sky_cubemap_cs.hlsl:85` — value is `(0.3, 0.3, 0.3)`, UE5 default is `(0, 0, 0)`. Make configurable via CB, default to UE5 value
- [ ] 2.7 **Fix NUM_STEPS_VIEW** in `sky_atmosphere_ps.hlsl:115` — value is 16, UE5 uses 20 (`SampleCountMax` in `SkyAtmosphere.usf`). Update with UE5 citation
- [ ] 2.8 **Fix FresnelSchlickRoughness** in `deferred_lighting_ps.hlsl:209` — uses raw `pow(1-cosTheta, 5.0f)` instead of `Pow5()` helper from `math_util.hlsli`. Risk of FXC/DXC divergence
- [ ] 2.9 **Fix PI literals** in `pcss.hlsli:68-71` — uses raw `3.14159265f` instead of including `pbr_common.hlsli` and using `PI` constant
- [ ] 2.10 **Fix PI literal** in `deferred_lighting_ps.hlsl:394-396` — uses truncated `3.14159f` instead of `PI` (already included via `pbr_common.hlsli`)
- [ ] 2.11 **Fix skyDist** in `height_fog_ps.hlsl:136` — invented `50000.0f` constant. UE5 uses camera far-plane distance in `HeightFogPixelShader.usf`. Pass via CB
- [ ] 2.12 **Add UE5 citations** to `exposure_average_cs.hlsl:33-36` — adaptation speeds `3.0`/`1.0` and percentile values `10`/`90` need UE5 file/line comments
- [ ] 2.13 **Fix Poisson hash constants** in `shadow_common.hlsli:99` — add UE5 source citation for `52.9829189f`, `0.06711056f`, `0.00583715f`
- [ ] 2.14 **Fix LinearToSrgb coefficient** in `math_util.hlsli:80` — `-0.368799f` should be `-0.368800f` per UE5 `Common.ush`

## 3. P2 — Dead Code & Orphaned Systems (remove or wire up)

- [ ] 3.1 **Remove orphaned `gpu_defrag_pass.cpp`** — "monitoring probe only", no callers, never defragments. Delete `.h` and `.cpp`
- [ ] 3.2 **Remove orphaned `gpu_residency_control.cpp`** — MakeResident/Evict are stubs, never instantiated. Delete `.h` and `.cpp`
- [ ] 3.3 **Remove orphaned `gpu_streaming_pool.cpp`** — no callers outside own `.cpp`. Delete `.h` and `.cpp`
- [ ] 3.4 **Remove orphaned `render_graph_parallel.cpp`** — `RgParallelExecutor` falls back to sequential, never wired to scene renderer. Delete `.h` and `.cpp`
- [ ] 3.5 **Remove orphaned `equirect_to_cubemap_pass.cpp`** — `AddEquirectToCubemapPass` never called. Delete `.h` and `.cpp`
- [ ] 3.6 **Remove stub `rhi_async_mipgen.cpp`** — `GenerateMips()` never dispatches anything (PSO and compute both TODO). Delete `.h` and `.cpp`
- [ ] 3.7 **Remove commented-out TEMP code** in `scene_renderer.cpp:656-663` — 7 lines of instancing for shadows disabled for debug. Git has history
- [ ] 3.8 **Remove stale comment** `// (debug removed)` in `deferred_lighting_ps.hlsl:282`
- [ ] 3.9 **Remove dead assignment** in `height_fog_ps.hlsl:161-170` — `rayOriginTerms` calculated then immediately overwritten. Remove first assignment and inline derivation comments
- [ ] 3.10 **Remove orphaned doc comment** in `shadow_common.hlsli:33-35` — describes `CalculateOcclusion` function that no longer exists

## 4. P3 — Architecture Violations (raw assert, raw new, engine API bypass)

- [ ] 4.1 **Convert 5x `assert()` to `UZ_CHECK`** in `frame_array.h:74,91,108,127,135`
- [ ] 4.2 **Convert 4x `assert()` to `UZ_CHECK`** in `free_list.h:145,147,193,199`
- [ ] 4.3 **Convert 4x `assert()` to `UZ_CHECK`** in `allocators.h:44,135,171,172`
- [ ] 4.4 **Convert 5x `assert()` to `UZ_CHECK`** in `ownership.h:117,128,141,145,156`
- [ ] 4.5 **Replace raw `new`/`delete` PlatformWindow** in `application.cpp:370,532` — use `Own<PlatformWindow>` or `MakeUnique`
- [ ] 4.6 **Replace raw `new`/`delete` in RHI factory** in `rhi_factory.cpp:39-80` — use `MakeUnique` for device creation
- [ ] 4.7 **Replace raw `new[]` Slot array** in `rhi_pso_cache.cpp:130,228` — use `std::vector` or engine allocator
- [ ] 4.8 **Split `vulkan_device.cpp`** (1735 lines) — extract multi-queue stubs, swapchain, and resource creation into separate files to get under 1500 lines

## 5. P4 — Stubs & Incomplete Features (implement or mark as explicit error)

- [ ] 5.1 **Fix `uz_runtime_version()`** in `runtime.cpp:17` — returns hardcoded `"0.2.0"`, should read from build-generated version or `CHANGELOG.md` latest
- [ ] 5.2 **Fix `uz_runtime_backend()`** in `runtime.cpp:24` — returns hardcoded `"uzengine"`, should query actual RHI backend name
- [ ] 5.3 **Implement or remove `uz_runtime_init()`** in `runtime.cpp:41` — stub returns 1 without doing anything
- [ ] 5.4 **Implement or remove `uz_runtime_shutdown()`** in `runtime.cpp:50` — empty function body
- [ ] 5.5 **Fix `scene_renderer.cpp:600` WaitIdle per frame** — replace with per-frame ring offset or triple-buffered instance buffers as the TODO(perf) says
- [ ] 5.6 **Wire Lua texture slots** in `lua_scene_api.cpp:659-662` — normal, roughness, metallic, AO IDs are silently discarded with `(void)`. Either implement or return Lua error
- [ ] 5.7 **Implement `save_scene`** in `lua_scene_api.cpp:862-875` — currently returns `false` with "not yet implemented". Either implement scene serialization or remove the API and log error
- [ ] 5.8 **Fix VRR detection** in `frame_pacer.cpp:38-40` — hardcoded `false`. Implement `IDXGIFactory5::CheckFeatureSupport` query via RHI
- [ ] 5.9 **Fix `gpu_residency_control.cpp:42`** — `m_supported = true` hardcoded without backend query (if system is kept in 3.2; skip if removed)
- [ ] 5.10 **Fix `ibl_baker.cpp:515-519`** — prefiltered cubemap uses only mip 0 with no LOD selection. Consolidate per-subresource copy into single mipmapped cubemap
- [ ] 5.11 **Implement light world-space direction** in `light_component.cpp:42` — currently deferred to Phase 2, extract rotation from `GetWorldMatrix()`
- [ ] 5.12 **Implement debug view modes** in `debug_view_ps.hlsl:233-238` — Lighting, Shadows, Wireframe modes output placeholder mid-grey

## 6. P4 — Y-up Documentation Fixes

- [ ] 6.1 **Fix quaternion.h:31 comment** — says "90 deg around Y" but `Vec3::Up()` is Z-axis. Change to "90 deg around Z (world up)"
- [ ] 6.2 **Fix transform.h:30 comment** — same Y-up error as quaternion.h
- [ ] 6.3 **Fix `FromEuler` docs** in `quaternion.h:61-63` — documents "yaw = rotation around Y axis". In Z-up, yaw is around Z. Update docs to match Z-up convention
- [ ] 6.4 **Audit camera Y-up intermediary** in `camera.cpp` — document clearly that `m_orientation` is stored in Y-up space internally and why (DirectXMath convention), add warning comments at conversion points

## 7. Verification

- [ ] 7.1 Build all targets (`zig build`) — zero warnings, zero errors
- [ ] 7.2 Run all unit tests — 100% pass rate
- [ ] 7.3 Run `pyramid-demo` — verify no visual regression (shadows, lighting, atmosphere)
- [ ] 7.4 Run `pbr-demo` (if exists) — verify PBR lighting matches after k-remapping fix
- [ ] 7.5 Verify no `assert(` in engine code (grep confirms zero matches outside thirdparty)
- [ ] 7.6 Verify no `TODO` comments remain in engine code (grep confirms zero matches)
- [ ] 7.7 Verify no commented-out code blocks remain (manual review)
- [ ] 7.8 Verify `vulkan_device.cpp` is under 1500 lines after split
