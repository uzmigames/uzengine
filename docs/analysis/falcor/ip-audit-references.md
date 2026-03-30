<!-- doc-meta
type: audit
title: IP Audit — References and Documentation
description: Legal/IP audit of all docs, analysis, and reference files for proprietary citations and copyright-infringing content.
tags: [legal, ip, audit, references]
status: complete
date: 2026-03-30
author: implementation-auditor
-->

# IP Audit — References and Documentation

**Date**: 2026-03-30
**Scope**: All files under `docs/`, `docs/analysis/`, `docs/specs/`, `docs/architecture/`,
`docs/research/`, `docs/reference/`, `.claude/rules/`, `.claude/agents/`, and
`runtime/` (C++ and HLSL sources).

---

## Summary

| Category | Count | Risk Level |
|---|---|---|
| VIOLATION — Verbatim proprietary code blocks | 6 | HIGH |
| VIOLATION — "Copy-paste ready" / "exact code" framing | 5 | HIGH |
| WARNING — UE5 source file paths cited as implementation reference | 35+ files | MEDIUM |
| WARNING — UE5 proprietary type/function names in `@ref` tags | 2 | LOW |
| SAFE — Published paper citations (Bruneton, Hillaire, etc.) | Many | OK |
| SAFE — "Similar to UE5" architectural references | Many | OK |
| SAFE — Runtime C++/HLSL code (no UE5 identifiers or paths) | All | OK |

---

## HIGH — VIOLATION: Verbatim Proprietary Code Blocks

These files explicitly self-describe as containing verbatim or copy-paste-ready UE5 source code
and include substantial quoted blocks. They pose the highest IP risk.

### 1. `docs/architecture/ue5-code-reference-snippets.md`

**Line 3**: `"This document contains verbatim code snippets from UE5 5.6 source, with line numbers and file paths for direct reference."`

Contains multi-block verbatim C++ from:
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.h` (lines 483-504) — full `FD3D12RootSignatureManager` class body
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp` (lines 162-195) — full `FD3D12RootSignatureDesc` constructor block
- `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12RootSignature.cpp` (lines 349-359) — conditional bindless flags block

**Action required**: Delete or replace all verbatim code blocks with architectural descriptions and citations to public Microsoft DX12 documentation.

---

### 2. `docs/research/ue5-dx11-code-reference.md`

**Line 3**: `"This document contains exact code snippets from UE5 5.6 source, with file paths and line numbers. Use these as templates for UzEngine."`

Contains verbatim C++ from:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Windows\D3D11RHI\Private\D3D11UniformBuffer.cpp` (lines 11-56) — `FPooledUniformBuffer` struct, `NumPoolBuckets`, `NumSafeFrames`, pool arrays
- Same file (lines 59-104) — `UniformBufferBeginFrame()` implementation body

Includes original comments (`// AMD driver bug with 11.10 drivers`), which are proprietary IP.

**Action required**: Delete or replace. Microsoft DX11 uniform buffer pooling is public knowledge; rewrite as architecture description citing MSDN.

---

### 3. `docs/analysis/lighting/ue5-shadows/ue5-shadow-sampling-exact-code.md`

**Line 13**: `"This document contains exact code snippets from UE5 5.6 source, with file paths and line numbers."`

Contains verbatim HLSL from:
- `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf` (lines 51-108) — full `SetShadowDepthOutputs()` function

**Action required**: Replace with description + citation to published Stamminger/Drettakis PSSM papers.

---

### 4. `docs/analysis/atmosphere/ue5-transmittance-lut-hlsl-snippets.md`
   and `docs/analysis/atmosphere/ue5/ue5-transmittance-lut-hlsl-snippets.md` (duplicate)

**Title**: `"UE5 Transmittance LUT — HLSL Code Snippets for Direct Porting"`
**Section**: `"Complete UV Mapping Functions (Copy-Paste Ready)"`

Contains quoted HLSL (`fromTransmittanceLutUVs`, `getTransmittanceLutUvs`) attributed to
`SkyAtmosphereCommon.ush` lines 156-192.

**MITIGATING FACTOR**: The functions cite Bruneton 2017 (public paper). The math itself is
not proprietary. However, the specific implementation (variable names `Xmu`, `Xr`, `Rho`, `Dmin`, `Dmax`) is UE5's specific port of Bruneton's reference code. The *framing* as "copy-paste ready" from UE5 source is the IP issue.

**Action required**: Replace with the original Bruneton 2017 reference implementation, cited from the public GitHub repo (`https://github.com/ebruneton/precomputed_atmospheric_scattering`), not from UE5's derivative.

---

### 5. `docs/analysis/atmosphere/ue5-height-fog-complete-reference.md`

**Line 11**: `"Line-by-line extraction of EVERY formula, constant, and computation step from UE5 5.6 height fog system. Not summarized — exact code with line numbers for direct parity implementation."`

Contains verbatim HLSL struct (`FFogStructData` / `FogStructData`) from `HeightFogCommon.ush:64-85`
with exact field names, line numbers, and full semantic comments extracted from UE5.

**Action required**: Replace with an architecture description. Mathematical fog formulas (exponential height fog) are public techniques; cite the appropriate academic sources (Wenzel 2006, Hobbs 2003).

---

### 6. `docs/analysis/lighting/ue5-shadows/ue5-far-shadows-code-snippets.md`

Contains verbatim C++ from:
- `Engine\Source\Runtime\Engine\Classes\Components\DirectionalLightComponent.h` (lines 110-118) — `UPROPERTY` declarations with full metadata
- `Engine\Source\Runtime\Engine\Public\LightSceneProxy.h` (lines 502-506) — member variable block
- `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (line 1023) — default value assignment
- Same file (line 1076) — post-edit clamping
- Same file (lines 62-74) — `CVarMaxNumFarShadowCascades` TAutoConsoleVariable block

This includes `UPROPERTY` macro metadata strings which are proprietary identifiers.

**Action required**: Replace with UzEngine-specific parameter table; cite Engel 2012 "Shadows" for cascade techniques.

---

## HIGH — VIOLATION: "Exact Code" Framing in Other Files

These files use explicit "exact code" or "for direct porting" framing that indicates IP risk
even if the individual code blocks may reference public techniques:

| File | Problematic Language |
|---|---|
| `docs/analysis/rhi/ue5-sm5-code-patterns.md` | `"exact code patterns from UE5 5.6 source that UzEngine should adapt (not copy)"` — contradictory framing |
| `docs/reference/niagara-analysis/NIAGARA_VFX_ANALYSIS.md` | Contains verbatim `FNiagaraRibbonGenerationConfig` struct from `U:/UnrealGITHUB/UnrealEngine/...` with exact fields and bit layout |
| `docs/analysis/engine-comparison/application-layer/ue5-app-layer-code-examples.md` | Contains quoted `FEngineLoop::Tick()` / `UGameEngine::Tick()` bodies |
| `docs/analysis/materials/ue5/ue5-pom-code-reference.md` | Title: `"UE5 POM Implementation: Exact Code References"` |
| `docs/analysis/engine-comparison/application-layer/ue5-4k-rendering-upscaling.md` | Contains multiple quoted function bodies from `TemporalSuperResolution.cpp`, `DynamicResolution.cpp` |

---

## MEDIUM — WARNING: UE5 Source File Paths as References

Citing a proprietary file path + line number as the *implementation basis* is a signal that
the code was written by reading proprietary source. It does not constitute copying by itself,
but indicates the implementation may be a derivative work.

### Files with 5+ UE5 source path citations (high density)

| File | Source Path Density |
|---|---|
| `docs/analysis/atmosphere/fog-line-number-reference.md` | 10+ `Engine/Source/Runtime/...` citations with line numbers |
| `docs/analysis/atmosphere/ue5/ue5-skyatmosphere-coordinate-system.md` | 8 citations |
| `docs/analysis/atmosphere/ue5-fog-cpp-data-flow.md` | 6 citations with inline code blocks |
| `docs/architecture/ue5-rhi-dx11-dx12-analysis.md` | 6 citations |
| `docs/analysis/lighting/ue5-environment-component-defaults.md` | 5 citations with code blocks |
| `docs/analysis/engine-comparison/application-layer/ue5-4k-rendering-upscaling.md` | 10+ citations |
| `docs/analysis/materials/quick-reference-material-system.md` | 6 citations with file sizes |
| `docs/analysis/dx12-descriptors/ue5-dx12-descriptor-management.md` | 5 citations |

### Additional files with UE5 source path references

All files listed below contain `Engine/Source/Runtime/...` path citations as implementation
references. These are analysis-only docs and do not contain verbatim code blocks (lower risk),
but should be reviewed to ensure UzEngine implementations were derived from published papers
rather than from these cited proprietary paths:

- `docs/analysis/gizmo/editor-gizmo-specifications.md`
- `docs/analysis/gizmo/gizmo-analysis-index.md`
- `docs/analysis/gizmo/gizmo-quick-reference.md`
- `docs/analysis/engine-comparison/godot-texture-streaming-vs-ue5-unity.md`
- `docs/analysis/engine-comparison/application-layer/ue5-dx11-readback-analysis.md`
- `docs/analysis/engine-comparison/general/godot-vs-ue5-renderingdevice.md`
- `docs/architecture/rhi-quick-reference.md`
- `docs/architecture/unity-dx11-dx12-abstraction-analysis.md`
- `docs/research/ue5-platform-abstraction-inventory.md`
- `docs/research/window-minimize-patterns.md`

---

## MEDIUM — WARNING: UE5 Type Names in `@ref` Tags (Runtime Code)

Found in actual runtime source (not docs):

| File | Line | Issue |
|---|---|---|
| `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl:178` | `@ref FAtmosphereSetup::ComputeTransmittance` | Cites a UE5 proprietary class/method |
| `runtime/build/generated/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl.gen.h:182` | `@ref FAtmosphereSetup::ComputeTransmittance` | Generated copy of above |
| `runtime/engine/renderer/passes/postprocess/temporal_upscaler.h:16` | `"Reference integrates with FSceneViewState for history"` | UE5 type name in comment |

**Action required**: Replace `@ref FAtmosphereSetup::ComputeTransmittance` with `@ref Bruneton 2017 "A Qualitative and Quantitative Evaluation of 8 Clear Sky Models"`. Replace `FSceneViewState` mention with a generic description.

---

## SAFE — No Issues Found

### `.claude/rules/` directory
All rules files checked. No proprietary source paths or code. CLAUDE.md explicitly
instructs agents to cite published papers and never cite proprietary source files. This
is a positive IP protection control.

### `.claude/agents/` directory
All agent definitions checked. No directives to copy from proprietary sources. The
`unreal-architect` and `godot-architect` agents are explicitly scoped to architectural
pattern analysis only. The CLAUDE.md instruction `"use the unreal-architect agent for
architectural pattern analysis (NOT for copying code)"` is present and correct.

### `docs/specs/` directory
No UE5 source file paths found in any spec. Safe.

### `runtime/` C++ and HLSL source
No UE5 proprietary identifiers in function names, struct names, or variable names.
No `Engine/Source/Runtime` paths in source code.
The `@ref` citation system uses published paper references (`Bruneton`, `Hillaire`,
`Karis`, `Lagarde`) almost universally. Two exceptions noted above (LOW risk).

### Published paper references
The following references found throughout docs are **SAFE** — they cite public academic works:
- Bruneton 2017 "A Qualitative and Quantitative Evaluation of 8 Clear Sky Models"
- Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
- Karis 2013 "Real Shading in Unreal Engine 4" (this is a public GDC presentation)
- Lagarde/de Rousiers 2014 "Moving Frostbite to PBR"
- Stamminger/Drettakis 2002 "Perspective Shadow Maps"
- Engel 2012 "Cascaded Shadow Maps" (ShaderX series)

---

## Recommended Actions (Priority Order)

### P0 — Delete or fully rewrite (contains verbatim proprietary code)

1. `docs/architecture/ue5-code-reference-snippets.md` — delete or replace code blocks with architectural descriptions
2. `docs/research/ue5-dx11-code-reference.md` — delete or replace with MSDN/public references
3. `docs/analysis/lighting/ue5-shadows/ue5-shadow-sampling-exact-code.md` — delete verbatim HLSL, replace with published paper reference
4. `docs/analysis/atmosphere/ue5-transmittance-lut-hlsl-snippets.md` — replace UE5-sourced HLSL with Bruneton reference implementation
5. `docs/analysis/atmosphere/ue5/ue5-transmittance-lut-hlsl-snippets.md` — same (duplicate)
6. `docs/analysis/atmosphere/ue5-height-fog-complete-reference.md` — replace verbatim struct with architectural description
7. `docs/analysis/lighting/ue5-shadows/ue5-far-shadows-code-snippets.md` — delete UPROPERTY blocks, replace with technique reference
8. `docs/reference/niagara-analysis/NIAGARA_VFX_ANALYSIS.md` — remove verbatim `FNiagaraRibbonGenerationConfig` struct
9. `docs/analysis/engine-comparison/application-layer/ue5-app-layer-code-examples.md` — remove verbatim function bodies
10. `docs/analysis/engine-comparison/application-layer/ue5-4k-rendering-upscaling.md` — remove verbatim TSR/dynamic resolution code blocks

### P1 — Update `@ref` tags in runtime code

11. `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl:178` — replace `@ref FAtmosphereSetup::ComputeTransmittance` with `@ref Bruneton 2017`
12. `runtime/engine/renderer/passes/postprocess/temporal_upscaler.h:16` — remove `FSceneViewState` reference

### P2 — Review analysis docs for derived-from-proprietary-source implementations

For each file below, verify that the corresponding UzEngine implementation (if any) was
written from a published paper reference rather than from the proprietary source cited:

- `docs/analysis/atmosphere/fog-line-number-reference.md`
- `docs/analysis/lighting/ue5-environment-component-defaults.md`
- `docs/architecture/ue5-rhi-dx11-dx12-analysis.md`
- `docs/analysis/dx12-descriptors/ue5-dx12-descriptor-management.md`

---

## Notes on Analysis Files Generally

The majority of the `docs/analysis/ue5/` content serves a legitimate purpose: understanding
*what parameters and defaults* published techniques require so UzEngine can implement them
correctly. Citing proprietary files to understand parameter defaults is different from copying
code. However:

1. **Verbatim code blocks** from proprietary files must be removed regardless of purpose.
2. **"Copy-paste ready" framing** must be removed — it indicates intent to reproduce rather than to understand.
3. **Default values** extracted from proprietary source should instead be sourced from published papers. Where a published paper doesn't specify defaults, the analysis should note that explicitly.
4. The IP protection instruction already in CLAUDE.md (`"NEVER cite proprietary source files"`) is correct and should be enforced retroactively on existing docs.
