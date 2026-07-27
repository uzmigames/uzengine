# Lighting Analysis — Index

**Purpose**: Deep reference for redesigning UzEngine's default lighting
& shadow pipeline to AAA quality. Gathers reference-engine studies,
modern-technique surveys, character-lighting analyses, UzEngine gap
analysis, and the concrete redesign plan with shader sketches + CB
layouts + Sponza debug procedure.

**Last updated**: 2026-04-17

---

## Reading order (recommended)

If you are reading this for the first time, follow this order — each
document builds on the previous:

### Foundations (research)

1. [`crimson-desert/00-blackspace-engine.md`](./crimson-desert/00-blackspace-engine.md) — The ground truth the user wants. Per-pixel RTGI, path tracing, ray reconstruction denoising.
2. [`crimson-desert/05-comparison-with-lumen.md`](./crimson-desert/05-comparison-with-lumen.md) — Side-by-side BlackSpace vs Lumen.
3. [`lumen/00-architecture.md`](./lumen/00-architecture.md) — UE5 Lumen: surface cache + radiance cache + screen probe gather.
4. [`modern-techniques/00-overview.md`](./modern-techniques/00-overview.md) — Technique selection matrix.

### Technique deep-dives (research)

5. [`modern-techniques/restir.md`](./modern-techniques/restir.md) — ReSTIR DI & GI.
6. [`modern-techniques/area-lights-ltc.md`](./modern-techniques/area-lights-ltc.md) — LTC area light integration.
7. [`modern-techniques/virtual-shadow-maps.md`](./modern-techniques/virtual-shadow-maps.md) — VSM.
8. [`modern-techniques/ddgi-sdfgi.md`](./modern-techniques/ddgi-sdfgi.md) — Probe-based dynamic GI.
9. [`modern-techniques/radiance-cascades.md`](./modern-techniques/radiance-cascades.md) — Sannikov's approach.
10. [`modern-techniques/ray-reconstruction.md`](./modern-techniques/ray-reconstruction.md) — Neural denoisers.
11. [`modern-techniques/pcss-contact-shadows.md`](./modern-techniques/pcss-contact-shadows.md) — Direct shadow quality.
12. [`modern-techniques/subsurface-scattering.md`](./modern-techniques/subsurface-scattering.md) — Character skin.
13. [`modern-techniques/hair-rendering.md`](./modern-techniques/hair-rendering.md) — Marschner hair.
14. [`modern-techniques/neural-radiance-cache.md`](./modern-techniques/neural-radiance-cache.md) — NRC (research).

### Reference engines (comparison material)

15. [`reference-engines/INDEX.md`](./reference-engines/INDEX.md) — 5 engines: Decima, id Tech 8, Frostbite, RE Engine, Snowdrop.

### UzEngine plan (actionable)

16. [`uzengine/gap-analysis.md`](./uzengine/gap-analysis.md) — Current state of UzEngine lighting vs AAA. Root cause for Sponza issues.
17. [`uzengine/aaa-default-pipeline.md`](./uzengine/aaa-default-pipeline.md) — The new pipeline design. 3 tiers, 9 phases.
18. [`uzengine/sponza-debug-plan.md`](./uzengine/sponza-debug-plan.md) — **Start here to fix Sponza today**.
19. [`uzengine/shader-sketches.md`](./uzengine/shader-sketches.md) — HLSL templates for new passes.
20. [`uzengine/cb-layouts.md`](./uzengine/cb-layouts.md) — Constant buffer data layouts (HLSL + C++).
21. [`uzengine/validation-methodology.md`](./uzengine/validation-methodology.md) — PSNR/SSIM against path-traced reference.
22. [`uzengine/migration-plan.md`](./uzengine/migration-plan.md) — Phase-by-phase rollout procedure.

---

## Full subdirectory map

### crimson-desert/ · BlackSpace Engine analysis
| File | Content |
|------|---------|
| `00-blackspace-engine.md` | Engine overview, pipeline, path tracing approach, comparison vs Lumen. |
| `01-path-tracing-architecture.md` | Per-pixel RTGI, BVH, ray budget, denoising stack. |
| `02-ray-reconstruction-integration.md` | DLSS-RR / FSR Ray Regen integration, neural denoiser. |
| `03-volumetric-fog-god-rays.md` | Physically accurate heterogeneous fog with light scattering. |
| `04-character-lighting.md` | Skin SSS, eye, hair, cloth, hero-lighting rig. |
| `05-comparison-with-lumen.md` | Side-by-side technique + performance + memory matrix. |

### lumen/ · UE5 Lumen deep-dive
| File | Content |
|------|---------|
| `00-architecture.md` | Overall architecture, pass flow, four-layer cache hierarchy. |
| `01-surface-cache.md` | Mesh cards, capture pipeline, atlas streaming. |
| `02-radiance-cache.md` | World probes + voxel clipmap. |
| `03-screen-probe-gather.md` | Screen probes + final gather + temporal. |
| `04-software-vs-hardware-rt.md` | SDF vs HWRT backends. |
| `05-reflections.md` | Lumen Reflections pipeline. |
| `radiance-cache-compositing.md` | Radiance cache compositing details (prior analysis). |
| `radiance-cache-quick-ref.md` | Quick reference (prior analysis). |

### modern-techniques/ · Algorithm surveys
| File | Technique |
|------|-----------|
| `00-overview.md` | Technique selection matrix. |
| `restir.md` | ReSTIR DI + ReSTIR GI (reservoir resampling). |
| `area-lights-ltc.md` | Linearly Transformed Cosines for area lights. |
| `radiance-cascades.md` | Sannikov's radiance cascades (2D + 3D). |
| `ray-reconstruction.md` | DLSS 3.5 RR / FSR Ray Regen / NRD / SVGF comparison. |
| `virtual-shadow-maps.md` | VSM (page-table, HZB traversal, Nanite integration). |
| `ddgi-sdfgi.md` | NVIDIA RTX DDGI + Godot SDFGI. |
| `neural-radiance-cache.md` | NRC (online-trained neural irradiance). |
| `pcss-contact-shadows.md` | PCSS + screen-space contact shadows. |
| `subsurface-scattering.md` | Separable SSS, path-traced SSS, thin-sheet translucency. |
| `hair-rendering.md` | Marschner BSDF + dual scattering + card vs strand. |

### reference-engines/ · Other AAA engine analyses
| File | Engine |
|------|--------|
| `INDEX.md` | Overview of folder. |
| `decima.md` | Decima (Guerrilla + Kojima) — Horizon, Death Stranding 2. |
| `id-tech-8.md` | id Tech 8 — Doom: The Dark Ages path tracing. |
| `frostbite.md` | Frostbite (DICE/EA) — the canonical PBR reference. |
| `re-engine.md` | RE Engine (Capcom) — Monster Hunter Wilds. |
| `snowdrop.md` | Snowdrop (Massive/Ubisoft) — Avatar, Division. |

### direct/ · Direct lighting & IBL references
| File | Content |
|------|---------|
| `ibl-rough-metal.md` | IBL reflection quality for rough metallic. |
| `skylight-fog-parity.md` | Skylight+fog parity. |
| `ue5-environment-defaults.md` | UE5 environment component defaults. |

### shadows/ · Shadow systems
(Preserved historical analyses — see directory listing. Key files: `local-light-overview.md`, `shadow-depth-z-convention.md`, `ue5-csm-vs-vsm.md`, `ue5-local-light-architecture.md`, `ue5-reversed-z.md`, `ue5-shadows-notes/`.)

### global-illumination/ · Generic GI refs
| File | Content |
|------|---------|
| `ue5-ground-radiance.md` | UE5 ground radiance analysis. |

### indoor/ · Indoor GI and Sponza
| File | Content |
|------|---------|
| `sponza-lighting-rootcause.md` | Root cause analysis for Sponza issues — see also [`uzengine/sponza-debug-plan.md`](./uzengine/sponza-debug-plan.md). |
| `sponza-quick-fix.md` | Short-term band-aid. |
| `implementation-guide.md`, `parameter-reference.md` | Guidance. |
| `frostbite-cryengine-gi.md`, `threejs-godot-analysis.md`, `ue5-indoor-lighting.md` | Reference-engine indoor GI. |

### uzengine/ · UzEngine-specific
| File | Content |
|------|---------|
| `gap-analysis.md` | Current state of UzEngine lighting vs AAA. |
| `aaa-default-pipeline.md` | The new pipeline design (target architecture). |
| `sponza-debug-plan.md` | Pass-by-pass Sponza diagnosis (actionable today). |
| `shader-sketches.md` | HLSL code templates for new passes. |
| `cb-layouts.md` | Constant buffer memory layouts (HLSL + C++ sides). |
| `validation-methodology.md` | PSNR/SSIM validation against path-traced reference. |
| `migration-plan.md` | Phased rollout: CVar-gated, additive-first, 9 phases. |
| `lumen-lite-correction-plan.md` | **2026-07-26 execution plan** — verified current state (no GI active by default; probe hit-lighting physics bug) + Lumen→UzEngine mapping + 6 phases with per-phase verification. |

---

## Quick paths

| Goal | Start here |
|------|------------|
| **Fix Sponza GI now (current plan)** | [`uzengine/lumen-lite-correction-plan.md`](./uzengine/lumen-lite-correction-plan.md) |
| Fix Sponza today (older pass-by-pass diagnosis) | [`uzengine/sponza-debug-plan.md`](./uzengine/sponza-debug-plan.md) |
| Understand Crimson Desert | [`crimson-desert/00-blackspace-engine.md`](./crimson-desert/00-blackspace-engine.md) |
| Understand Lumen | [`lumen/00-architecture.md`](./lumen/00-architecture.md) |
| Pick a GI technique | [`modern-techniques/00-overview.md`](./modern-techniques/00-overview.md) |
| Implement the new pipeline | [`uzengine/aaa-default-pipeline.md`](./uzengine/aaa-default-pipeline.md) → [`uzengine/migration-plan.md`](./uzengine/migration-plan.md) |
| Write shader code for the new passes | [`uzengine/shader-sketches.md`](./uzengine/shader-sketches.md) |
| Validate against ground truth | [`uzengine/validation-methodology.md`](./uzengine/validation-methodology.md) |
| Compare engines | [`reference-engines/INDEX.md`](./reference-engines/INDEX.md) |

---

## Doc volume

| Category | Count | Total lines |
|----------|-------|-------------|
| Crimson Desert | 6 | ~1500 |
| Lumen | 8 | ~1500 |
| Modern techniques | 11 | ~2200 |
| Reference engines | 6 | ~1200 |
| Direct lighting | 3 | (existing) |
| Shadows | (many) | (existing) |
| Indoor | 7 | (existing) |
| UzEngine | 7 | ~2500 |
| **Total new** | **43** | **~9000 lines** |

---

## The user's brief (original)

> "A iluminação do Crimson Desert parece ray tracing com ray reconstruction.
> Quero análise detalhada, assim como Lumen. Armazenar em /docs/analysis/lighting.
> Nossa iluminação precisa melhorar — temos muita dificuldade de projeção na
> Sponza. Refazer nossa implementação default de light/shadow para ter
> iluminação consistente de qualidade AAA."

**Coverage**: Crimson Desert — 6 docs. Lumen — 8 docs. Modern techniques — 11 docs. Reference engines — 5 more AAA engines. UzEngine gap + new pipeline + shader sketches + CB layouts + validation methodology + migration plan — 7 docs. Sponza fix has its own step-by-step file.
