# Rendering System -- AAA Gap Analysis

**Author**: Shader/Rendering Engineer
**Date**: 2026-03-02
**Status**: Review
**Scope**: All rendering, shader, and post-processing specs in `docs/specs/`

---

## Executive Summary

UzEngine's current rendering specification covers a solid mid-tier deferred/forward+ hybrid pipeline with PBR (Cook-Torrance GGX), CSM shadows, tile-based light culling, GPU instancing, HBAO+ SSAO, Hi-Z SSR, and TAA. This is a competent baseline comparable to early-2010s AAA engines (roughly UE4.0 or Unity 5 era).

However, the gap between this baseline and modern AAA rendering (UE5.4, Unity 6 HDRP, Frostbite, id Tech 7) is substantial. The analysis below identifies 28 critical to important gaps, organized by subsystem. Each gap is assessed for impact, complexity, and dependencies against the existing spec surface.

Engines referenced for comparison:
- **Unreal Engine 5.4** (Lumen, Nanite, Virtual Shadow Maps, Substrate materials)
- **Unity 6 HDRP** (APV, SSR with ray-traced fallback, PCSS, VRS)
- **Frostbite** (clustered forward, GPU-driven rendering, volumetric fog, Mantle/DX12 multi-engine)
- **id Tech 7** (megatexture successor, ray-traced GI, mesh shaders)

---

## Gap 1: Global Illumination (GI) System

### Why Critical

Every modern AAA engine provides a multi-method global illumination solution. UE5 has Lumen (hybrid software + hardware ray tracing GI). Unity 6 HDRP has Adaptive Probe Volumes (APV). Frostbite has screen-space + precomputed probe GI. Without GI, the engine produces flat, unnatural lighting where shadowed areas appear completely black with only a constant ambient hack.

### Current State in UzEngine

The spec has zero GI. The PBR pipeline in `renderer.md` and `lighting.hlsli` evaluates direct lighting only (directional, point, spot). The BRDF in `brdf.hlsli` includes `F_SchlickRoughness` for IBL and `G_Smith_IBL` geometry term, suggesting IBL (image-based lighting) is planned but the spec in `renderer.md` mentions IBL only as "PBR GGX BRDF + IBL" in the built-in shader list -- there is no IBL spec, no irradiance convolution pipeline, no BRDF LUT generation, and no probe system.

### What Is Needed

A layered GI system with at minimum:

1. **IBL Environment Maps (baseline)**: Pre-filtered environment cubemap + split-sum BRDF LUT. This is the minimum viable indirect lighting. Requires: cubemap convolution compute shader, Hammersley importance sampling, BRDF integration LUT (2D texture, computed once at startup). Engines like Frostbite do this at asset-cook time.

2. **Irradiance Probes / Light Probes**: Spherical Harmonic (L2 SH, 9 coefficients) probes placed in the world, either manually or on a grid. Runtime interpolation gives per-pixel indirect diffuse. Unity HDRP APV uses tetrahedral interpolation of dense probe grids. Minimum viable: manual placement with trilinear interpolation on a 3D grid.

3. **Screen-Space Global Illumination (SSGI)**: Reuse the Hi-Z ray marching infrastructure from SSR to trace diffuse bounces in screen space. This is a cheap first bounce that fills in contact GI (e.g., color bleeding from a red wall onto a nearby white surface). Cost is ~2-3ms at 1080p.

4. **Dynamic Diffuse Global Illumination (DDGI)** (stretch goal): NVIDIA's DDGI algorithm uses a grid of probes that are updated each frame via short ray traces (DXR or compute-based). This provides fully dynamic indirect diffuse for moving lights and geometry. Requires DXR or a probe ray-casting fallback.

### Impact if Absent

- All indirect lighting is either a flat ambient constant or requires hand-placed lights to simulate bounced light
- Interior scenes look dead; outdoor scenes lack sky-colored fill light
- Artists must compensate with excessive fill lights, increasing draw cost and breaking physical accuracy
- Cannot compete visually with any modern engine

### Complexity: XL (6-10 weeks for the full stack; M for IBL baseline alone)

### Dependencies
- PBR Renderer (existing)
- Shader Library compute infrastructure (existing)
- DXR pipeline (Gap 4) for DDGI hardware path
- Render graph (Gap 15) for managing probe update passes

---

## Gap 2: Virtual Geometry / Nanite-Style Mesh Pipeline

### Why Critical

UE5's Nanite eliminates manual LOD authoring and enables rendering of film-quality assets with billions of triangles. While a full Nanite implementation is extremely complex, the underlying principles -- GPU-driven cluster-based rendering, meshlet culling, software rasterization -- are becoming the expected baseline for geometry handling in 2025+ engines. id Tech 7 and Unity 6 both have mesh shader / meshlet pipelines.

### Current State in UzEngine

The spec uses traditional CPU-side frustum culling (`renderer.md` step 1), fixed LOD levels per terrain chunk (`terrain-system.md` with 4 LOD levels), and meshoptimizer for offline LOD generation. `engine-vision.md` mentions `meshopt_simplify` + `clusterlod.h` (Nanite-style DAG) in the tech stack but there is no spec for a runtime cluster-based renderer.

### What Is Needed

1. **Meshlet / Cluster-Based Geometry**: Decompose meshes into clusters (typically 64-128 triangles each) using `meshoptimizer::meshopt_buildMeshlets`. Store cluster AABB bounds and cone-of-normals for efficient GPU culling.

2. **GPU-Driven Culling Pipeline**: Compute shader that culls clusters per-frame against the frustum, HZB (hierarchical-Z buffer), and optionally backface cone. This replaces the CPU frustum cull entirely for static geometry, eliminating CPU-side draw call overhead.

3. **Mesh Shader Pipeline (DX12 Ultimate)**: Use the Amplification Shader + Mesh Shader pipeline (SM 6.5) instead of the traditional VS/IA path. The amplification shader performs per-cluster culling and the mesh shader outputs the surviving clusters. This eliminates the index buffer and vertex fetch bottleneck.

4. **Software Rasterizer** (Nanite-level, stretch goal): For sub-pixel triangles, a compute-based software rasterizer avoids the fixed-function overhead. This is what makes Nanite scale to billions of triangles.

### Impact if Absent

- Large outdoor scenes will be geometry-bound with traditional draw call submission
- Artists must manually author LOD chains for every asset
- Cannot render film-quality assets (photogrammetry, CAD data) in real-time
- GPU utilization is poor for scenes with many small objects (draw call overhead dominates)

### Complexity: XL (8-12 weeks for meshlet pipeline; 16+ weeks for full Nanite-class)

### Dependencies
- Mesh shaders require DX12 Ultimate (SM 6.5) GPU support check
- GPU-driven rendering infrastructure (Gap 20)
- HZB / Occlusion culling (Gap 27)

---

## Gap 3: Virtual Shadow Maps (VSM)

### Why Critical

UE5's Virtual Shadow Maps provide pixel-consistent shadows for the entire scene without manual cascade tuning, shadow popping, or resolution artifacts. Traditional CSM (which UzEngine specs) suffers from fixed resolution distribution, visible cascade transitions, and limited range. VSM uses clipmap-like paging with GPU-driven rendering to allocate shadow resolution only where needed.

### Current State in UzEngine

`renderer.md` specifies 3-cascade CSM (2048x2048 per cascade, 2048x6144 atlas) with PCF 3x3. `point-spot-light-shadows.md` specifies dual-paraboloid for point lights (512x512 per face, max 8 lights) and perspective maps for spot lights (512x512, max 16 lights). These are fixed-resolution atlases with no dynamic allocation.

### What Is Needed

1. **Virtual Texture Shadow Atlas**: A large virtual texture (e.g., 16K x 16K virtual resolution) backed by a physical page pool (e.g., 4K x 4K actual GPU memory). Shadow pages are allocated on demand based on screen-space coverage.

2. **GPU-Driven Page Allocation**: A compute pass analyzes screen-space shadow texel density and marks pages for (re)rendering. Only dirty pages (where geometry has moved or the camera has shifted) are re-rendered.

3. **Clipmap for Directional Light**: Instead of fixed cascades, use a clipmap approach where the shadow map follows the camera with mip levels that provide uniform screen-space texel density.

4. **Caching**: Static geometry pages are cached across frames. Only moving objects invalidate their pages.

### Impact if Absent

- Shadow quality degrades visibly at cascade boundaries (banding)
- Point/spot light shadows at 512x512 are visibly blocky for close-up surfaces
- Fixed atlas limits constrain the number of shadow-casting lights
- Large open worlds cannot have consistent shadow quality at all distances

### Complexity: XL (8-12 weeks)

### Dependencies
- GPU-driven rendering (Gap 20)
- Virtual texture infrastructure (new subsystem)
- Render graph (Gap 15)

---

## Gap 4: Ray Tracing (DXR) Pipeline

### Why Critical

DXR ray tracing is the standard for premium-tier rendering in 2024+ titles. UE5, Unity 6, Frostbite, CryEngine, and id Tech 7 all ship with DXR support. Even when used selectively (reflections, shadows, AO), ray tracing dramatically improves visual quality over screen-space approximations.

### Current State in UzEngine

There is zero mention of DXR, ray tracing, acceleration structures (BLAS/TLAS), ray tracing pipeline state objects, or ray generation/intersection/miss shaders anywhere in the spec suite. The shader compiler targets SM 6.0 (`vs_6_0`, `ps_6_0`, `cs_6_0`); DXR requires at minimum SM 6.3 for `lib_6_3`.

### What Is Needed

1. **Acceleration Structure Management**: Build and maintain Bottom-Level Acceleration Structures (BLAS) for each unique mesh and Top-Level Acceleration Structures (TLAS) per frame for the scene. BLAS rebuilds on mesh deformation (skinned meshes); TLAS rebuilds every frame for dynamic objects with incremental updates.

2. **RT Pipeline State Object**: DXR RTPSO with ray generation, closest-hit, any-hit, miss, and optionally intersection shaders. These must integrate with the existing material system for shading at hit points.

3. **Ray-Traced Shadows**: Replace or supplement PCF shadows with 1-SPP ray-traced shadows + denoiser. This eliminates shadow map resolution artifacts entirely and provides correct shadows for any geometry (foliage, alpha-tested, thin objects). Denoiser options: NVIDIA NRD (REBLUR/SIGMA), or custom spatiotemporal filter.

4. **Ray-Traced Reflections**: Supplement SSR with ray-traced fallback for off-screen reflections. SSR fails when the reflected content is not visible in the current frame; RT reflections always produce correct results. Denoise with spatiotemporal filter.

5. **Ray-Traced Ambient Occlusion**: Short-range rays for ground truth AO. Eliminates the artifacts of screen-space HBAO+ (halos at screen edges, missing occlusion from off-screen geometry). 1 SPP + denoiser is sufficient.

6. **Shader Model 6.3+ Compilation**: Update DXC targets to support `lib_6_3` for DXR shader libraries.

### Impact if Absent

- Reflections are limited to screen-space (SSR), failing for off-screen content
- Shadows have visible aliasing and cannot handle complex geometry (foliage, hair)
- AO has screen-space artifacts (missing occlusion from off-screen, halo at edges)
- Cannot target the premium visual tier expected by 2025+ AAA titles
- Missing a key GPU feature that hardware vendors (NVIDIA, AMD, Intel) push

### Complexity: XL (10-16 weeks for the full stack)

### Dependencies
- DX12 Ultimate feature detection
- BLAS/TLAS build infrastructure
- Denoiser library integration (NRD or custom)
- Shader variant system update for RT libraries

---

## Gap 5: Deferred Decal System

### Why Critical

Decals (bullet holes, blood splatters, tire marks, dirt, graffiti) are used extensively in every AAA title. UE5 uses DBuffer decals that write into the G-Buffer before lighting. Unity HDRP supports both DBuffer and Screen-Space decals. Without a decal system, all surface detail must be baked into textures or handled by unique material instances.

### Current State in UzEngine

No decal system is mentioned anywhere in the specs. The G-Buffer layout in `screen-space-effects.md` (GBuffer0-3 + Depth) has no provision for decal blending or DBuffer channels.

### What Is Needed

1. **DBuffer Decals**: Before the G-Buffer pass, render decal volumes (oriented bounding boxes) that write to a DBuffer (albedo delta, normal delta, roughness/metallic override). The G-Buffer pass then composites DBuffer data. This allows decals to affect all G-Buffer channels.

2. **Screen-Space Decals (alternative)**: After the G-Buffer pass, project decal textures onto the depth buffer and modify G-Buffer targets in place. Simpler but cannot affect normal maps as cleanly.

3. **Decal Component and .uzscene Integration**: Decal node type with projected texture, size, normal influence, fade distance, sort priority.

4. **Decal Manager**: Batched rendering of many decals, atlas packing for decal textures, distance-based LOD/fade.

### Impact if Absent

- No dynamic surface detail (bullet marks, environmental storytelling)
- Artists must bake all surface variation into base textures
- Repeated textures are visually obvious (tiling artifacts)
- World feels static and non-interactive

### Complexity: L (3-4 weeks)

### Dependencies
- G-Buffer pass (existing)
- Shader variant for decal blending
- Material system extension for decal materials

---

## Gap 6: Bloom, Lens Flare, and Chromatic Aberration

### Why Critical

These post-process effects are standard in every modern engine and contribute significantly to visual fidelity and cinematic quality. UE5, Unity HDRP, and Frostbite all include physically-based bloom, procedural lens flares, and chromatic aberration.

### Current State in UzEngine

`renderer.md` lists bloom as "Dual-threshold downsample + additive upsample" which is the basic approach. There is no mention of lens flares (procedural or texture-based), chromatic aberration, lens distortion, film grain, or vignette (vignette is listed but minimal). The bloom implementation lacks:
- Energy conservation (bloom should not add energy to the image)
- Per-mip kernel variation (wider blur at lower mips)
- Physically-based bloom kernel (diffraction-based starburst pattern)

### What Is Needed

1. **Improved Bloom (Dual Kawase / 13-tap downscale)**: Replace basic dual-threshold with a multi-pass downscale/upscale chain using a wide kernel (Kawase blur or 13-tap tent filter as in Call of Duty). Add energy conservation by normalizing filter weights. Support per-mip tint for anamorphic/stylized bloom.

2. **Procedural Lens Flares**: Generate lens flare ghosts from bright spots in the image using a screen-space approach (sample bright pixels from downscaled mip, create ghost images by reflecting through screen center, apply chromatic shift). Support artist-authored flare textures for anamorphic streaks.

3. **Chromatic Aberration**: Lateral chromatic aberration by sampling R, G, B channels at slightly different UV offsets from screen center. Typically 1-3 pixel offset at edges. Radial distortion optional. Cost is negligible (single fullscreen pass).

4. **Film Grain**: Animated noise overlay (blue noise or procedural) with intensity control. Essential for cinematic rendering.

### Impact if Absent

- Bloom looks basic and unrealistic (uniform glow rather than optical diffraction)
- No lens character (the image looks like a framebuffer render, not a camera capture)
- Cinematic quality is noticeably below competitor engines

### Complexity: M (2-3 weeks for all four effects)

### Dependencies
- Post-process stack (existing)
- HDR render target (existing)

---

## Gap 7: Depth of Field (Bokeh DoF)

### Why Critical

Depth of Field is essential for cinematic rendering, cutscenes, and gameplay focus cues. UE5 provides circle-of-confusion based DoF with physically-based bokeh shapes. Unity HDRP has a multi-pass DoF with near/far field separation and bokeh sprite rendering. Frostbite pioneered tile-based bokeh.

### Current State in UzEngine

`engine-vision.md` mentions DoF in the camera system section (`dof = { focus=6, aperture=2.8 }`), and the ROADMAP lists "Depth-of-field: focal distance, f-stop, bloom integration" as a Camera System DoD item. However, there is no rendering spec for the DoF post-process shader, no algorithm choice (gathering vs scattering, circle-of-confusion), and no compute shader design.

### What Is Needed

1. **Circle-of-Confusion (CoC) Computation**: From depth buffer and camera parameters (focal distance, aperture, focal length), compute per-pixel CoC radius. This requires the camera's physical parameters.

2. **Near/Far Field Separation**: Split the scene into near-field (between camera and focus plane), in-focus, and far-field. Each requires different treatment to avoid bleeding artifacts.

3. **Bokeh Blur**: A gathering-based approach using a disc kernel (weighted by CoC) for far-field, and a scattering approach (sprite-based) for bright near-field bokeh. The disc kernel shape (circular, hexagonal, cat-eye) should be configurable.

4. **Half-Resolution Processing**: DoF blur at half resolution for performance, with edge-aware upscale.

### Impact if Absent

- No cinematic camera simulation
- Cutscenes lack depth and focus direction
- Gameplay cannot use focus as a narrative/mechanical tool

### Complexity: L (2-3 weeks)

### Dependencies
- Camera system physical parameters
- Depth buffer access (existing)
- Post-process stack (existing)

---

## Gap 8: Motion Blur

### Why Critical

Motion blur sells the perception of speed and smooth motion. Both per-object and camera motion blur are standard in UE5, Unity HDRP, and Frostbite. Without motion blur, fast-moving objects exhibit temporal aliasing (strobing) even at 60 FPS.

### Current State in UzEngine

The velocity buffer in `screen-space-effects.md` (GBuffer3: RG16F screen-space motion vectors) is already specified for TAA reprojection. This is the exact data needed for motion blur. However, motion blur itself is not specified anywhere.

### What Is Needed

1. **Per-Object Motion Blur**: Use the existing velocity buffer to blur pixels along their motion direction. A tile-based approach (McGuire-Lottes 2012) groups pixels into tiles, computes dominant motion per tile, and applies directional blur along the motion vector. The existing velocity buffer from the TAA spec is the input.

2. **Camera Motion Blur**: For static objects, camera rotation and translation produce motion vectors. These are already in the velocity buffer (computed from `prev_view_proj`).

3. **Quality Tiers**: Variable sample count (4/8/16 taps along the motion vector). Tile-based early-out for stationary tiles.

4. **Integration**: Insert between tonemapping and TAA in the post-process chain.

### Impact if Absent

- Fast camera pans look stuttery
- Moving objects strobe instead of blurring naturally
- Racing/action games lack the visual feedback of speed
- The velocity buffer infrastructure (already specced for TAA) is underutilized

### Complexity: M (1-2 weeks, much infrastructure already exists)

### Dependencies
- Velocity buffer (specced in `screen-space-effects.md`)
- Post-process stack (existing)

---

## Gap 9: Volumetric Fog and Atmospheric Scattering

### Why Critical

Volumetric effects define the mood and atmosphere of a scene. UE5 has Volumetric Fog and Exponential Height Fog with volumetric light shafts. Unity HDRP has a froxel-based volumetric fog system. Frostbite pioneered the froxel approach used widely today. God rays through trees, volumetric spotlights in dusty rooms, and atmospheric haze are expected in AAA.

### Current State in UzEngine

`global_cbuffer.hlsli` defines basic analytical fog parameters: `g_FogColor`, `g_FogDensity`, `g_FogStart`, `g_FogEnd`, `g_FogMode` (off/linear/exponential/exp2). The `apply_fog()` function computes distance-based fog attenuation. This is 1990s-era fog with no volumetric component, no light scattering, no height variation, and no shadow-aware extinction.

### What Is Needed

1. **Froxel-Based Volumetric Fog**: Divide the view frustum into a 3D grid of froxels (frustum voxels), typically 160x90x64. For each froxel, accumulate scattering and extinction from local lights and ambient. March through froxels front-to-back to integrate in-scattered light. This provides volumetric spotlights, light shafts, and fog density variation.

2. **Light Injection**: For each light (directional, point, spot), inject scattering contribution into affected froxels. Shadow maps modulate the injection (shadow-aware fog).

3. **Height Fog with Density Falloff**: Exponential height-based density falloff (denser at ground level, fading with altitude). This is essential for outdoor scenes.

4. **Temporal Reprojection**: Reuse previous frame's froxel data with jittered ray offsets to reduce per-frame sample count. Uses the velocity buffer for reprojection.

5. **God Rays / Volumetric Light Shafts**: Either as part of the froxel system (natural result of shadow-modulated light injection) or as a dedicated screen-space radial blur from bright light sources.

### Impact if Absent

- No volumetric lighting (a defining visual feature of modern games)
- Interiors lack depth and atmosphere
- Fog is a flat uniform tint with no variation or light interaction
- Forest/dungeon/cave scenes look flat without god rays and volumetric light

### Complexity: L (3-5 weeks)

### Dependencies
- Shadow maps (existing, for shadow-aware injection)
- Tile/cluster light list (existing, reuse for froxel light assignment)
- Compute shader infrastructure (existing)

---

## Gap 10: Sky and Atmosphere System

### Why Critical

A physically-based sky model (Rayleigh + Mie scattering) is standard in UE5 (Sky Atmosphere component), Unity HDRP (Physically Based Sky), and Frostbite. It provides realistic sky colors at all times of day, aerial perspective (distant objects take on atmospheric color), and natural integration with volumetric fog and clouds.

### Current State in UzEngine

`renderer.md` step 9 renders a skybox using a cubemap (`skybox.hlsl`). This is a static pre-baked sky image with no dynamic time-of-day, no atmospheric scattering, and no aerial perspective.

### What Is Needed

1. **Bruneton Atmospheric Scattering**: Precompute transmittance, single-scattering, and multi-scattering lookup tables (LUTs) using the Bruneton 2017 model. At runtime, a fullscreen pass samples these LUTs to render the sky dome. Parameters: planet radius, atmosphere height, Rayleigh/Mie scattering coefficients, Mie anisotropy.

2. **Aerial Perspective**: Apply atmospheric scattering to distant objects based on their depth. Objects far from the camera take on the color of the atmosphere between them and the camera. This is sampled from the same LUTs.

3. **Dynamic Time of Day**: Varying the sun direction angle recomputes the sky. The LUTs can be updated incrementally or precomputed for a set of sun angles.

4. **Sun Disk**: Physically-based sun rendering in the sky dome with limb darkening.

5. **Volumetric Clouds** (stretch goal): Ray-marched cloud layer using noise textures (Worley/Perlin), with temporal reprojection for performance. This is a significant feature (2-4 weeks alone) but transforms outdoor scenes.

### Impact if Absent

- Sky is a static cubemap; time of day requires swapping cubemaps
- Distant objects have no atmospheric haze (breaks depth perception)
- Outdoor scenes look artificial without physically-based sky color
- No sunset/sunrise transitions

### Complexity: L (3-4 weeks for atmosphere; +4 weeks for volumetric clouds)

### Dependencies
- Post-process infrastructure (existing)
- GlobalParams for sun direction (existing wind direction pattern can be extended)

---

## Gap 11: Water Rendering

### Why Critical

Water is a prominent feature in most game environments. UE5 Water system provides FFT-based ocean waves, shoreline foam, underwater caustics, and refraction. Unity HDRP has water rendering with Gerstner waves and screen-space refraction. Frostbite pioneered modern water rendering in Battlefield.

### Current State in UzEngine

`terrain-system.md` section 9 specifies a basic water plane: flat mesh at a fixed height, scrolling normal maps, Fresnel-based blending of shallow/deep color, and edge foam from depth buffer. There is no wave displacement, no FFT ocean, no underwater rendering, no caustics, no flow maps, and no refraction beyond "simple screen-space refraction."

### What Is Needed

1. **Gerstner Waves (minimum)**: GPU vertex displacement using sum-of-Gerstner-waves formula. Parameters: wave count, direction, amplitude, frequency, steepness. Computed in the vertex shader for the water mesh. Much cheaper than FFT and sufficient for most game scenarios.

2. **FFT Ocean** (premium tier): Compute shader FFT on a Tessendorf ocean spectrum. Produces displacement map, normal map, and foam/Jacobian map. This is the standard for AAA ocean rendering (Assassin's Creed, Sea of Thieves).

3. **Screen-Space Refraction**: Distort the underwater scene buffer by sampling at offset UVs based on the water surface normal. Requires a copy of the scene color before water rendering.

4. **Underwater Rendering**: When the camera is below the water surface, apply volumetric fog tint, caustic patterns projected onto underwater surfaces, and a distorted water surface seen from below.

5. **Flow Maps**: UV distortion maps painted in the editor that control water flow direction (rivers, streams, waterfalls).

6. **Shore Foam**: Depth-based foam (existing basic version), plus Jacobian-based foam for wave crests.

### Impact if Absent

- Water is a flat, unconvincing plane
- No wave motion breaks immersion immediately
- Underwater gameplay is impossible
- Rivers and streams cannot have directional flow

### Complexity: L (3-4 weeks for Gerstner + refraction; +3 weeks for FFT ocean)

### Dependencies
- Terrain system (existing water plane baseline)
- Compute shader infrastructure (existing)
- Scene color copy pass (new)

---

## Gap 12: Subsurface Scattering (SSS)

### Why Critical

SSS is essential for realistic skin, wax, marble, leaves, and any translucent material. Without SSS, skin looks like plastic. UE5 has Burley SSS in its Substrate material model. Unity HDRP has Diffusion Profiles. Frostbite uses separable screen-space SSS.

### Current State in UzEngine

The PBR material model in `renderer.md` has no subsurface scattering parameter. The BRDF in `brdf.hlsli` is purely surface-reflective (Lambertian diffuse + Cook-Torrance specular). There are no diffusion profiles, no SSS texture channels, and no screen-space SSS blur pass.

### What Is Needed

1. **Diffusion Profile System**: Define SSS profiles (skin, wax, jade, foliage) with scatter radius and scatter color. Stored as a look-up table indexed by material.

2. **Separable Screen-Space SSS (SSSSS)**: Jimenez et al. 2015 -- two-pass separable Gaussian blur in screen space, weighted by diffusion profile. Applied only to pixels flagged as SSS materials (via a stencil bit in the G-Buffer or a material flag channel).

3. **G-Buffer Extension**: Add a material type flag (or SSS profile index) to the G-Buffer. This can use the unused alpha channel of GBuffer2 (currently `Unused (A)`).

4. **Material System Extension**: Add `subsurface_color`, `subsurface_radius`, `subsurface_profile` parameters to `.uzmat`.

5. **Transmission / Translucency**: Thin-surface transmission for leaves and ears (light passing through from behind). A simplified model using thickness maps (precomputed or artist-painted) and back-lighting evaluation.

### Impact if Absent

- All character faces look like plastic mannequins
- Organic materials (plants, food, candles) appear opaque and lifeless
- Significant visual quality gap vs any engine with SSS

### Complexity: M (2-3 weeks)

### Dependencies
- G-Buffer extension (existing layout has unused channel)
- Material system extension
- Post-process pass (new SSS blur pass)

---

## Gap 13: Hair Rendering

### Why Critical

Realistic hair rendering is a defining feature of AAA character rendering (UE5 Groom, Unity hair system, Frostbite Frostbite Hair). Without specialized hair rendering, characters use opaque mesh proxies that lack the visual quality of strand-based or card-based hair with proper light transport.

### Current State in UzEngine

No hair rendering is specified. The material model has no anisotropic specular, no Kajiya-Kay or Marschner BSDF, no hair strand data structures, and no order-independent transparency for overlapping hair strands.

### What Is Needed

1. **Hair Card Pipeline (minimum viable)**: Textured alpha-masked polygonal strips representing hair clumps. Requires alpha-to-coverage or alpha testing in the forward pass. The specular lobe uses a modified Kajiya-Kay model (anisotropic along the hair strand tangent).

2. **Kajiya-Kay BRDF**: An anisotropic specular model that shifts the specular highlight along the hair fiber direction. Two lobes (primary specular R, secondary transmission TRT) for realistic hair shading. Implemented as a new BRDF path in the material system.

3. **Strand-Based Hair** (premium tier): Individual hair strands rendered as line primitives or mesh strips. Requires strand data (position, tangent per control point), GPU simulation for dynamics, and order-independent transparency (OIT) for correct blending.

4. **Order-Independent Transparency (OIT)**: Per-Pixel Linked Lists or Weighted Blended OIT for sorting many overlapping transparent hair layers. The current back-to-front sort in the forward transparent pass cannot handle hundreds of overlapping hair strands.

### Impact if Absent

- Character hair looks blocky and opaque
- No realistic hair highlights or translucency
- Cannot achieve realistic human characters

### Complexity: L (2-3 weeks for hair cards + Kajiya-Kay; XL for strand-based + OIT)

### Dependencies
- Forward transparent pass (existing)
- Anisotropic BRDF extension
- Material system extension

---

## Gap 14: Eye Rendering

### Why Critical

Realistic eyes are a known "uncanny valley" challenge. UE5 has a dedicated eye shader model with refraction, iris parallax, caustics, and sclera SSS. Unity HDRP has a similar eye shader. Eyes are the focal point of character close-ups and wrong rendering is immediately noticeable.

### Current State in UzEngine

No eye-specific rendering. Standard PBR materials would treat eyes as a flat textured surface.

### What Is Needed

1. **Parallax Refraction**: The cornea acts as a refractive dome over the iris. Fake this with a parallax offset that shifts the iris texture UV based on the view angle, simulating depth.

2. **Limbal Darkening Ring**: The boundary between iris and sclera darkens with a Fresnel-based falloff.

3. **Sclera SSS**: The white of the eye has subsurface scattering (red/pink tint visible at the edges). Reuses the SSS infrastructure from Gap 12.

4. **Specular Caustic**: The cornea focuses specular reflections into a caustic highlight on the iris. Approximated with an offset specular sampling.

5. **Dual Specular Lobes**: A sharp lobe for the cornea reflection and a soft lobe for the iris/sclera.

### Impact if Absent

- Character close-ups look artificial
- Eyes appear flat and dead
- Uncanny valley effect in any character-focused scene

### Complexity: M (1-2 weeks; mostly shader work)

### Dependencies
- SSS system (Gap 12, for sclera)
- Material system extension
- Custom shader path in variant system

---

## Gap 15: Render Graph / Frame Graph

### Why Critical

A render graph (also called frame graph) provides automatic resource lifetime management, automatic barrier/transition insertion, and optimal pass scheduling with aliasing. Frostbite's FrameGraph paper (2017) established this as the standard architecture. UE5's RDG (Render Dependency Graph), Unity's Render Graph API (SRP), and id Tech 7 all use this pattern.

### Current State in UzEngine

`renderer.md` shows a "Frame Graph (Simplified)" that is a static ordered list of passes with manually specified dependencies. The render pass order is hardcoded with sort keys (`ShadowPass=100`, `DepthPrepass=200`, etc., per memory). There is no automatic resource lifetime tracking, no transient resource aliasing, and no automatic barrier insertion.

### What Is Needed

1. **Pass Declaration API**: Each pass declares its read/write resources. The graph builder collects these declarations.

2. **Resource Lifetime Analysis**: The graph determines the lifetime of each transient resource (render targets, UAV buffers) and aliases GPU memory between non-overlapping lifetimes. This can save 30-50% of transient render target memory.

3. **Automatic Barrier Insertion**: Based on read/write declarations, the graph inserts `ResourceBarrier` calls at the correct points. This eliminates an entire class of bugs (missing transitions, redundant transitions).

4. **Pass Culling**: Passes whose outputs are not consumed by any subsequent pass can be automatically culled.

5. **Async Compute Scheduling**: Passes that only use compute can be scheduled on the async compute queue in parallel with graphics passes (e.g., SSAO compute in parallel with the transparent forward pass).

### Impact if Absent

- Adding new passes requires manual barrier management (error-prone)
- GPU memory is not optimally aliased (higher memory consumption)
- No async compute (leaving GPU resources underutilized)
- Pass ordering bugs cause subtle corruption (barriers are DX12's #1 bug category)
- Scaling the renderer to 20+ passes becomes a maintenance nightmare

### Complexity: L (3-5 weeks)

### Dependencies
- None (this is infrastructure that all passes benefit from)
- Should be implemented before adding many more passes

---

## Gap 16: Multi-View Rendering (VR / Split-Screen)

### Why Critical

VR support is increasingly important (Meta Quest, PlayStation VR2, Steam VR). Split-screen is needed for local multiplayer. Both require rendering the scene from multiple viewpoints efficiently. UE5 has instanced stereo rendering. Unity has Single-Pass Instanced VR.

### Current State in UzEngine

The renderer is single-view only. There is no mention of VR, stereo rendering, or multi-view instancing. The per-frame CBV has a single `view_proj` matrix.

### What Is Needed

1. **Instanced Stereo Rendering**: Render both eyes in a single pass using `SV_ViewID` or instance ID to select the view/projection matrix. This halves the draw call count for VR.

2. **Multi-View Extension**: Arrays of view-projection matrices in the per-frame CBV. Render targets are texture arrays (one slice per view).

3. **Split-Screen Support**: Viewport scissoring with per-player camera parameters.

4. **VR SDK Integration**: OpenXR for cross-platform VR headset support.

### Impact if Absent

- No VR support (growing market segment)
- No local multiplayer rendering
- Cannot target Meta Quest, PSVR2, or PC VR

### Complexity: L (3-4 weeks for instanced stereo; +2 weeks for OpenXR)

### Dependencies
- Render graph (Gap 15, for managing doubled render targets)
- Per-frame CBV extension

---

## Gap 17: Variable Rate Shading (VRS)

### Why Critical

VRS (DX12 Tier 2) reduces pixel shader invocations in areas that tolerate lower resolution (e.g., peripheral vision, fast-moving objects, low-contrast regions). UE5, Unity, and Frostbite all support VRS. It provides 10-30% GPU performance recovery with minimal visual impact.

### Current State in UzEngine

No mention of VRS anywhere in the specs. The renderer runs at uniform shading rate.

### What Is Needed

1. **Screen-Space VRS Image Generation**: A compute pass that analyzes the scene (motion vectors, luminance gradients, depth discontinuities) and generates a VRS rate image (per-tile shading rate: 1x1, 1x2, 2x1, 2x2, 2x4, 4x2, 4x4).

2. **DX12 VRS API Integration**: `ID3D12GraphicsCommandList5::RSSetShadingRate()` and `RSSetShadingRateImage()`. Requires Tier 1 (per-draw) or Tier 2 (per-tile image) support check.

3. **Foveated Rendering** (VR extension): In VR, use eye-tracking data to set high shading rate at the gaze point and low rate in the periphery.

### Impact if Absent

- 10-30% free GPU performance left on the table
- No foveated rendering for VR (critical for standalone headsets)

### Complexity: M (1-2 weeks)

### Dependencies
- DX12 Tier 2 VRS feature detection
- Motion vector buffer (existing from TAA)

---

## Gap 18: Mesh Shader Pipeline

### Why Critical

Mesh shaders (DX12 SM 6.5, Vulkan VK_EXT_mesh_shader) replace the traditional Input Assembler + Vertex Shader pipeline with a more flexible Amplification Shader + Mesh Shader model. This enables GPU-driven geometry processing, per-meshlet culling, and procedural geometry generation without CPU involvement. id Tech 7, UE5 (Nanite), and the latest Unity experimental builds all use mesh shaders.

### Current State in UzEngine

The shader system targets SM 6.0 (`vs_6_0`, `ps_6_0`, `cs_6_0`). There is no mention of amplification shaders, mesh shaders, or `ms_6_5`/`as_6_5` targets.

### What Is Needed

1. **DXC Target Update**: Compile mesh shaders with `-T ms_6_5` and amplification shaders with `-T as_6_5`.

2. **Meshlet Data Structures**: Per-mesh arrays of meshlets (vertex indices, primitive indices, AABB, cone) generated by `meshopt_buildMeshlets`.

3. **Mesh Shader Pipeline State**: `D3D12_PIPELINE_STATE_STREAM_DESC` with mesh shader + pixel shader (no input layout, no vertex shader).

4. **Amplification Shader for Culling**: Per-meshlet frustum + cone culling in the AS, dispatching surviving meshlets to the MS.

5. **Fallback Path**: Traditional VS/IA pipeline for hardware that lacks mesh shader support.

### Impact if Absent

- Cannot adopt GPU-driven rendering efficiently
- Meshlet culling requires compute pre-pass + indirect draw instead of native AS/MS
- Cannot render Nanite-style geometry pipelines

### Complexity: L (3-4 weeks)

### Dependencies
- Virtual Geometry pipeline (Gap 2)
- DX12 SM 6.5 feature detection

---

## Gap 19: Bindless Resources

### Why Critical

Bindless resource access (DX12 descriptor heap indexing, Vulkan descriptor indexing) eliminates the descriptor table binding bottleneck. Instead of binding textures/buffers per draw call via root signature slots, all resources are placed in a global descriptor heap and accessed by index. UE5, Frostbite, id Tech 7, and modern Unity all use bindless patterns.

### Current State in UzEngine

`renderer.md` uses traditional descriptor tables: `t0..t7` for SRVs, `s0..s3` for samplers. This limits the number of textures per draw call and requires descriptor table switches when materials change. The root signature layout is fixed and small.

### What Is Needed

1. **Global SRV Heap**: A single `CBV_SRV_UAV` heap containing ALL textures and buffers. Each resource gets a persistent index.

2. **Material Buffer**: A `StructuredBuffer<MaterialData>` containing per-material texture indices (albedo_index, normal_index, etc.) instead of binding textures to fixed slots.

3. **Shader-Side Indexing**: `ResourceDescriptorHeap[index]` in SM 6.6, or `Texture2D textures[]` unbounded arrays in SM 6.0+.

4. **Per-Draw Material Index**: Instead of switching descriptor tables, pass a single material index (uint32) via root constants. The shader uses this to fetch texture handles from the material buffer.

### Impact if Absent

- Descriptor table switching is a significant CPU and GPU cost for scenes with many materials
- Draw call merging is limited (can only merge if materials share the same descriptor table)
- Cannot efficiently implement GPU-driven rendering (Gap 20) without bindless
- Limits scalability to hundreds of unique materials per scene

### Complexity: M (2-3 weeks)

### Dependencies
- SM 6.0+ (existing) or SM 6.6 for `ResourceDescriptorHeap`
- Material system refactor

---

## Gap 20: GPU-Driven Rendering (Indirect Draw Calls)

### Why Critical

GPU-driven rendering eliminates CPU involvement in per-draw-call decisions. The GPU performs culling, LOD selection, and draw call generation via compute shaders and `ExecuteIndirect`. This is how Frostbite, UE5 Nanite, and id Tech 7 achieve high object counts with minimal CPU overhead. The terrain foliage spec already hints at this pattern (`ExecuteIndirect` for foliage).

### Current State in UzEngine

Draw call submission is CPU-driven (`renderer.md` step 2: "Build Draw Lists -- CPU"). Frustum culling is CPU-side. `BatchBuilder` (instancing spec) groups entities on the CPU. Only the terrain foliage system mentions `ExecuteIndirect`.

### What Is Needed

1. **GPU Frustum + Occlusion Culling**: A compute shader that tests per-object or per-meshlet bounds against the frustum and HZB. Outputs a compacted list of visible objects.

2. **Indirect Argument Buffer Generation**: The compute shader writes `D3D12_DRAW_INDEXED_ARGUMENTS` into a buffer. `ID3D12GraphicsCommandList::ExecuteIndirect` consumes this buffer to issue draws without CPU involvement.

3. **GPU LOD Selection**: Based on screen-space size (distance / projected size), select LOD level per object in the compute shader.

4. **Multi-Draw Indirect (MDI)**: Batch multiple indirect draw calls into a single `ExecuteIndirect` with a command signature that changes the per-draw root constant (material index).

### Impact if Absent

- CPU becomes the bottleneck at ~5000-10000 objects (common in open-world scenes)
- Cannot scale to dense foliage, particle-heavy, or architectural visualization scenes
- The GPU is often idle while waiting for CPU draw call submission

### Complexity: L (3-5 weeks)

### Dependencies
- Bindless resources (Gap 19)
- HZB / Occlusion culling (Gap 27)
- Mesh shader pipeline (Gap 18, optional but beneficial)

---

## Gap 21: Clustered / Tiled Deferred Light Culling Improvements

### Why Critical

The current tile-based light culling (16x16 pixel tiles) is a solid foundation but has known limitations: it cannot cull lights along the depth axis (a light behind a wall still affects the entire tile). Clustered shading (dividing the frustum into 3D clusters) solves this and is the standard in Frostbite, Unity HDRP, and modern Doom.

### Current State in UzEngine

`point-spot-light-shadows.md` describes a 2D tile-based light culling compute pre-pass (16x16 tiles, min/max depth per tile). This is the Forward+ approach. The tile stores up to 32 point lights + 32 spot lights per tile.

### What Is Needed

1. **Clustered Shading**: Replace 2D tiles with 3D clusters (e.g., 16x16x24 or 64x64x16 in depth slices using exponential distribution). Each cluster is a smaller frustum volume that tightly bounds a depth range.

2. **Depth Slicing**: Exponential depth distribution concentrates more clusters near the camera where precision matters.

3. **Cluster Light Assignment**: Compute shader tests each light against each cluster (sphere-frustum or cone-frustum intersection). The per-cluster light list is much tighter than per-tile, reducing false positives.

4. **Area Lights** (stretch): Once clustered infrastructure exists, add sphere and tube area light types with linearly-transformed cosines (LTC) for shading.

### Impact if Absent

- Scenes with many lights at different depths waste GPU on false-positive evaluations
- Cannot efficiently handle "light heavy" scenes (hundreds of point/spot lights in a building)
- Area lights (tube, sphere, rect) cannot be efficiently culled without 3D clustering

### Complexity: M (2 weeks to upgrade from tiled to clustered)

### Dependencies
- Existing tile-based culling (refactor)
- Depth buffer access (existing)

---

## Gap 22: Reflection Probes and Irradiance Volumes

### Why Critical

Reflection probes provide local cubemap reflections for indoor environments. Irradiance volumes provide local indirect diffuse. These are standard in UE5 (Reflection Captures + irradiance fields), Unity HDRP (Reflection Probes + APV), and Frostbite. Without them, SSR fails for off-screen content and there is no indoor indirect lighting variation.

### Current State in UzEngine

No reflection probes, no irradiance volumes, no cubemap capture system. SSR in `screen-space-effects.md` mentions "IBL fallback for roughness" but there is no spec for how this IBL cubemap is generated or how multiple local cubemaps are blended.

### What Is Needed

1. **Reflection Probe Component**: A scene node that captures a local cubemap (or uses an artist-provided one). Supports box and sphere influence volumes with parallax correction.

2. **Probe Capture Pipeline**: Render the scene from the probe position into a cubemap, convolve for roughness mip levels. Can be baked offline or updated at runtime.

3. **Probe Blending**: When multiple probes overlap, blend their cubemaps based on the pixel's position within each influence volume. Fallback to the sky cubemap outside all probes.

4. **Irradiance Volumes**: 3D grids of SH probes for local indirect diffuse. Updated at bake time or dynamically via short ray casts.

### Impact if Absent

- Indoor reflections are either missing (SSR fails for off-screen) or use a single sky cubemap
- All indoor indirect lighting is uniform (no variation between rooms)
- Metallic surfaces look wrong in enclosed spaces

### Complexity: L (2-3 weeks)

### Dependencies
- GI system (Gap 1, for irradiance)
- Material system (for probe blending selection)
- Render graph (Gap 15, for probe capture passes)

---

## Gap 23: Material Layering System

### Why Critical

Modern material systems (UE5 Substrate, Unity HDRP Layered Lit, Frostbite material model) support layering multiple materials on a single surface with blend masks. This enables effects like rust over metal, moss on stone, or wet surfaces, without requiring unique textures for every combination.

### Current State in UzEngine

The material system in `renderer.md` is a flat parameter set: albedo, roughness, metallic, normal, AO, emission, alpha. The terrain system uses 4-layer splatmap blending, but this is terrain-specific. General meshes have no layering capability.

### What Is Needed

1. **Layered Material Type**: A `.uzmat` can define 2-4 material layers, each with its own albedo/normal/roughness/metallic, plus a blend mask (texture or vertex color channel).

2. **Blend Modes**: Linear interpolation, height-blend (using a height map to create natural transitions), or multiplicative overlay.

3. **Per-Pixel Evaluation**: The pixel shader evaluates each layer and blends based on the mask. This requires a shader variant (`MATERIAL_LAYERS=2`, `MATERIAL_LAYERS=3`, etc.).

4. **Macro Detail**: A large-scale detail texture that breaks up tiling by modulating roughness/normal at macro scale.

### Impact if Absent

- Surface detail requires unique textures for every material combination
- Cannot do dynamic effects (wetness, snow accumulation, wear)
- Artists cannot achieve the material complexity expected in AAA

### Complexity: M (2-3 weeks)

### Dependencies
- Shader variant system (existing)
- Material system extension
- Additional texture slots (may exceed current t0-t7 limit -- requires bindless Gap 19)

---

## Gap 24: Procedural Texture Generation

### Why Critical

Runtime procedural textures (noise patterns, gradients, tiled patterns, wear maps) reduce texture memory and enable infinite variety. Substance-style node graphs at runtime are used in Frostbite and some UE5 workflows.

### Current State in UzEngine

`noise.hlsli` provides Perlin, simplex, and FBM noise functions for shader use. There is no procedural texture system (no compute-based texture generation, no noise-to-texture baking).

### What Is Needed

1. **Compute Texture Generator**: A system that dispatches compute shaders to generate textures (noise, Voronoi, cellular, gradient) into GPU textures at load time or on demand.

2. **Texture Node Graph** (stretch): A visual node graph (similar to Substance Designer) that defines procedural texture recipes. Output is a generated GPU texture.

3. **Detail Noise Textures**: Pre-generate noise textures (blue noise for dithering, Halton for sampling, curl noise for particles) at engine startup. Currently, `noise.hlsli` computes noise per pixel per frame which is wasteful for static patterns.

### Impact if Absent

- All textures must be authored externally
- Higher texture memory usage (no procedural generation)
- Missing blue noise / dithering textures for sampling patterns

### Complexity: M (1-2 weeks for compute generator; L for node graph)

### Dependencies
- Compute shader infrastructure (existing)
- Noise functions (existing in `noise.hlsli`)

---

## Gap 25: Terrain Rendering Pipeline Improvements

### Why Critical

The terrain spec provides a functional baseline (heightmap, splatmap, 4 LOD levels, Jolt collision) but lacks features expected in AAA terrain: virtual texturing for large terrains, more than 4 material layers, tessellation for smooth LOD transitions, cliff material projection, and holes/caves.

### Current State in UzEngine

`terrain-system.md` specifies 4-layer splatmap (RGBA), 4 LOD levels with geomorphing, 1024x1024m max size (4096 with Jolt limit), and simple foliage scattering.

### What Is Needed

1. **Virtual Texturing for Terrain**: Instead of tiling 4 material layers, composite them offline into a virtual texture that streams tiles on demand. This supports unlimited layers without shader cost scaling.

2. **More Material Layers**: 8-16 layers using multiple splatmaps or virtual texturing.

3. **Hardware Tessellation or Continuous LOD**: Instead of fixed 4-LOD meshes, use tessellation stages (Hull + Domain shader) for continuous LOD with no popping.

4. **Terrain Holes**: Support cut-outs in the terrain for caves, tunnels, and basements. Requires stencil-based or vertex-discard approach.

5. **Tri-Planar Projection for Cliffs**: Steep surfaces need tri-planar UV projection instead of top-down UV to avoid texture stretching.

6. **Distance Blending**: Reduce tiling artifacts by blending between macro and micro detail based on distance.

### Impact if Absent

- Limited to 4 terrain materials (insufficient for varied landscapes)
- LOD transitions may pop visually despite geomorphing
- Cliff faces have stretched textures
- No terrain holes for caves or buildings with basements

### Complexity: L (3-4 weeks for the full set)

### Dependencies
- Terrain system (existing baseline)
- Virtual texturing infrastructure (new, shared with Gap 3)

---

## Gap 26: Impostor / Billboard LOD System

### Why Critical

For distant objects (trees, rocks, props), rendering full 3D meshes is wasteful. Impostors use pre-captured billboard images that replace geometry at extreme distances. This is standard in UE5 (HLOD system), Unity (SpeedTree LOD billboard), and every open-world engine.

### Current State in UzEngine

meshoptimizer provides LOD simplification (3 levels per mesh). Terrain foliage has `cull_distance` but no impostor replacement. Beyond cull distance, objects simply disappear.

### What Is Needed

1. **Octahedral Impostor Capture**: Pre-render an object from multiple angles (e.g., 12x12 hemisphere) into an atlas. At runtime, sample the two closest views and blend.

2. **Billboard Crossfade**: Smooth transition from 3D mesh LOD to billboard using alpha dithering (screen-door transparency).

3. **HLOD System**: Hierarchical Level of Detail -- merge clusters of distant objects into simplified proxy meshes. Automatically generated at cook time.

### Impact if Absent

- Distant objects either render at full geometry cost or pop out of existence
- Open-world draw distance is limited
- Forest/vegetation scenes are GPU-bound at distance

### Complexity: M (2-3 weeks)

### Dependencies
- Asset cook pipeline (for impostor capture)
- Alpha dithering shader support

---

## Gap 27: Occlusion Culling (HZB / Software Rasterizer)

### Why Critical

Occlusion culling eliminates rendering of objects hidden behind other objects. The Hierarchical Z-Buffer (HZB) approach is standard in UE5, Unity, and Frostbite. A software rasterizer for occluder depth testing (as in Nanite) provides exact occlusion for complex scenes.

### Current State in UzEngine

`renderer.md` mentions frustum culling (CPU-side, step 1) and the depth prepass (step 4). `screen-space-effects.md` specifies a Hi-Z mip chain for SSR. However, the Hi-Z is used only for SSR ray marching, not for occlusion culling. There is no HZB-based occlusion test.

### What Is Needed

1. **HZB Generation**: After the depth prepass, generate a hierarchical Z-buffer mip chain (compute shader, max reduction per mip). This is partially specified for SSR but needs to be available before draw call submission.

2. **GPU Occlusion Test**: For each object AABB, test against the HZB. Objects fully occluded are removed from the draw list. This can be done in the GPU culling compute pass (Gap 20).

3. **Two-Phase Occlusion Culling**: Phase 1 (previous frame's HZB): cull objects that were occluded last frame. Phase 2 (current frame's depth prepass HZB): re-test objects that passed Phase 1 but might be newly occluded.

4. **Software Rasterizer** (optional, Nanite-level): For very dense scenes, rasterize simplified occluder geometry on the GPU via compute, generating the HZB without the depth prepass.

### Impact if Absent

- Indoor scenes waste GPU rendering objects behind walls
- Dense urban environments render far more geometry than visible
- Overdraw increases with scene complexity (no early rejection)

### Complexity: M (2-3 weeks for HZB culling; XL for software rasterizer)

### Dependencies
- Hi-Z mip chain (partially exists for SSR)
- GPU-driven rendering (Gap 20, for GPU-side culling integration)

---

## Gap 28: Explicit Async Compute

### Why Critical

DX12 supports multiple command queues (Graphics, Compute, Copy) that execute in parallel on the GPU. Properly overlapping compute work (SSAO, Hi-Z, particle simulation, light culling) with graphics work (shadow rendering, G-Buffer fill) can improve GPU utilization by 15-30%. Frostbite, UE5, and id Tech 7 all leverage async compute extensively.

### Current State in UzEngine

All work appears to execute on a single graphics command list. The render pass ordering (sort keys 100-1000) is a serial sequence. There is no mention of multiple command queues, fences, or async overlap.

### What Is Needed

1. **Compute Command Queue**: Create a dedicated `D3D12_COMMAND_LIST_TYPE_COMPUTE` queue.

2. **Fence-Based Synchronization**: Use `ID3D12Fence` to signal/wait between graphics and compute queues at defined sync points.

3. **Overlap Candidates**:
   - SSAO compute overlaps with shadow pass (shadow is graphics-only)
   - Hi-Z generation overlaps with transparency pass
   - Particle simulation overlaps with shadow or G-Buffer pass
   - Light culling overlaps with depth prepass

4. **Render Graph Integration**: The render graph (Gap 15) should automatically detect async compute opportunities from pass dependency analysis.

### Impact if Absent

- GPU ALUs sit idle during graphics-bound passes (shadow, G-Buffer fill)
- Compute-heavy post-processing adds serially to frame time instead of overlapping
- 15-30% GPU throughput opportunity missed

### Complexity: M (2-3 weeks; tightly coupled with render graph)

### Dependencies
- Render graph (Gap 15)
- DX12 multiple queue setup

---

## Summary Table

| # | Gap | Priority | Complexity | Phase Fit |
|---|-----|----------|-----------|-----------|
| 1 | Global Illumination (IBL + probes + SSGI) | Critical | XL | Phase 2-3 |
| 2 | Virtual Geometry (meshlets, GPU culling) | Important | XL | Phase 4+ |
| 3 | Virtual Shadow Maps | Important | XL | Phase 4+ |
| 4 | Ray Tracing (DXR) | Important | XL | Phase 3-4 |
| 5 | Deferred Decal System | Critical | L | Phase 2 |
| 6 | Bloom + Lens Flare + Chromatic Aberration | High | M | Phase 2 |
| 7 | Depth of Field (Bokeh DoF) | High | L | Phase 3 |
| 8 | Motion Blur | High | M | Phase 3 |
| 9 | Volumetric Fog and Atmospheric Scattering | Critical | L | Phase 3 |
| 10 | Sky / Atmosphere System | Critical | L | Phase 2-3 |
| 11 | Water Rendering (waves, refraction) | High | L | Phase 4 |
| 12 | Subsurface Scattering (SSS) | High | M | Phase 3 |
| 13 | Hair Rendering | Medium | L | Phase 4 |
| 14 | Eye Rendering | Medium | M | Phase 4 |
| 15 | Render Graph / Frame Graph | Critical | L | Phase 2 |
| 16 | Multi-View Rendering (VR) | Medium | L | Phase 4+ |
| 17 | Variable Rate Shading (VRS) | Medium | M | Phase 4 |
| 18 | Mesh Shader Pipeline | Important | L | Phase 4 |
| 19 | Bindless Resources | Critical | M | Phase 2 |
| 20 | GPU-Driven Rendering | Important | L | Phase 3-4 |
| 21 | Clustered Light Culling | High | M | Phase 2 |
| 22 | Reflection Probes + Irradiance Volumes | Critical | L | Phase 2-3 |
| 23 | Material Layering System | High | M | Phase 3 |
| 24 | Procedural Texture Generation | Low | M | Phase 4 |
| 25 | Terrain Pipeline Improvements | Medium | L | Phase 4 |
| 26 | Impostor / Billboard LOD | Medium | M | Phase 3 |
| 27 | Occlusion Culling (HZB) | Important | M | Phase 2-3 |
| 28 | Async Compute | High | M | Phase 3 |

---

## Recommended Implementation Order

The following order maximizes visual impact and respects dependency chains:

### Immediate (Phase 2 integration)

1. **Render Graph (Gap 15)** -- Foundation for all subsequent passes; reduces barrier bugs
2. **Bindless Resources (Gap 19)** -- Unlocks material scalability, required for GPU-driven
3. **IBL Environment Maps (Gap 1, baseline)** -- Minimum viable indirect lighting
4. **Reflection Probes (Gap 22)** -- Local reflections for SSR fallback
5. **Bloom/Lens/CA improvements (Gap 6)** -- Quick visual quality win
6. **Clustered Shading (Gap 21)** -- Upgrade from tiled, better light scalability
7. **Deferred Decals (Gap 5)** -- Essential for environmental detail

### Near-term (Phase 3 integration)

8. **Sky/Atmosphere (Gap 10)** -- Dynamic time-of-day, aerial perspective
9. **Volumetric Fog (Gap 9)** -- Defines scene atmosphere
10. **Depth of Field (Gap 7)** -- Cinematic camera
11. **Motion Blur (Gap 8)** -- Leverages existing velocity buffer
12. **SSS (Gap 12)** -- Character quality
13. **HZB Occlusion Culling (Gap 27)** -- Performance
14. **Async Compute (Gap 28)** -- Performance
15. **Material Layering (Gap 23)** -- Artist workflow
16. **Impostor LOD (Gap 26)** -- Open-world scalability
17. **GPU-Driven Rendering (Gap 20)** -- Performance scalability

### Medium-term (Phase 4)

18. **DXR Ray Tracing (Gap 4)** -- Premium visual tier
19. **Mesh Shaders (Gap 18)** -- GPU-driven geometry
20. **Virtual Geometry (Gap 2)** -- Nanite-class mesh pipeline
21. **Virtual Shadow Maps (Gap 3)** -- Replaces CSM
22. **Water Rendering (Gap 11)** -- Environment quality
23. **VRS (Gap 17)** -- Performance
24. **Hair Rendering (Gap 13)** -- Character quality
25. **Eye Rendering (Gap 14)** -- Character quality
26. **Terrain Improvements (Gap 25)** -- Large-world quality
27. **Multi-View / VR (Gap 16)** -- Platform expansion
28. **Procedural Textures (Gap 24)** -- Asset pipeline

---

## Appendix: Existing Spec Quality Notes

The existing specs are generally well-structured with clear data layouts, HLSL code samples, and IPC handler definitions. Specific quality observations:

1. **`renderer.md`**: Correctly describes the hybrid pipeline but conflates "Forward+" (line 5) with "Deferred" (steps 6-7). The pipeline is actually deferred for opaque + forward for transparent. The terminology should be "Hybrid Deferred" not "Forward+."

2. **`brdf.hlsli`**: The Cook-Torrance implementation is correct for direct lighting. Missing: multi-scattering compensation (Turquin 2019), energy-conserving sheen (for fabric), and clear-coat layer.

3. **`lighting.hlsli`**: Point light attenuation uses `1/(d^2 + 1)` which is not physically correct (should be `1/d^2` with a singularity guard). The `+ 1.0` in the denominator distorts the falloff curve at close range. Consider `1 / max(d^2, min_dist^2)` instead.

4. **`screen-space-effects.md`**: The HBAO+ and Hi-Z SSR specs are thorough. TAA variance clamping is correct. Missing: temporal stability metrics, ghosting analysis for different motion types.

5. **`shader-variants.md`**: The FNV-1a variant key system is pragmatic. Note: FNV-1a has known collision weaknesses at high variant counts; consider xxhash3 if variant count exceeds ~1000.

6. **`particle-system.md`**: The GPU compute pipeline is well-designed. Missing: depth buffer collision for soft particles, sub-emitters (child emitters triggered by particle death), and GPU sorting for transparent particles.

7. **`terrain-system.md`**: The water plane section is thin. The `[water]` section should be broken out into a dedicated water rendering spec given the complexity of realistic water.

8. **Root Signature Pressure**: The current root signature uses 5 root parameters (b0, b1, descriptor table, sampler table, b15). DX12 recommends keeping the root signature under 64 DWORDs. With b2 added for screen-space effects and potential expansion for decals/probes, the signature may need restructuring. Bindless resources (Gap 19) would consolidate the descriptor tables.
