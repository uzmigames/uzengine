# NEXT SESSION: TAA + Bloom + Remaining Post-Process

## STATUS
- Branch: `feature/falcor-parity`
- **118 HLSL shaders** — 20+ fully implemented, rest are 1-liner scaffolds
- **Pipeline**: GBuffer → Shadows → Deferred Lighting → Atmosphere/Fog → Tonemap → backbuffer (simplified, no CAS/FXAA/TAA intermediate)
- **Working**: PBR (Falcor D*V*F), CSM shadows, IBL split-sum, sky atmosphere (ray march + transmittance LUT), height fog (Frostbite), auto-exposure (histogram + temporal), ACES tonemap, HDRI environment, sky cubemap capture
- **TAA disabled**: `m_taaEnabled = false` in `scene_renderer_impl.h:272` — scaffold `taa_resolve_cs.hlsl` zeros the HDR, causing black screen
- **Bloom**: shaders implemented (Falcor binomial filters) but output not visually confirmed — bloom pass executes, tonemap receives bloom SRV, but effect may be too subtle or threshold too high
- **PostProcess pipeline**: simplified in `frame_postprocess.cpp` — only Bloom + Tonemap, direct to backbuffer. Full pipeline (TAA → motion blur → DOF → tonemap → CAS → FXAA → backbuffer) commented out via `return;` at line 114

## PRIORITY 1: TAA Resolve Shader
The `taa_resolve_cs.hlsl` is a 1-liner scaffold. Needs full implementation.

### C++ Context
- `m_taaEnabled = false` — change to `true` after implementing
- Upscaler: `TsrUpscaler` or `TaaUpscaler` (selectable via `m_upscaler`)
- TAA history: ping-pong `m_taaHistory[2]` textures (RGBA16F)
- Jitter: Halton(2,3) sequence, 8 samples
- Root constants: see `frame_postprocess.cpp:170-190` for UpscalerInput struct

### Old shader reference
- `C:/Users/Bolado/Desktop/backup-shaders/shaders-old/postprocess/taa_resolve_cs.hlsl`
- Look at Falcor for TAA approach (they may use a different technique)

### What to implement
1. `taa_resolve_cs.hlsl` — variance clipping in YCoCg space (Karis 2014, Salvi 2016)
2. `tsr_resolve_cs.hlsl` — temporal super resolution (3 sub-passes: dilate, reject, history)
3. `velocity_ps.hlsl` — depth-based motion vectors (needed by TAA)

### After TAA works
1. Restore full postprocess pipeline in `frame_postprocess.cpp` (remove the early `return;`)
2. Re-enable `m_taaEnabled = true`
3. Test with visible window — TAA should eliminate specular aliasing and moire

## PRIORITY 2: Bloom Verification
Bloom shaders exist but effect not confirmed visually. After TAA:
1. Create a bright emissive test scene
2. Verify bloom glow appears around bright objects
3. If not, debug the bloom compute dispatch → threshold → downsample → upsample chain

## PRIORITY 3: CAS Sharpening
`cas_sharpen_ps.hlsl` is a passthrough (samples and returns LDR). Need real AMD CAS implementation.
Old shader: `C:/Users/Bolado/Desktop/backup-shaders/shaders-old/postprocess/cas_sharpen_ps.hlsl`

## PRIORITY 4: WarmupAll
Re-enable `WarmupAll()` in `scene_renderer_init.cpp:97` — currently disabled for shader debugging.

## KNOWN ISSUES
- Shader cache must be cleared (`rm -rf runtime/build/bin/cache/shaders/`) when modifying shaders — DXC compilation happens at runtime
- `needsLdrIntermediate` is hardcoded `true` in original pipeline but simplified pipeline writes directly to backbuffer
- Descriptor ring contiguity check relaxed to warning (allows HDRI runtime load but CommandAllocator::Reset may fail)
- `m_taaEnabled = false` prevents TAA from zeroing the HDR output

## FALCOR REFERENCE
- Falcor at `E:/Falcor` — BSD licensed
- Falcor doesn't have rasterized TAA — uses path tracer accumulation
- For TAA reference, use UE5 patterns from published papers:
  - Karis 2014 "High Quality Temporal Supersampling"
  - Salvi 2016 "An Excursion in Temporal Supersampling"
  - Jimenez 2016 "Filmic SMAA / Temporal Reprojection"

## OLD SHADERS BACKUP
- `C:/Users/Bolado/Desktop/backup-shaders/shaders-old/` — read-only reference
- DO NOT COPY — implement from published papers + Falcor, use old shaders only for CB layout verification

## SCENE FILES FOR TESTING
- `sample/projects/pbr_showcase.uzproject` — PBR spheres + cubes + concrete ground
- `sample/projects/fbx_test.uzproject` — mushroom monster FBX + checkerboard
- `sample/projects/hdri_test.uzproject` — HDRI environment + 7 material spheres
- `sample/projects/bloom_test.uzproject` — emissive spheres (needs camera controller fix)

## COMMITS THIS SESSION
```
f52af87f fix: roughness default 1.0 + shadow specular occlusion + fog baseHeight=-10
33fb5cd0 fix: sun disk + IBL exposure + scene light intensity corrections
2993ebc0 fix: sun disk intensity was 260x too bright
33843929 fix: IBL diffuse missing /PI + analysis
f8858e27 fix: remove debug exposure log + ACES 0.6 pre-exposure
86f9ec08 fix: height fog defaults
7c47657a feat: auto-exposure histogram + temporal adaptation
e5ddf996 wip: bloom pipeline integration + bloom test scene
f7dc7f8f feat: HDRI environment + skybox + V-function BRDF fix
af18b667 feat: sky cubemap capture + IBL SH + height fog + sky atmosphere LUT
bef8f7fc fix: shadow acne on ground plane + specular AA
bd4db199 feat: complete shader rewrite — 118 HLSL files, Falcor BRDF parity
```
