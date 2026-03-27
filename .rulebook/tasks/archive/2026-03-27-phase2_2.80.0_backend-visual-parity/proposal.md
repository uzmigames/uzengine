# Proposal: Backend Visual Parity — DX11 + SM6.0 Match DX12 SM6.6 (Phase 2)

## Why
DX11 and DX12 SM6.0 (non-bindless) backends produce visually different output compared to the reference DX12 SM6.6 (bindless) path. 29 issues identified across shader bindings, missing render passes, wrong samplers, and absent features. Users on older hardware or forcing SM6.0 see missing reflections, no bloom, no TAA, wrong metallic sampling, and broken masked materials. All backends must produce identical visual output for the engine to be production-ready.

Analysis: `docs/analysis/dx11-sm6-parity-report.md`

## What Changes

### Shader Fixes (SM6.0 + DX11)
1. Add SSR and SSGI register bindings (t12, t13) in deferred_lighting_ps.hlsl non-bindless path
2. Fix GBuffer metallic sampler — use SampleBias(linearSampler) instead of SampleLevel(clampSampler)
3. Add opacity texture binding (t6) in gbuffer_pass.cpp SM5 path
4. Gate normal mapping on texture validity in SM5 gbuffer path
5. Add null-guard for IBL textures in SM5 deferred lighting path
6. Add lens flare register binding in tonemap SM5 path

### DX11 Missing Passes
7. Enable TAA for DX11 — remove IsBindless() gate or implement PS-based fallback
8. Enable bloom for DX11 — PS-based bloom or remove bindless requirement
9. Create missing samplers s2 (bilinear clamp) and s3 (bilinear wrap) in DX11 device
10. Fix DX11 SRV table multi-bind — dx11_command_list only binds slot 0
11. Add DX11 3D texture support for color grading LUT

### SM6.0 Pass Binding
12. Bind SSR/SSGI textures via SetFragmentTexture in lighting_pass.cpp
13. Fix IBL baker UAV-to-SRV barrier ordering for SM6.0
14. Add sky cubemap multi-scattering for SM6.0 PS capture path

## Impact
- Affected specs: docs/specs/rendering/renderer.md, docs/specs/rendering/screen-space-effects.md
- Affected code: runtime/shaders/runtime/ (6 shaders), runtime/engine/renderer/passes/ (5 passes), runtime/engine/rhi/dx11/ (3 files)
- Breaking change: NO — all changes are additive fixes to existing fallback paths
- User benefit: Visual parity across all supported GPU configurations
- Enables: Reliable multi-backend testing, CI visual regression on DX11
