# UzEngine Analysis Library

Reference material, reverse engineering notes, gap analyses, and architectural
research used to drive UzEngine's design. Reorganized 2026-04-17 from a flat
disarray into the taxonomy described below.

See [INDEX.md](./INDEX.md) for the full navigable index.

## Top-level taxonomy

| Directory | Scope |
|-----------|-------|
| [lighting/](./lighting/) | **Direct lighting, shadows, GI, Lumen, Crimson Desert, modern techniques, AAA redesign plan.** |
| [reflections/](./reflections/) | SSR, SSSR, hybrid reflections, UE5/Godot/Three.js SSR analyses. |
| [atmosphere-sky/](./atmosphere-sky/) | Sky, aerial perspective, height fog, volumetric fog, volumetric clouds. |
| [materials/](./materials/) | PBR, IBL, glass/transmission, decals. |
| [post-process/](./post-process/) | Tonemap, auto-exposure, bloom, GTAO. |
| [upscaling/](./upscaling/) | TAA, TSR, DLSS, FSR, Godot upscaling. |
| [path-tracing/](./path-tracing/) | Reference path tracer plans and architecture. |
| [atmosphere-sky/clouds/](./atmosphere-sky/clouds/) | Volumetric clouds. |
| [vfx-particles/](./vfx-particles/) | Niagara, flipbook/sub-UV, module stack UI. |
| [animation-skeletal/](./animation-skeletal/) | Skeletal mesh, animation sequences, GPU skinning. |
| [rendering-arch/](./rendering-arch/) | Renderer architecture, CVar patterns, expert reviews, pipeline gap analysis. |
| [gpu-backend/](./gpu-backend/) | RHI, DX12, descriptor management, SM5/SM6 parity. |
| [asset-pipeline/](./asset-pipeline/) | Loaders, mesh import/normals, texture streaming. |
| [editor-tools/](./editor-tools/) | Gizmo, viewport toolbar. |
| [engineering/](./engineering/) | Debug tools inventory, VSCode integration, hardcoded-path audit. |
| [dataflow/](./dataflow/) | Per-feature data flow documents (ECS → CB → shader). |
| [_references/](./_references/) | External engine references: Falcor, DLSS, Intel GPU research, UE5 parity, engine comparisons. |
| [_archive/](./_archive/) | Historical notes, old root-level READMEs, session logs. |

## How to navigate

1. **You want to improve lighting or shadows** → start at [lighting/INDEX.md](./lighting/INDEX.md).
2. **You want to implement a feature** → look in the feature dir (e.g. `reflections/`, `atmosphere-sky/fog/`), then cross-reference `rendering-arch/cvars/` for wiring.
3. **You want reference-engine source analysis** → `_references/` for external engines, feature dirs for specific systems.
4. **You want to trace data through the engine** → `dataflow/` has per-feature flow documents.

## Conventions

- Filenames are kebab-case, English only.
- Each non-trivial directory has an `INDEX.md` at its root.
- Files prefixed with `_` are archived, grandfathered, or meta (README snapshots, old index files).
- Files prefixed with `ue5-` / `godot-` / `unity-` / `frostbite-` / `threejs-` / `cryengine-` / `falcor-` are reference-engine analyses.
- Files with no engine prefix are UzEngine-specific (plans, gap analyses, implementation guides).
