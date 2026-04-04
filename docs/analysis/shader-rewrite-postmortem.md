<!-- doc-meta
title: Shader Rewrite Postmortem
category: analysis
status: final
created: 2026-04-01
-->

# Shader Rewrite Postmortem — Falcor Parity

## Overview

Rewrote ~99 HLSL shaders from scratch to match Falcor's StandardBSDF quality baseline. All shaders cite published papers and follow UzEngine naming conventions.

## Timeline

- **Phase 1-13**: Shader implementation (common, material, GBuffer, lighting, atmosphere, IBL, post-process, compute, debug, GI, SSGI, VFX) — ~12 sessions
- **Phase 14**: Pipeline integration (shadow, geometry, lighting, postprocess, overlay passes)
- **Phase 15**: Bug fixes (winding order, IBL hotspot, TAA velocity, shadow penumbra, decals)

## Key Decisions

1. **Falcor as quality baseline** — all BRDF math matches Falcor's StandardBSDF.slang
2. **Published paper citations** — every shader function has `@ref Author Year "Title"`
3. **SM6.6 bindless + SM5 fallback** — dual-path via `#ifdef UZ_BINDLESS`
4. **Entry point = `main`** — enforced by ShaderManager, documented in rules
5. **CPU parity tests** — brdf_math.h mirrors D_GGX, F_Schlick, Burley, Smith, ACES

## What Went Well

- 99 shaders compiled and running without GPU crashes
- 18 sample scenes render correctly
- GPU validation clean (0 DX12 debug layer errors)
- PBR materials visually match reference quality
- TAA/TSR temporal upscaler working with Halton jitter

## What Went Wrong

- TAA jitter enabled without resolve pass active → mesh trembling (caught in session)
- TSR 3-pass upscaler doesn't work in simplified pipeline → black screen (fell back to single-pass TAA)
- SM5 compilation not tested in CI — deferred to shader variant system task
- Several passes still commented out in RegisterLightingPasses (GTAO, SSGI, SSR, DFS)

## Lessons Learned

1. **Never enable jitter without resolve** — always verify the full TAA pipeline is wired
2. **Simplified pipeline `return` blocks everything after it** — any new pass must go BEFORE the return
3. **Shader permutation names must end with _cs/_ps/_vs** — stage inference from name suffix
4. **EnvMapSampler::Rebuild uses WaitIdle** — only call during IBL bake, never per-frame
5. **Root constants have 16 DWORD limit** — verified in dx12_pso_creation.cpp, safe for 6 GBuffer indices

## Metrics

- **Shaders**: ~99 HLSL files
- **Lines of HLSL**: ~15,000
- **CPU parity tests**: 66 tests, all passing
- **Sample scenes verified**: 18/18
- **GPU validation errors**: 0
