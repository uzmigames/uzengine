# UzEngine Analysis — Master Index

**Reorganized**: 2026-04-17
**Scope**: Reference material, reverse engineering, gap analyses, architectural research.
**Convention**: Every directory has its own `INDEX.md`. This file points to them.

---

## 1 · Lighting & Shadows  *(primary focus)*

Path: [`lighting/`](./lighting/) — See [lighting/INDEX.md](./lighting/INDEX.md)

| Subdir | Focus |
|--------|-------|
| [`lighting/crimson-desert/`](./lighting/crimson-desert/) | BlackSpace Engine per-pixel RTGI, ray reconstruction, denoising. |
| [`lighting/lumen/`](./lighting/lumen/) | UE5 Lumen: surface cache, radiance cache, screen probe gather. |
| [`lighting/modern-techniques/`](./lighting/modern-techniques/) | ReSTIR DI/GI, DDGI, SDFGI, VSM, radiance cascades, ray reconstruction. |
| [`lighting/direct/`](./lighting/direct/) | Analytic lights (directional, point, spot), IBL, skylight. |
| [`lighting/shadows/`](./lighting/shadows/) | CSM, VSM, PCSS, local light atlas, reversed-Z, point/spot data flow. |
| [`lighting/global-illumination/`](./lighting/global-illumination/) | Generic GI references (ground radiance, etc.). |
| [`lighting/indoor/`](./lighting/indoor/) | Sponza, Frostbite/CryEngine/Three.js/Godot indoor GI. |
| [`lighting/uzengine/`](./lighting/uzengine/) | UzEngine gap analysis + AAA default pipeline redesign. |

---

## 2 · Reflections

Path: [`reflections/`](./reflections/)

| Subdir | Focus |
|--------|-------|
| [`reflections/ssr/`](./reflections/ssr/) | SSR — Frostbite stochastic, CryEngine, UE5, Three.js, Godot, idTech. |
| [`reflections/ssr-academic/`](./reflections/ssr-academic/) | Academic SSR survey. |
| [`reflections/next-gen-2025.md`](./reflections/next-gen-2025.md) | Next-gen reflection survey (2025). |
| [`reflections/ssr-state-of-the-art-2025.md`](./reflections/ssr-state-of-the-art-2025.md) | SSR SOTA 2025. |

---

## 3 · Atmosphere, Sky, Fog, Clouds

Path: [`atmosphere-sky/`](./atmosphere-sky/)

| Subdir | Focus |
|--------|-------|
| [`atmosphere-sky/sky/`](./atmosphere-sky/sky/) | Bruneton/Hillaire sky, aerial perspective, UE5 blue horizon. |
| [`atmosphere-sky/fog/`](./atmosphere-sky/fog/) | Exponential height fog, volumetric fog (froxels), UE5 fog defaults. |
| [`atmosphere-sky/clouds/`](./atmosphere-sky/clouds/) | Volumetric clouds (Schneider, Hillaire). |

---

## 4 · Materials

Path: [`materials/`](./materials/)

| Subdir | Focus |
|--------|-------|
| [`materials/core/`](./materials/core/) | PBR, IBL, metallic, glass/transmission. |
| [`materials/decals/`](./materials/decals/) | Deferred decals (UE5 architecture + quick reference). |

---

## 5 · Post-Process & Upscaling

- [`post-process/`](./post-process/) — Tonemap, auto-exposure (histogram), bloom, GTAO.
- [`post-process/ue5-gtao-deep-dive/`](./post-process/ue5-gtao-deep-dive/) — UE5 GTAO algorithm.
- [`upscaling/`](./upscaling/) — TAA, TSR, DLSS (incl. Ray Reconstruction), FSR, Godot upscaling.

---

## 6 · Path Tracing

Path: [`path-tracing/`](./path-tracing/) — Reference path tracer architecture and plan.

---

## 7 · VFX & Particles

Path: [`vfx-particles/`](./vfx-particles/)

| Subdir | Focus |
|--------|-------|
| [`vfx-particles/niagara/`](./vfx-particles/niagara/) | UE5 Niagara DI, module stack UI, GPU buffer lifecycle. |
| [`vfx-particles/particles/`](./vfx-particles/particles/) | Flipbook / sub-UV. |

---

## 8 · Animation & Skeletal

Path: [`animation-skeletal/`](./animation-skeletal/)

| Subdir | Focus |
|--------|-------|
| [`animation-skeletal/skeletal/`](./animation-skeletal/skeletal/) | Skeleton, GPU skinning, vertex formats. |
| [`animation-skeletal/animation/`](./animation-skeletal/animation/) | Sequences, pose, debug traces. |

---

## 9 · Rendering Architecture

Path: [`rendering-arch/`](./rendering-arch/)

| Subdir | Focus |
|--------|-------|
| [`rendering-arch/cvars/`](./rendering-arch/cvars/) | UE5 CVar reference, anti-patterns, UzEngine implementation guide. |
| [`rendering-arch/patterns/`](./rendering-arch/patterns/) | UE5 / Godot / Unity / Three.js / Falcor renderer patterns. |
| [`rendering-arch/expert-reviews/`](./rendering-arch/expert-reviews/) | Multi-expert consensus reviews (UE5, Godot, Unity, shader, pipeline). |
| [`rendering-arch/gap-analysis/`](./rendering-arch/gap-analysis/) | Feature matrices, UE5 implementation checklists. |
| [`rendering-arch/overview/`](./rendering-arch/overview/) | Strategic plan, renderer audit, postmortem. |
| [`rendering-arch/debug/`](./rendering-arch/debug/) | Debug visualization. |

---

## 10 · GPU Backend

Path: [`gpu-backend/`](./gpu-backend/)

| Subdir | Focus |
|--------|-------|
| [`gpu-backend/rhi/`](./gpu-backend/rhi/) | RHI layer, cbuffer layout, Agility SDK, SM5 fallback. |
| [`gpu-backend/dx12/`](./gpu-backend/dx12/) | DX12 descriptors, PSO, root signature. |
| [`gpu-backend/parity/`](./gpu-backend/parity/) | DX11/DX12 parity, bindless issues. |

---

## 11 · Asset Pipeline

Path: [`asset-pipeline/`](./asset-pipeline/)

| Subdir | Focus |
|--------|-------|
| [`asset-pipeline/loading/`](./asset-pipeline/loading/) | UE5/UzEngine loading architecture, texture streaming. |
| [`asset-pipeline/mesh/`](./asset-pipeline/mesh/) | Normal smoothing, tangent generation (MikkTSpace), Godot / UE5. |

---

## 12 · Editor Tools

Path: [`editor-tools/`](./editor-tools/)

| Subdir | Focus |
|--------|-------|
| [`editor-tools/gizmo/`](./editor-tools/gizmo/) | Transform gizmo: UE5 analysis, implementation guide. |
| [`editor-tools/render-toolbar/`](./editor-tools/render-toolbar/) | Viewport toolbar. |

---

## 13 · Engineering

Path: [`engineering/`](./engineering/)

| Subdir | Focus |
|--------|-------|
| [`engineering/debug-tools/`](./engineering/debug-tools/) | Rendering diagnostics inventory, Falcor integration. |
| [`engineering/vscode-integration/`](./engineering/vscode-integration/) | Runtime/N-API IPC, frame capture. |
| [`engineering/hardcoded-paths/`](./engineering/hardcoded-paths/) | Audit of hardcoded filesystem paths in runtime/. |

---

## 14 · Data Flow Reference

Path: [`dataflow/`](./dataflow/) — Per-feature data flow documents (ECS → CB → shader).

Covers: Bruneton LUT, decal component, flipbook sub-UV, GPU SDF bake, Hi-Z occlusion culling,
radiance cache, reflection probe capture, SDF GI pipeline, sky LUT, skylight+fog,
SSGI, stochastic SSR, TAA→tonemap, VFX parameter UI, volumetric fog pipeline.

---

## 15 · External References

Path: [`_references/`](./_references/)

| Subdir | Focus |
|--------|-------|
| [`_references/falcor/`](./_references/falcor/) | NVIDIA Falcor reference implementation analysis. |
| [`_references/dlss/`](./_references/dlss/) | DLSS integration analysis. |
| [`_references/intel-gpu/`](./_references/intel-gpu/) | Intel GPU research: AO, denoising, materials, neural graphics, ray tracing, sampling, upscaling. |
| [`_references/engine-comparison/`](./_references/engine-comparison/) | Godot/Unity/UE5 feature matrices. |
| [`_references/ue5-parity/`](./_references/ue5-parity/) | UE5 property parity reports. |

---

## 16 · Archive

Path: [`_archive/`](./_archive/) — Old root READMEs/INDEXes, overnight notes, historical issue lists. Kept for continuity; not authoritative.

---

## Quick paths

- **I want to fix Sponza lighting**: start at [`lighting/indoor/sponza-lighting-rootcause.md`](./lighting/indoor/sponza-lighting-rootcause.md) → then [`lighting/uzengine/gap-analysis.md`](./lighting/uzengine/gap-analysis.md).
- **I want to understand Lumen**: [`lighting/lumen/00-architecture.md`](./lighting/lumen/00-architecture.md).
- **I want Crimson Desert's tech stack**: [`lighting/crimson-desert/00-blackspace-engine.md`](./lighting/crimson-desert/00-blackspace-engine.md).
- **I want the new default lighting pipeline plan**: [`lighting/uzengine/aaa-default-pipeline.md`](./lighting/uzengine/aaa-default-pipeline.md).
- **I want to wire a new CVar**: [`rendering-arch/cvars/uzengine-implementation-guide.md`](./rendering-arch/cvars/uzengine-implementation-guide.md).
