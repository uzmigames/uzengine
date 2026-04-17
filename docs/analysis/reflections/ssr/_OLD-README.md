<!-- doc-meta
title: SSR Analysis — Complete Industry & Academic Survey
created: 2026-04-05
status: complete
-->

# SSR Analysis — Complete Industry & Academic Survey

Comprehensive analysis of Screen-Space Reflections across 10+ engines, 15+ academic papers,
and diagnosis of UzEngine's current SSR noise issues. Created 2026-04-05.

## Master Report

- [ssr-master-report.md](ssr-master-report.md) — **START HERE**. Executive summary, UzEngine diagnosis, engine comparison matrix, recommended refactor plan.

## Per-Engine Analysis

| Engine | File | Key Technique |
|--------|------|---------------|
| UE5 (SSR classic) | [../rendering/ue5-ssr-architecture.md](../rendering/ue5-ssr-architecture.md) | Hi-Z + VNDF + variance temporal |
| UE5 (Lumen) | [../rendering/next-gen-reflections-analysis-2025.md](../rendering/next-gen-reflections-analysis-2025.md) | Screen trace + SDF + radiance cache cascade |
| Frostbite/DICE | [../rendering/frostbite-stochastic-ssr-analysis.md](../rendering/frostbite-stochastic-ssr-analysis.md) | Stochastic SSR (Stachowiak 2015) — the foundational paper |
| AMD FidelityFX SSSR | [../rendering/ssr-state-of-the-art-2025.md](../rendering/ssr-state-of-the-art-2025.md) | Best open-source reference (MIT license) |
| Godot 4 | [../godot4-ssr-technical-analysis.md](../godot4-ssr-technical-analysis.md) | Parametric T Hi-Z + Karis tonemap |
| Three.js | [../rendering/three-js-ssr-technical-analysis.md](../rendering/three-js-ssr-technical-analysis.md) | DDA linear march, no temporal |
| CryEngine | [../rendering/cryengine-ssr-technical-analysis.md](../rendering/cryengine-ssr-technical-analysis.md) | Linear march + blur pyramid |
| id Tech / DOOM | [../rendering/id-tech-ssr-industry-analysis.md](../rendering/id-tech-ssr-industry-analysis.md) | Mirror-only + inline forward SSR |
| Decima / Naughty Dog / Insomniac | [../rendering/id-tech-ssr-industry-analysis.md](../rendering/id-tech-ssr-industry-analysis.md) | Various AAA approaches |

## Academic Survey

| Part | File | Content |
|------|------|---------|
| Part 1 | [../rendering/ssr-academic-survey-part1.md](../rendering/ssr-academic-survey-part1.md) | Foundational algorithms: McGuire, Stachowiak, Heitz VNDF, Karis TAA, SVGF |
| Part 2 | [../rendering/ssr-academic-survey-part2.md](../rendering/ssr-academic-survey-part2.md) | Production pipelines: STBN, AMD SSSR, NRD, comparison matrix, BibTeX |
