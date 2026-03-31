# Shader Rewrite — Falcor/UE5 Industry-Standard Parity

## Directory Structure (AAA standard — modular by subsystem, NO flat include/)

```
runtime/shaders/runtime/
├── common/           # Shared utilities — every shader includes these
│   ├── constants.hlsli
│   ├── math.hlsli
│   ├── color.hlsli
│   ├── encoding.hlsli
│   ├── sampling.hlsli
│   └── bindless.hlsli
│
├── material/         # Material model — Falcor StandardBSDF equivalent
│   ├── brdf.hlsli
│   ├── diffuse.hlsli
│   ├── specular.hlsli
│   ├── sheen.hlsli
│   ├── clearcoat.hlsli
│   ├── subsurface.hlsli
│   ├── pom.hlsli
│   └── standard_material.hlsli
│
├── gbuffer/          # Geometry buffer
│   ├── mesh_vs.hlsl
│   ├── gbuffer_ps.hlsl
│   ├── mesh_ps.hlsl
│   ├── gbuffer_debug_ps.hlsl
│   ├── decal_vs.hlsl
│   └── decal_ps.hlsl
│
├── lighting/         # Light evaluation
│   ├── light_types.hlsli
│   ├── shadow.hlsli
│   ├── ibl.hlsli
│   ├── fullscreen_vs.hlsl
│   ├── shadow_depth_vs.hlsl
│   ├── deferred_lighting_ps.hlsl
│   └── cluster_assign_cs.hlsl
│
├── atmosphere/       # Sky, fog, clouds, aerial perspective
│   ├── sky_common.hlsli
│   ├── transmittance_lut_cs.hlsl
│   ├── multi_scatter_lut_cs.hlsl
│   ├── single_scattering_lut_cs.hlsl
│   ├── scattering_density_cs.hlsl
│   ├── indirect_scattering_cs.hlsl
│   ├── irradiance_lut_cs.hlsl
│   ├── sky_atmosphere_ps.hlsl
│   ├── sky_cubemap_cs.hlsl
│   ├── sky_cubemap_ps.hlsl
│   ├── skybox_cubemap_ps.hlsl
│   ├── aerial_perspective_volume_cs.hlsl
│   ├── height_fog_ps.hlsl
│   ├── god_rays_cs.hlsl
│   ├── cloud_raymarch_cs.hlsl
│   ├── cloud_composite_ps.hlsl
│   ├── cloud_temporal_cs.hlsl
│   ├── cloud_noise_shape_cs.hlsl
│   ├── cloud_noise_detail_cs.hlsl
│   └── cloud_weather_cs.hlsl
│
├── postprocess/      # Post-processing
│   ├── tonemap_ps.hlsl
│   ├── bloom_threshold_cs.hlsl
│   ├── bloom_downsample_cs.hlsl
│   ├── bloom_upsample_cs.hlsl
│   ├── bloom_composite_ps.hlsl
│   ├── bloom_blur_h_ps.hlsl
│   ├── bloom_blur_v_ps.hlsl
│   ├── bloom_downsample_ps.hlsl
│   ├── gtao_cs.hlsl
│   ├── gtao_denoise_cs.hlsl
│   ├── gtao_temporal_cs.hlsl
│   ├── gtao_upsample_ps.hlsl
│   ├── taa_resolve_cs.hlsl
│   ├── tsr_resolve_cs.hlsl
│   ├── ssr_trace_cs.hlsl
│   ├── ssr_resolve_cs.hlsl
│   ├── ssr_temporal_cs.hlsl
│   ├── exposure_histogram_cs.hlsl
│   ├── exposure_average_cs.hlsl
│   ├── velocity_ps.hlsl
│   ├── motion_blur_cs.hlsl
│   ├── motionblur_tilemax_cs.hlsl
│   ├── motionblur_neighbormax_cs.hlsl
│   ├── dof_setup_cs.hlsl
│   ├── dof_blur_cs.hlsl
│   ├── fxaa_ps.hlsl
│   ├── smaa_edge_ps.hlsl
│   ├── smaa_blend_ps.hlsl
│   ├── smaa_resolve_ps.hlsl
│   ├── cas_sharpen_ps.hlsl
│   ├── color_grading_lut_cs.hlsl
│   ├── lensflare_cs.hlsl
│   ├── fsr_easu_cs.hlsl
│   └── fsr_rcas_cs.hlsl
│
├── ibl/              # Image-based lighting compute
│   ├── brdf_lut_cs.hlsl
│   ├── irradiance_cs.hlsl
│   ├── prefilter_cs.hlsl
│   └── equirect_to_cubemap_cs.hlsl
│
├── compute/          # GPU compute utilities
│   ├── gpu_cull_cs.hlsl
│   ├── hiz_pyramid_cs.hlsl
│   ├── draw_compact_cs.hlsl
│   ├── mipgen_cs.hlsl
│   └── decal_apply_cs.hlsl
│
├── gi/               # Global illumination
│   ├── sdf_common.hlsli
│   ├── global_sdf_composite_cs.hlsl
│   ├── mesh_sdf_bake_cs.hlsl
│   └── radiance_cache_trace_cs.hlsl
│
├── ssgi/             # Screen-space GI
│   ├── ssgi_trace_cs.hlsl
│   ├── ssgi_temporal_accum_cs.hlsl
│   ├── ssgi_spatial_denoise_cs.hlsl
│   ├── ssgi_upsample_cs.hlsl
│   └── prev_color_reduce_cs.hlsl
│
├── shadows/          # Distance field shadows
│   ├── dfs_common.hlsli
│   ├── dfs_cone_trace_cs.hlsl
│   ├── dfs_cull_objects_cs.hlsl
│   └── dfs_upsample_cs.hlsl
│
├── vfx/              # Visual effects
│   ├── vfx_common.hlsli
│   ├── vfx_curves.hlsli
│   ├── vfx_attractors.hlsli
│   ├── vfx_collision.hlsli
│   ├── vfx_flipbook.hlsli
│   ├── vfx_motion_vectors.hlsli
│   ├── vfx_vector_field.hlsli
│   ├── decal_box_mesh.hlsli
│   ├── trail_common.hlsli
│   ├── particle_spawn_cs.hlsl
│   ├── particle_update_cs.hlsl
│   ├── particle_sort_cs.hlsl
│   ├── particle_indirect_args_cs.hlsl
│   ├── particle_billboard_vs.hlsl
│   ├── particle_billboard_ps.hlsl
│   ├── particle_mesh_instance_cs.hlsl
│   ├── particle_mesh_vs.hlsl
│   ├── particle_mesh_ps.hlsl
│   ├── trail_vs.hlsl
│   ├── trail_ps.hlsl
│   ├── decal_deferred_vs.hlsl
│   ├── decal_deferred_ps.hlsl
│   ├── distortion_ps.hlsl
│   └── distortion_resolve_ps.hlsl
│
└── debug/            # Diagnostic tools
    ├── debug_view_ps.hlsl
    ├── pixel_debug_cs.hlsl
    └── vis_fullscreen_ps.hlsl
```

---

## Phase 1: Common Headers (foundation — everything depends on these)
- [x] 1.1 `common/constants.hlsli` — PI, M_1_PI, M_2PI, M_PI_2, M_SQRT2, FLT_EPSILON, FLT_MAX, kCmPerMeter, kMinCosTheta, kMinGGXAlpha=0.0064, kSentinelSrvIndex=0xFFFFFFFF
- [x] 1.2 `common/math.hlsli` — Pow2/4/5/Sq, SafeRcp/Sqrt/Acos, LengthSq(2D+3D), HashPCG, HashJenkins, InterleavedGradientNoise (Jimenez 2014), BuildOrthonormalBasis (Duff 2017), SphericalToCartesian, CartesianToSpherical, Remap
- [x] 1.3 `common/color.hlsli` — SRGBToLinear/LinearToSRGB (exact IEC 61966, `select()`, param=`linearColor`), fast gamma, Luminance (BT.709), ACESFilmic (Narkowicz 2015), Reinhard/Extended, HableUncharted2, WhiteBalance (Bradford)
- [x] 1.4 `common/encoding.hlsli` — OctEncode/OctDecode (`select()` for vector conditional), PackUnorm2x16/Snorm2x16, PackR11G11B10F (with helper FloatToR11/B10), PackRGBE/UnpackRGBE, PackNormalMetallicRoughness
- [x] 1.5 `common/sampling.hlsli` — RadicalInverseVdC, Hammersley, ConcentricSampleDisk (Shirley 1997), SampleCosineHemisphere, SampleUniformSphere/Cone, ImportanceSampleGGX (Walter 2007), SampleGGX_VNDF (Dupuy 2023)
- [x] 1.6 `common/bindless.hlsli` — `#ifdef UZ_BINDLESS` macros for Texture2D/Cube/3D/Array/RW/Buffer + SM5 fallback comments, UzSampleTexture2D/Level/Cube/CubeLevel/3DLevel/2DArrayLevel/Load with kSentinelSrvIndex guard
- [x] 1.7 Build — headers embedded by uzembed. 97 pre-existing errors from missing Phase 3+ .hlsl shaders (EngineShader enum refs). Zero errors from new .hlsli headers.

## Phase 2: Material Model (Falcor StandardBSDF equivalent)
- [x] 2.1 `material/brdf.hlsli` — D_GGX (exact Falcor `evalNdfGGX`), Lambda_GGX, G1_SmithGGX, G_SmithGGXCorrelated, V_SmithGGXCorrelated (Hammon 2017), F_Schlick (3 overloads), F_DielectricIOR (with TIR), IORToF0/F0ToIOR, ComputeF0, RoughnessToAlpha
- [x] 2.2 `material/diffuse.hlsli` — EvalFrostbiteDiffuse (exact Falcor `evalWeight`: energyBias+energyFactor+fd90), EvalLambertDiffuse
- [x] 2.3 `material/specular.hlsli` — EvalSpecular (D*V*F), EvalSpecularDVF (convenience)
- [x] 2.4 `material/sheen.hlsli` — D_Charlie (Estevez & Kulla 2017), V_Neubelt, EvalClothSheen
- [x] 2.5 `material/clearcoat.hlsli` — kClearcoatIOR=1.5, kClearcoatF0=0.04, EvalClearcoat with Fc output for energy conservation
- [x] 2.6 `material/subsurface.hlsli` — WrapDiffuse (Valve 2006), BackLighting (Brisebois 2011), EvalSubsurface/Foliage/ThinTranslucent (5 functions)
- [x] 2.7 `material/pom.hlsli` — ParallaxOcclusionMapping + PomSelfShadow (both bindless + Texture2D overloads)
- [x] 2.8 `material/standard_material.hlsli` — MaterialParams struct, 6 shading models (Default/Subsurface/Clearcoat/Cloth/Foliage/ThinTranslucent), InitMaterialParams, IBLParams, EvalStandardMaterial dispatch
- [x] 2.9 Build — verified: zero new errors from material headers

## Phase 3: Core Rendering (7 shaders — TEST EACH ONE INDIVIDUALLY)
- [x] 3.1 `gbuffer/mesh_vs.hlsl` — 3 paths (UZ_BINDLESS+UZ_INSTANCED / UZ_BINDLESS / SM5 IA), PushConstants b0 + PerFrameCB b1 + PerObjectCB b2. Written by shader-engineer agent.
- [ ] 3.2 Build + uncomment `RegisterGeometryPasses` in scene_render_frame.cpp + run `--frames 30` + verify no DEVICE_REMOVED
- [x] 3.3 `gbuffer/gbuffer_ps.hlsl` — GBuffer write RT0-RT3, sentinel > 0, ORM mode, TBN normal map, mipBias via SampleBias, POM support, alpha clip. Written by shader-engineer agent.
- [ ] 3.4 Build + run `--frames 30` + `--pass-dump` to verify GBuffer has content
- [x] 3.5 `lighting/fullscreen_vs.hlsl` — SV_VertexID fullscreen triangle, zero resources. Written by shader-engineer agent.
- [x] 3.6 `lighting/shadow_depth_vs.hlsl` — PushConstants b0, PerObjectCB b2 (model at offset 32), ShadowDataCB b4 (shadowViewProj). 3 paths. Written by shader-engineer agent.
- [ ] 3.7 Build + uncomment `RegisterShadowPasses` + run `--frames 30` + verify no crash
- [x] 3.8 `lighting/deferred_lighting_ps.hlsl` — full PBR deferred lighting: GBuffer decode (albedo+AO, oct-normal+metallic+roughness, worldPos, emissive), directional light with CascadeShadow+ContactShadow+DFS, 64 point lights + 16 spot lights with local shadows, IBL via EvalIBL (SH+split-sum), GTAO+SSGI AO, SSGI indirect diffuse, SSR, 4 shadow debug modes. Full 768B PerFrameCB with all fields verified byte-for-byte against C++ struct. SM5 fallback t0-t11.
- [ ] 3.9 Build + uncomment `RegisterDeferredLightingPass` only in frame_lighting.cpp + run `--frames 30` + verify HDR has content
- [x] 3.10 `postprocess/tonemap_ps.hlsl` — full pipeline: chromatic aberration, HDR sample, bloom composite, exposure, saturation, contrast, ACES, 3D LUT, film grain, vignette, sRGB. b0=TonemapPushConstants, b1=PerFrameCB. Written by shader-engineer agent.
- [ ] 3.11 Build + uncomment `RegisterPostProcessPasses` + run `--frames 30` + verify LDR output
- [x] 3.12 `gbuffer/mesh_ps.hlsl` — forward NdotL + ambient, `#define UZ_LIGHT_TYPES_NO_CB`, PerFrameCB b1, PerObjectCB b2, alpha clip + emissive. Written by shader-engineer agent.
- [ ] 3.13 Build + verify ForwardPBR pipeline creates
- [ ] 3.14 Uncomment `RegisterOverlayPasses` + verify editor grid/gizmos
- [ ] 3.15 Re-enable `WarmupAll()` + verify all core pipelines warm up with 0 errors
- [ ] 3.16 Visual verification: `--pass-dump` at 3 camera positions, confirm geometry + lighting + tonemap

## Phase 4: Lighting Headers
- [x] 4.1 `lighting/light_types.hlsli` — PointLightData (64B), SpotLightData (80B), LightingCB at b2 with `#ifndef UZ_LIGHT_TYPES_NO_CB` guard, DistanceAttenuation (Karis 2013), SpotAngleAttenuation. All structs verified byte-identical to C++ constant_buffers.h.
- [x] 4.2 `lighting/shadow.hlsli` — LocalLightShadowData (80B), ShadowDataCB at b4 (2304B, `#ifndef UZ_SHADOW_NO_CB` guard), SelectCascade, GetCascadeUV, SampleShadowPCF3x3, CascadeShadow (with inter-cascade fade + distance fade), ContactShadow (8-step screen-space), SampleLocalLightShadow. SM5 fallback t3/t11.
- [x] 4.3 `lighting/ibl.hlsli` — IBLTextures struct, EvalSH_L2 (matching sh_projection.cpp compact 7-float4 packing: per-channel L0+L1 + L2 + Y22), IsSHZero, EvalIBL (SH diffuse + cubemap fallback + split-sum specular via BRDF LUT). All bindless-guarded with kSentinelSrvIndex checks.

## Phase 5: Atmosphere (19 shaders)
- [ ] 5.1 `atmosphere/sky_common.hlsli` — AtmosphereParams struct, RayleighPhase, MiePhaseHG, CornetteShanksPhase, density profiles, RaySphereIntersect, WorldToAtmosphere, TransmittanceUV mapping (Bruneton 2017)
- [ ] 5.2 `atmosphere/transmittance_lut_cs.hlsl` — 256x64, 40-step trapezoidal, b0=8 DWORD root constants, b2=CBV, s0=linear clamp
- [ ] 5.3 `atmosphere/multi_scatter_lut_cs.hlsl` — 32x32, Hillaire 2020 isotropic multi-scatter
- [ ] 5.4 `atmosphere/single_scattering_lut_cs.hlsl` — 256x128x32, Rayleigh RGB + Mie A
- [ ] 5.5 `atmosphere/scattering_density_cs.hlsl` — N-th order density
- [ ] 5.6 `atmosphere/indirect_scattering_cs.hlsl` — integrate density along rays
- [ ] 5.7 `atmosphere/irradiance_lut_cs.hlsl` — ground irradiance 64x16
- [ ] 5.8 Build + verify all 6 LUT pipelines create without `hr=0x80070057`
- [ ] 5.9 `atmosphere/sky_atmosphere_ps.hlsl` — sky-view LUT + sun disk, b2=SkyAtmosphereCB
- [ ] 5.10 `atmosphere/sky_cubemap_cs.hlsl` + `sky_cubemap_ps.hlsl` — cubemap capture CS + SM5 PS fallback
- [ ] 5.11 `atmosphere/skybox_cubemap_ps.hlsl` — HDRI skybox
- [ ] 5.12 `atmosphere/aerial_perspective_volume_cs.hlsl` — 32x32x16 froxel
- [ ] 5.13 `atmosphere/height_fog_ps.hlsl` — exponential height fog (Hillaire 2016)
- [ ] 5.14 `atmosphere/god_rays_cs.hlsl` — radial blur light shafts
- [ ] 5.15 Build + uncomment `RegisterAtmosphereFogCloudPasses` + test
- [ ] 5.16 `atmosphere/cloud_raymarch_cs.hlsl` — volumetric clouds (Schneider 2015)
- [ ] 5.17 `atmosphere/cloud_composite_ps.hlsl` + `cloud_temporal_cs.hlsl`
- [ ] 5.18 `atmosphere/cloud_noise_shape_cs.hlsl` + `cloud_noise_detail_cs.hlsl` + `cloud_weather_cs.hlsl`
- [ ] 5.19 Re-enable cloud dispatch + verify

## Phase 6: IBL Compute (4 shaders)
- [ ] 6.1 `ibl/brdf_lut_cs.hlsl` — split-sum BRDF LUT 512x512 RG16F (Karis 2013)
- [ ] 6.2 `ibl/irradiance_cs.hlsl` — cosine hemisphere convolution (Ramamoorthi 2001)
- [ ] 6.3 `ibl/prefilter_cs.hlsl` — GGX prefiltered env (Karis 2013, Lagarde 2014)
- [ ] 6.4 `ibl/equirect_to_cubemap_cs.hlsl` — HDR panorama to cubemap
- [ ] 6.5 Build + verify IBL bake runs

## Phase 7: Post-Processing (34 shaders — test sub-passes)
- [ ] 7.1 `postprocess/gtao_cs.hlsl` — GTAO (Jimenez 2016), sinGamma=-cosAng, multi-bounce=ao+ao*(1-ao)*0.2
- [ ] 7.2 `postprocess/gtao_denoise_cs.hlsl` + `gtao_temporal_cs.hlsl` + `gtao_upsample_ps.hlsl`
- [ ] 7.3 Uncomment `RegisterGTAOPass` in frame_lighting.cpp + test
- [ ] 7.4 `postprocess/bloom_threshold_cs.hlsl` + `bloom_downsample_cs.hlsl` + `bloom_upsample_cs.hlsl`
- [ ] 7.5 `postprocess/bloom_composite_ps.hlsl` + `bloom_blur_h_ps.hlsl` + `bloom_blur_v_ps.hlsl` + `bloom_downsample_ps.hlsl` (legacy)
- [ ] 7.6 `postprocess/exposure_histogram_cs.hlsl` + `exposure_average_cs.hlsl`
- [ ] 7.7 `postprocess/taa_resolve_cs.hlsl` — variance clipping YCoCg (Karis 2014, Salvi 2016)
- [ ] 7.8 `postprocess/tsr_resolve_cs.hlsl` — temporal super resolution (3 sub-passes via defines)
- [ ] 7.9 `postprocess/ssr_trace_cs.hlsl` + `ssr_resolve_cs.hlsl` + `ssr_temporal_cs.hlsl`
- [ ] 7.10 Uncomment `RegisterSSRPasses` + test
- [ ] 7.11 `postprocess/velocity_ps.hlsl` — depth-based motion vectors
- [ ] 7.12 `postprocess/motion_blur_cs.hlsl` + `motionblur_tilemax_cs.hlsl` + `motionblur_neighbormax_cs.hlsl`
- [ ] 7.13 `postprocess/dof_setup_cs.hlsl` + `dof_blur_cs.hlsl`
- [ ] 7.14 `postprocess/fxaa_ps.hlsl` — FXAA 3.11 (Lottes 2009)
- [ ] 7.15 `postprocess/smaa_edge_ps.hlsl` + `smaa_blend_ps.hlsl` + `smaa_resolve_ps.hlsl`
- [ ] 7.16 `postprocess/cas_sharpen_ps.hlsl` — AMD CAS
- [ ] 7.17 `postprocess/color_grading_lut_cs.hlsl` — 32³ LUT bake
- [ ] 7.18 `postprocess/lensflare_cs.hlsl` — ghost-based lens flare
- [ ] 7.19 `postprocess/fsr_easu_cs.hlsl` + `fsr_rcas_cs.hlsl` — FSR 1.0
- [ ] 7.20 Test all postprocess together + visual verification

## Phase 8: Compute Utilities (5 shaders)
- [ ] 8.1 `compute/gpu_cull_cs.hlsl` — frustum + HiZ culling (Wihlidal 2015)
- [ ] 8.2 `compute/hiz_pyramid_cs.hlsl` — max-downsample for reversed-Z
- [ ] 8.3 `compute/draw_compact_cs.hlsl` — visibility-driven compaction
- [ ] 8.4 `compute/mipgen_cs.hlsl` — sRGB-correct box filter
- [ ] 8.5 `compute/decal_apply_cs.hlsl` — DBuffer → GBuffer merge
- [ ] 8.6 Uncomment `RegisterDecalPasses` + test

## Phase 9: Debug (3 shaders)
- [ ] 9.1 `debug/debug_view_ps.hlsl` — 15 view modes + 4x2 overview grid
- [ ] 9.2 `debug/pixel_debug_cs.hlsl` — [numthreads(1,1,1)], 13 float readback
- [ ] 9.3 `debug/vis_fullscreen_ps.hlsl` — 6 display modes
- [ ] 9.4 Verify `--diag`, `--pixel-debug`, `--vis`, `--view-mode` work

## Phase 10: GI + Shadows (6 shaders)
- [ ] 10.1 `gi/sdf_common.hlsli` — SampleGlobalSDF, SDFSphereTrace, SDFSoftShadow, SDFAO
- [ ] 10.2 `gi/global_sdf_composite_cs.hlsl` — mesh SDFs → global clipmap
- [ ] 10.3 `gi/mesh_sdf_bake_cs.hlsl` — per-mesh SDF from triangles
- [ ] 10.4 `gi/radiance_cache_trace_cs.hlsl` — probe SH via SDF (groupshared at FILE scope)
- [ ] 10.5 `shadows/dfs_common.hlsli` — DFSConeTrace wrapper
- [ ] 10.6 `shadows/dfs_cone_trace_cs.hlsl` + `dfs_cull_objects_cs.hlsl` + `dfs_upsample_cs.hlsl`
- [ ] 10.7 Uncomment `RegisterDFSPasses` + `RegisterSDFGIPasses` + test

## Phase 11: SSGI (5 shaders)
- [ ] 11.1 `ssgi/ssgi_trace_cs.hlsl` — screen-space GI via HiZ
- [ ] 11.2 `ssgi/ssgi_temporal_accum_cs.hlsl` — variance clip
- [ ] 11.3 `ssgi/ssgi_spatial_denoise_cs.hlsl` — a-trous SVGF
- [ ] 11.4 `ssgi/ssgi_upsample_cs.hlsl` — bilateral upsample
- [ ] 11.5 `ssgi/prev_color_reduce_cs.hlsl` — downsample prev frame HDR
- [ ] 11.6 Uncomment `RegisterSSGIPasses` + `RegisterSSGIPrevColorCopy` + test

## Phase 12: VFX (20 files)
- [ ] 12.1 `vfx/vfx_common.hlsli` — Particle struct, RNG, distributions
- [ ] 12.2 `vfx/vfx_curves.hlsli` + `vfx_attractors.hlsli` + `vfx_collision.hlsli` + `vfx_flipbook.hlsli` + `vfx_motion_vectors.hlsli` + `vfx_vector_field.hlsli` + `decal_box_mesh.hlsli` + `trail_common.hlsli`
- [ ] 12.3 `vfx/particle_spawn_cs.hlsl` + `particle_update_cs.hlsl` + `particle_sort_cs.hlsl` + `particle_indirect_args_cs.hlsl`
- [ ] 12.4 `vfx/particle_billboard_vs.hlsl` + `particle_billboard_ps.hlsl`
- [ ] 12.5 `vfx/particle_mesh_instance_cs.hlsl` + `particle_mesh_vs.hlsl` + `particle_mesh_ps.hlsl`
- [ ] 12.6 `vfx/trail_vs.hlsl` + `trail_ps.hlsl`
- [ ] 12.7 `vfx/decal_deferred_vs.hlsl` + `decal_deferred_ps.hlsl`
- [ ] 12.8 `vfx/distortion_ps.hlsl` + `distortion_resolve_ps.hlsl`
- [ ] 12.9 Uncomment `RegisterParticlePasses` + test

## Phase 13: Geometry Extras
- [ ] 13.1 `gbuffer/gbuffer_debug_ps.hlsl` — GBuffer debug overlay
- [ ] 13.2 `gbuffer/decal_vs.hlsl` + `decal_ps.hlsl` — DBuffer decals
- [ ] 13.3 `lighting/cluster_assign_cs.hlsl` — clustered light assignment (Olsson 2012)

## Phase 14: Final Integration
- [ ] 14.1 Re-enable ALL passes in scene_render_frame.cpp
- [ ] 14.2 Re-enable ALL sub-passes in frame_lighting.cpp
- [ ] 14.3 Re-enable cloud dispatch
- [ ] 14.4 Re-enable `WarmupAll()` — 0 pipeline creation errors
- [ ] 14.5 `--diag` on demo — 0 FAIL targets, 0 DEVICE_REMOVED
- [ ] 14.6 `--pass-dump` — every pass produces non-zero output
- [ ] 14.7 Visual comparison at 3 camera positions

## 2. Testing
- [ ] 2.T1 CPU math parity: C++ mirrors for D_GGX, Frostbite diffuse, F_Schlick — `zig build test`
- [ ] 2.T2 Each shader compiles without DXC errors (verified per phase)
- [ ] 2.T3 Each pipeline creates without `hr=0x80070057` or `hr=0x887A0005`
- [ ] 2.T4 No DEVICE_REMOVED after 120 frames on demo/sponza/cornell_box
- [ ] 2.T5 `--gpu-validation` clean (0 DX12 debug layer errors)
- [ ] 2.T6 PSNR >= 35 dB vs Falcor reference (BRDF parity)
- [ ] 2.T7 SM5 compilation: all shaders compile with UZ_BINDLESS undefined
- [ ] 2.T8 95%+ coverage on new C++ parity test code

## 3. Documentation
- [ ] 3.1 Update `docs/specs/shader-library.md` with new directory structure
- [ ] 3.2 Update `docs/specs/shader-variants.md` with SM5/bindless strategy
- [ ] 3.3 Update `.sandbox/NEXT-SESSION-PROMPT.md` per-phase status
- [ ] 3.4 Add `docs/analysis/shader-rewrite-postmortem.md`
