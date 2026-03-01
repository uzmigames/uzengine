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

## See Also
- `docs/specs/shader-variants.md` - variant/permutation system
- `docs/specs/shader-library.md` - shared HLSL includes
- `docs/specs/point-spot-light-shadows.md` - point/spot shadow maps
- `docs/specs/gpu-instancing.md` - dynamic GPU instancing
- `docs/specs/screen-space-effects.md` - SSAO, SSR, TAA post-process effects
