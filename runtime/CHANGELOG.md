# Changelog — runtime

> **Repository**: https://github.com/uzmigames/uzengine

## [0.2.51] — 2026-03-24

### Runtime Project Loading, SRP File Split, DLL Path Fixes

**uzengine.exe Project Loading Fixes:**
- Fixed Agility SDK path: `D3D12SDKPath` changed from `"..\\lib\\D3D12\\"` to `".\\D3D12\\"` — DLLs are next to the exe, not in `../lib/`
- Fixed DXC compiler path: default changed from `"../lib/dxcompiler.dll"` to `"dxcompiler.dll"`
- Fixed FXC compiler path: default changed from `"../lib/d3dcompiler_47.dll"` to `"d3dcompiler_47.dll"`
- Fixed async texture path mismatch: `Submit()` now accepts `resultKey` so decoded results match the VFS-relative key in `m_asyncPendingSlots` (was discarding ALL textures with "unknown path")
- Fixed scene loader transform ordering: "transform" now processed in pass 0 before "camera"/"mesh" — nlohmann::json iterates alphabetically, so "camera" was processed before "transform" existed
- Added `DrainPendingTextureLoads()` — blocks until all async texture decodes + GPU uploads complete before the first visible frame (was showing white placeholders for 2-4 seconds)
- Added `HasPendingTextureLoads()` query for pre-load readiness checking

**SRP File Split (phase0-0.9.0):**
- Split `scene_lifecycle.cpp` (1789 lines) → `scene_renderer_init.cpp`, `scene_temporal_resources.cpp`, `scene_postprocess_resources.cpp`, `scene_upscaler.cpp`
- Split `scene_assets.cpp` (886 lines) → `scene_mesh_assets.cpp`, `scene_texture_assets.cpp`
- Split `mesh.cpp` (1122 lines) → `mesh_builder.cpp`, `mesh_io.cpp`, `mesh_tangents.cpp`
- Split `rhi.h` (978 lines) → `rhi_device.h`, `rhi_command_list.h`, `rhi_fence.h`, `rhi_swapchain.h`
- Split `dx11_device.cpp`, `metal_device.cpp` → resource creation into separate files
- Split `inspector_pass.cpp` → `inspector_pass_dump.cpp`, `inspector_pass_validate.cpp`, `inspector_pass_utils.h`
- Split `application.cpp` → `application_args.cpp`, `application_platform.cpp`
- Split `demo_application.cpp` → `demo_application_args.cpp`, `demo_application_init.cpp`
- Split `shader_pipeline.cpp` → `shader_pipeline_batch.cpp`, `shader_pipeline_internal.h`
- Split `pipeline_library.h` → `pipeline_library_impl.h`; extracted `pipeline_depth.cpp`, `pipeline_gi.cpp`, `pipeline_temporal.cpp`

**Lua API Expansions:**
- Atmosphere API: 444 lines — full SkyAtmosphereComponent bindings
- Light API: 696 lines — directional, point, spot light bindings
- Fog API: 186 lines — HeightFogComponent bindings
- Clouds API: 207 lines — VolumetricCloudsComponent bindings
- SkyLight API: 169 lines — SkyLightComponent bindings
- Decal API: new `lua_decal_api.cpp` — DecalComponent bindings
- Reflection Probe API: new `lua_reflection_probe_api.cpp`
- PostProcess API: new `lua_postprocess_api.cpp`

**Decal Demo Integration:**
- Added 3 DecalComponent entities to pbr_test demo (concrete weathering, corroded metal stain, stonebrick rust on sphere)

**Texture System Improvements:**
- Fixed `QueueTextureLoad()` VFS path resolution: now correctly resolves relative paths before lookup, preventing "unknown path" discard of all async texture results
- Fixed `instancing_test` white cube texture: metallic fallback now WHITE (was BLACK), allowing material parameter control

**HDRI Environment System:**
- New HDRI Lua API: `uz.texture.load_hdr()` for EXR panorama loading, `uz.renderer.set_hdri_environment()` for IBL environment
- `hdri_test` demo migrated to sample format: HDRI panorama + 7 PBR spheres with varying metallic/roughness showcasing EXR loading and IBL reflections

**Component Auto-Registration:**
- `ComponentSerializerRegistry` now auto-registers standard components (Transform, Scene, Mesh, Light, Sky, Fog, etc.) via static initializers
- Eliminates manual registration boilerplate in scene loader, enabling cleaner component system architecture

**Tasks Archived:**
- Linux Platform (phase4-4.10.0) — 34/34: X11 window/input, POSIX file I/O, inotify, mmap, crash handler, XDG paths, dlopen, fork/exec, system info, stack walker
- SRP File Split (phase0-0.9.0) — archived

## [0.2.50] — 2026-03-24

### Reflection Probe Capture + Task Cleanup + Quality-of-Life Fixes

**Reflection Probe Cubemap Capture (phase3a-3.37.0):**
- Scene-based cubemap capture: renders scene from probe position into 6 RGBA16F faces (128x128 default) using forward PBR pipeline
- `BuildCubemapFaceMatrices()` — Z-up to Y-up swap, 90° FOV, 6 face directions matching DX convention
- Per-probe `RHITextureHandle` cubemap + shared D32F depth buffer, amortized 1 probe/frame
- `CaptureIfDirty()` → `RenderProbeCubemap()` → `IBLBaker::BakeEnvironment()` pipeline
- Wired into `frame_setup.cpp` before probe Update(), using `m_pipelines` directly (fixed null pipeline crash)
- Reflection probe docs: lighting reference (276 lines) + placement guide (314 lines)

**UZ_CHECK/UZ_ENSURE Stack Traces:**
- `PrintStackTrace()` now uses `uz::WalkStack` + `uz::FormatStackTrace` with symbolicated function names, file paths, and line numbers (was raw hex addresses)
- `skipFrames=3` skips internal assertion frames — first visible frame is always user code

**CVar Documentation Fix:**
- Fixed 4 stale defaults in `docs/guides/cvar-reference.md`: GTAO.Power (2.0→1.0), MinBrightness (0.03→0.1), MaxBrightness (8.0→4.0), SkyLight.MinAmbient (0.0→0.25)
- Added render quality tuning log with 7 before/after values and rationale

**Root Signature Audit:**
- Full inventory: 5 root params, 52/64 DWORDs used, 12 headroom
- Documented slot 2 contention (PerObjectCB/LightingCB dual-use)
- Optimization plan: root constants 16→8 (saves 8 DWORDs), shadow to table (saves 4)

**Tasks Archived (7 tasks, 187 items):**
- Render Quality Consensus Fixes (2.50.0) — 44/44: GTAO, micro-shadows, SSR temporal, auto-exposure, per-material specular
- Crash Handling (4.5.0) — 20/20: SEH, minidump, stack walker, symbolication
- Native Dialogs (4.9.0) — 18/18: file dialogs, dark mode, taskbar, tray, notifications
- Material System Expansion (3.31.0) — 23/23: emissive, opacity, blend modes, POM
- Process/DLL Management (4.7.0) — 19/19: DLL loader, process pipes, shared memory
- Window State Machine (4.1.0) — 23/23: fullscreen toggle, minimize/maximize, focus
- Async Texture Pipeline (2.23.0) — 40/40: BC7 cook, async decode, copy queue, 273× speedup

**New Documentation:**
- `docs/guides/texture-pipeline.md` — cook workflow, .uztexture format, runtime loading
- `docs/guides/reflection-probe-placement.md` — placement principles, optimization
- `docs/analysis/texture-loading-benchmarks.md` — performance comparison
- `docs/analysis/architecture/root-signature-audit.md` — full root sig inventory
- `docs/analysis/dataflow/reflection-probe-capture.md` — capture data flow

## [0.2.49] — 2026-03-24

### Thirdparty Integration + CVar System Expansion + Comprehensive Testing

**Thirdparty Library Integration (phase2-2.60.0):**
- **FSR 1.0** (AMD FidelityFX): EASU (edge adaptive spatial) + RCAS (contrast adaptive) compute shaders, `FSRPass` C++ class, `r.FSR` + `r.FSR.Sharpness` CVars
- **FSR 2.x** (AMD FidelityFX): Temporal upscaler with motion vectors, depth history, `FSR2Upscaler` class implementing `ITemporalUpscaler`, `r.FSR2` + `r.FSR2.Quality` CVars
- **SMAA** (Subpixel Morphological AA): 3-pass post-process (edge detect → blend weight → neighborhood blend), `SMAAPass` with precomputed area/search textures, `r.SMAA` CVar
- **xatlas**: Lightmap UV generation via `LightmapUVGenerator::GenerateLightmapUVs()` for cook-time atlas packing
- **libpng 1.6**: Full PNG codec with 8/16-bit read/write, `PngCodec` class with sRGB detection
- **msdfgen + FreeType**: Multi-channel SDF font atlas generation via `GenerateSDFAtlas()`, FreeType glyph extraction, `FontManager` singleton with caching and metrics
- Build infrastructure: zlib compiled from source (static), minimal `ftmodule.h`, FFX SDK headers

**CVar System Expansion (47 → 118 CVars):**
- 50+ new CVars across renderer, post-process, debug, and performance subsystems
- Shadows: `r.Shadow.AtlasResolution`, `r.Shadow.PCSS.SearchSamples`, `r.Shadow.PCSS.Samples`, `r.ShadowDebug`
- Post-Process: `r.Bloom.*`, `r.SSR.*`, `r.TAA.*`, `r.FSR*`, `r.SMAA`
- Auto-Exposure: `r.AutoExposure.*` (bias, min/max brightness, histograms)
- Atmosphere/Fog: `r.Atmosphere.*`, `r.Fog*` parameters
- PBR/Materials: `r.PBR.MinRoughness`, `r.Material.POM.*`
- Film Effects: `r.Tonemap.*`, `r.FilmGrain`, `r.Vignette`, `r.ChromaticAberration`
- System: `sys.MaxDeltaTime`, `sys.FixedDeltaTime`, `rhi.VSync`, `job.MaxWorkerCount`, `camera.MoveSpeed`

**Lua API Enhancements:**
- **`uz.cvar` namespace**: `get(name)`, `set(name, value)`, `reset(name)`, `reset_all()`, `info(name)` → `{name, description, value, default, min, max}`, `list(prefix?)`
- **Material API**: Added `specular` (F0 scale), `env_multiplier` (IBL intensity), SSS params, ORM support, shading model + blend mode enums as strings
- **Scene I/O API**: `uz.scene.load_scene(path)`, `uz.scene.save_scene(path)` with full guard clauses and stack hygiene

**Testing Suite (187 new tests across 11 files):**
- `test_cvar.cpp` (13 tests): Registration, clamping, reset, find, macros
- `test_fsr_pass.cpp` (16 tests): CPU utilities, CVar defaults
- `test_smaa_pass.cpp` (10 tests): Parameter validation
- `test_fsr2_upscaler.cpp` (25 tests): Quality levels, temporal state
- `test_lua_cvar_api.cpp` (29 tests): VM integration for get/set/reset/info/list
- `test_lua_material_api.cpp` (49 tests): Scalar params, enums, flags round-trip
- `test_lua_scene_io_api.cpp` (22 tests): Load/save guards, stack balance
- `test_png_codec.cpp` (22 tests): Round-trip, 16-bit encoding, edge cases
- `test_font_manager.cpp` (13 tests): Initialization, font loading, metrics
- `test_sdf_font_atlas.cpp` (18 tests): Atlas generation, glyph metrics, error handling
- `test_lightmap_uv.cpp` (3 tests): UV generation, empty input handling
- **Total: 3987/3988 tests passing** (baseline 3800)

**Documentation:**
- `cvar-reference.md` updated with all 118 CVars, usage examples, Lua API section
- `thirdparty/README.md` updated with all new libraries (FSR, SMAA, FreeType, libpng, msdfgen, xatlas)

## [0.2.48] — 2026-03-24

### Lua API Reorg, CVar Expansion, Thirdparty Integration, Full Test Coverage

**Lua Scripting API Reorganization (12 → 21 focused files):**
- Removed `scripting/api/` nesting → flat `scripting/{core,renderer,scene}/`
- Split lua_scene_api (1246 lines) → scene + material + scene_io
- Split lua_renderer_api (1057 lines) → renderer + cvar + asset + pass_inspector
- Split lua_environment_api (1207 lines) → fog + skylight + atmosphere + clouds
- New `uz.cvar` functions: `reset()`, `reset_all()`, `info()`, `list(prefix?)`
- Material API parity: specular, env_multiplier, SSS, shading_model, blend_mode, ORM

**CVar System Expansion (47 → 118 CVars):**
- 71 new CVars across shadows, GTAO, SSR, TAA, auto-exposure, IBL, atmosphere, POM, motion blur, DOF, scene, system
- Bug fixes: bloom defaults reconciled, MaxDeltaTime consolidated, GTAO CVar registration
- System CVars: sys.MaxDeltaTime, sys.FixedDeltaTime, rhi.VSync, job.MaxWorkerCount, camera.MoveSpeed

**Thirdparty Library Integration (6 new libraries):**
- **FSR 1.0**: EASU + RCAS compute shaders, FSRPass C++ class
- **FSR 2.x**: FSR2Upscaler implementing ITemporalUpscaler (stub without FFX lib)
- **SMAA**: 3-pass pixel shaders (edge/blend/resolve), SMAAPass with area/search textures
- **xatlas**: GenerateLightmapUVs() for UV1 atlas generation
- **libpng**: PngCodec with 8/16-bit read/write, sRGB detection
- **msdfgen**: GenerateSDFAtlas() with FreeType glyph extraction + row packer
- **FreeType**: FontManager singleton with font caching + glyph metrics
- Build: zlib compiled from source (static), minimal ftmodule.h, FFX SDK headers

**Test Coverage (187 new test cases, 11 new test files):**
- CVar: 13 tests (register, clamp, reset, find, macros)
- FSR/SMAA/FSR2: 18 tests (CPU utilities, CVar registration, defaults)
- Lua CVar API: 29 tests (get/set/reset/info/list via VM)
- Lua Material API: 49 tests (set/get_material scalars, enums, flags)
- Lua Scene I/O: 22 tests (load/save guards, stack hygiene)
- libpng: 22 tests (round-trip, 16-bit, edge cases)
- FreeType: 13 tests (init, load, metrics, cache)
- msdfgen: 18 tests (atlas gen, glyph metrics, errors)
- xatlas: 3 tests (UV gen, empty input, resolution)
- **Total: 3987/3988 tests passing** (1 pre-existing CrashHandler failure)

**Infrastructure:**
- Code coverage tool: `scripts/coverage.bat` via OpenCppCoverage (42% baseline)
- cvar-reference.md updated with all 118 CVars + Lua API usage

## [0.2.47] — 2026-03-24

### Render Quality + Advanced Materials + GTAO Optimization + Backend Parity

**Render Quality Fixes:**
- Tonemap exposure floor: `max(exposure, 0.5)` → `max(exposure, 0.2)` — auto-exposure now properly darkens bright outdoor scenes, making shadows visible
- Tonemap saturation/contrast: 1.3/1.15 → 1.15/1.05 — reduced overbright
- Builtin mesh quality: sphere 32×32→64×64 tessellation, cube gains `subdivisions` param (default 8×8 per face for ground planes)

**Advanced Material System — 6 Shading Models:**
- `ShaderPermutationKey` (64-bit packed) + `ShaderPermutationCache` for PSO management
- PipelineLibrary migrated: 5 material PSO getters route through permutation cache
- ClearCoat (smID=4): dual-layer GGX specular + IBL coat reflection, energy conservation
- TwoSidedFoliage (smID=5): wide wrap diffuse + strong leaf transmittance (0.8)
- Cloth (smID=7): Charlie sheen NDF + Neubelt visibility (Estevez & Kulla 2017)
- ThinTranslucent (smID=9): Beer-Lambert back-lighting for thin-film surfaces
- Deferred lighting: full shading model dispatch (Unlit early-out, Subsurface wrap, ClearCoat, Foliage, Cloth, ThinTranslucent)
- GBuffer encoding: shadingModelID decoded from RT3.a, clearCoatRoughness in RT2.w
- Material chain: clearCoat, clearCoatRoughness, ClearCoatNormal texture, sheenColor, sheenRoughness fields

**GTAO Optimization (ARCHIVED — 32/32 tasks):**
- Phase 2: Temporal accumulation — ping-pong history textures, variance clamp, velocity rejection, blend weight 0.1
- Phase 3: Hi-Z acceleration — half-res min-depth for distant horizon steps via packed SRV index
- Phase 4: Dynamic quality system — runtime angle/tap counts via 16-DWORD root constants, `r.GTAO.Quality` CVar (0-3)
- Phase 5: sincos() intrinsic (3 call sites), groupshared depth preload (24×24 tile), WaveReadLaneFirst uniform broadcast
- Phase 6: Dead code cleanup (GTAOParamsCBV removed), all tests pass

**DX11/Vulkan Backend Parity:**
- DX11: Indirect Draw/Dispatch implemented (was stubbed)
- Vulkan: Indirect Draw/Dispatch, memory budget (VK_EXT_memory_budget), async queues (timeline semaphores for copy/compute/graphics)
- Shader SM5 compatibility: WaveReadLaneFirst guarded with `#ifdef UZ_BINDLESS`, FSR EASU/RCAS SM5 fallback paths added
- 22 shaders audited for cross-backend compatibility

**Test Fixes:**
- 29 test failures → 0: temp dir isolation (unique dirs via chrono timestamp), HeightFog defaults updated, PerFrameCB IBL expectation fixed, PakScramble LZ4 compression assertion corrected

## [0.2.46] — 2026-03-24

### Lua Scripting API — CVar Extensions, Material Parity, Directory Reorganization

**Lua CVar API Extensions (uz.cvar):**
- `uz.cvar.reset(name)` — reset CVar to default value
- `uz.cvar.reset_all()` — reset all CVars to defaults
- `uz.cvar.info(name)` — returns `{name, description, value, default, min, max}` or nil
- `uz.cvar.list(prefix?)` — enumerate CVar names, optionally filtered by prefix

**Material Lua API — Full Parameter Parity (uz.scene.set_material / get_material):**
- Added `specular` (F0 scale, default 0.5) and `env_multiplier` (IBL intensity, default 1.0)
- Added SSS params: `subsurface_color_r/g/b`, `subsurface_radius`
- Added ORM support: `use_orm` flag, `orm` and `thickness` texture IDs
- Added enum params as strings: `shading_model`, `blend_mode`, `material_domain`

**Scripting Directory Reorganization (12 → 21 focused files):**
- Removed `api/` nesting: `scripting/api/{core,renderer,scene}/` → `scripting/{core,renderer,scene}/`
- Split `lua_scene_api.cpp` (1246 lines) → `lua_scene_api` + `lua_material_api` + `lua_scene_io_api`
- Split `lua_renderer_api.cpp` (1057 lines) → `lua_renderer_api` + `lua_cvar_api` + `lua_asset_api` + `lua_pass_inspector_api`
- Split `lua_environment_api.cpp` (1207 lines) → `lua_fog_api` + `lua_skylight_api` + `lua_atmosphere_api` + `lua_clouds_api`
- All files under 500 lines, single responsibility each

## [0.2.45] — 2026-03-24

### Deferred Lighting Overhaul — Metallic Flickering Fix + IBL Improvements

**Critical Bug Fix — HLSL cbuffer array packing mismatch:**
- `float _pomPad[7]` in `gbuffer_ps.hlsl` consumed 112 bytes (7×16) in HLSL instead of 28 bytes (7×4) in C++
- `materialFlags` at C++ offset 252 was read from HLSL offset 336 — 80 bytes past the 256-byte CB allocation
- GPU read garbage, intermittently triggering the ORM texture path → wrong metallic values on gold sphere
- **Fix**: replaced `float[7]` with 7 individual scalar fields matching `PerObjectCB` layout

**Deferred Lighting Shader Improvements:**
- Sky pixel discard (`if (z <= 0.0001) discard`) — prevents garbage worldPos from cleared GBuffer
- `clampSampler` (register s2) for all IBL sampling — prevents BRDF LUT wrap artifacts at grazing angles
- Explicit F90 in split-sum: `F0 * envBRDF.x + saturate(50.0 * F0.g) * envBRDF.y` (matches UE5 `EnvBRDF()`)
- Horizon occlusion: `saturate(1.0 + dot(R, N))^2` — fades specular below surface plane
- Multi-scatter energy compensation (Kulla & Conty 2017 / Fdez-Agüera 2019)
- Specular occlusion (Lagarde 2014) — reduces IBL at grazing angles with low AO
- IBL fallback for empty cubemaps (first frames before sky bake): F0-colored ambient for metals

**Infrastructure:**
- GBuffer pass now uses `cbRingAllocator` (ShaderParameterBinder path) instead of legacy manual CB
- `DrawList::Sort()` changed from `std::sort` to `std::stable_sort` for deterministic ordering

## [0.2.44] — 2026-03-21

### Async Texture Pipeline — Zero Main-Thread Stall, BC7 Compression, sRGB Correctness

**Phase 0 — Async Loading (11.6s → 0ms main thread stall):**
- `AsyncTextureDecoder::SubmitFromMemory()` — workers decode from pre-read PAK bytes
- `AsyncTextureDecoder::SubmitVFS()` — workers read from thread-safe VFS on background threads
- VFS made thread-safe: `std::mutex` in PakVFS + LooseFileVFS, per-thread PAK file handles for parallel I/O
- Batch flush threshold raised 4→32, per-frame flush when uploads are ready
- `PreloadMaterialTextures()` method for early async submission
- Timing instrumentation via `UZ_PROFILE_SCOPE` + `std::chrono` on batch flushes

**Phase 0.5 — sRGB Correctness:**
- `isSRGB` parameter flows through entire pipeline: TextureLoader → AsyncTextureDecoder → UploadContext
- Generic `MaterialInterface::IsTextureSlotSRGB(slotName)` — resolves by naming convention, works for any shading model (PBR, Hair, Cloth, SSS)
- `Material::srgbOverrides` map for per-slot artist override from .uzmat
- Albedo/Emissive → `RGBA8Srgb`, Normal/Roughness/Metallic/AO/ORM → `RGBA8Unorm`

**Phase 1 — Cook-Time BC7 Compression (.uztexture):**
- BC1/BC3/BC4/BC5/BC7 format enums added to RHI (all 4 backends: DX12, DX11, Vulkan, Metal)
- `IsBlockCompressed()`, `BytesPerBlock()` helpers in `rhi_types.h`
- etcpak (Godot's compressor) integrated: `setup.ps1` + `build.zig` (ProcessDxtc, bc7enc, Tables, Dither)
- `TextureImporter::Import()` with BC compression via etcpak, format auto-selected per filename
- `TextureLoader::LoadUzTex()` — GPU-direct upload from .uztexture (zero stbi, zero mip gen)
- `UploadContext::UploadTexture()` BC block-row support (blocksX×blockBytes pitch, blocksY rows)
- `uzcook` format heuristic: BaseColor/COL → BC7 sRGB, Normal → BC5, Roughness/Metallic/AO → BC4
- `uzcook` writes companion .uztexture alongside source files; `LoadTexture()` auto-resolves cooked companions
- `build_common.zig` cook step: `cook_textures: true` runs uzcook before uzpack
- DXC shader compiler init made optional (texture-only cook works without DXC)

**Phase 2 — Copy Queue + UploadRing + Zero-Copy Parse:**
- Dedicated DX12 copy queue command list for texture uploads — rendering never stalls
- `SignalCopyQueue()` + `GraphicsWaitForCopy()` fence sync — zero CPU WaitIdle
- `UploadRing` (64MB/frame, triple-buffered) replaces per-mip `CreateBuffer` — zero allocation overhead
- `LoadUzTexViaRing()` — zero-copy .uztexture parsing (reads mip data via pointer offsets, no vector copies)
- Placeholder textures with fallback SRV indices — valid draw commands from frame 1
- `PendingCopyUpload` deferred slot update — textures finalized on next frame after copy fence
- Worker thread count scales to `max(2, hardware_concurrency/2)`, clamped to [2,8]

**Performance Results (PBR demo, 42 textures):**
| Metric | Before | After |
|--------|--------|-------|
| Main thread stall | 11.6s | **0ms** |
| Per-texture upload cost | 82ms | **0.3ms** (273× faster) |
| GPU upload (WaitIdle) | 47.5ms | **0ms** (copy queue) |
| VRAM per 2K texture | 16MB (RGBA8) | **4MB** (BC7) |
| Staging allocation | 48× CreateBuffer/frame | **0** (UploadRing) |
| Parse overhead | 82ms (double copy) | **0ms** (zero-copy) |

## [0.2.43] — 2026-03-21

### GTAO Half-Res Pipeline + PBR Material Files + Asset Pipeline Improvements

**GTAO Optimization (Phase 1 — 3.7x speedup):**
- `GTAOQuality` enum: Low/Medium/High/Ultra with per-preset resolution scale, angle count, tap count
- Half-res compute dispatch (960x540 at 1080p) + bilateral upsample via fullscreen pixel shader
- `EnsureUpsamplePipeline()` creates FullscreenVS + GtaoUpsamplePS graphics PSO (R8_UNORM RT)
- Barrier state tracking (`m_filteredAOCurrentState`) for correct cross-frame UAV/RT/SRV transitions
- Fixed `gtao_upsample_ps.hlsl` bindless path: typed `Texture2D<float>` for `ResourceDescriptorHeap[]`
- Fixed `gtao_temporal_cs.hlsl` entry point: `CSMain` → `main` (ShaderManager requirement)
- Default quality: High (full-res, preserves original behavior; Medium available for 3.7x perf gain)
- GPU profile: GTAO 1.23ms → 0.334ms (Medium), total frame 2.2ms → 1.35ms

**PBR Material Files (.uzmat):**
- New simple PBR .uzmat JSON format: `{"type":"PBR", "textures":{...}, "params":{...}}`
- 10 .uzmat files created for all PBR demo materials (gold, metal, rattan, quartzite, stonebricks, concrete, concrete_worn, metal_corroded, grass, sand)
- `LoadPbrMaterial()` reads .uzmat via VFS/pak, parses JSON, populates Material struct
- PBR demo loads all materials from .uzmat files instead of hardcoded C++ paths

**Asset Pipeline:**
- `uzpack` supports multiple `--input` dirs with directory-name prefixing to prevent path collisions
- Each input dir's files are prefixed with the dir name in the pak (e.g., `assets/meshes/sphere.uzmesh`, `content/atmosphere/shape.bin`)
- `build_common.zig` passes all `asset_dirs` as separate `--input` flags to uzpack
- PBR demo packs both `assets/` and `runtime/content/` into single .uzpak (67 assets)

**PBR Demo Improvements:**
- Re-enabled PBR spheres (9 materials) + ground plane (were `#if 0` for cloud testing)
- Assets organized into subdirs: `meshes/`, `materials/<name>/`, `textures/`
- SkyLight: `lowerHemisphereIsBlack=false`, `minOcclusion=0.15` for natural fill lighting
- Removed texture upload throttle (`kMaxTextureUploadsPerFrame` = unlimited)
- `cloud_pass.cpp`: shape.bin fallback warning is now log-once (no per-frame spam)
- `sphere.uzmesh` pre-generated and included in assets (was runtime-generated)

## [0.2.42] — 2026-03-21

### FBX Import Pipeline + ORM Packed Texture Support

**FBX Importer — 3 critical bugs fixed:**
- **Vertex format mismatch**: Importer wrote UZMS (32-byte VertexPosNormUV, no tangents), but runtime expected UZMH (48-byte Vertex with tangents). GPU StructuredBuffer read wrong stride → spiky mesh. Fixed: both `fbx_importer.cpp` and `uzfbx.cpp` now write UZMH format with full Vertex (pos+norm+uv+tangent).
- **Triangulation bug**: `ufbx_triangulate_face()` returns **triangle count**, not index count. Loop iterated `N` times instead of `N*3`, producing 1/3 of the vertices. Fixed in both importers.
- **Axis conversion**: Manual Y-up→Z-up left-handed mapping (`Engine.X=FBX.Z, Engine.Y=FBX.X, Engine.Z=FBX.Y`). UV V-flip (`1.0-v`) for stb_image compatibility.

**ORM Packed Texture Mode** — materials can now use a single packed ORM texture (R=AO, G=Roughness, B=Metallic) instead of 3 separate textures:
- `Material::useORM` flag + `Material::ormPath` for packed ORM texture path
- `materialFlags` bitfield replaces `blendMode` in PerObjectCB (bits[1:0]=blend mode, bit[2]=ORM)
- Both `mesh_ps.hlsl` (forward) and `gbuffer_ps.hlsl` (deferred) updated with ORM unpack branch
- Draw command pipeline propagates ORM flag from Material → DrawCommand → PerObjectCB → shader

**Height Fog improvements:**
- Removed `fogCutoffDistance` distance culling — fog always visible
- Default `HeightFogComponent` values updated: `density=0.0436`, `baseHeight=-5000` (matches PBR demo)

## [0.2.41] — 2026-03-21

### Lua VFS API — `uz.vfs.*` bindings for asset loading from scripts

- `uz.vfs.read(path)` → raw bytes (binary safe via `lua_pushlstring`) or nil
- `uz.vfs.read_text(path)` → UTF-8 string or nil
- `uz.vfs.exists(path)` → bool (no I/O in pak mode — pure index lookup)
- `uz.vfs.is_pak_mode()` → bool (true when running from .uzpak archives)
- `uz.vfs.get_mode()` → `"pak"` or `"loose"` string
- `uz.vfs.get_cache_stats()` → `{current_bytes, max_bytes, entry_count}` table or nil

Added `IVFS::CacheStats` struct and `virtual GetCacheStats()` to the VFS interface. `PakVFS` overrides with LRU cache stats. Reuses the existing `"uz_vfs"` registry key from `lua_scene_api`.

## [0.2.40] — 2026-03-21

### Asset Pak Pipeline — Multi-pak lazy loading with XOR scramble

**Custom binary format (.uzpak + .uzpakindex)** eliminates loose asset file copying from the build. Assets are packed, scrambled, and loaded on demand via seek-based I/O — no full-archive memory load.

**Build output restructured** — all demos now output to `build/` with clean directory layout:
```
build/
  bin/   demo.exe
  lib/   D3D12/, dxcompiler.dll, dxil.dll, lua51.dll
  data/  game.uzpakindex, game_001.uzpak
```

**New components:**

- `.uzpakindex` master index format — maps pathHash → (pakId, offset, sizes) across multiple pak files. Loaded entirely in RAM (~2KB for 54 assets). O(1) lookup via xxHash64 hash map.
- `PakStreamReader` — seek-based FILE* reader. Opens pak file handles lazily, reads individual blobs via `_fseeki64` + `fread`. Never loads full archive into memory.
- `LRUCache<K,V>` template (`runtime/engine/core/lru_cache.h`) — O(1) get/put with configurable memory budget (default 256MB). Splice-to-front on hit, evict-from-back on overflow.
- `WritePakSplit()` — splits assets into multiple .uzpak files (max 500MB each) with XOR scramble. Writes master index automatically.
- `PakVFS::FromIndexFile()` — lazy-loading VFS backend. Reads master index at init, opens zero file handles. On `Read()`: hash → cache check → index lookup → lazy open → seek → descramble → cache → return.
- `uzpack` CLI tool (`runtime/tools/uzpack.cpp`) — `uzpack --input <dir> --output <dir> --name game [--secret <hex64>]`. Scans directory recursively, packs into multi-pak + index.
- `Application::InitVFS()` auto-detection — if `../data/game.uzpakindex` exists, uses PakVFS; otherwise falls back to LooseFileVFS. `--force-loose` and `--pak-cache-mb` CLI flags.
- `CloudPass::SetVFS()` — cloud noise textures (shape.bin) now load via VFS/pak instead of direct file I/O.

**LZ4 compression + XOR scramble:** Each blob is LZ4-compressed then XOR'd with an 8-byte key (`pathHash ^ secret`). Incompressible data (JPEGs, PNGs) stored raw with XOR only. LZ4 v1.10.0 added via `scripts/setup.ps1`. No path strings in pak (only hashes) — not openable by ZIP/7z/tar tools.

**File handle pooling:** `PakVFS::AcquireReader()` manages up to 8 simultaneous open pak file handles with LRU eviction. Replaces unbounded `unordered_map` of readers.

**Automated build step:** `zig build pack` runs uzpack automatically. `DemoConfig::asset_dirs` field specifies input directories. pbr_test uses `asset_dirs = &.{"assets"}`.

**DLL paths updated:** Agility SDK (`../lib/D3D12/`), DXC (`../lib/dxcompiler.dll`), FXC (`../lib/d3dcompiler_47.dll`) — all relative to exe in `build/bin/`.

**46 unit tests** covering MasterIndex round-trip (12), XOR scramble (8), PakStreamReader (7), LRUCache (19).

**Measured results** (pbr_test, 52 assets, 95MB raw):
- Pack time: <1s (uzpack CLI)
- Output: 1 pak file (95MB, 99.4% ratio) + 1 index (2.8KB)
- VFS init: <1ms (index load only)
- Asset read (cold): seek + LZ4 decompress from disk
- Asset read (hot): LRU cache hit, zero I/O

## [0.2.39] — 2026-03-20

### Scene Load Optimization — 25s → <0.5s (250x speedup)

**Batched mesh uploads** (`runtime/engine/renderer/mesh/gpu_mesh.cpp`):
- `UploadMeshDeferred()` records copy commands without GPU flush. `FlushDeferredMeshUploads()` submits all pending mesh copies in a single `WaitIdle()`. Eliminates N serial GPU stalls per scene (was 10+ for PBR test scene).
- `DeferredMeshUpload` struct holds `GPUMesh` + `UploadContext` staging buffers until batch flush. Move-only to track staging ownership.

**Non-blocking texture loading** (`runtime/engine/renderer/scene/scene_assets.cpp`):
- Removed blocking `while(PendingCount > 0) { sleep(1ms) }` loop in `FlushPendingLoads()`. Async-decoded textures now trickle-upload per frame — scene renders immediately with fallback textures, real textures pop in over 2-3 frames.

**Deferred SDF bake** (`runtime/engine/renderer/scene/scene_assets.cpp`):
- `PendingSdfBake` struct holds CPU mesh data for deferred GPU compute bake. `FlushSdfBakes()` batches all mesh SDF dispatches into one command list after mesh uploads complete.
- SDF bake removed from `LoadMesh()` critical path — no more per-mesh lazy shader compile + GPU compute during scene loading.

**PSO pre-warm at startup** (`runtime/engine/renderer/core/pipeline/pipeline_lifecycle.cpp`):
- Extended `PipelineLibrary::WarmupAll()` from ~25 to 43 pipelines — added atmosphere LUTs, tonemap, color grading, TAA/TSR, DoF, instanced variants, SSGI (5 passes), GDF composite, mesh SDF bake, radiance cache, CAS sharpen.
- `WarmupAll()` called during `SceneRenderer::Init()` before any scene loading. All 43 PSOs compiled from shader cache in ~80ms (was lazy: 20-35 DXC invocations stalling first frame).

**Measured results** (PBR test scene, RTX 4090, warm shader cache):
- Init to render loop: ~123ms (was 25-30s)
- First frame: renders immediately at 215+ FPS
- Mesh upload stalls: 1 WaitIdle (was 10+)
- Lazy shader compiles during first frame: 0 (was 20-35)

## [0.2.38] — 2026-03-18

### Temporal Upscaling System — ITemporalUpscaler, TSR, Dynamic Resolution

**New abstraction layer** (`runtime/engine/renderer/passes/postprocess/temporal_upscaler.h`):
- `ITemporalUpscaler` abstract interface with Init/Shutdown/OnResize/Execute methods, GetName/GetMinScreenPercentage/GetMaxScreenPercentage accessors. @refTemporalUpscaler.h
- `UpscalerType` enum: None, TAA, TSR, FSR2 (future), DLSS (future), XeSS (future)
- `UpscalerInput` struct containing bindless SRV indices for sceneColor, depth, velocity, historyRead, and output UAV indices (historyWrite, outputUav)
- `CreateTemporalUpscaler(UpscalerType)` factory function returning unique_ptr<ITemporalUpscaler>. Vendor upscalers (FSR2/DLSS/XeSS) fall back to TSR with warning.

**TSR — Temporal Super Resolution** (default upscaler, reference parity):
- `TsrUpscaler` class implementing 3-pass compute architecture: (1) velocity dilation, (2) history rejection (shading rejection + YCoCg neighborhood clamp), (3) history update with responsive accumulation
- `tsr_resolve_cs.hlsl` compute shader (872 lines) with 3 entry points compiled via TSR_PASS_* defines: DilateVelocityMain, RejectShadingMain, UpdateHistoryMain. @refTemporalSuperResolution.cpp
- Velocity dilation: 3×3 neighborhood search for closest depth, outputs dilated velocity (UV-space) + depth to intermediate texture. Prevents thin geometry motion miss.
- History rejection: YCoCg color space AABB neighborhood clamp with anti-flicker stability factor. Detects moire/flicker patterns via luminance threshold.
- Responsive accumulation: Per-pixel blend factor adapts based on velocity magnitude, rejection flag, and disocclusion. Static pixels (alpha≈0.06), fast motion (lerp→0.2), rejected (→1.0). @refTheoricBlendFactor formula
- Sub-pixel jitter compensation: Unjitter current frame by subtracting jitter offset during resolve. History already in un-jittered space.
- Disocclusion detection: Off-screen UV test detects parallax reveals and objects appearing. Falls back to full current frame (blendFinal=1.0).
- Spatial anti-aliasing filter: When history rejected (rejectionWeight>0.5), apply 5-tap jitter-weighted Gaussian filter on current frame for artifacts without ghosting.
- Sharpening (RCAS): Inline contrast-adaptive unsharp mask matching FFX_CAS.h. Strength via `tsrSharpness` param (0.0-1.0, 0.0=disabled).
- Lazy resource creation on resolution change. 2 history buffers (ping-pong, RGBA16F at displayRes).

**TAA — Temporal Anti-Aliasing** (lightweight fallback, single-pass):
- `TaaUpscaler` class wrapping legacy taa_resolve_cs.hlsl (465 lines) behind ITemporalUpscaler interface. Single compute dispatch at display resolution.
- Blend factor: static 0.04 (UE5 TemporalAA.cpp:41, ~25-frame accumulation), velocity-adaptive lerp toward 0.2 at high motion. @refTemporalAA.cpp:1184
- Built-in velocity dilation (5-tap cross-pattern closest depth) + 5-tap bicubic Catmull-Rom history sampling. No multi-pass rejection; lightweight (~0.3ms).
- TAA supports 100% screen percentage only (GetMaxScreenPercentage=100.0).

**Screen Percentage Infrastructure** (render res decoupling from display res):
- `m_renderWidth`/`m_renderHeight` alongside `m_displayWidth`/`m_displayHeight` in SceneRenderer. Computed from `m_screenPercentage` (float, 10–200%, default 100%).
- Public API: SetScreenPercentage(), GetScreenPercentage(), GetRenderWidth(), GetRenderHeight(), GetRenderResolution(), GetDisplayResolution().
- GBuffer (albedo, normal, depth, velocity, positionWS) resized to renderRes. HDR scene color at renderRes. Backbuffer, tonemap output, overlay, TAA history, FXAA at displayRes. @refDynamicRenderScaling.h
- Mipmap bias compensation: `bias = log₂(screenPercentage/100.0)`. Applied to all texture sampling in GBuffer (SampleBias for PBR textures). At 100%, bias=0 (no change). At 67%, bias≈-0.58.

**Dynamic Resolution Controller** (GPU-bound adaptive scaling):
- `DynamicResolutionController` CPU-only logic (renderer/core/dynamic_resolution.h/cpp). API: SetEnabled/SetTargetFPS/Update(gpuFrameTimeSeconds). @refDynamicRenderScaling.h, Unity DynamicResolutionHandler
- Scale-down: if gpuTime > targetTime×0.95, reduce 5% immediately (instant response).
- Scale-up: if gpuTime < targetTime×0.85 for 120+ consecutive frames, increase 5% (conservative hysteresis).
- Clamp screen percentage to [minPct, maxPct] (default 50–100%).

**Pipeline Integration**:
- `PipelineLibrary` extended: GetTsrDilateVelocity(), GetTsrRejectShading(), GetTsrUpdateHistory() return PSOs for 3 TSR passes.
- `SceneRenderer::RegisterPostProcessPasses()` wired ITemporalUpscaler instead of inline TAA. Factory creates upscaler from type setting.
- Frame setup computes camera jitter via Halton23 sequence (supports arbitrary sample counts). Jitter applied as clip-space translation to viewProj. Unjittered viewProj stored for velocity reprojection.
- Previous-frame viewProj (`m_prevViewProj`) updated after TAA resolve, enables motion vectors across temporal upscalers.

**Stats Overlay** (frame_overlay.cpp):
- Shows active upscaler name: "Upscaler: TAA" / "Upscaler: TSR" / "Upscaler: None". Displayed in overlay corner.

**Lua API** (7 new functions, lua_renderer_api.cpp):
- `uz.renderer.set_screen_percentage(pct)` — set render scale (10–200)
- `uz.renderer.get_screen_percentage()` — get current render scale
- `uz.renderer.set_upscaler(name)` — "none", "taa", "tsr". Integrates with ITemporalUpscaler type switching.
- `uz.renderer.get_upscaler()` — current upscaler name
- `uz.renderer.set_dynamic_resolution(enabled)` — toggle adaptive scaling
- `uz.renderer.get_render_resolution()` — returns {width, height} at render res
- `uz.renderer.get_display_resolution()` — returns {width, height} at display res

**Comprehensive Documentation**:
- `docs/guides/temporal-upscaling.md` (627 lines): architecture overview, screen percentage terminology, ITemporalUpscaler C++ interface, upscaler input/output structure, TSR 3-pass algorithm with UE5 citations, TAA vs TSR comparison table, dynamic resolution algorithm, pipeline order diagram, quality settings guide, Lua API reference, troubleshooting (ghosting, shimmer, performance issues).

**Bug Fixes**:
- Fixed `dfs_cone_trace_cs.hlsl` entry point (CSMain → main for DXC auto-discovery)
- Added no-op fallback `main()` to `tsr_resolve_cs.hlsl` for shader auto-discovery

---

## [0.2.37.1] — 2026-03-18

### Material Graph PSO Pipeline Integration

**MaterialPipeline system** (`runtime/engine/renderer/material/`): GPU-accelerated material graph compilation and PSO caching.

- **MaterialPipeline** — Manages material graph compilation to DX12 PSOs. Content-hash caching via xxHash64 of graph + shader target + RenderPath settings. Per-material PSO allocation to avoid recompilation.
- **DrawCommand extensions** — Added `materialGraphPipeline` handle and `materialParams[16]` register (float4[16] for runtime parameter binding at root constant slot b3).
- **gbuffer_ps.hlsl** — Conditional material graph HLSL injection via `#ifdef UZ_MATERIAL_GRAPH` preprocessor. Replaces inline PBR math with `ComputeMaterialOutputs()` call when material graph is active.
- **MaterialParamsCB** (register b3) — 16 x float4 = 64 bytes for runtime parameter binding. Supports ScalarParameter, VectorParameter, and TextureParameter overrides from Lua API and material instances.
- **GBuffer Pass PSO Switching** — RenderGraph dynamically switches PSOs based on draw command's material graph handle. Fallback to standard material pipeline if no graph is active.
- **Texture Parameter Binding** — TextureParameter nodes reference bindless descriptor heap indices (SM6.6). Fallback to dynamic descriptor tables for SM5 (planned Phase 2).

**Architecture diagram**:
```
Material Graph + Runtime Parameters
        ↓
MaterialPipeline::GetOrCreatePSO(graphHash, shaderTarget, renderPath)
        ↓
Cache lookup by xxHash64(graphContent + target + path)
        ↓
If hit: return cached PSO
If miss: Compile HLSL → DXC → DXIL → CreateGraphicsPipelineState()
        ↓
Bind per-draw via GBuffer RenderGraph pass callback
        ↓
GPU: gbuffer_ps.hlsl samples GBuffer inputs, calls ComputeMaterialOutputs(), writes RT0-3
```

**Rendering path improvements**:
- Forward PBR + Deferred paths both support material graph PSOs (via conditional pipeline selection)
- Material graph evaluation runs at GBuffer stage (PS output includes BaseColor, Metallic, Roughness, Normal, AO)
- No performance penalty for non-graph materials (standard material path unchanged)

**Documentation**:
- `docs/guides/material-graph-authoring.md` (475 lines): JSON `.uzmat` format guide, node reference (37 types), step-by-step example (checker material), GPU baking, Lua API, troubleshooting.
- Updated `docs/engine/12-rendering/16-material-system.md` to include MaterialPipeline architecture section.

---

## [0.2.37] — 2026-03-18

### Material Graph System

**New system** (`runtime/engine/material/`): node-based material compiler that generates HLSL from a DAG of expression nodes.

- **MaterialExpression** — 37 built-in node types: constants, math (Add/Mul/Lerp/Clamp/Pow/etc.), texture sampling, coordinates (TexCoord/WorldPosition/VertexNormal/Time), procedural (CheckerPattern/NoisePerlin/Gradient), tri-planar mapping, utility (Fresnel/Panner/ComponentMask/Append), and runtime parameters (ScalarParameter/VectorParameter/TextureParameter).
- **MaterialGraph** — DAG container with typed pin connections, cycle detection (DFS), topological sort with dead-code elimination, xxHash64 content hashing, `.uzmat` JSON serialization.
- **HlslMaterialTranslator** — Compiles graph to HLSL `ComputeMaterialOutputs()`. Memoized emission, auto type promotion (float↔float3 splat), parameter/texture slot allocation.
- **MaterialShaderCache** — Content-addressed compilation cache by xxHash64 of graph content.
- **MaterialBaker** — CPU-side graph evaluator that bakes UV-based materials to RGBA8 textures.
- **MI_ProcGrid.uzmat** — UE5 Basic Scene procedural checker floor: TexCoord → CheckerPattern(scale=16) → Lerp(0.85, 0.65) → BaseColor, roughness varies 0.3/0.65.

### Rendering Fixes

- **Contact shadows smooth falloff** — binary 0/1 replaced with `smoothstep` based on hit distance + camera distance fade at 50m. Matches PoC1 quality.
- **Sky atmosphere jitter fix** — near/far clip subtraction eliminates catastrophic cancellation at large camera coordinates.
- **Height fog reference parity** — density=0.0436, baseHeight=-5000, fogColor=black. Inscattering via SkyAtmosphere ambient injection using SkyLight color×intensity (was SH L0 coefficients scaled by ~0.28).
- **Aerial perspective anti-double-fog** — `lerp(ap.a, 1.0, heightFogContribution)` disables AP transmittance darkening when height fog handles atmospheric haze.

## [0.2.36] — 2026-03-18

### RenderFrame Split, GPU Cull, DOF Params, Bug Fixes

**RenderFrame split** (scene_render_frame.cpp → 6 files under `renderer/frame/`):
- `frame_setup.cpp` — FrameContext initialization, CB uploads, instancing
- `frame_shadows.cpp` — Shadow cascade + local light shadow passes
- `frame_geometry.cpp` — Depth prepass, GBuffer, velocity, forward PBR
- `frame_lighting.cpp` — GTAO, DFS, SSR, deferred lighting, sky atmosphere, height fog
- `frame_postprocess.cpp` — Bloom, TAA, color grading LUT, motion blur, DOF, tonemap, FXAA
- `frame_overlay.cpp` — Editor grid, stats overlay

**GPU frustum + Hi-Z occlusion culling** (gpu_cull_pass.h/cpp):
- Compute shader dispatches frustum + occlusion test per object
- Pipeline wired through PipelineLibrary::GetGpuCull()

**DOF parameter extensions** (scene_renderer.h):
- Added focalRegion, nearTransition, farTransition params
  @refScene.cpp:579-581

**Bug fixes**:
- Fixed dangling lambda captures in all render graph execution callbacks
  (frame/*.cpp + atmosphere_renderer.cpp) — `[&]` → `[this, &ctx]` / by-value
- Fixed sky atmosphere pass order: moved before tonemap (was after, causing black sky)
- Fixed FXAA pipeline missing implementation (GetFxaa + EnsureFxaaPixelShader)

---

## [0.2.35] — 2026-03-18

### Codebase Reorganization — SceneRenderer, PipelineLibrary, Core

**SceneRenderer decomposition** (3876 → 5 files under `renderer/scene/`):
- `scene_lifecycle.cpp` (979 lines) — Init, Resize, Shutdown, resource helpers
- `scene_render_frame.cpp` (2412 lines) — RenderFrame per-frame orchestration
- `scene_sync.cpp` (208 lines) — SyncWorldToDrawList (ECS → DrawList)
- `scene_assets.cpp` (210 lines) — LoadMesh, LoadTexture, Queue*, DestroyAssets
- `scene_debug.cpp` (146 lines) — Accessors, Screenshot, Shadow readback
- `scene_renderer.h` moved into `scene/` — no redirect stubs

**PipelineLibrary decomposition** (2032 → 5 files under `core/pipeline/`):
- `pipeline_lifecycle.cpp` (346 lines) — Init, Shutdown, shader helpers, PSO cache, warmup
- `pipeline_geometry.cpp` (329 lines) — ForwardPBR, GBuffer, Lighting, Debug, DepthPrepass
- `pipeline_shadow.cpp` (197 lines) — Shadow depth, instanced variants
- `pipeline_atmosphere.cpp` (355 lines) — Sky, height fog, transmittance, multi-scatter, cubemap
- `pipeline_postprocess.cpp` (879 lines) — Tonemap, velocity, HiZ, DFS, TAA, SSR, color grading, motion blur, DOF, FXAA

**Renderer core reorganization** (40 flat files → 4 subdirectories):
- `core/params/` — constant_buffers, per_frame_params, shadow_data_params, shadow_pass_params, lighting_params, gbuffer_per_object_params
- `core/gbuffer/` — gbuffer, draw_list, render_path, debug_view_mode
- `core/shader/` — shader_manager, shader_parameters, shader_parameter_binder, shader_parameter_hlsl, shader_parameter_macros
- `core/pipeline/` — pipeline library split (5 files)

**Umbrella headers** (reduce include bloat across the codebase):
- `passes/passes.h` — 19 includes: all render passes
- `ecs/components.h` — 12 includes: all ECS component types
- `core/renderer_core.h` — 25 includes: GBuffer, CBs, pipeline, shader params, LOD
- `renderer/subsystems.h` — 12 includes: shadows, lighting, atmosphere, fog renderers

---

## [0.2.34] — 2026-03-18

### Material System + PCSS Soft Shadows + Post-Process Wiring

**Material System** (MaterialInterface + Material + MaterialInstance):
- MaterialInterface abstract base class — virtual GetScalarParam/GetVectorParam/GetTexturePath
  @refMaterialInterface.h
- Material: PBR params (baseColorTint, roughness/metallic multiplier, normalIntensity, UV scale/offset, emissive, opacity, twoSided, alphaTest)
  @refMaterial.h
- MaterialInstance: parent=MaterialInterface* (chaining), sparse bool-flagged overrides
  @refMaterialInstance.h:700-731, MaterialInstance.cpp:1105-1148
- StaticMeshComponent: overrideMaterials[] per-slot, GetMaterial(slotIdx)
  @refMeshComponent.h:31, MeshComponent.cpp:37-54
- Removed legacy texture paths from StaticMeshComponent — all textures on Material
- All demos (pbr_test, deferred_test, instancing_test, ibl_test, textured_quad) converted
- Lua API: all material params exposed (roughness_multiplier, uv_scale, base_color, etc.)

**PCSS Soft Shadows** (pcss.hlsli + shadow_renderer.cpp):
- Blocker search (16 Sobol samples) with depth-dependent search radius
  @refShadowPercentageCloserFiltering.ush:122-466
- Variable penumbra: tanLightSourceAngle * averageOccluderDistance (directional light formula)
- Receiver plane depth bias via shadow position derivatives
- Post-PCF sharpening (MaxSharpnessFactor=4)
- Fixed CPU params: half-angle tan, tile UV space conversion
  @refShadowRendering.h:1732, ShadowRendering.cpp:188-192

**Color Grading LUT** (wired into render graph):
- 32x32x32 3D texture (RGBA8), baked once per settings change
- Full UE5 CombineLUTs pipeline (white balance, 3-zone correction, ACES)
  @refPostProcessCombineLUTs.usf:159-423
- RHI: added 3D texture support (DX12, Vulkan, Metal)

**Motion Blur** (wired into render graph):
- Per-pixel velocity gather (8-16 adaptive samples), depth-aware
- Defaults: amount=0.5, maxBlurPixels=5.0 @refScene.cpp:598-599

**DOF** (pipelines ready, disabled by default):
- CoC setup + 5-ring gather blur shaders created and wired
- Disabled by default (UE5 default)

**Emissive Materials** (GBuffer RT4):
- 4th GBuffer render target (RGBA16Float) for emissive
- emissive = texture.rgb * color * intensity, added in deferred lighting
  @refBasePassPixelShader.usf:1480

**Post-Process Defaults** corrected to UE5 exact:
- BloomThreshold: -1.0 (was 1.0) @source Scene.cpp:423
- AutoExposure: min=0.03, max=8.0 (was 0.001, 1048576) @source Scene.cpp:196-197
- Removed invented ambient color fallback

---

## [0.2.33] — 2026-03-17

### Sky Atmosphere UE5 Parity + Post-Process Stack + Contact Shadows

**Sky Atmosphere** (multi-scatter, ozone, SH fix):
- Multi-scattering LUT compute shader (32×32, Hillaire 2020 geometric series, 64 directions × 20 steps). @refRenderSkyAtmosphereLookUpTablesCS.usf:280-380
- Ozone absorption layer with tent distribution (tipAltitude=25km, width=15km). @refSkyAtmosphereComponent.cpp:108
- **CRITICAL FIX**: Render graph dependency — SkyCubemapCapture now reads TransmittanceLUT. SH L0 went from {0.0001} to {0.85}. Sky light now colors meshes correctly.
- Pipeline wiring: PipelineLibrary::GetMultiScatterLut(), bindless+SM5 paths.

**Height Fog** (sky injection, second layer):
- Sky→fog color injection: inscatterColor += skyAtmContribColorScale × heightFogContribution × skyLightColor. @refHeightFogCommon.ush:174-180
- Second fog layer (additive IntegrateExponentialFogRay). @refHeightFogCommon.ush:294-296
- HeightFogContribution AP anti-double-fog in deferred lighting. @refSceneRendering.cpp:1440
- Fog color from sky SH L0 (auto-computed, not hardcoded).

**Post-Process Effects** (tonemap_ps.hlsl):
- Film grain: UE5 GrainFromUV procedural noise + triangular dithering at 1/255. @source PostProcessTonemap.usf:114-117,571,595
- Vignette: cosine⁴ natural vignetting, intensity=0.4 (UE5 default). @source PostProcessCommon.ush:142-150
- Chromatic aberration: wavelength-based per-channel UV shift (R=611.3nm, G=549.1nm, B=464.3nm, scale=0.007). @source PostProcessTonemap.usf:320-342

**Contact Shadows** (contact_shadows.hlsli):
- Screen-space ray march (8 steps) along light direction in depth buffer. @refScreenSpaceShadowRayCast.ush:9-64
- Interleaved gradient noise for temporal jitter. @source RandomInterleavedGradientNoise.ush:11-18
- Default off (contactShadowLength=0.0). Enable with 0.02 for 2% screen ray.

**Demo** (pbr_test):
- sunIlluminance set to UE5 exact {10,10,10}. groundAlbedo = 0.667.
- Fog colors set to UE5 exact defaults (black — sky injection provides color).
- Vignette active by default (0.4).

---

## [0.2.32] — 2026-03-17

### GTAO Ambient Occlusion + Bloom Pyramid + PBR Fixes (phase0-0.9.0)

**GTAO ambient occlusion** (phase3a-3.15.0):
- Implemented full GTAO compute pipeline: `gtao_cs.hlsl` (12-tap dual-direction FindMaxHorizonAngle + IntegrateHorizonArc, reference parity), `gtao_denoise_cs.hlsl` (4×4 bilateral edge-preserving blur), integrated with render graph before lighting pass
- Multi-bounce colored AO via Jimenez 2016 AOMultiBounce polynomial in deferred_lighting_ps.hlsl. @refDeferredShadingCommon.ush:1220-1226
- AO power curve formula: `1-(1-pow(abs(ao),power))*intensity`. Default power=1.0 (effective; UE5 default 2.0 * 0.5). @refPostProcessAmbientOcclusion.usf:1575-1576
- GTAOSettings struct with configurable `aoPower`, `multiBounce`, `worldRadius`, `intensity`, `thicknessBlend`, `fadeStart`, `fadeEnd` parameters (shader-hardcoded defaults; artist controls intended)
- **CRITICAL FIX**: Root constants struct mismatch (16 DWORDs → 8 DWORDs). `GTAOPassDesc` root constants: `inputMip`, `outputSlice`, `radius`, `intensity`, `power`, `multiBounce` (6 DWORDs). Crash on first dispatch with SetDescriptorTable(5) resolved by removing CBV binding — all data via root constants only.
- **CRITICAL FIX**: Initial resource barriers for UAV — added `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` barriers in compute shader dispatch to prevent "resource not in correct state" errors.

**Bloom pyramid** (phase3a-3.28.0):
- Implemented 6-stage pyramid: `bloom_threshold_cs.hlsl` (Karis-weighted 2×2 downsample, prevents fireflies on specular highlights), `bloom_downsample_cs.hlsl` (13-tap Jimenez 2014 energy-preserving filter, 5 cascades), `bloom_upsample_cs.hlsl` (9-tap tent filter, additive blend)
- Per-stage tint weights with UE5 defaults: `stageTints[6]` array in BloomSettings. Each upsample stage multiplies contribution by `stageTints[lowerIdx]` before compositing. @refScene.cpp:426-437
- Soft threshold pattern: `bloomAmount = saturate((totalLuminance - threshold) * 0.5)` for smooth bloom onset. @refBloomSetupCommon.ush
- **CRITICAL FIX**: Compute shader entry point mismatch: `CSMain` → `main` (DXC default). Fixed bloom_upsample_cs.hlsl entry point to match other compute shaders.
- **CRITICAL FIX**: SM5 guard — bloom compute shaders now crash on DX11/SM5 (bindless-only). Added compilation guards to prevent dispatch on non-bindless backends; fallback to old single-mip bloom (if available) or skip bloom entirely.
- Full compute pipeline: threshold → downsample[0..5] → upsample[5..0] → tonemap composite. 12 total dispatches per frame. Integrated into scene_renderer.cpp render graph.

**PBR BRDF + Tonemap** (delegated to phase3a-3.27.0):
- Multiscatter energy compensation via Kulla-Conty & Turquin 2018: Ems/Favg polynomial fit in deferred_lighting_ps.hlsl and mesh_ps.hlsl. Compensates for missing light bounces between rough surfaces.
- Analytic GGXEnergyLookup fit: precomputed EnvBRDF as `float2(Ems, Favg)` per (roughness, NdotV). Used in both deferred (IBL) and forward (direct) lighting.
- Film tonemap (ACES curve) with 5 artist controls: FilmSlope, FilmToe, FilmShoulder, FilmBlackClip, FilmWhiteClip. All passed via PerFrameCB and consumed by film_tonemap.hlsli.

**Debug infrastructure** (comprehensive logging):
- Added `UZ_LOG(Shadow, Info, ...)` statements to 18 render passes: GTAO, Bloom (threshold/downsample/upsample), Auto-Exposure, IBL (irradiance/prefilter/BRDF), Transmittance LUT, Sky Cubemap, HiZ Pyramid, GBuffer, Depth Prepass, Shadow Cascade, Deferred Lighting, Sky Atmosphere, Height Fog, Velocity, PBR Forward, Debug View, GBuffer Debug. Logs CB allocation offsets, dispatch grid dims, resource bindings.
- C++/HLSL root constants alignment audit: 20 compute/graphics shaders checked — all alignments verified (8/16/32-byte boundaries for D3D12 root constants). No mismatches found after GTAO fix.

### Shadow Quality Improvements (phase0-0.9.0, §5)

- **PCF adaptive radius per cascade** — Added `GetPCFRadiusTexels(cascadeIdx)` in shadow_common.hlsli returning [2.0, 3.0, 4.0, 6.0] texels per cascade 0-3. Radius increases with distance (coarser texels). @refShadowRendering.cpp:1932-1937 ComputeTransitionSize
- Shadow bias per cascade already correctly implemented (verified in shadow_cascade.cpp:308-315). Bias formula matches UE5 exactly: `constantBias = csmDepthBias / depthRange`, then `lerp(constantBias, constantBias * worldUnitsPerTexel, cascadeBiasDistribution)`. @refShadowRendering.cpp:1844-1846
- Shadow sharpen + overblur correction active in shadow_common.hlsli:380,389. Sharpen: `saturate((shadow - 0.5) * 1.0 + 0.5)`. Overblur: `shadow = shadow * shadow`. @refShadowProjectionPixelShader.usf:375,385

### Distance Field Shadows (phase0-0.6.0, §6) — Production Ready

- Scene renderer integration complete: `CreateDfsResources()` allocates AO + directional shadow output textures on demand. `RenderDFS()` executes on demand (currently controlled by `RenderDFSIfEnabled()` check).
- SDF baker (sdf_baker.cpp): spatial hash triangle lookup, pseudo-normal sign calculation, 8-bit quantization, sparse brick extraction.
- DFS GPU pipeline: `dfs_cull_objects_cs.hlsl` (AABB frustum test), `dfs_cone_trace_cs.hlsl` (cone marching with configurable quality: 20/32/64 steps), `dfs_shadow_apply_cs.hlsl` (output to shadow texture).
- **CRITICAL FIX**: IndirectArgs buffer missing `D3D12_BUFFER_UAV_FLAG_RAW` flag. Changed from `RWByteAddressBuffer` to `RWStructuredBuffer<uint>` for correct D3D12 semantics. UAV descriptor now specifies RAW flag in creation.
- **CRITICAL FIX**: `CreateDfsResources()` called unconditionally in render graph. Now checks `m_dfsEnabled` flag before allocating. Saves GPU memory when DFS inactive.
- Band boundary handling for sparse brick atlas: clamping at edges prevents wrap-around sampling artifacts.
- Bilateral upsample shader (HLSL): 4-sample tent filter with depth weighting. @refShadowFactorsUpsampleCommon.ush

## [0.2.31] — 2026-03-17

### DX11 and Vulkan Full Visual Parity with DX12

All three RHI backends (DX12, DX11, Vulkan) now render identically — verified with PBR test demo side-by-side screenshots. Zero D3D11 Debug Layer errors. Zero Vulkan validation errors.

**DX11 critical fixes (8 bugs)**:
- **CB ring write-combined memory read-back** — DYNAMIC buffers use WC memory (write-only). `SetRootCBV` read from WC `alloc.cpuPtr` causing all matrices to arrive as zeros. Fix: CPU-side shadow copy for ConstantBuffer-usage DYNAMIC buffers.
- **Stale SRV/UAV bindings** — DX11 immediate context leaked state between render graph passes and compute dispatches. Fix: clear all SRV/UAV bindings in `Begin()`, `SetPipeline(compute)`, and `BeginRenderPass()`. Matches UE5's `FD3D11DynamicRHI::CommitRenderTargets`.
- **Buffer UAV creation** — non-structured buffers (auto-exposure) now use `D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS` + `DXGI_FORMAT_R32_TYPELESS` + `D3D11_BUFFER_UAV_FLAG_RAW`.
- **Depth SRV/DSV conflict** — SRV clearing in `BeginRenderPass()` before `OMSetRenderTargets` prevents resource hazards.
- **Tonemap SM5 path** — missing `SetFragmentTexture(0, m_hdrSceneColor)` for non-bindless path.
- **Sky atmosphere transmittance LUT** — SM5 path never bound LUT at t1. Added `SetFragmentTexture(1, transmittanceLutTexture)`.
- **`SetFragmentSampler` implementation** — was no-op default in `IRHICommandList`. Implemented DX11 override using `PSSetSamplers`/`CSSetSamplers`. Fixed stats overlay font rendering.
- **`MapBuffer` scope** — shadow copy restricted to ConstantBuffer usage only. Vertex/index buffers use direct `MAP_WRITE_DISCARD`.

**Auto-exposure buffer fix**:
- Histogram and exposure buffers changed from `RWByteAddressBuffer` to `RWStructuredBuffer<uint>`/`RWStructuredBuffer<float>`. C++ buffers use `StructuredBuffer` flag with correct stride. Eliminates D3D11 RAW buffer mismatch errors.

### Pass Migration to Shader Parameter Structs (7.1-7.3)

All render passes now use `ShaderParameterBinder::Bind()` driven by metadata:
- **GBuffer pass** — `GBufferPerObjectParams` (byte-identical to `PerObjectCB`, appends t0-t4 material textures). Handles both per-frame CB via `PerFrameParams::BindCB()` and per-object CB+textures via `Bind()`.
- **Shadow pass** — `ShadowCascadeParams` (viewProj, 256B) and `ShadowPerObjectParams` (byte-identical to `PerObjectCB`, no textures — depth-only). Both use `BindCB()`.
- **PBR forward pass** — reuses `GBufferPerObjectParams` (same CB layout + material textures).
- Legacy fallback preserved: passes check `cbRingAllocator != nullptr` to choose binder vs manual path.

### UE5 Parity Cleanup (phase0-0.7.0 — 34/43 items)

- Removed analytical SH fallback with invented values, compensation factors, minimum clamps
- Fixed light attenuation window `(d/r)^2` to `(d/r)^4` per UE5
- Fixed k-remapping from `alpha/2` to `(roughness+1)^2/8` per Karis 2013
- Fixed GROUND_ALBEDO, NUM_STEPS_VIEW, LinearToSrgb coefficient per UE5 source
- Deleted 12 orphaned files (6 pairs of .h/.cpp)
- Converted all `assert()` to `UZ_CHECK` (4 files)
- Fixed Y-up documentation in quaternion.h and transform.h

### Static Shadow Cache — Two-Pass Rendering (phase0-0.4.0)

- `EMobility` enum (Static, Stationary, Movable) in `draw_list.h`
- Draw commands classified by mobility in `SyncWorldToDrawList()`
- Shadow pass filters by `cacheMode`: StaticPrimitivesOnly / MovablePrimitivesOnly / Uncached
- Separate `m_staticShadowAtlas` texture with conditional re-render
- Compositing via `ShadowDataCB::useShadowCache` + `staticShadowAtlasIndex`

### Distance Field Shadows — GPU Pipeline (phase0-0.6.0)

- SDF baker (`sdf_baker.h/cpp`): spatial hash triangle lookup, point-triangle distance, pseudo-normal sign, 8-bit quantization, sparse brick extraction
- Cone tracing compute shader (`dfs_cone_trace_cs.hlsl`): UE5-parity algorithm with quality levels (20/32/64 steps), self-shadow bias, adaptive step size
- Object culling compute shader (`dfs_cull_objects_cs.hlsl`): convex hull frustum test, AABB culling
- SDF sampling functions (`dfs_common.hlsli`): sparse brick atlas sampling via indirection table

---

## [0.2.30] — 2026-03-16

### New System: UE5-Style Shader Parameter Macros

Introduces a metadata-driven shader parameter system modelled on UE5's
`BEGIN_SHADER_PARAMETER_STRUCT` / `SHADER_PARAMETER` pattern. Eliminates per-pass
boilerplate for constant buffer upload and resource binding.

**Core types** (`runtime/engine/renderer/core/shader_parameters.h`):
- `ShaderParamType` enum: `Scalar`, `Texture2D`, `TextureCube`, `Sampler`, `UAV`.
- `ShaderParamMember` struct: name, type, byte offset, size, HLSL register slot.
- `ShaderParameterMetadata` struct: member array, member count, `cbSize`, resource counts.
- `AlignCBSize()` — constexpr round-up to 256-byte D3D12 CB alignment boundary.

**Declaration macros** (`runtime/engine/renderer/core/shader_parameter_macros.h`):
- `BEGIN_SHADER_PARAMETER_STRUCT(Name)` — opens `struct alignas(16) Name`.
- `SHADER_PARAMETER(Type, Name)` — scalar member copied into the constant buffer.
- `SHADER_PARAMETER_ARRAY(Type, Name, Count)` — scalar array (also used for padding).
- `SHADER_PARAMETER_SCALAR_END()` — documentation boundary marker between scalar and resource sections.
- `SHADER_PARAMETER_TEXTURE(HlslType, Name, Slot)` — `RHITextureHandle` bound at `register(tSlot)` on SM5.
- `SHADER_PARAMETER_SAMPLER(Name, Slot)` — `RHISamplerHandle` bound at `register(sSlot)`.
- `SHADER_PARAMETER_UAV(Name, Slot)` — `RHITextureHandle` bound at `register(uSlot)`.
- `END_SHADER_PARAMETER_STRUCT()` — closes struct, declares `GetMetadata()`.
- Metadata definition macros: `BEGIN_SHADER_PARAMETER_METADATA` / `METADATA_SCALAR` / `METADATA_SCALAR_ARRAY` / `METADATA_TEXTURE2D` / `METADATA_TEXTURE_CUBE` / `METADATA_SAMPLER` / `METADATA_UAV` / `END_SHADER_PARAMETER_METADATA`.
  `END_SHADER_PARAMETER_METADATA` includes a `static_assert(cbSize % 256 == 0)`.

**Binder** (`runtime/engine/renderer/core/shader_parameter_binder.h/.cpp`):
- `ShaderParameterBinder::BindCB()` — suballocates from the CB ring, copies scalar data (exactly `cbSize` bytes from offset 0), binds root CBV. Returns `CBAllocation`.
- `ShaderParameterBinder::BindResources()` — iterates metadata; on SM5 calls `SetFragmentTexture(slot)` / `SetFragmentSampler(slot)` per resource member; on SM6.6 bindless, texture binding is skipped (shaders fetch via `ResourceDescriptorHeap[index]`).
- `ShaderParameterBinder::Bind()` — convenience: `BindCB` + `BindResources` in one call.

**HLSL generation** (`runtime/engine/renderer/core/shader_parameter_hlsl.h/.cpp`):
- `GenerateHLSL(meta, structName, cbRegister)` — converts `ShaderParameterMetadata` into an HLSL `cbuffer` declaration and `Texture2D`/`TextureCube`/`SamplerState` resource declarations. Used for manual validation that C++ layout matches hand-written HLSL. Not injected at compile time.

### Migrated Parameter Structs

Three existing constant buffer definitions migrated to the new system:

- **`PerFrameParams`** (`per_frame_params.h/.cpp`) — 512-byte scalar section byte-identical to `PerFrameCB`, plus `RHITextureHandle` members for IBL textures at t5 (irradiance), t6 (prefiltered specular), t7 (BRDF LUT). Layout verified with `static_assert(offsetof(PerFrameParams, irradianceCubemap) == 512)`.
- **`ShadowDataParams`** (`shadow_data_params.h/.cpp`) — 768-byte scalar section byte-identical to `ShadowDataCB`, plus shadow atlas handles at t3 (dynamic atlas), t8 (static shadow cache), t9 (DFS output). Layout verified with `static_assert(offsetof(ShadowDataParams, shadowAtlas) == 768)`.
- **`LightingParams`** (`lighting_params.h/.cpp`) — 3328-byte scalar section byte-identical to `LightingCB`, no resource handles currently. Layout verified with `static_assert(sizeof(LightingParams) == sizeof(LightingCB))`.

All three use `ShaderParameterBinder::Bind()` in their respective passes. Legacy CB structs coexist during migration; passes that have not adopted the binder yet continue using manual `SetRootCBV` + `SetFragmentTexture` calls.

### RHI Backend Fixes for SM5 Compute Parity (4 Critical Bugs)

**DX11 backend** (`runtime/engine/rhi/dx11/`):
- `SetDescriptorTable` now correctly dispatches `UAV_Table` — previously always routed to the SRV binding path, causing compute UAV writes to silently no-op.
- Added `FindTextureUAVByPoolIndex()` and `FindBufferUAVByPoolIndex()` — allows the command list to resolve UAV pool indices to `ID3D11UnorderedAccessView*` for compute dispatch.

**Vulkan backend** (`runtime/engine/rhi/vulkan/`):
- `Dispatch()` now calls `FlushDescriptorSet()` before `vkCmdDispatch` — compute shaders were reading stale descriptor sets from the previous graphics pass.
- `FlushDescriptorSet()` now selects the correct bind point (`VK_PIPELINE_BIND_POINT_GRAPHICS` vs `VK_PIPELINE_BIND_POINT_COMPUTE`) based on whether the active pipeline is graphics or compute. Previously always bound to the graphics point, silently breaking compute dispatches.
- Added `STORAGE_IMAGE` bindings at descriptor set slots 48–63 and `-fvk-u-shift 48 0` SPIR-V compile flag so UAV (`RWTexture2D`) registers map correctly to the Vulkan descriptor layout.

### Vulkan Per-Slice Image Views for Cubemap Rendering

- `BeginRenderPass()` now creates a per-face `VkImageView` (2D view of one array layer) when the render target is a cubemap texture. Previously used the whole-cubemap image view, causing Vulkan validation errors and rendering all six faces into face 0.

### Static Shadow Cache — Two-Pass Rendering (phase0-0.4.0 infrastructure)

`ShadowCacheManager` infrastructure completed:
- Two-pass rendering protocol: Pass 1 renders only dirty static geometry into the static atlas; Pass 2 composites static + dynamic shadows. Dynamic geometry always renders into the dynamic atlas.
- Per-cascade dirty flags invalidated on first render, geometry move, or material change.
- VRAM budget tracking (150MB default) with LRU eviction of stale cascade slots.

### Distance Field Shadows — Infrastructure (phase0-0.6.0)

Foundation types and atlas manager for future DFS cone tracing:
- `DistanceFieldShadowConfig` — max trace distance, cone angle, step count, fade range.
- `SdfBrick` — 8×8×8 signed distance field brick (fp16 values).
- `MeshSdfAsset` — per-mesh SDF baked from triangle geometry via GPU ray marching.
- `SdfAtlasManager` — brick atlas with GPU buffer, handles mesh SDF upload, culls objects outside the view frustum before trace.
- `dfsEnabled` / `dfsOutputSrvIndex` fields added to `ShadowDataCB` and `ShadowDataParams`; DFS output texture bound at t9 in SM5 path.

### Documentation

- `docs/guides/shader-parameter-binding-guide.md` — Complete rewrite. Now documents the actual implemented macro system, metadata definition workflow, `ShaderParameterBinder::Bind()` usage, SM5 vs SM6.6 binding differences, alignment rules, and all three migrated structs.
- `docs/guides/shader-parameter-migration-guide.md` — New guide: step-by-step instructions for migrating an existing manual CB + resource binding to the new parameter struct system.

---

## [0.2.29] — 2026-03-16

### New Feature: Headless Mode (`--headless`)
- **`--headless` CLI flag**: Creates the window but keeps it hidden (HWND exists for DX12 swapchain, but no GUI appears). Rendering works normally — useful for CI, automated testing, and LLM-driven visual verification.
- **Auto-headless**: `--screenshot-stdout` implies `--headless` automatically.

### New Feature: Base64 Screenshot Output (`--screenshot-stdout`)
- **`--screenshot-stdout` CLI flag**: Captures the backbuffer as a PNG, base64-encodes it in memory, and prints to stdout with `SCREENSHOT_BASE64:` prefix. Eliminates filesystem round-trip for LLM/AI visual verification workflows. Implies `--headless` + suppresses all console logs (stdout is clean).
- **`ScreenshotCapture::ToBase64PNG()`**: New public API — returns PNG as base64 string. Uses `stbi_write_png_to_func` for in-memory encoding + RFC 4648 base64 encoder. Zero disk I/O.
- **`SceneRenderer::RequestScreenshotBase64()`**: Triggers base64 capture instead of file save.

### Bug Fix: Screenshot Readback Buffer Leak
- **D3D12MA assertion fix**: `ScreenshotCapture::Shutdown()` was never called in `SceneRenderer::Shutdown()` — the readback buffer (3.7MB) leaked every session. Now properly destroyed.
- **IBL texture leak fix**: `irradianceTex` and `prefilteredMipTextures[0]` were local variables in `IBLBaker::BakeEnvironment()` that leaked on every sky re-bake. Now tracked as `m_irradianceTexture`/`m_prefilteredTexture` and destroyed in `Shutdown()`.
- **WaitIdle before shutdown**: `SceneRenderer::Shutdown()` now calls `WaitIdle()` before destroying any GPU resources — prevents D3D12MA assertion when GPU is still processing the last frame.

### Stats Overlay Fix
- **Vertex/triangle count**: `SetVertexCount()`/`SetTriangleCount()` were never called — overlay always showed 0. Now computed per-frame from draw list.

### UE5 Parity Enforcement
- **CLAUDE.md refactored**: 846 → ~120 lines. Only TOP 5 RULES visible. Secondary rules moved to `.claude/rules/` (10 files).
- **Agent checklists**: shader-engineer, cpp-core-expert, render-graph-engineer, rhi-engineer now have mandatory reference parity pre-flight checklists with `@ref<file>:<line>` citation template.
- **Codebase audit task**: `phase0-0.7.0-codebase-cleanup-ue5-parity` — 44 items covering all legacy code, dead systems, and UE5 incompatibilities.

## [0.2.28] — 2026-03-16

### Critical Bug Fix: Shadows Restored
- **Reversed-Z frustum corners**: Fixed `ComputeFrustumCorners()` in `shadow_cascade.cpp` — NDC Z values were using standard-Z convention (near=0, far=1) instead of reversed-Z (near=1, far=0). This caused the cascade bounding sphere to be ~90x too large (114,000cm vs 1,274cm), making shadows invisible since geometry occupied <0.01% of the shadow atlas. **One-line fix** that restores all shadow projection.

### Memory Leak Fix
- **TransmittanceLUT leak**: Fixed 128KB D3D12MA leak at shutdown — `m_transmittanceLut` (256×64 RGBA16F) was never destroyed in `SceneRenderer::Shutdown()`. Added `DestroyTexture` call. Also added live allocation stats logging to `DX12Device::~DX12Device()` for future leak detection.

### Shadow System Improvements
- **ShadowDataCB HLSL packing fix**: Changed `float cascadeSplits[8]` to `float4 cascadeSplitsPacked[2]` in HLSL cbuffers — HLSL pads `float[8]` to 128 bytes (16B/element) while C++ packs to 32 bytes. Added accessor macros `cascadeSplits(i)` for transparent indexing.
- **Shadow PSO depth func**: Changed from `Less` to `LessEqual` (reference parity, `ShadowDepthRendering.cpp:760`) — eliminates self-shadow acne on coplanar surfaces.
- **Shadow depth constants**: Added `kShadowDepthClear`, `kShadowDepthNear`, `kShadowDepthFar` to `rhi_types.h`. Replaced magic `1.0f` in shadow_pass.cpp with named constants.
- **Default clearDepth**: Changed `RHIRenderPassDepthAttachment::clearDepth` default from `1.0f` to `kDepthFar` (0.0) — new passes default to reversed-Z; shadow passes explicitly override.

### Multi-Mesh Rendering Fix
- **Per-draw mesh binding**: Shadow pass and GBuffer pass now bind vertex/index buffers per draw command using `DrawCommand::meshId`, instead of using a single mesh for all draws. Fixes rendering when scene contains multiple mesh types (e.g., sphere + cube ground).
- **Instancing guard**: GPU instancing automatically disabled when multiple mesh types are loaded (`m_meshes.size() > 1`) — instanced path only supports homogeneous batches.

### New Debug Infrastructure
- **Buffer Visualization Grid** (`--shadow-debug 1`): 3x3 on-screen grid showing Albedo, Normal, Raw Atlas, Shadow, LIT, Depth, Cascade, AO, Roughness simultaneously. Permanent debug tool in `shadow_debug.hlsli`.
- **RenderDoc integration** (`renderdoc_capture.h/cpp`): In-application API via `renderdoc_app.h`. F12 triggers capture, `--renderdoc-capture <N>` auto-captures frame N. DLL loaded only when injected by RenderDoc GUI or `renderdoccmd`.
- **Sphere mesh generator**: `GenerateSphereMesh()` and `GeneratePlaneMesh()` in `mesh.h/cpp` — UV sphere (Songho pattern, Z-up) and tiled ground plane.

### New Engine Features
- **Far shadow cascades**: `ShadowConfig::farShadowCascadeCount/farShadowDistance` — extends shadow coverage from 400m to 3km with additional cascade tiers. Dynamic NxM atlas grid (2x2→4x2). Default disabled (zero cost).
- **Static shadow cache**: `ShadowCacheManager` with per-cascade invalidation, VRAM budget tracking (150MB default), and compositing shader for static+dynamic shadow atlases. Infrastructure only — two-pass rendering deferred.
- **Distance field shadow types**: `DistanceFieldShadowConfig`, `SdfBrick`, `MeshSdfAsset`, `SdfAtlasManager` — foundation types and atlas manager for future DFS cone tracing.
- **OrthographicReversedZ()**: New `Mat4::OrthographicReversedZ()` for main-view orthographic projection (UE5 `FReversedZOrthoMatrix` parity).

### Documentation
- **PCSS soft shadows doc**: `docs/engine/12-rendering/28-pcss-soft-shadows.md` — 3-stage algorithm, parameters, performance analysis.
- **Reversed-Z convention table**: Added to `docs/architecture/coding-standards.md` — main view vs shadow maps depth constants.
- **UE5 reversed-Z shadow analysis**: `docs/analysis/rendering/ue5-reversed-z-shadow-maps.md` with adaptation status table.
- **GPU debugging rules**: Updated `.claude/rules/gpu-debugging.md` with Buffer Viz Grid, RenderDoc CLI, and headless Python analysis patterns.
- **RenderDoc thirdparty**: Added to `runtime/thirdparty/README.md` with integration documentation.

## [0.2.27] — 2026-03-16

### Shadow Cascade Fixes
- **Z-up light vector**: `ChooseLightUp()` fallback corrected from Y-axis `(0,1,0)` to X-forward `(1,0,0)` — fixes shadow flickering when sun is near vertical in Z-up coordinate system
- **Texel snapping simplified**: Shadow VP matrix snapping now operates directly on the combined view-projection matrix in NDC space instead of transforming origin back and forth — cleaner math, same result
- **LookAt documentation**: Added detailed comments explaining why `XMMatrixLookAtLH` (Y-up assumption) works correctly with Z-up world coordinates

### SH Ambient & SkyLight Tuning (UE5 Parity)
- **SkyLight intensity**: Reduced from 1.5 → 1.0 (exact UE5 Basic Scene value) — removed the 50% compensation hack for analytical SH
- **SH fallback ratios**: Sky hemisphere color ratios changed from `(0.7, 0.8, 1.0)` to `(0.90, 0.92, 1.0)` — warmer, matching UE5 measured cubemap SH which includes sun contribution and horizon glow
- **Ground hemisphere**: Reduced from `(0.10, 0.09, 0.08)` to `(0.05, 0.04, 0.03)` — closer to UE5's `lowerHemisphereIsBlack=true` behavior

### IBL Build & Test Fixes
- **Build system**: Added `engine/renderer/passes/ibl` directory to Zig build (include paths + source compilation)
- **EquirectToCubemap**: Removed compile-time `UZ_CHECK` assertions for cubemap size — size validation is handled at a higher level
- **SH test sentinel**: Tests updated to expect `coeffs[6].w = 1.0` (UE5 sentinel value) instead of 0.0, for both black cubemap and null faces cases

### Documentation & Standards
- **CLAUDE.md**: Added "UE5 Exact Parity — Zero Creativity Policy" section enforcing exact math replication from UE5 source
- **AGENTS.md**: UE5 exact parity elevated to rule #1 (highest priority) in critical rules
- **Sky atmosphere doc**: New `docs/engine/12-rendering/27-sky-atmosphere.md` — Hillaire 2020 model, Rayleigh/Mie scattering, transmittance LUT, SkyAtmosphereComponent
- **Cascade shadow analysis**: Root cause analysis of shadow flickering bug (Y-up vs Z-up light vector)
- **Rendering README**: Added sky atmosphere entry and quick navigation guide

### Task Housekeeping
- Archived completed tasks: `phase3a-3.7.0-ibl-environment`, `phase3a-3.8.0-sky-atmosphere`

### Temporary
- **Instanced shadows disabled**: Shadow pass instancing temporarily commented out while debugging shadow flickering — falls back to per-object CB path

## [0.2.26] — 2026-03-15

### GPU Instancing
- **InstanceBatcher**: Groups sorted draw list commands by (meshId, material SRV indices) into batched `DrawIndexedInstanced` calls — O(N) linear scan over sorted commands
- **InstanceData**: 128-byte per-instance GPU struct (world + normal matrices) uploaded to a persistent `StructuredBuffer` on Upload heap
- **Shader variants**: `UZ_INSTANCED` define for `mesh_vs.hlsl` and `shadow_depth_vs.hlsl` — reads per-instance transforms from `ResourceDescriptorHeap[instanceBufferIndex]` via `SV_InstanceID` (SM6.6 bindless only)
- **GBuffer + Shadow passes**: Both passes support instanced code path via `batchData`/`batchCount` in pass descriptors, with automatic fallback to per-object CB path for SM5
- **Pipeline**: `GetGBufferInstanced()` and `GetShadowDepthInstanced()` in PipelineLibrary, `CreateShaderWithDefines()` in ShaderManager
- **Performance**: 1001 objects → 2 draw calls, 310 FPS @ 3.22ms on RTX 4090
- **Demo**: `demos/instancing_test/` — 1000 identical cubes in 10×10×10 grid

### UE5 Component Parity (phase0-0.1.0)
- **SkyLightComponent** (new): intensity, lightColor, indirectLightingIntensity, sourceType (CapturedScene/SpecifiedCubemap), cubemapResolution, lowerHemisphereIsBlack, minOcclusion, occlusionTint, occlusionExponent — wired into PerFrameCB + deferred lighting shader
- **LightComponent base**: useTemperature, temperature (6500K), specularScale, diffuseScale, volumetricScatteringIntensity, indirectLightingIntensity
- **DirectionalLightComponent**: atmosphereSunLight link, atmosphereSunLightIndex, lightSourceAngle, shadowCascades, cascadeDistributionExponent, cascadeTransitionFraction, dynamicShadowDistance, contactShadowLength, atmosphereSunDiskColorScale
- **PointLightComponent**: useInverseSquaredFalloff, lightFalloffExponent, sourceRadius, softSourceRadius, sourceLength
- **SpotLightComponent**: now inherits from PointLightComponent (UE5 pattern), angles stored in degrees (innerAngleDeg/outerAngleDeg), converted to radians in proxy sync
- **HeightFogComponent**: UE5 defaults (color=white, heightFalloff=0.2, maxOpacity=1.0), second fog layer, directional inscattering (color/exponent/startDistance), fogCutoffDistance, enableVolumetricFog placeholder
- **SkyAtmosphereComponent**: artistic multipliers (rayleigh/mie/absorption scales), multiScatteringFactor, skyLuminanceFactor, heightFogContribution, deprecated skyLight fields
- **SceneComponent**: Mobility enum (Static/Stationary/Movable), hiddenInGame
- **PrimitiveComponent**: minDrawDistance, maxDrawDistance, receivesDecals, renderInMainPass, renderInDepthPass

### Sun-Atmosphere Link
- DirectionalLight with `atmosphereSunLight=true` now drives sky atmosphere sun direction/color/intensity/angular radius — SkyAtmosphereComponent sun fields are fallback defaults

### GPU Data & Shader Updates
- **PointLightData**: expanded 32→48 bytes (3×float4) with sourceRadius, falloffExponent, specularScale, diffuseScale, flags
- **HeightFogCB**: expanded with second fog layer, directional inscattering, cutoff distance
- **Fog shader**: rewritten with Z-up height fix, two fog layers, directional inscattering (sun-facing glow)
- **Deferred lighting**: point light attenuation supports inverse square toggle + custom falloff exponent
- **Kelvin-to-RGB**: `color_utils.h` utility, wired into directional + point light gathering
- **RenderProxy**: light struct extended with temperature, specularScale, diffuseScale, sourceRadius, falloffExponent, useInverseSquaredFalloff

### Fixes
- **Screenshot alpha**: force alpha=255 in readback buffer — backbuffer alpha was undefined, causing transparent PNG output
- **Screenshot frame**: default capture frame increased from 5 to 400 to ensure IBL bake completes before capture

## [0.2.24] — 2026-03-15

### Sky Atmosphere Transmittance LUT & IBL Integration
- **Transmittance LUT**: Precomputed 256×64 RGBA16F lookup table using Bruneton 2017 parameterization — replaces per-pixel secondary ray marching with single texture lookup for 2-3x faster atmosphere rendering
- **Sky → IBL Pipeline**: SkyAtmospherePass now captures 64×64×6 cubemap via compute shader, fed into IBL baker for irradiance (32×32) and prefiltered specular (128×128, 5 mips) — sky environment now drives split-sum IBL ambient lighting
- **Deferred lighting update**: `lighting_pass.hlsl` replaced constant ambient with split-sum IBL (diffuse via irradiance, specular via prefiltered + BRDF LUT)
- **Ground contribution**: Sky shader now adds Lambertian ground reflection when view ray hits the planet surface (replaces black bottom hemisphere)
- **Z-up shader corrections**: All atmosphere shaders corrected from Y-up to Z-up (SkyAtmospherePass, TransmittanceLutPass, SkyCubemapCapturePass, HeightFogPass)
- **Sampler naming standardization**: All HLSL shaders now use `linearSampler` (s0) and `shadowSampler` (s1) consistently across bindless and SM5 paths
- **ECS directory reorganization**: Components reorganized into semantic domains — `ecs/core/` (entity, world), `ecs/scene/` (camera, primitive), `ecs/lighting/` (lights), `ecs/mesh/` (meshes), `ecs/environment/` (sky, fog), `ecs/metadata/` (name), `ecs/update/` (tick system), `ecs/loaders/` (scene loading)
- **Render passes reorganization**: Passes reorganized by function — `passes/geometry/` (gbuffer, pbr), `passes/lighting/` (lighting, shadow), `passes/atmosphere/` (sky_atmosphere, transmittance_lut, sky_cubemap_capture, height_fog), `passes/postprocess/` (postprocess, velocity), `passes/debug/` (debug_view, gbuffer_debug)
- **Screenshot capture fix**: RequestScreenshot now called before RenderFrame instead of after Present — capture happens in the same frame as render, eliminating one-frame lag

## [0.2.23] — 2026-03-15

### Z-up Coordinate System Fixes & PBR Showcase Scene
- **Camera ECS transform fix**: `CameraComponent` now inherits position/rotation from the entity's `SceneComponent` in scene_loader — previously the camera was always at origin
- **Camera lookTarget**: `uz.camera.look_at()` now stores world-space target directly instead of computing a Y-up quaternion — eliminates Z-up ↔ Y-up conversion bugs
- **Lua input connected**: `LuaSetInputSystem()` wired in `uzengine.cpp` — `uz.input.*` API was silently returning false for all queries
- **Mouse/WASD axis fix**: Lua camera controller corrected for Z-up (yaw sign, pitch sign, right vector direction) to match `FPSCameraController`
- **Builtin mesh primitives**: `SceneRenderer::LoadMesh()` supports `"builtin:cube"` and `"builtin:sphere"` asset paths — generates meshes at runtime without binary `.uzmesh` files
- **cube_rotator.lua rewrite**: accumulates angle internally instead of read/modify/write Euler cycle that caused texture "jumping" from quat↔euler Y-up conversion drift
- **CB padding fix**: `SkyAtmosphereCB` (80→256 bytes) and `HeightFogCB` (48→256 bytes) padding corrected to satisfy D3D12 constant buffer alignment
- **PBR showcase scene**: `pbr_showcase.uzscene` updated with Z-up coordinates, builtin meshes, 3 material types (stone, metal, concrete), 4 point lights, Lua controller
- **Project default scene**: `demo.uzproject` now loads `pbr_showcase.uzscene`

### Sky Atmosphere & Height Fog Passes
- **SkyAtmospherePass**: fullscreen Bruneton-style single-scatter atmosphere with Rayleigh/Mie, sun disk, configurable planet radius
- **HeightFogPass**: exponential height fog with configurable density, height falloff, base height, max opacity, start distance
- **ECS components**: `SkyAtmosphereComponent` (50+ params) and `HeightFogComponent` for scene-driven environment
- **Pipeline integration**: both passes registered in `PipelineLibrary`, wired into render graph after lighting

## [0.2.22] — 2026-03-15

### Lua `uz.debug` API — Debug View Mode Scripting
- **`uz.debug.set_view_mode(mode_name)`**: sets the active debug view mode from Lua (delegates to `SceneRenderer::SetDebugViewMode`); accepts same names as `--view-mode` CLI flag (e.g. `"albedo"`, `"depth"`, `"overview"`, `"lit"`)
- **`uz.debug.get_view_mode() → string`**: returns the current debug view mode name
- **Registration**: `LuaRegisterRendererAPI()` now creates the `uz.debug` table alongside `uz.renderer`, `uz.mesh`, `uz.texture`
- `uz.renderer.set_debug_view` / `uz.renderer.get_debug_view` still present for backwards compatibility; `uz.debug.*` is the canonical namespace per spec

## [0.2.21] — 2026-03-15

### Debug View Modes — G-Buffer Channel Visualization
- **DebugViewMode enum** (`engine/renderer/core/debug_view_mode.h`): 13 modes — Lit, Albedo, WorldNormal, Metallic, Roughness, AO, Depth, WorldPosition, Lighting, Shadows, Wireframe, Unlit, Overview
- **DebugViewPass** (`engine/renderer/passes/`): fullscreen triangle pass reading G-Buffer via bindless SRVs; outputs false-colour diagnostic visualization per mode
- **Overview mode**: 4×2 composite grid showing all 8 diagnostic channels simultaneously with thin black borders
- **DX12 root constants fix**: `Num32BitValues` bumped from 4→8 in `dx12_pso_creation.cpp` — DebugViewPass pushes 5 root constants (mode + 4 SRV indices), 4 caused a segfault on frame 0
- **Keyboard shortcuts**: F1–F11 cycle through modes at runtime via `ApplyDebugViewKeyboard()`
- **CLI**: `--view-mode <name>` sets initial mode (e.g. `--view-mode albedo`, `--view-mode overview`)

### `--all-modes` — Single-Run Full Mode Capture
- **`DemoApplication::Run()`**: all-modes sequencer built into the render loop — iterates all 13 `DebugViewMode` values in a single GPU session (no window reopen per mode)
- **Flow**: after `SetReady()`, advances mode every `screenshotFrame` frames, captures screenshot, composes labeled 4-column grid PNG, cleans up temp files automatically
- **CLI**: `--all-modes <filename.png>` — captures all debug view modes and saves composed grid
- **`ScreenshotCapture` init**: now created when `allModesOutputPath` is set, even without `--screenshot`
- **`PBRTestConfig::allModesOutputPath`** field propagated from `DemoConfig` through `PBRTestDemo` constructor

### Screenshot Directory — Platform-Aware AppData Path
- **`GetScreenshotsDir()`** (static, `demo_application.cpp`): resolves user-scoped output directory per platform:
  - Windows: `%APPDATA%\UzEngine\.screenshots\`
  - Linux: `$XDG_DATA_HOME/UzEngine/.screenshots/` (fallback: `~/.local/share/UzEngine/.screenshots/`)
  - macOS: `~/Library/Application Support/UzEngine/.screenshots/`
- **`ParseCommonArgs()`**: bare filenames in `--screenshot` and `--all-modes` are automatically routed to `GetScreenshotsDir()` — never written to exe/build directory

## [0.2.20] — 2026-03-14

### CompareGrid — Visual Comparison & Diff System
- **CompareGrid** (`editor/screenshot/compare_grid.h/.cpp`): CPU-side image compositing for visual regression testing — composes N screenshots into a single labeled grid image
  - Built-in CP437 8x8 bitmap font (95 ASCII glyphs) with 1px black outline
  - Bilinear scaling, auto grid layout (`cols = ceil(sqrt(N))`), forced column override via `SetColumns()`
  - `AddCellFromPNG()` loads PNGs via stb_image, `SaveToPNG()` writes composed grid
  - Labels with primary + sublabel (RHI backend, SM version, frame number)
- **ImageDiff**: pixel-by-pixel comparison with heatmap visualization
  - `Compare()` returns stats: diffPixels, diffPercentage, PSNR, maxChannelDiff, identical
  - `GenerateHeatmap()` produces color-coded diff (black→red→yellow→white, magenta for size mismatch)
  - `DiffToPNG()` generates 3-panel comparison: [Image A | Image B | Diff Heatmap] with stats
  - Per-channel threshold (default 2) tolerates minor float rounding between RHI backends
- **DemoApplication CLI**:
  - `--compose-grid <output.png> [--cell-size N] <img> <label> <sublabel> ...` — compose grid from PNGs
  - `--diff <output.png> <imgA> <labelA> <imgB> <labelB> [--cell-size N]` — 3-panel diff comparison

### Shader Model 6.8 / 6.9 Support
- **ShaderModel enum** (`rhi_config.h`): added `SM6_8` (work graphs, wave size range) and `SM6_9` (cooperative vectors, inline RT in compute)
- **ShaderModelCompile enum** (`shader_compiler.h`): added `SM6_8`, `SM6_9` with DXC target profiles (`vs_6_8`, `vs_6_9`, etc.)
- **DX12 negotiation** (`dx12_device.cpp`): now probes up to `D3D_SHADER_MODEL_6_9` — driver clamps to hardware max
- **GetActiveShaderModel()** (`rhi_factory.cpp`): new global getter, set at device creation — replaces hardcoded SM6.6 in shader pipeline
- **ApplyBindlessConfig** (`shader_pipeline.cpp`): compiles shaders at device's negotiated SM (not hardcoded SM6.6), with SM6.6 minimum floor for bindless
- **CLI / config**: `--sm 6.8` and `--sm 6.9` parsing in both `rhi_config.cpp` and `demo_application.cpp`

### StatsOverlay — System Info Panel
- **System info capture** (`stats_overlay.cpp`): `CaptureSystemInfo()` reads GPU name, feature level, RHI backend, shader model, VRAM budget, and bindless status from `IRHIDevice` at init
- **Redesigned text panel**: 3 organized sections — SYSTEM (GPU, RHI|SM, FL, VRAM|Bindless, Res), PERFORMANCE (smoothed FPS, frame time, raw FPS), RENDERING (draw calls, vertices, triangles, VRAM used, frame#)
- Color-coded section headers, FPS color (green ≥60, yellow ≥30, red <30), separators between sections

## [0.2.19] — 2026-03-14

### FXC/DXC Shader Parity — DX11 Brightness Fix
- **`math_util.hlsli`** (NEW): centralized compiler-agnostic math utilities — `Pow2/Pow4/Pow5` (multiply chains, no `pow()`), `LinearToSrgb/SrgbToLinear` (exact IEC 61966-2-1 curve). Follows UE5 `MathUtil.ush` / `Common.ush` pattern.
- **Root cause**: FXC (DX11/SM5) compiles `pow(x, 5)` as multiply chain while DXC (DX12/SM6) uses log-mul-exp, producing different Fresnel reflectance on angled surfaces — cubes 25-40 RGB values brighter on DX11.
- **FresnelSchlick / FresnelSchlickRoughness**: replaced `pow(1-cosTheta, 5)` with `Pow5()` in `pbr_common.hlsli`, `ibl.hlsli`, `brdf_lut_cs.hlsl`
- **Gamma correction**: replaced approximate `pow(color, 1/2.2)` with exact sRGB transfer function (`LinearToSrgb()`) in `deferred_lighting_ps.hlsl` and `mesh_ps.hlsl`
- **CBAllocation.sizeBytes**: added allocation size tracking to prevent out-of-bounds reads on DX11 — passes now use `alloc.sizeBytes` instead of hardcoded `sizeof(PerFrameMultiLightCB)`
- **Reverted debug edit**: restored cube PBR textures (normal/roughness/metallic/AO) in `pbr_test_demo.cpp`

## [0.2.18] — 2026-03-14

### DX11 Backend — Full Lighting & Rendering Fix
- **`--backend dx11` CLI flag**: PBR demo now correctly routes backend selection via `ParseCommonArgs` (was ignored due to duplicate arg parser)
- **SetFragmentTexture (DX11)**: implemented `PSSetShaderResources` binding — base class was a no-op, textures were never bound to pixel shader
- **Dynamic scratch constant buffers**: `GetOrCreateScratchCB` now accepts `requiredSize` and recreates buffer if too small (LightingCB needs 2064 bytes, old hardcoded 256)
- **Default samplers (DX11)**: created linear-wrap sampler (s0) and shadow comparison sampler (s1) in `DX11Device::Init`, bound in `DX11CommandList::Begin()` — DX12 uses static samplers in root signature, DX11 needs manual `PSSetSamplers`
- **GBuffer SM5 texture binding**: `gbuffer_pass.cpp` now calls `SetFragmentTexture(t0-t4)` per draw command on non-bindless path — root cause of black scene
- **DrawCommand texture handles**: added `RHITextureHandle` fields (albedo/normal/roughness/metallic/ao) to `DrawCommand` struct for SM5 binding
- **CB Ring increased**: 128KB → 2MB per frame to prevent ring exhaustion during shadow pass
- **Architecture analysis docs**: UE5, Unity, Godot backend compatibility patterns documented in `docs/architecture/`

## [0.2.17] — 2026-03-13

### Phase 100 — RHI Optimization Suite
- **PSO Cache** (`rhi/optimization/`): FNV-1a content hash + Robin Hood open-addressing hash map (215x vs linear scan), disk blob cache, invalidation by shader handle
- **Compute Pipeline**: `CreateComputePipeline()` + `RHIComputePipelineDesc`, `isCompute` flag dispatches to compute root signature in all `SetRoot*`/`SetDescriptorTable` methods
- **Async Compute Queue**: DX12 `D3D12_COMMAND_LIST_TYPE_COMPUTE` with dedicated fence, `SignalComputeQueue()`/`GraphicsWaitForCompute()`/`ComputeWaitForGraphics()`
- **GPU Memory Tracking** (`renderer/memory/`): per-category budgets (Textures/RenderTargets/Buffers/Staging), pressure callbacks, O(1) stats (1556x vs naive iterate)
- **Streaming Pool**: LRU mip-level eviction, `MipResidencyState` enum, per-texture mip tracking
- **Residency Control**: DX12 `MakeResident`/`Evict` abstraction (stubs for future native calls)
- **Defrag Pass**: incremental fragmentation monitoring with configurable byte budget
- **Staging Batch** (`renderer/upload/`): groups small uploads into single copy queue submission with 256-byte alignment
- **Parallel Command Recording** (`rhi/optimization/`): thread-safe acquire/release with mutex, acquisition-order submission
- **GPU-Driven Rendering**: `DrawIndexedIndirect`/`DispatchIndirect` via DX12 `ExecuteIndirect` with command signatures
- **Frame Pacer** (`renderer/core/`): VRR/tearing support, rolling average timing, dropped frame detection
- **Backend Capabilities** (`rhi/optimization/`): feature tier detection (Tier1/2/3), runtime capability queries
- **Compute Shaders**: `mipgen_cs.hlsl` (bilinear downsample 8x8), `gpu_cull_cs.hlsl` (frustum culling 64x1x1)
- **Stat Counters**: `RHI.ComputeQueueSignals`, `GPU.Memory*`, `GPU.Streaming*`, `GPU.DefragPassRuns`, `IO.StagingBatchSubmissions`, `Rendering.DroppedFrames`
- **Benchmark suite**: realistic naive-vs-optimized comparisons with ASCII impact table (all speedups > 1x)
- **File organization**: RHI optimization files moved to `rhi/optimization/` with `rhi_` prefix
- All backends updated (DX12 full, DX11/Vulkan/Metal stubs)
- 2806/2807 tests passing (1 pre-existing shader cross-compile failure)

## [0.2.16] — 2026-03-13

### Engine Simplification — `uzengine.h` umbrella header + DemoApplication refactor
- **`uzengine.h` umbrella header**: single `#include "uzengine.h"` replaces all manual engine includes
  - Platform-conditional RHI backends (`#ifdef _WIN32` for DX12/DX11, `__APPLE__` for Metal, etc.)
  - ECS headers guarded by `#ifdef UZ_HAS_ENTT`
  - Proper include ordering to resolve forward-declaration issues (ShaderCompiler, ShaderCompilerFXC)
- **DemoApplication moved to `demos/common/`**: no longer part of engine — it's demo infrastructure
  - Owns all engine infrastructure: InputSystem, ShaderManager, PipelineLibrary, CBRingAllocator, Camera, FPSCameraController, EditorGrid, StatsOverlay, ScreenshotCapture, RgResourcePool
  - Protected accessors: `GetInput()`, `GetShaderManager()`, `GetPipelines()`, `GetCBRing()`, `GetCamera()`, etc.
  - DemoConfig expanded with screenshot and camera default fields
- **PBR demo simplified**: removed ~1000 lines of boilerplate, only keeps demo-specific code
- **Screenshot capture fixes**:
  - `SetReady()` mechanism — subclass signals when resources are loaded, screenshot waits for ready
  - `CaptureFromPresent()` — new method for capture after render graph (Present → CopySrc → Present)
  - Save moved to `Run()` after `Execute()` — fixes zero-data readback from unsubmitted command list
  - Spurious resize fix — `ClearResizedFlag()` before render loop prevents WM_SIZE double GBuffer creation
- **PipelineLibrary lazy creation**: PSOs created on first use instead of upfront
- **ShaderManager `GetPipeline()`**: returns root pipeline state for subsystem init

## [0.2.15] — 2026-03-13

### Security & Memory Safety Audit — COMPLETE
- **Descriptor heap bounds checking**: UZ_CHECKF assertions in CpuHandle/GpuHandle, out-of-bounds guard in Free()
- **Shader bytecode validation**: 64MB max size check before PSO creation
- **Fence overflow protection**: WaitIdle() detects UINT64_MAX and resets
- **Debug layer rate limiting**: ID3D12InfoQueue configured with message count limit (1024) and suppressed noisy IDs
- **Integer overflow checks**: UINT_MAX validation in buffer SRV/UAV element count calculations
- **ComPtr RAII migration**: Root signature serialization blobs and breadcrumb cmdList2 converted from manual Release() to ComPtr
- **ID3D12InfoQueue GUID**: Added to dx12_guids.h for Zig/MinGW ABI compatibility
- **8 new tests**: DescriptorAllocator overflow, free invalid/OOB, recycle, handle offsets, capacity, count tracking
- **CrashHandler fix**: Fixed double-skip in WalkStack → CaptureRawStackTrace call chain
- **Shader compiler include support**: Added includeDirs to ShaderCompileDesc, DXC CreateDefaultIncludeHandler, FXC D3D_COMPILE_STANDARD_FILE_INCLUDE
- **Documentation**: RAII ownership conventions + shader security docs in docs/engine/14-security/
- All 2648 tests passing

## [0.2.14] — 2026-03-13

### Shader Correctness Audit — COMPLETE
- **Shared HLSL includes**: extracted duplicated PBR/shadow code into reusable headers
  - `pbr_common.hlsli` — DistributionGGX, GeometrySmith, FresnelSchlick, EvaluateBRDF, ACESFilm
  - `shadow_common.hlsli` — SampleShadowPCF (atlas boundary clamping), SampleShadowMap (clean cascade selection)
- **ACES tonemapping**: replaced per-channel Reinhard with ACESFilm in mesh_ps.hlsl and deferred_lighting_ps.hlsl
- **Shader quality fixes**:
  - `[earlydepthstencil]` on opaque pixel shaders (mesh_ps, gbuffer_ps)
  - BC5 two-channel normal map reconstruction guard
  - Tangent transform uses normalMatrix instead of model matrix (mesh_vs.hlsl)
  - Removed hardcoded near/far plane from debug depth shader (reads PerFrameCB)
  - PCF kernel UV clamping to cascade atlas quadrant boundaries
  - Refactored cascade selection loop (removed dual-assignment pattern)
  - Fixed fullscreen_vs.hlsl bit-trick comment
- **Register binding fixes**: aligned constant_buffers.h/lighting_pass.h comments with actual b2 usage, fixed overlay_text_ps.hlsl field naming
- **Shader compiler include support**: added `includeDirs` to ShaderCompileDesc, DXC CreateDefaultIncludeHandler, FXC D3D_COMPILE_STANDARD_FILE_INCLUDE
- **Documentation**: register binding convention + shader includes docs in docs/engine/12-rendering/
- All 2640 tests passing (21 shader cross-backend compilation tests)

## [0.2.13] — 2026-03-13

### Engine API Consolidation (Phase 5.2.0) — COMPLETE
- **DemoApplication base class** (`engine/app/demo_application.h/.cpp`):
  - Eliminates window/device/swapchain/fence boilerplate from all demos
  - Virtual hooks: `OnInit()`, `OnFrame(dt)`, `OnResize(w,h)`, `OnShutdown()`
  - Triple-buffered frame sync, resize handling, minimize sleep, FPS stats
- **PipelineLibrary** (`engine/renderer/core/pipeline_library.h/.cpp`):
  - Lazy PSO cache for ForwardPBR, GBuffer, Lighting, GBufferDebug pipelines
  - Uses ShaderManager + VertexLayout::Standard() internally
  - 31 unit tests
- **Demo migrations** — all 3 demos now extend DemoApplication:
  - `textured_quad`: main.cpp reduced to 15 lines
  - `pbr_test`: main.cpp reduced to 43 lines (from 125), full PBR pipeline preserved
  - `deferred_test`: main.cpp reduced to 42 lines, deferred/forward toggle preserved
- **Build fixes**: resolved include paths (constant_buffers.h, upload_context.h, vertex.h, cascade.h), removed duplicate fps_camera_controller from extra_sources
- **Log category namespace fix**: moved all `UZ_DECLARE_LOG_CATEGORY` outside `namespace uz {}` across 6 files (gpu_mesh.cpp, shader_manager.cpp, pipeline_library.cpp, text_renderer.cpp, bitmap_font.cpp, stats_overlay.cpp)
- All 2641 tests passing, all 3 demos verified running at ~480 FPS
- Task archived

## [0.2.12] — 2026-03-13

### Rendering Roadmap — 0.1 Parity Planning (Phase 3A)
- **7 new Rulebook tasks** created for visual feature parity with 0.1 POC:
  - `phase3a-3.6.0-multi-light` — Point + spot light evaluation (8 max), CPU distance culling
  - `phase3a-3.7.0-ibl-environment` — BRDF LUT, irradiance cubemap, prefiltered specular, SkyLight
  - `phase3a-3.8.0-sky-atmosphere` — Bruneton physical atmosphere, Rayleigh/Mie, height fog
  - `phase3a-3.9.0-advanced-post-processing` — GTAO, SSR, TAA, DoF, motion blur, auto-exposure, color grading, contact shadows, screen effects, PostProcessVolume
  - `phase3a-3.10.0-volumetric-effects` — Ray-marched clouds (3D noise), god rays, temporal filtering
  - `phase3a-3.11.0-gltf-mesh-primitives` — glTF 2.0 importer (fastgltf), sphere/cone/cylinder/plane/torus generators
  - `phase3a-3.12.0-material-format` — .uzmat files, MaterialManager (cache + hot-reload)
  - `phase5-5.1.0-editor-visual-tools` — Transform gizmos, node picking, selection outline, debug draw, wireframe
- Each task includes proposal.md, tasks.md checklist, and specs/ with Given/When/Then scenarios
- Updated `docs/engine/12-rendering/README.md` with complete rendering roadmap (Phase 3A → Phase 5 → Phase 7+)

## [0.2.11] — 2026-03-13

### Vulkan Backend Deferred Tests (Phase 1)
- **Barrier tests** (`tests/engine/rhi/test_vulkan_barriers.cpp`):
  - 64 CPU-only tests: ToVkImageLayout, ToVkAccessFlags, ToVkPipelineStage, GetAspectFlags, IsDepthFormat
  - 8 GPU integration tests: texture barrier round-trips (color, depth, depth+stencil), buffer barriers, null/empty guards
- **Format conversion tests** (`tests/engine/rhi/test_vulkan_format_conversion.cpp`):
  - 22 CPU-only tests: all RHIFormat → VkFormat mappings including Unknown and out-of-range
  - 16 GPU integration tests: texture creation with each non-depth format on real Vulkan device
- **Total**: 102 new tests (137 total Vulkan tests), all passing

## [0.2.10] — 2026-03-12

### Tick System (Phase 3b-3.3b)
- **TickableComponent** (`engine/ecs/tickable_component.h/.cpp`):
  - Base class for components that receive per-frame `OnTick(float dt)` callbacks
  - Configurable `TickGroup` (PrePhysics, PostPhysics, Update, PostUpdate)
  - Enable/disable toggle, interval-based throttling with overshoot preservation
- **TickSystem** (`engine/ecs/tick_system.h/.cpp`):
  - Per-group flat vector storage for cache-friendly iteration
  - Entity validity checks, duplicate registration guard
  - Zero allocations on tick hot path
- **TickSubsystem** (`engine/ecs/tick_subsystem.h/.cpp`):
  - ISubsystem adapter dispatching all 4 tick groups in order each frame
  - Depends on World subsystem
- **51 tests** (`tests/engine/ecs/test_tick_system.cpp`)

## [0.2.9] — 2026-03-12

### Property System (Phase 3b-3.2b)
- **PropertyType enum** (`engine/property/property_types.h`):
  - 10 reflected types: Bool, Int, Float, Double, String, Vec2, Vec3, Vec4, Quat, Entity
  - PropertyFlags bitfield: ReadOnly, Hidden, Replicated, SaveGame, Transient
  - Bitwise operators and `HasFlag()` helper
- **PropertyInfo** (`engine/property/property_info.h`):
  - Offset-based field descriptor: name, type, offset, size, description, category, flags
- **ClassInfo** (`engine/property/class_info.h/.cpp`):
  - Reflected class descriptor: name, properties vector, placement-new factory, instance_size
  - FindProperty (linear scan), ForEachProperty, PropertyCount, AddProperty<T> template
- **ClassRegistry** (`engine/property/class_registry.h/.cpp`):
  - Singleton registry mapping class names to ClassInfo descriptors
  - Register, FindClass, IsRegistered, ForEachClass, ClassCount
  - Logs registration/overwrites via Property log category
- **UZ_COMPONENT macro** (`engine/property/property_macros.h`):
  - Auto-registers class in ClassRegistry during static initialization
  - Provides `ThisClass` typedef, `GetStaticClassInfo()`, placement-new factory
- **UZ_PROP / UZ_PROP_EX macros** for declarative property registration
- **GetPropertyType<T>** compile-time specializations for all 10 types
- **PropertySerializer** (`engine/property/property_serializer.h/.cpp`):
  - JSON serialization/deserialization via nlohmann/json
  - Math types as arrays, Entity as {id, generation} object
  - Transient property exclusion, unknown field tolerance
- **74 tests** (`tests/engine/property/test_property_system.cpp`)

## [0.2.8] — 2026-03-12

### Resource Loaders (Phase 2.2)
- **.uzmesh binary format** (`engine/asset/formats/uzmesh_format.h/.cpp`):
  - 64-byte header (magic `UZMS` 0x534D5A55, version 1), LOD and submesh descriptors
  - `VertexPosNormUV` interleaved layout (32 bytes: position + normal + UV)
  - 64-byte aligned vertex/index data regions for direct GPU upload
  - `WriteUzMesh()` / `ReadUzMesh()` serialization with magic/version validation
- **.uztexture binary format** (`engine/asset/formats/uztexture_format.h/.cpp`):
  - 64-byte header (magic `UZTX` 0x5554585A, version 1), mip level descriptors (24 bytes each)
  - `TextureFormat` enum: Uncompressed_RGBA8, BC1, BC3, BC5, BC7 (future compression)
  - `WriteUzTexture()` / `ReadUzTexture()` with mip chain offset/size validation
- **MeshImporter v1** (`engine/asset/importers/mesh_importer.h/.cpp`):
  - glTF 2.0 parsing via fastgltf v0.8 (.glb and .gltf)
  - Extracts positions (required), normals (optional), UVs (optional) from first mesh primitive
  - Auto-generates flat-shading normals when normals are missing (cross-product per triangle)
  - meshoptimizer vertex cache + fetch optimization (`meshopt_optimizeVertexCache` + `meshopt_optimizeVertexFetch`)
  - Outputs single-LOD single-submesh .uzmesh binary
- **TextureImporter v1** (`engine/asset/importers/texture_importer.h/.cpp`):
  - Image decoding via stb_image (PNG, JPG, JPEG, TGA → RGBA8)
  - Box-filter mipmap chain generation (floor(prev/2), min 1, down to 1x1)
  - Outputs .uztexture binary with complete mip chain
- **uzcook integration**: MeshImporter + TextureImporter registered alongside ShaderImporter
  - Importer-specific options (shader stage detection only for shader files)
  - Output extension mapping: `.uzmesh`, `.uztexture`, `.uzshader`
  - `stb_impl.cpp` added for stb_image compilation
- **97 new tests** (1158 total): format roundtrip, error paths, importer validation, GLB/PNG in-memory parsing
- **Asset directory reorganized** into `formats/`, `importers/` subdirectories
- **Thirdparty**: fastgltf v0.8.0 + simdjson v3.9.4, meshoptimizer v0.21

## [0.2.7] — 2026-03-12

### Job System (Phase 2.1)
- **Worker thread pool** (`engine/job/job_system.h/.cpp`):
  - N-1 worker threads (hardware concurrency minus main thread)
  - Per-priority lock-free queues via `atomic_queue::AtomicQueue2`
  - Work-stealing: idle workers steal from other workers' queues (high-priority first)
  - Implements `ISubsystem` (PreInit phase, depends on Logging)
- **Job struct** (`engine/job/job_types.h`):
  - Function pointer + 48-byte inline closure data (no heap allocation)
  - `MakeJob()` template for type-erased lambda capture
  - `JobPriority` enum: Critical, High, Normal, Low
- **Counter** (`engine/job/counter.h/.cpp`):
  - Atomic completion counter with mutex/condvar-based `Wait()`
  - `Reset(N)`, `Decrement()`, `IsComplete()`, multi-waiter support
- **Fence** (`engine/job/fence.h/.cpp`):
  - Higher-level sync: buffers jobs via `AddWork()`, flushes on `Wait()`
  - Reusable after `Wait()` completes
- **ParallelFor\<T\>** (`engine/job/job_system.h`):
  - Automatic work partitioning by worker count
  - Supports empty spans, single elements, large arrays
- **Debug access guards** (`engine/job/access_guard.h`):
  - `ReadAccess<T>` / `WriteAccess<T>` wrappers with atomic race detection
  - Zero-cost in release builds (compiled out via `#ifdef UZ_DEBUG`)
- **Thread-local profiling**: per-worker `executedJobs` and `stolenJobs` counters
- **97 new tests** (job types, counter, fence, job system, stress test 100K jobs)

## [0.2.6] — 2026-03-12

### Application Lifecycle (Phase 2.0)
- **ISubsystem interface** (`engine/app/subsystem.h`):
  - `InitPhase` enum: PreInit, Init, Start (3-phase initialization)
  - `TickGroup` enum: PrePhysics, PostPhysics, Update, PostUpdate, None
  - Dependency declaration via `GetDependencies()` for topological sort
- **SubsystemRegistry** (`engine/app/subsystem_registry.h/.cpp`):
  - Kahn's algorithm topological sort for initialization ordering
  - 3-phase init (PreInit → Init → Start), reverse-order shutdown
  - Tick group dispatch: `RunTickGroup(group, deltaTime)` per frame
  - Cycle detection with descriptive error messages
- **TimeManager** (`engine/app/time_manager.h/.cpp`):
  - Fixed-timestep accumulator pattern (configurable dt, default 1/60s)
  - Delta time clamping at 0.25s (prevents spiral-of-death)
  - Frame counting, total elapsed time, raw vs smoothed delta
- **EngineLoop** (`engine/app/engine_loop.h/.cpp`):
  - `Run(argc, argv)` and `Run(const EngineConfig&)` entry points
  - Fixed-step physics ticks + variable-rate update in main loop
  - `EngineConfig` struct for headless mode, custom tick rates, max frames
  - Command-line parsing (--headless, --max-frames, --fixed-dt)
- **Module registry** (`engine/app/module_registry.h/.cpp`):
  - `UZ_REGISTER_MODULE(Name, Func)` static self-registration macro
  - Function-local static vector avoids static initialization order fiasco
  - Duplicate detection with descriptive error logging
- **Subsystem wrappers** (`engine/app/subsystem_wrappers.h`):
  - LoggingSubsystem, PathsSubsystem, ConfigSubsystem adapters
  - Wrap existing free functions as ISubsystem implementations
- **97 new tests** (subsystem registry, time manager, engine loop, module registry)

## [0.2.5] — 2026-03-12

### DX11 Black Screen Fix
- **Root cause**: Deferred context rendering produces black frames — UE5 and Godot do NOT use deferred contexts on DX11
- **Rewritten `DX11CommandList`** to use immediate context directly (matching UE5 pattern)
  - Removed `CreateDeferredContext` / `FinishCommandList` / `ExecuteCommandList` entirely
  - All draw, clear, barrier, copy commands go directly to the immediate context
  - `Begin()` / `End()` / `Execute()` are now no-ops (satisfy RHI interface only)
- **Constant buffer binding**: Changed from `UpdateSubresource` on DEFAULT buffers to `Map(WRITE_DISCARD)` on DYNAMIC buffers (UE5 pattern)
- **All 5 demos verified rendering on DX11**: clear_screen, triangle, textured_quad (2755 FPS), camera_test, mesh_test

### RHI-Agnostic Screenshot Capture
- **`ScreenshotCapture`** rewritten to use only `IRHIDevice` / `IRHICommandList` / `IRHISwapChain` (was DX12-only)
- **`CopyTextureToBuffer`** added to `IRHICommandList` interface — implemented in DX12, DX11, Vulkan
  - DX12: `CopyTextureRegion` with placed footprint
  - DX11: Staging texture + `CopyResource` + `Map(READ)` + row-by-row memcpy
  - Vulkan: `vkCmdCopyImageToBuffer`
- **`UnmapBuffer`** added to `IRHIDevice` interface — DX12 no-op (persistent mapping), DX11 explicit Unmap, Vulkan vmaUnmapMemory
- **`SetRootCBV`** extended with `cpuData` + `dataSize` params for DX11 shadow-buffer pattern
- **`CBAllocation`** now includes `bufferHandle` and `offsetBytes` fields
- Screenshot verified on DX11 and DX12 via `--screenshot` flag in camera_test and mesh_test demos

### Research
- UE5 DX11 architecture analysis saved to `docs/research/UE5_DX11_ANALYSIS.md`
- Godot analysis files moved to `docs/research/`

## [0.2.4] — 2026-03-12

### Vulkan RHI Backend
- **Full Vulkan backend** (`engine/rhi/vulkan/`): VulkanDevice, VulkanCommandList, VulkanSwapChain, VulkanFence
  - volk dynamic loader (VK_NO_PROTOTYPES) — no static vulkan-1.lib linking
  - VMA v3.2.1 GPU memory allocator
  - VK_KHR_dynamic_rendering for render passes (no VkRenderPass objects)
  - VK_KHR_synchronization2 for pipeline barriers
  - VK_KHR_timeline_semaphore for fences (1:1 IRHIFence mapping)
  - VK_KHR_buffer_device_address for GPU virtual address queries
  - MAILBOX present mode, triple-buffered swapchain
  - Descriptor set layout: 16 textures (binding 0-15) + 16 samplers (16-31) + 16 UBOs (32-47)
  - Per-frame descriptor set allocation and automatic flush before draw calls
- **Automatic HLSL→SPIR-V compilation**: ShaderPipeline auto-detects Vulkan backend and compiles to SPIR-V via DXC (`-spirv -fspv-target-env=vulkan1.3`)
  - DXC SPIRV binding shifts: `-fvk-t-shift 0 0`, `-fvk-s-shift 16 0`, `-fvk-b-shift 32 0`
  - Bindless auto-disabled for Vulkan (ResourceDescriptorHeap not supported with -spirv)
  - Separate `vulkan_spirv` cache directory for SPIR-V bytecode
  - Zero demo code changes required — fully transparent to application layer
- **IRHIDevice::GetBackend()** virtual method + `GetActiveRHIBackend()` global accessor
- **IsBindless()** returns false for Vulkan (descriptor set model, not DX12 ResourceDescriptorHeap)
- **Build system**: Vulkan sources, volk.c, include paths added to runtime build.zig and all 5 demo build.zig files
- **Thirdparty**: vulkan-headers (SDK 1.4.309), volk, VMA added to setup.ps1 and thirdparty/README.md
- **All 5 demos verified**: clear_screen, triangle (5187 FPS), textured_quad (5334 FPS), camera_test (332 FPS), mesh_test (331 FPS) on RTX 4090 Vulkan 1.4.325

## [0.2.3] — 2026-03-12

### Build System
- **CMake**: `TRACY_ENABLE` + `TRACY_ON_DEMAND` promoted from PRIVATE to PUBLIC definitions
- **CMake**: Added `UZ_PROFILING_ENABLED` define so `UZ_PROFILE_*` macros are active
- **CMake**: Added Tracy link libs (`ws2_32`, `dbghelp`, `advapi32`) for networking/callstack support
- **build.zig**: Tracy link libs added to Zig build (conditional on `UZ_PROFILING_ENABLED`)

### Tracy CPU Instrumentation
- **UZ_PROFILE_FUNCTION()** added to ~39 engine .cpp files across all subsystems:
  - RHI backends: DX12Device, DX12CommandList, DX12SwapChain, DX12Fence, DX11Device, DX11CommandList, DX11SwapChain
  - Shader pipeline: ShaderPipeline, ShaderCompiler, ShaderCompilerFXC, ShaderSourceManager, ShaderDependencyGraph, ShaderCache
  - VFS & assets: VirtualFileSystem, AssetCache, TextureImporter, MeshImporter
  - Renderer: Renderer, TextRenderer, StatsOverlay, EditorGrid
  - Platform: PlatformWindow, InputSystem, ScreenshotCapture
- **UZ_PROFILE_FRAME_MARK()** at swapchain Present boundary for frame-level analysis
- Function-level profiling captures allocation, I/O, GPU command recording, and compilation time per subsystem

### Tracy GPU Profiling
- **DX12 integration** (`engine/rhi/dx12/dx12_device.cpp`, `dx12_swapchain.cpp`):
  - TracyD3D12Context integrated into DX12Device Init/Destroy
  - `TracyD3D12Collect` + `TracyD3D12NewFrame` called in DX12SwapChain::Present
  - Captures GPU timeline for all compute/graphics work on the direct queue
- **DX11 integration** (`engine/rhi/dx11/dx11_device.cpp`, `dx11_swapchain.cpp`):
  - TracyD3D11Context integrated into DX11Device Init/Destroy
  - `TracyD3D11Collect` called in DX11SwapChain::Present
  - Captures GPU timeline for deferred context command lists
- **TracyD3D12.hpp ABI patch** (`thirdparty/tracy/patches/0001-fix-getdesc-abi-directx-headers.patch`):
  - Fixes GetDesc() struct-return compatibility with directx-headers (out-parameter form for Clang/MinGW)
  - Godot-style numbered patch, applied by `scripts/setup.ps1`

### Custom GPU Timestamp Profiler
- **DX12 implementation** (`engine/rhi/dx12/dx12_gpu_profiler.h/.cpp`):
  - ID3D12QueryHeap-based timestamp queries (TIMESTAMP type)
  - Triple-buffered (frame-in-flight) query resolve buffer
  - GPU frequency via `ID3D12CommandQueue::GetTimestampFrequency()` — converts GPU ticks to milliseconds
  - Readback buffer with Map/Unmap pattern
- **DX11 implementation** (`engine/rhi/dx11/dx11_gpu_profiler.h/.cpp`):
  - Individual ID3D11Query objects for each timestamp (TIMESTAMP + TIMESTAMP_DISJOINT pair)
  - Triple-buffered, GetData() CPU readback with synchronization
  - Frequency extracted from TIMESTAMP_DISJOINT query result
  - Discards results when `Disjoint` flag is set (GPU frequency changed mid-frame)
- **Shared types**(`engine/rhi/gpu_profiler_types.h`):
  - GpuTimingResult struct: timestamp label, start/end ticks, ms duration
  - kMaxGpuTimestamps = 64 per frame, kProfilerFramesInFlight = 3
  - Used by both DX12 and DX11 backends
- **Exposure**: GetGpuProfiler() on IRHIDevice, non-fatal warning on init failure

### Stat Counters
- **8 built-in counters** (thread-safe atomic):
  - DrawCalls: incremented per BeginRenderPass
  - Triangles: incremented per bound mesh (vertex count / 3)
  - BufferUploads: incremented in UploadContext::Flush
  - ShaderCompiles: incremented in ShaderPipeline::CompileShader (non-cache hits)
  - CacheHits: incremented on shader cache lookup success
  - CacheMisses: incremented on shader cache lookup failure
  - VFSReads: incremented on VirtualFileSystem::ReadFile
  - PipelineCreations: incremented in CreateGraphicsPipeline
- **UZ_STAT_INCREMENT macro** — atomic increment with stat ID, compile-time category filtering
- **Per-frame reset**: Stats exposed to IPC handlers for editor telemetry (Phase 2+)

## [0.2.2] — 2026-03-12

### DX11 RHI Backend
- **Complete DX11 backend** implementing all 4 RHI interfaces:
  - **DX11Device** (29 methods): D3D11CreateDevice, feature level 11_0+, debug layer, SM5 slot-based SRV/UAV binding
  - **DX11CommandList** (28 methods): deferred context recording, immediate context execution, SM5 register-based binding
  - **DX11SwapChain** (7 methods): DXGI FLIP_DISCARD, tearing support, resize handling
  - **DX11Fence** (4 methods): emulated timeline fence via D3D11_QUERY_EVENT + CPU monotonic counter
- **ResourcePool\<T\>** extracted to shared `rhi/resource_pool.h` (reused by DX12 and DX11)
- **RHI factory** unified in `rhi_factory.cpp` — dispatches DX12/DX11 via `RHIBackend` enum
- **DX11-specific design**: no explicit barriers (ResourceBarrier = no-op), slot-based binding (not bindless), deferred context command lists
- **96 new DX11 tests** (test_dx11_device.cpp, test_dx11_render_loop.cpp, test_dx11_resources.cpp)

### Backend-Agnostic Demos
- **All 5 demos refactored** from DX12-specific code to abstract RHI interfaces:
  - `clear_screen`, `triangle`, `textured_quad`, `camera_test`, `mesh_test`
  - Use only `IRHIDevice*`, `IRHICommandList*`, `IRHISwapChain*`, `IRHIFence*`
  - `--rhi dx11|dx12` CLI flag for runtime backend selection
- **DX12 swapchain fix**: backbuffers now properly registered in device texture pool with RTV descriptors allocated through the descriptor allocator, enabling abstract handle-based `ClearRenderTarget` and `ResourceBarrier`
- **Screenshot capture** gracefully skips on non-DX12 backends (DX12 readback API only)
- **832 total tests passing** across all suites

## [0.2.1] — 2026-03-12

### FXC Dual-Compiler (SM5.0 Fallback)
- **Problem solved**: DXC only supports SM 6.0+. Older GPUs (RX 580, DX12 driver bugs) need SM 5.0.
- **Solution**: Dual-compiler pattern — SM >= 6.0 → DXC (DXIL), SM 5.0 → FXC (DXBC)
- **ShaderCompilerFXC** (`core/shader/shader_compiler_fxc.h/.cpp`):
  - Dynamic FXC loading via `LoadLibrary` + `GetProcAddress` on `d3dcompiler_47.dll`
  - Compiles HLSL → DXBC bytecode for SM5.0
  - FXC types (IFxcBlob, FxcShaderMacro) defined inline to avoid header conflicts
- **ShaderPipeline routing** (`core/shader/shader_pipeline.h/.cpp`):
  - Added `RequiresDXC()` logic — automatically routes SM5.0 to FXC, SM6.0+ to DXC
  - Added `SetCompilerFXC()` to manually override compiler selection
  - Unified compile interface — caller doesn't need to know which compiler is used
- **RHI changes** (`core/rhi/rhi_backend.h`, `dx12_device.h/.cpp`):
  - Added `SM5_0` to ShaderModelCompile enum
  - Added `std::optional<ShaderModel> forceShaderModel` to RHIDeviceDesc
  - NegotiateShaderModel() now respects forceShaderModel (allows `UZ_FORCE_SM5=1` env var)
- **HLSL shader dual-path** — All runtime shaders use `#ifdef UZ_BINDLESS`:
  - **SM6.6** (UZ_BINDLESS=1): ResourceDescriptorHeap[], SamplerDescriptorHeap[], SV_VertexID
  - **SM5.0** (!UZ_BINDLESS): register-bound textures (t0), Input Assembler vertex input (POSITION, TEXCOORD0, COLOR0)
- **Text rendering on SM5.0** (`editor/font/text_renderer.h/.cpp`):
  - Added `bool bindless` param to Init()
  - SM5 path uses Input Assembler pipeline: vertex input layout (POSITION float2, TEXCOORD0 float2, COLOR0 float4), SetVertexBuffer, SetDescriptorTable for font atlas
- **Stats overlay on SM5.0** (`editor/stats/stats_overlay.h/.cpp`):
  - Added `bool bindless` param to Init()
  - Passes through to TextRenderer
- **Demos updated** (all 4 demos: triangle, textured_quad, camera_test, mesh_test):
  - FXC DLL initialization and compiler setup
  - `UZ_FORCE_SM5=1` environment variable for testing SM5.0 path
  - Bindless flag propagation to overlay/text rendering
  - All demos verified working in both SM6.6 bindless and SM5.0 non-bindless modes
- **Build system** (`build.zig`, demo `build.zig` files):
  - FXC DLL (`d3dcompiler_47.dll`) deployed to `zig-out/bin/` during build
  - Available for runtime dynamic loading
- **Vendor DLL** (`runtime/thirdparty/fxc/bin/x64/d3dcompiler_47.dll`):
  - Vendored alongside project (ensures consistent shader compilation across team)
- **Shader cache now differentiates** SM5.0 vs SM6.6 via ShaderTarget enum (separate cache dirs)
- **Verification**: All 4 demos tested and working in both modes:
  - ✅ SM6.6 bindless (default): full rendering + overlays + grid
  - ✅ SM5.0 forced (UZ_FORCE_SM5=1): full rendering + overlays + grid via FXC

## [0.2.0] — 2026-03-11

### Stats Overlay
- **StatsOverlay** (`editor/stats/stats_overlay.h/.cpp`):
  - FPS counter, frame time (ms), GPU adapter name, resolution display
  - Configurable position (top-left/top-right/bottom-left/bottom-right)
  - Uses TextRenderer for bitmap font rendering
  - Toggle visibility at runtime

### Camera System & 3D Space
- **Z-up coordinate system**: All camera, controller, and grid operations use left-handed X-forward, Y-right, Z-up (matching UE5)
- **Camera space conversion**: Internal storage uses Y-up quaternions for consistency, public API converts via ToYUp/FromYUp helpers at boundaries
- **View matrix**: Swap matrix transforms Z-up coordinates to Y-up before feeding to XMMatrixLookToLH (Y-up space)
- **FPS camera controller rewritten**:
  - Euler angle-based orientation system (pitch, yaw, roll)
  - LookAt vector construction from Euler angles
  - Ground-projected movement (WASD moves along flat plane, not in view direction)
  - Delta-time smoothing for stable, responsive controls
- **Grid depth correctness**:
  - Pixel shader now outputs explicit SV_Depth to Z=0 plane
  - Grid pipeline changed from depthWrite=false to depthWrite=true to enable depth output
  - Prevents grid from overlapping with actual 3D geometry
- **Cube positioning**: Vertex offsets set cube bottom face at Z=0 for correct grid alignment

### Screenshot System
- **ScreenshotCapture utility** (`runtime/editor/screenshot/screenshot.h/.cpp`):
  - `Init()`: Create staging buffer for back buffer readback
  - `CaptureBackBuffer()`: Async GPU → CPU copy via staging buffer (MapDiscard pattern)
  - `SaveToPNG()`: PNG encoding via stb_image_write from RGBA8 readback
  - `Shutdown()`: Cleanup GPU resources
- **Integration**: Editor utility for automated back buffer → PNG capture (off-screen rendering, CI screenshots, automated testing)
- **Back buffer readback**: Proper DX12 staging + fence synchronization to ensure GPU work completes before CPU reads

### Build System
- **CMake support** — Added `CMakeLists.txt` and `CMakePresets.json` for MSVC and Clang on Windows
  - Presets: `msvc-debug`, `msvc-release`, `clang-debug`, `clang-release`
  - Static library + test executable + 3 demo executables
  - DLL deployment (Agility SDK, DXC) via post-build commands
- **Cross-compiler DX12 ABI** — `dx12_compat.h` normalizes struct-return COM methods (MSVC vs MinGW)
- **Three compilers verified**: Zig (Clang/MinGW), MSVC, Clang (clang-cl)
- **Unified output directory** — Zig now defaults to `build/` (same as CMake), override with `-p <path>`

### Milestone 1.2 — RHI (Rendering Hardware Interface)
- **RHI abstract interfaces** (`core/rhi/rhi.h`, `rhi_types.h`, `rhi_backend.h`):
  - IRHIDevice, IRHICommandList, IRHISwapChain, IRHIFence — pure virtual interfaces
  - RHIHandle<Tag> generational handles (Buffer, Texture, Pipeline, Shader, Sampler)
  - 20+ enums, 15+ descriptor structs, backend factory (CreateRHIDevice/DestroyRHIDevice)
- **DX12 SM6.7 backend** (`core/rhi/dx12/`):
  - DX12Device: IDXGIFactory6 + IDXGIAdapter4 + ID3D12Device, feature level auto-detect (12_2→12_0)
  - D3D12MA v3.1.0 integration for GPU memory management
  - Global descriptor heaps: CBV_SRV_UAV (1M, SM6.6 bindless), Sampler (2048), RTV (256), DSV (64)
  - Direct + Copy command queues, triple-buffered command allocators
  - SM6.6 Resource Binding Tier 3 validation
  - DescriptorAllocator: O(1) free-list allocator for all heap types
  - ResourcePool<T>: generational handle validation with use-after-free protection
  - CreateBuffer: vertex/index/constant/structured via D3D12MA, auto SRV/UAV for bindless
  - CreateTexture: 2D/Cube/Array with SRV + optional RTV/DSV/UAV, typeless depth for SRV+DSV
  - CreateShader: bytecode copy with pool storage
  - CreateSampler: sampler descriptor in global sampler heap
  - CreateGraphicsPipeline: full PSO from RHIGraphicsPipelineDesc with root signature
  - Global bindless root signature (SM6.6 DIRECTLY_INDEXED, one root constant at b0)
  - State conversion: cull, fill, blend, depth-stencil, topology → D3D12 enums
  - No input layout — SM6.6 bindless pulls vertex data from SRV
  - Persistent CPU mapping for Upload/Readback buffers
- **DX12 swapchain** (`dx12_swapchain.h/.cpp`):
  - IDXGISwapChain4 (FLIP_DISCARD), triple-buffered, tearing support
  - Resize handling (release + recreate back buffers)
- **DX12 fence** (`dx12_fence.h/.cpp`):
  - ID3D12Fence with Win32 event, Signal/WaitCPU/IsComplete/GetCompletedValue
- **DX12 command list** (`dx12_command_list.h/.cpp`):
  - ID3D12GraphicsCommandList (base interface for Clang/MinGW compat)
  - SM6.6 bindless heap binding on Begin(), SetDescriptorIndex for root constants
- **Clear screen demo** (`demos/clear_screen/`):
  - Animated sine-wave RGB clear color, 6069 FPS on RTX 4090 (0.16ms/frame)
  - Triple-buffered frame pacing with fence synchronization
- **Agility SDK activation** (`core/rhi/dx12/dx12_agility.cpp`):
  - Exports D3D12SDKVersion + D3D12SDKPath for runtime SDK loading
  - DLL deployment: D3D12Core.dll + d3d12SDKLayers.dll → build/bin/D3D12/
  - Unlocks FL 12_2 + SM6.6 on supported hardware (RTX 4090: FL 12_1→12_2, SM6.5→SM6.6)
  - Linked into every executable (demo + runtime), not the static lib
- **Standalone runtime executable** (`tools/uzengine.cpp`, `demos/triangle/`):
  - CLI flags: --scene, --width, --height, --headless, --rhi, --shader-model, --gpu-validation
  - Windowed mode with DX12 clear screen render loop, headless mode for CI/testing
  - `zig build run` step for quick iteration
- **Debug & validation**:
  - D3D12 debug layer + GPU-based validation (optional)
  - DRED enabled (auto-breadcrumbs + page fault tracking)
- **DX12 Clang/MinGW compatibility**:
  - Microsoft directx-headers (SDK 614) with correct GUIDs
  - Struct-return ABI: out-parameter form for GetCPUDescriptorHandleForHeapStart etc.
  - Base COM interfaces only (ID3D12Device, ID3D12GraphicsCommandList)
- **Cross-platform build** (`build.zig`):
  - Platform-aware: Windows-only code (DX12, platform/windows) guarded behind `is_windows`
  - DirectXMath compiles on Linux via dotnet/runtime sal.h stub (SAL annotations as no-ops)
  - Cross-compile validated: `zig build -Dtarget=x86_64-linux-gnu` passes
  - Setup script (`setup.ps1`) downloads sal.h automatically after DirectXMath clone
- **Backend selection** (`core/rhi/rhi_config.h/.cpp`):
  - RHIConfig resolution: CLI → .uzproject → platform auto-detect
  - ShaderModel enum: SM5_0, SM5_1, SM6_0, SM6_6
  - Flags: --rhi, --shader-model, --gpu-validation, --gpu-breadcrumbs, --adapter
  - ToDeviceDesc() conversion to RHIDeviceDesc
- **Shader model negotiation** (DX12Device):
  - NegotiateShaderModel(): probes SM6.6 → SM6.0 → SM5.1 → SM5.0
  - Root signature adapts: DIRECTLY_INDEXED for SM6.6, standard for lower
- **DXC DLL deployment** (`build.zig`):
  - `dxcompiler.dll` + `dxil.dll` copied to `build/bin/` alongside executables
  - Test runner executes from install directory so `GetExeDir()` finds deployed DLLs
- **321 unit tests** — 19 engine shader + 77 shader pipeline + 22 RHI config + 35 RHI type/resource + 8 paths + 160 core/math/container

### Image Libraries (Texture Pipeline Dependencies)
- **stb** (v2.30/1.16/2.16) — stb_image, stb_image_write, stb_image_resize2 for PNG/JPG/TGA/BMP/HDR loading, PNG writing, and high-quality resizing
- **libjpeg-turbo** (3.0.3) — SIMD-accelerated JPEG codec (SSE2/AVX2), 2-6x faster than libjpeg
- **tinyexr** (1.0.9) — OpenEXR HDR image loader/saver with bundled miniz for compression
- **basis_universal** (git 2025) — GPU texture supercompression transcoder (BC7, BC1, ASTC, ETC2, PVRTC)
- **astcenc** (5.3.0) — ASTC texture encoder/decoder for mobile GPU targets (Android, iOS)
- **22 integration tests** — Real functional tests for all 5 image libraries (round-trip encode/decode, format validation, error handling)
- **Build system** — Both Zig (`build.zig`) and CMake (`CMakeLists.txt`) updated with include paths, source compilation, and test registration

### Embedded Engine Shaders
- **`uzembed` build tool** (`tools/uzembed.cpp`):
  - Single-file C++ tool: `.hlsl` → `.hlsl.gen.h` (`constexpr char[]` in `uz::generated` namespace)
  - Auto-generates `engine_shaders.gen.h` index with `enum class EngineShader` + `EngineShaders::GetSource()`/`GetName()` registry
  - CLI: `uzembed --input-dir <dir> --output-dir <dir>`, deterministic alphabetical output
  - `--quiet` / `-q` flag suppresses stdout during build (errors still go to stderr)
  - Adding new shaders: drop `.hlsl` in `runtime/shaders/` (any subdirectory) and rebuild — no manual registration
- **`ShaderPipeline::LoadBytecodeFromSource()`** — compile from in-memory HLSL string (bypasses VFS/cache/hot-reload)
- **Migration**: TextRenderer and EditorGrid now use embedded shaders — no VFS mount or disk paths needed
- **`engine_shaders.h`** — thin wrapper that includes the auto-generated index header
- **19 unit tests** — source non-empty, HLSL keyword validation, filename convention, stage suffix, distinctness
- **Build integration**: Zig `build.zig` compiles and runs `uzembed` before runtime library compilation

### Mesh Format (.uzmesh)
- **Binary mesh format** (`core/renderer/mesh.h/.cpp`):
  - 128-byte fixed header (`MeshHeader`): magic `0x484D5A55`, version 1, AABB bounds
  - Two vertex formats: `Vertex` (48B, position+normal+uv+tangent) and `SimpleVertex` (36B, position+normal+uv)
  - `MeshBuilder`: `CreateFromVertices()`, `CreateFromSimpleVertices()`, `CreateCube()` (Z-up, bottom at Z=0)
  - `LoadMesh()` / `SaveMesh()`: binary I/O with magic/version validation and truncation detection
  - 25 unit tests (header, builder, file I/O round-trip, error cases)
- **Mesh shaders** (`shaders/runtime/mesh_vs.hlsl`, `mesh_ps.hlsl`):
  - SM6.6 bindless vertex fetch via `ResourceDescriptorHeap[index]`
  - N·L directional lighting with ambient, optional albedo texture

### Shader Pipeline
- **Shader VFS** (`core/shader/shader_vfs.h/.cpp`):
  - Virtual path mounting: `Mount("/engine", "shaders/engine")`
  - Path resolution, exists check, binary file reading
  - Backslash normalization, last-mounted-wins priority
  - 14 unit tests
- **Shader source manager** (`core/shader/shader_source_manager.h/.cpp`):
  - Load source via VFS, parse `#include "path"` recursively (max depth 32)
  - xxHash64 identity hash: source + sorted includes + defines + target + compiler version
  - Content-addressed — no timestamps, no stale cache bugs
  - Include deduplication, relative/absolute path resolution
  - 14 unit tests
- **Shader dependency graph** (`core/shader/shader_dependency_graph.h/.cpp`):
  - Forward graph: file → its includes (direct dependencies)
  - Reverse graph: file → all shaders that depend on it (transitive BFS)
  - Incremental edge rebuild on file change, diamond dependency handling
  - 13 unit tests
- **Shader cache** (`core/shader/shader_cache.h/.cpp`):
  - Disk-based bytecode cache: `cache/shaders/{backend}_{sm}/{hash}.bin`
  - Lookup by xxHash64 — hash changes on source change, no explicit invalidation
  - Store/Lookup/Contains/Remove, entry count and total size statistics
  - 11 unit tests
- **DXC shader compiler** (`core/shader/shader_compiler.h/.cpp`):
  - Dynamic loading of dxcompiler.dll via LoadLibrary/GetProcAddress
  - HLSL → DXIL compilation (SM6.6, SM6.0)
  - HLSL → SPIRV compilation (Vulkan 1.3 target via `-spirv`)
  - Preprocessor defines, debug info (`-Zi`), optimization control (`-Od`)
  - DXC version query (IDxcVersionInfo) — DXC 1.8 confirmed
  - Auto-extracted IIDs via constexpr ParseUUID from CROSS_PLATFORM_UUIDOF
  - 17 unit tests
- **Engine path resolution** (`core/paths/paths.h/.cpp`):
  - `GetExeDir()`: cached executable directory via platform-native API
  - `ResolveEngineResource()`: resolve relative paths against exe directory
  - Follows UE5/Godot pattern — never CWD, never hardcoded absolute paths
  - Build system deploys DLLs to `build/bin/` via `addInstallBinFile`
  - 8 unit tests
- **Shader pipeline spec**: `docs/specs/1.5.0-shader-pipeline.md`
  - Two-mode design: UZ_SHADER_DEV (VFS + disk load + hot-reload) / UZ_SHADER_SHIP (precompiled archive)
  - Learned from POC 1 failures: never embed shaders as C header strings during dev

### Runtime CLI & Backend Selection (docs only)
- **Runtime executable design**: CLI flags (--scene, --screenshot, --rhi, --shader-model, --headless)
- **Backend selection priority**: CLI → .uzproject → user config → auto-detect
- **Post-switch shader compilation**: compile missing shaders before first frame when backend changes

### Milestone 1.1 — Math & Containers
- **Math foundation** (`core/math/engine_math.h`) — SIMD-accelerated vectors and matrices via DirectXMath:
  - Vec2, Vec3, Vec4 with zero-initialization, SIMD operations (dot, cross, normalize)
  - Mat4 with identity default, transforms (point/direction), decomposition, comparisons
  - Quat with slerp, lerp, axis-angle, vector rotation
  - Transform with position/rotation/scale composition, decomposition, interpolation
- **Geometry types** (`core/math/geometry.h`) — Frustum culling and collision:
  - Plane (half-space, signed distance for frustum clipping)
  - AABB (axis-aligned bounds with containment, intersection, expansion tests)
  - Frustum (6-plane view frustum extraction from projection matrix, conservative culling)
- **Color space conversions** (`core/math/color.h`) — Linear ↔ sRGB gamma correction:
  - LinearColor (float4, linear RGB for rendering)
  - sRGBColor (uint8_t RGBA, sRGB for storage/UI)
  - Gamma-correct conversions, hex color support
- **Containers** (`core/containers/`) — Zero-copy, allocation-friendly data structures:
  - RingBuffer: Lock-free SPSC circular queue with acquire/release semantics
  - FreeList: Intrusive free list for element-stable allocation
  - HandleMap: Generation-based handle → T mapping (safe indices)
  - FrameArray: Temporary allocations with frame lifetime scoping
- **Memory allocators** (`core/memory/allocators.h`) — Named, budget-tracked:
  - LinearAllocator: Bump pointer with reset (frame/streaming allocation)
  - PoolAllocator: Fixed-size block pool with free chain
  - FrameAllocator: Linear allocator with auto-reset per frame
- **Random number generation** (`core/random/random.h`) — PCG generator:
  - PCG (Permuted Congruential Generator) XSH-RR variant
  - 64-bit state, 32-bit output, statistically excellent (passes BigCrush)
  - Seeded and stream variants for deterministic parallel generation
  - Utility methods: Float, Range, Int, UInt, Bool, UnitSphere, UnitDisk
- **160 unit tests** — Complete coverage of all math/container/memory types (27 test suites)
- **Rust-style documentation** — All public types/methods documented with `///` comments

### Added
- Initial Zig build system (`build.zig`)
- Thirdparty dependency setup via `scripts/setup.ps1`
- Stub N-API entry point linking to extension
- Unit tests via `zig build test` (`tests/test_runtime.cpp`)
- clang-format config (`.clang-format`)
- clang-tidy config with warnings-as-errors (`.clang-tidy`)

### Milestone 1.0 — Bootstrap & Project Setup
- **Logging system** (`core/logging/`) — spdlog-backed, 12 categories (Core, RHI, Renderer, Scene, Physics, Audio, Input, Platform, Memory, IO, Config, Window), compile-time level filtering, UZ_LOG macro
- **Assertion system** (`core/assertions/`) — UZ_CHECK (fatal), UZ_ENSURE (non-fatal), UZ_VERIFY (never stripped), stack trace via CaptureStackBackTrace, debugger break support
- **CPU profiler** (`core/profiling/`) — Tracy integration (compile-time toggleable), StatCounter with atomic int64, 6 stat groups
- **Memory foundation** (`core/memory/`) — Rust-inspired ownership: Own<T>, Ref<T>, RefMut<T> with debug borrow checker (std::atomic)
- **Platform window** (`platform/windows/`) — Win32 HWND creation, WndProc message pump, fullscreen toggle, show/hide
- **Input system** (`platform/windows/`) — Keyboard (VK codes), mouse (move/button/scroll), action mapping with RegisterAction/IsActionDown/IsActionJustPressed
- **Config system** (`core/config/`) — .uzproject JSON loader (nlohmann/json), dotted-path resolution, CLI override cascade (--key=value), typed getters with defaults
- **JSON schema** (`schemas/uzproject.schema.json`) — JSON Schema 2020-12 for .uzproject validation
- **25 test functions, 78 assertions** — full coverage of all 1.0 systems
