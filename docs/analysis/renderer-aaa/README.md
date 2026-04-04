<!-- doc-meta: renderer-aaa-analysis, Cross-engine rendering analysis — UzEngine vs UE5 vs Godot vs Three.js vs Falcor, 2026-04-04 -->

# Renderer AAA Analysis — UzEngine vs Industry

**Date**: 2026-04-04
**Overall Score**: UzEngine at **~80% UE5 parity**, significantly ahead of Godot (~52%) and Three.js (~31%)

---

## Quick Start

| You want to... | Read this |
|-----------------|-----------|
| See where we stand vs everyone | [feature-matrix.md](feature-matrix.md) |
| Know what to fix next (prioritized) | [gap-analysis.md](gap-analysis.md) |
| Deep dive UE5 techniques | [ue5-rendering-comprehensive-analysis.md](ue5-rendering-comprehensive-analysis.md) |
| Deep dive Godot 4 techniques | [godot4-rendering-analysis.md](godot4-rendering-analysis.md) |
| Three.js + Falcor reference | [threejs-falcor-analysis.md](threejs-falcor-analysis.md) |
| UE5 architecture patterns | [ue5-rendering-architecture-patterns.md](ue5-rendering-architecture-patterns.md) |
| Godot architecture patterns | [godot-architectural-patterns.md](godot-architectural-patterns.md) |

---

## Document Index

### Synthesis (start here)

| Document | Size | Description |
|----------|------|-------------|
| [feature-matrix.md](feature-matrix.md) | 13 categories, 120+ features | Cross-engine feature comparison with quality ratings |
| [gap-analysis.md](gap-analysis.md) | P0-P3 priorities, 4-sprint roadmap | Prioritized gaps with ROI, effort, and implementation order |

### Per-Engine Deep Dives

| Document | Engine | Size | Description |
|----------|--------|------|-------------|
| [ue5-rendering-comprehensive-analysis.md](ue5-rendering-comprehensive-analysis.md) | UE5 5.6 | 40 KB | 10 major systems, quality tiers, code examples |
| [ue5-rendering-architecture-patterns.md](ue5-rendering-architecture-patterns.md) | UE5 5.6 | 24 KB | Thread model, proxy pattern, render graph, RHI |
| [ue5-implementation-checklist.md](ue5-implementation-checklist.md) | UE5 5.6 | 15 KB | Per-system implementation checklists |
| [ue5-dataflow-examples.md](ue5-dataflow-examples.md) | UE5 5.6 | 22 KB | Component → CB → shader data flow examples |
| [godot4-rendering-analysis.md](godot4-rendering-analysis.md) | Godot 4.3 | 29 KB | Feature-by-feature breakdown, code references |
| [godot-vs-uzengine-rendering-matrix.md](godot-vs-uzengine-rendering-matrix.md) | Godot 4.3 | 13 KB | Quick-reference comparison checklist |
| [godot-architectural-patterns.md](godot-architectural-patterns.md) | Godot 4.3 | 16 KB | 10 design patterns extracted from source |
| [threejs-falcor-analysis.md](threejs-falcor-analysis.md) | Three.js + Falcor | 8 KB | Web renderer + ground truth reference |

### Navigation Guides

| Document | Description |
|----------|-------------|
| [INDEX.md](INDEX.md) | UE5-specific topic index |
| [README-godot-analysis.md](README-godot-analysis.md) | Godot analysis navigation guide |

---

## Key Findings

### UzEngine Strengths (ahead of Godot/Three.js)
- **Screen-space effects**: SSGI (4 quality presets), SSR (3-pass), GTAO, contact shadows — 95% UE5
- **Atmosphere**: Hillaire 2020 sky + volumetric clouds + height fog + god rays — 85% UE5
- **Shadow system**: CSM + PCSS + contact + DF + static cache — 85% UE5
- **IBL pipeline**: Split-sum + Fdez-Aguera + env MIS + radiance cache — 90% UE5
- **VFX**: GPU particles with 8+ force modules, trails, distortion — 85% UE5
- **Debug tools**: --diag, --pixel-debug, --pass-dump, 13 view modes — 95% UE5
- **GPU-driven**: Compute culling + HiZ + indirect draw + bindless SM6.6 — 80% UE5

### Critical Gaps (P0 — fix next)
1. **Froxel volumetric fog** — every AAA title has it, we only have analytical height fog
2. **Anisotropic GGX** — glTF standard, brushed metal/fabric look wrong without it
3. **Aerial perspective completion** — partially implemented, high impact for low effort
4. **Stephen Hill ACES fit** — trivial upgrade from Narkowicz, better highlights
5. **Specular occlusion** — simple formula, prevents IBL light leaking

### Implementation Roadmap
- **Sprint 1** (1 week): Quick wins — Hill ACES, specular occlusion, anisotropy, aerial perspective, HDRI skybox
- **Sprint 2** (1-2 weeks): Froxel fog + FSR 2.0 + AgX tonemap
- **Sprint 3** (2-3 weeks): Material graph + baked lightmaps
- **Sprint 4** (1-2 weeks): Iridescence, omni PCSS, advanced SSS

---

## Statistics

| Metric | Value |
|--------|-------|
| Total render passes | 67 |
| Total shader files | 145 (.hlsl + .hlsli) |
| Features at PROD quality | ~85 |
| Features PARTIAL | ~12 |
| Features MISSING (vs UE5) | ~20 |
| Features STUB | ~4 (raytracing infra) |
| Overall UE5 parity | ~80% |
| Overall vs Godot | ahead in 10/13 categories |
| Overall vs Three.js | ahead in 12/13 categories |
| Documents in this analysis | 10 files, ~190 KB total |
