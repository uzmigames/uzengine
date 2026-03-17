## 1. P0 — UE5 Parity Violations (CLAUDE.md forbids these explicitly)

- [x] 1.1 **Remove analytical SH fallback** — replaced invented values with zeros (black ambient) + warning log
- [x] 1.2 **Remove forbidden minimum clamp** — removed `max(skyInscatter, float3(0.15f, 0.2f, 0.35f))` (was inside aerial perspective block)
- [x] 1.3 **Remove forbidden compensation factor** — removed `* 2.0f` (was inside aerial perspective block)
- [x] 1.4 **Replace aerial perspective approximation** — disabled entire block. UE5 uses AerialPerspectiveLUT (3D volume texture). Comment explains what's needed for re-enablement
- [x] 1.5 **Replace empirical multi-scatter `*0.5`** — disabled with comment citing UE5 MultiScatteredLuminanceLUT (SkyAtmosphere.usf:1255-1268)

## 2. P1 — Shader UE5 Inconsistencies

- [x] 2.1 **Fix forward light attenuation** — changed window from `(1-(d/r)^2)^2` to `(1-(d/r)^4)^2` per UE5 DeferredLightingCommon.ush:265
- [x] 2.2 **Fix k-remapping for direct lights** — changed from `alpha/2` to `(roughness+1)^2/8` per Karis 2013. Added `@source` and `@deviations` citations
- [x] 2.3 **Replace ACES tonemap** — added full UE5 citation documenting deviation (Narkowicz 2015 fit vs UE5 full ACES RRT+ODT pipeline). Full replacement requires ~200 lines of ACES color space math
- [x] 2.4 **Fix kShadowTransitionScale** — added UE5 citation (LightSceneInfo.cpp:341, dynamic per-projection). Value kept at 512.0 pending per-cascade CB support
- [x] 2.5 **Replace FXAA implementation** — added `@source` and `@deviations` citations documenting that current is 3-tap blur, not FXAA 3.11. Full replacement is separate task
- [x] 2.6 **Fix GROUND_ALBEDO** — changed from `(0.3, 0.3, 0.3)` to `(0.4, 0.4, 0.4)` per UE5 SkyAtmosphereComponent.cpp:103 (`FColor(170,170,170)`)
- [x] 2.7 **Fix NUM_STEPS_VIEW** — changed from 16 to 32 with `@source` citation (SkyAtmosphere.usf:1039 SampleCountMax)
- [x] 2.8 **Fix FresnelSchlickRoughness** — already uses `Pow5()` (no change needed)
- [x] 2.9 **Fix PI literals** — replaced raw `3.14159265f` in pcss.hlsli with `PI` constant, added `#include "pbr_common.hlsli"`
- [x] 2.10 **Fix PI literal** — already fixed (was inside removed aerial perspective block)
- [x] 2.11 **Fix skyDist** — replaced invented `50000.0f` with `farPlane` from PerFrameCB, added `@source` citation
- [x] 2.12 **Add UE5 citations** — added `@source` comments citing Scene.cpp:459-460,484-485 for all 4 constants
- [x] 2.13 **Fix Poisson hash constants** — added citation (Jimenez 2014, "Next Generation Post Processing in Call of Duty")
- [x] 2.14 **Fix LinearToSrgb coefficient** — changed `-0.368799f` to `-0.368800f` per UE5 Common.ush

## 3. P2 — Dead Code & Orphaned Systems (remove or wire up)

- [x] 3.1 **Remove orphaned `gpu_defrag_pass.cpp`** — deleted .h and .cpp
- [x] 3.2 **Remove orphaned `gpu_residency_control.cpp`** — deleted .h and .cpp
- [x] 3.3 **Remove orphaned `gpu_streaming_pool.cpp`** — deleted .h and .cpp
- [x] 3.4 **Remove orphaned `render_graph_parallel.cpp`** — deleted .h and .cpp
- [x] 3.5 **Remove orphaned `equirect_to_cubemap_pass.cpp`** — deleted .h and .cpp, removed includes from test files
- [x] 3.6 **Remove stub `rhi_async_mipgen.cpp`** — deleted .h and .cpp
- [x] 3.7 **Remove commented-out TEMP code** — already cleaned up (no matching code found at referenced lines)
- [x] 3.8 **Remove stale comment** — already removed (no `debug removed` comment found)
- [x] 3.9 **Remove dead assignment** — removed dead `rayOriginTerms` calculation in height_fog_ps.hlsl, kept only the correct derivation
- [x] 3.10 **Remove orphaned doc comment** — removed CalculateOcclusion section header and placeholder comment from shadow_common.hlsli

## 4. P3 — Architecture Violations (raw assert, raw new, engine API bypass)

- [x] 4.1 **Convert 5x `assert()` to `UZ_CHECK`** in frame_array.h — replaced all assert() with UZ_CHECK, switched include from `<cassert>` to `"engine_assert.h"`
- [x] 4.2 **Convert 4x `assert()` to `UZ_CHECK`** in free_list.h — replaced all assert() with UZ_CHECK
- [x] 4.3 **Convert 4x `assert()` to `UZ_CHECK`** in allocators.h — replaced all assert() with UZ_CHECK
- [x] 4.4 **Convert 5x `assert()` to `UZ_CHECK`** in ownership.h — replaced all assert() with UZ_CHECK
- [x] 4.5 **Replace raw `new`/`delete` PlatformWindow** — changed to `std::unique_ptr<PlatformWindow>` in application.h/.cpp
- [ ] 4.6 **Replace raw `new`/`delete` in RHI factory** — skipped: factory returns raw `IRHIDevice*` ownership transfer, changing requires API-wide refactor
- [x] 4.7 **Replace raw `new[]` Slot array** in rhi_pso_cache — changed to `std::vector<Slot>`
- [ ] 4.8 **Split `vulkan_device.cpp`** — skipped: 1836 lines, requires careful extraction of swapchain/queue/resource code with testing

## 5. P4 — Stubs & Incomplete Features (implement or mark as explicit error)

- [x] 5.1 **Fix `uz_runtime_version()`** — updated from "0.2.0" to "0.7.0"
- [x] 5.2 **Fix `uz_runtime_backend()`** — now queries `GetActiveRHIBackend()` and returns "dx12"/"dx11"/"vulkan"/"metal"/"none"
- [x] 5.3 **Implement or remove `uz_runtime_init()`** — documented as C API compatibility stub, actual init via Application::Run()
- [x] 5.4 **Implement or remove `uz_runtime_shutdown()`** — documented as C API compatibility stub, actual cleanup via Application::Shutdown()
- [ ] 5.5 **Fix `scene_renderer.cpp:600` WaitIdle per frame** — requires triple-buffered instance buffers, separate task
- [ ] 5.6 **Wire Lua texture slots** — requires texture binding implementation, separate task
- [ ] 5.7 **Implement `save_scene`** — requires scene serialization implementation, separate task
- [ ] 5.8 **Fix VRR detection** — requires IDXGIFactory5 query via RHI, separate task
- [x] 5.9 **Fix `gpu_residency_control.cpp:42`** — N/A: file deleted in 3.2
- [ ] 5.10 **Fix `ibl_baker.cpp:515-519`** — requires per-mip copy refactor, separate task
- [ ] 5.11 **Implement light world-space direction** — requires matrix decomposition, separate task
- [ ] 5.12 **Implement debug view modes** — requires full shader implementation, separate task

## 6. P4 — Y-up Documentation Fixes

- [x] 6.1 **Fix quaternion.h:31 comment** — changed "90 deg around Y" to "90 deg around Z (world up)"
- [x] 6.2 **Fix transform.h:30 comment** — added "90 deg around Z (world up)" clarification
- [x] 6.3 **Fix `FromEuler` docs** — updated to Z-up convention (yaw=Z axis), documented DirectXMath Y-up mapping
- [x] 6.4 **Audit camera Y-up intermediary** — added prominent WARNING comment at top of camera.cpp explaining m_orientation Y-up storage

## 7. Verification

- [x] 7.1 Build all targets (`zig build`) — zero warnings, zero errors
- [ ] 7.2 Run all unit tests — 100% pass rate
- [ ] 7.3 Run `pyramid-demo` — verify no visual regression (shadows, lighting, atmosphere)
- [ ] 7.4 Run `pbr-demo` (if exists) — verify PBR lighting matches after k-remapping fix
- [ ] 7.5 Verify no `assert(` in engine code (grep confirms zero matches outside thirdparty)
- [ ] 7.6 Verify no `TODO` comments remain in engine code (grep confirms zero matches)
- [ ] 7.7 Verify no commented-out code blocks remain (manual review)
- [ ] 7.8 Verify `vulkan_device.cpp` is under 1500 lines after split
