# UzEngine Shader Engineer Memory

## Project Architecture
- **Language**: C++23, DX12 primary GPU API
- **Build**: CMake + vcpkg
- **Spec format**: Markdown in `docs/specs/`, module docs in `docs/modules/`
- **Task format**: Rulebook tasks in `.rulebook/tasks/<id>/` with `proposal.md` + `tasks.md` + `.metadata.json`

## Renderer Key Decisions
- **Pipeline**: Hybrid deferred + forward (GBuffer pass -> deferred lighting -> forward transparency)
- **Root signature**: b0=PerObject, b1=PerFrame, t0-t7=SRVs, s0-s3=Samplers, b15=GlobalParams
- **Shadow atlases**: CSM (2048x6144), point light DPSM (2048x2048, 8 lights), spot light (2048x2048, 16 lights)
- **Instancing**: Dynamic only, StructuredBuffer<float4x4> at t7, INSTANCED shader variant
- **Shader includes**: `shaders/include/*.hlsli`, DXC `-I shaders/include`
- **Variant key**: FNV-1a 64-bit hash of sorted defines
- **PSO cache key**: {shader_path, variant_key, vertex_layout, blend_state, depth_state}
- **GlobalParams cbuffer at b15**: g_Time, g_DeltaTime, g_ScreenSize, g_Wind*, g_Fog*

## Render Graph Pass Order
- ShadowPass=100, PointLightShadow=110, SpotLightShadow=120, DepthPrepass=200
- GBuffer=300, LightCull=350, Lighting=400, Transparency=500, Skybox=600
- PostProcess=700, UI=800, Overlay=900, Readback=1000

## File Conventions
- Specs use `---` section dividers, tables for data layouts, code blocks for structs/shaders
- Proposal.md has: Why, What Changes, Dependencies, Spec Reference, Impact
- Tasks.md has numbered sections with checkbox items, Testing and Documentation sections last
- All docs in English, user communicates in Portuguese BR

## Screen-Space Effects (SSAO, SSR, TAA)
- **Spec**: `docs/specs/screen-space-effects.md` — HBAO+, Hi-Z SSR, TAA with variance clamp
- **G-Buffer update**: GBuffer3 = RG16F velocity (for TAA/SSR temporal reprojection)
- **Per-frame CBV additions**: prev_view_proj, jitter_offset (for TAA)
- **Per-object CBV addition**: prev_model (for velocity buffer)
- **New shared include**: `shaders/include/screen_space.hlsli` (linearize_depth, YCoCg, Halton)
- **Post-process stack order**: SSAO -> Hi-Z -> SSR march -> SSR resolve -> Bloom -> Tonemap -> TAA -> Output
- **Phase schedule**: SSAO=Phase2 (standalone), TAA+SSR=Phase3 (TAA adds velocity buffer, SSR needs TAA)
- **b2 register**: used for pass-specific CBVs (SSAOParams, SSRParams, TAAParams, BlurParams)
- **Compute entry point**: CSMain (matches project convention from shader-system.md)

## Material Editor & Material System
- **Spec**: `docs/tools/material-editor.md` — artist-facing material configuration tool
- **Material instances**: inheritance chain (master -> base -> character -> LOD), override-only storage
- **Material layers**: Base/Dirt/Wetness/Blood/Burn/Ice/Damage, blend modes (multiply/overlay/additive/replace)
- **Dye system**: 4 tint channels via RGBA mask texture, HSV shift per channel, `shaders/include/dye_system.hlsli`
- **Texture packing**: ORM preset (R=AO, G=Roughness, B=Metallic), PACKED_ORM shader variant
- **LOD materials**: LOD0=full PBR, LOD1=simplified+mip bias, LOD2=vertex color, LOD_CROSSFADE dither variant
- **Layer masks**: texture, curvature (ddx/ddy normals), height (world Y), vertex_color, noise (Perlin)
- **Layer HLSL**: `shaders/include/material_layers.hlsli` (blend_multiply, blend_overlay, blend_additive, apply_layer)
- **IPC namespace**: `material.*` (open, save, setParam, setTexture, addLayer, setLayerIntensity, etc.)
- **C++ classes**: MaterialRegistry, MaterialInstanceCache, TextureSlotBinder, MaterialLayerEvaluator, DyeSystemManager
- **Preview**: isolated DX12 render pass, 5 IBL presets, A/B mode, channel isolation, specular probe sphere
- **.uzmat format**: INI-like, [uzmaterial] header, [variants] section, [layer] sections, res:// paths

## Visual Shader Editor
- **Spec**: `docs/tools/shader-editor.md` — node-based visual shader authoring tool
- **File format**: `.uzshader` (INI-like, plain text, AI-friendly), compile cache `.uzshader.cache`
- **Output**: generates `.hlsl` + `.uzmat` pair from node graph
- **108 node types**: Input(10), Math(33), Texture(7), Lighting(8), UV(6), Noise(5), Constants(8), Utility(8), Channel(5), Color(5), Global(7), Custom(3), Output(3)
- **Master Output types**: PBR Surface, Unlit, PostProcess
- **Pin types**: float, float2, float3, float4, matrix, texture2d, texturecube, sampler, bool, int
- **Type coercion**: auto splat (float->float4), truncate (float4->float3), cast (int<->float)
- **Texture limit**: t0-t5 user (6 max), t6=bone palette, t7=instance transforms
- **MaterialParams cbuffer**: register b2, holds exposed graph parameters
- **Transpiler**: topological sort -> type inference -> dead elimination -> HLSL emit -> DXC validate
- **Variable naming**: `node_<id>_<outputPin>` for traceability
- **Frontend**: TypeScript + HTML5 Canvas 2D (not DOM), Monaco for HLSL view
- **C++ backend**: `src/renderer/shader_graph/` — ShaderGraphCompiler, NodeRegistry, NodeGraph
- **IPC**: `shader_graph.compile`, `shader_graph.validate`, `shader_preview.render`
- **AI tools**: `shader_graph_assist` MCP tool (explain, generate subgraph, optimize, fix errors)

## Global Illumination System
- **Spec**: `docs/tools/global-illumination.md` — runtime GI + editor baking/authoring tools
- **3 GI modes**: Dynamic (probes+SSIL), Baked (lightmaps), Hybrid (lightmaps+probes)
- **Probe grid**: L2 SH (9 coeff x RGB = 27 floats), half-precision GPU storage, 64 bytes/probe
- **Default grid**: 64x16x64 = 65K probes, 2m spacing, 4 MB VRAM
- **Probe update**: DXR ray casting (preferred) or SDF fallback, async compute, priority queue
- **SSIL**: Screen-space indirect lighting, combined with SSAO pass, temporal+spatial denoise
- **Emissive injection**: 3D voxel grid (128x64x128 RGBA16F = 8 MB), 3-step propagation
- **Lightmap baking**: CPU Embree path tracer, BC6H compression, xatlas UV2, scenarios (day/night)
- **Reflection captures**: Sphere/Box shapes, parallax-corrected box, priority+blend, sky IBL fallback
- **New registers**: b3=GIProbeParams, b4=AmbientCube, t8=probe SH buffer
- **New render passes**: ProbeUpdate=355, EmissiveVoxelize=356-358, SSILGather=360-361, ReflCapture=365
- **New shader includes**: gi_probes.hlsli, gi_ssil.hlsli, reflection_capture.hlsli, sh_math.hlsli, sky_ibl.hlsli
- **File format**: `.uzgi` (INI-like per-scene GI config), `.uzlightmap` (BC6H HDR), `.uzreflection`
- **IPC namespaces**: `gi.*`, `lightmap.*`, `reflection.*`
- **C++ classes**: GISystem, RadianceProbeGrid, ProbeUpdater, SSILPass, EmissiveInjector, LightmapBaker, LightmapManager, ReflectionCaptureManager, SkyLight, GIDebug
- **Console vars**: `r.gi.*` prefix for all runtime overrides
- **Quality tiers**: Off/Low/Medium/High/Ultra, 0-6.5ms GPU budget at 1080p

## Post-Process Pipeline Editor
- **Spec**: `docs/tools/postprocess-editor.md` — PPV system for level designers
- **File format**: `.uzppv` (INI-like), stores volume shape + overridable PP settings
- **Volume types**: Global (infinite, p:0), Box, Sphere, Unbound (infinite + priority)
- **Blending**: weighted lerp by camera distance, priority-ordered, cubic bezier falloff curves
- **Categories**: ColorGrading, Exposure, Bloom, DoF, SSAO, SSR, ToneMapping, FilmGrain, Vignette, CA, MotionBlur, Fog, CustomPP
- **PP pass order**: ColorGrade(710)->SSAO(720)->SSAOApply(725)->HiZ(730)->SSR(735)->SSRResolve(740)->CustomBeforeTM(745)->Bloom(750)->DoF(755)->AutoExposure(760)->ToneMap(765)->CustomAfterTM(770)->TAA(775)->FilmEffects(780)->MotionBlur(785)->CustomBeforeUI(790)->UI(800)->CustomAfterAll(810)
- **C++ classes**: PostProcessPipeline, PostProcessVolume, PostProcessBlender, PPVolumeSettings, PPVolumeRegistry, PostProcessPassManager, ExposureState
- **IPC namespace**: `postprocess.*` (createVolume, deleteVolume, setVolumeParam, getBlendedState, save, load, getHistogram, lockExposure)
- **Lua API**: `ctx.postprocess:push_volume()`, `pop_volume()`, `push_override()`, `pop_override()`, `set_param()`, `get_param()`
- **Custom PP**: .uzmat with POSTPROCESS domain, 4 insertion points (BeforeToneMap, AfterToneMap, BeforeUI, AfterAll)
- **Performance**: ~3.5ms typical, ~6.5ms cinematic max at 1080p

## Visibility & GPU-Driven Culling
- **Spec**: `docs/tools/visibility-culling.md` — HZB occlusion, meshlets, virtual geo, LOD, impostor baker
- **3 strategies**: Standard LOD+frustum, GPU-Driven (HZB), Cluster/Meshlet (Nanite-inspired)
- **HZB**: R32_FLOAT mip chain from depth prepass, max downsample, CS_BuildHZB at 8x8 threads
- **Two-phase culling**: Phase 1 (prev HZB) -> draw + rebuild HZB -> Phase 2 (re-test rejects)
- **GPU cull output**: VisibleInstance buffer + IndirectArgs for ExecuteIndirect, zero CPU readback
- **SW occlusion**: 256x144 CPU rasterizer, max 256 occluder meshes, convex hull proxies
- **Meshlet pipeline**: AS_CullMeshlets (AS 6.5) -> MS_DrawMeshlets (MS 6.5) -> PBR PS
- **Meshlet size**: 128 vertices, 128 primitives, MeshletDesc = 48 bytes
- **Virtual geometry**: Cluster DAG hierarchy, screen-space error < 1px, offline build in UzBuild.exe
- **LOD levels**: LOD0(100%) LOD1(50%) LOD2(25%) LOD3(12.5%) LOD4(impostor billboard)
- **LOD crossfade**: 4x4 Bayer dither, `shaders/include/lod_crossfade.hlsli`, LOD_CROSSFADE variant
- **New render passes**: HZB Build Phase1=210, GPU Cull Phase1=220, HZB Build Phase2=310, GPU Cull Phase2=320, GBuffer Late=330
- **New shader variants**: MESHLET_RENDERING, LOD_CROSSFADE, IMPOSTOR, REVERSED_Z, VIRTUAL_GEOMETRY
- **Impostor baker**: editor tool, N angles -> atlas, `.uzimpostor` INI format
- **C++ classes**: VisibilitySystem, HZBBuilder, GPUCuller, SoftwareOccluder, MeshletRenderer, VirtualGeometrySystem, LODSystem, ImpostorBaker
- **IPC namespace**: `visibility.*`, `impostor.*`
- **DX12 requirements**: SM 6.0 for HZB/culling, SM 6.5 for mesh shaders, D3D12_MESH_SHADER_TIER_1

## Render Console & CVar System
- **Spec**: `docs/tools/render-console.md` -- r.* CVar system, scalability presets, GPU profiler, screenshot/capture
- **CVar naming**: dot-separated IPC/config (`r.Shadow.Quality`), underscore C++ (`r_Shadow_Quality`)
- **CVar types**: Int, Float, Bool, Enum -- all numeric (atomic reads, no string CVars for render)
- **Registration**: `RENDER_CVAR_INT/FLOAT/BOOL/ENUM` macros in subsystem source files
- **Persistence**: hardcoded default < `project/render.cfg` < `user_render.cfg` < command-line `--cvar`
- **Thread safety**: `std::atomic<T>` for reads, dirty queue flushed at frame boundary for callbacks
- **Scalability groups**: sg.ShadowQuality, sg.TextureQuality, sg.FoliageQuality, sg.PostProcessQuality, sg.LightingQuality, sg.AntiAliasingQuality, sg.GeometryQuality (each 0-3 Low/Medium/High/Ultra)
- **IPC namespaces**: `r.cvar.*`, `r.scalability.*`, `r.profiler.*`, `r.screenshot`, `r.capture.*`
- **GPU profiler**: DX12 timestamp queries per pass, BeginPass/EndPass/ResolveQueries/ReadbackResults
- **Screenshot**: tiled rendering for >GPU max RT, PNG/EXR/JPEG, F9 hotkey
- **EXR capture**: multi-layer G-Buffer export (Albedo, Normal, Roughness, Metallic, Depth, Velocity, Emissive)
- **Config files**: INI-like `.cfg` format, `#` comments, `key = value` lines, `[section]` headers
- **Lua API**: `ctx.render:get_cvar()`, `set_cvar()`, `apply_scalability_preset()`, `save_user_settings()`
- **C++ classes**: CVarRegistry, CVar<T>, ScalabilityManager, ConfigParser, GPUProfiler, ScreenshotSystem

## Module Violation Analysis (2026-03-04)
- Two competing `renderer/` dirs: `src/renderer/` (DX12Renderer) vs `src/engine/renderer/` (rhi, render_graph)
- `renderer.h` includes `<d3d12.h>` directly -- DX12 types in public API
- HLSL shaders are C++ string literals in `renderer.h` -- blocks hot-reload/permutations
- `editor_component.h` includes `<d3d12.h>` -- should use RHI
- `main.cpp` is god file (41 includes, owns everything)
- `rhi.h`, `render_graph.h`, `shader_manager.h` all exist but DX12Renderer ignores them
- Full analysis: see `ue5-module-analysis.md` in this directory

## UE5 Shader Patterns (from W:\UE_5.6_Source, 2026-03-04)
- **Auto-registration**: IMPLEMENT_GLOBAL_SHADER -> static FShaderTypeRegistration -> linked list at startup
- **No switch/case**: Shader map indexed by FShaderType* pointer (hash map)
- **Permutation domain**: TShaderPermutationDomain<Dim...>, compile-time count, flat int32 ID
- **Declarative params**: BEGIN_SHADER_PARAMETER_STRUCT generates struct + metadata, DXC reflection binding
- **Layer cake**: Renderer -> RenderCore -> RHI -> Core (never upward)
- **Dynamic backend**: RHI dynamically loads D3D12RHI/VulkanRHI (DynamicallyLoadedModuleNames)
- **Key files**: GlobalShader.h:341-416, Shader.h:1260-1842, ShaderPermutation.h, ShaderParameterStruct.h

## Target UzEngine Module Hierarchy
```
core/ -> platform/ -> rhi/ -> render_core/ -> renderer/ -> scene/ -> editor/ -> app/
```
- `rhi/`: pure C header, all DX12 confined to rhi/dx12/
- `render_core/`: shader types, render graph, params -- MUST NOT depend on renderer/
- `renderer/`: render passes -- NEVER includes d3d12.h
- `scene/`: pure data -- MUST NOT depend on any rendering module

## Atmosphere Shader System (2026-03-30)
- **Directory**: `runtime/shaders/runtime/atmosphere/` — 10 files (LUTs + sky + fog)
- **Shared header**: `sky_common.hlsli` — AtmosphereParams struct, phase functions, density profiles, ray-sphere, LUT parameterization
- **LUTs**: `transmittance_lut.hlsl` (256x64 CS), `multiscatter_lut.hlsl` (32x32 CS), `sky_view.hlsl` (192x108 CS)
- **Sky rendering**: `sky_atmosphere.hlsl` (PS, samples sky-view LUT + sun disk), `sky_cubemap.hlsl` (CS, 6-face from sky-view), `skybox_cubemap.hlsl` (PS, static HDRI)
- **Fog/AP**: `aerial_perspective.hlsl` (CS, 32x32x16 froxel volume), `height_fog.hlsl` (PS, analytical exp fog)
- **God rays**: `god_rays.hlsl` (CS, screen-space radial blur)
- **CB layouts**: AtmosphereLutCB (b0 compute), SkyAtmosphereCB (b2 fullscreen), HeightFogCB (b2 fog pass)
- **References**: Bruneton 2008/2017, Hillaire 2016/2020, Cornette & Shanks 1992, Toth & Umenhoffer 2009
- **Conventions**: entry point `main`, Z-up, cm world / km atmosphere, kCmToKm = 1e-5
- **Cloud shaders** (9 files, written 2026-03-30):
  - `cloud_noise_shape_cs.hlsl` (128^3 R8, CS 4x4x4, Perlin-Worley 4 octaves)
  - `cloud_noise_detail_cs.hlsl` (32^3 R8, CS 4x4x4, 3-octave Worley erosion)
  - `cloud_weather_cs.hlsl` (512^2 RGBA8, CS 8x8x1, coverage/type/precip/secondary)
  - `cloud_raymarch_cs.hlsl` (RGBA16F, CS 8x8x1, main volumetric march, dual-lobe HG, Beer-powder)
  - `cloud_temporal_cs.hlsl` (CS 8x8x1, variance-clip reprojection, 0.95 history weight)
  - `cloud_composite_ps.hlsl` (PS fullscreen, scene*T+inscatter, depth-aware sky-only)
  - `irradiance_lut_cs.hlsl` (64x16 CS 8x8x1, ground irradiance, Bruneton 2017 Eq.15)
  - `single_scattering_lut_cs.hlsl` (256x128x32 CS 8x8x1, 4D single scatter, R=Rayleigh A=Mie)
  - `scattering_density_cs.hlsl` (256x128x32 CS 8x8x1, multi-scatter iteration density)
- **Cloud CB**: CloudParams at b0, ~50 params (noise SRVs, camera, atmosphere, noise tiling, wind, density, lighting, march)
- **Cloud references**: Schneider 2015/2017 (HZD), Hillaire 2016 (Frostbite), Bruneton 2017

## See Also
- `docs/specs/shader-variants.md` - variant/permutation system
- `docs/specs/shader-library.md` - shared HLSL includes
- `docs/specs/point-spot-light-shadows.md` - point/spot shadow maps
- `docs/specs/gpu-instancing.md` - dynamic GPU instancing
- `docs/specs/screen-space-effects.md` - SSAO, SSR, TAA post-process effects
- `docs/tools/material-editor.md` - Material Editor tool spec
- `docs/tools/shader-editor.md` - Visual Shader Editor tool spec
- `docs/tools/global-illumination.md` - GI system + baking/authoring tools
- `docs/tools/postprocess-editor.md` - Post-Process Pipeline Editor tool spec
- `docs/tools/visibility-culling.md` - Visibility & GPU-Driven Culling system spec
- `docs/tools/render-console.md` - Render Console & CVar system spec
