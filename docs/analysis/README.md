<!-- doc-meta: analysis-directory-index; date: 2026-03-21 -->

# Analysis Documents

Reference analysis documents organized by subsystem. Used for architectural research, gap analysis, and implementation planning.

## Directory Structure

| Directory | Contents | Files |
|-----------|----------|-------|
| [asset-pipeline/](asset-pipeline/) | Asset loading architecture, texture benchmarks, bottleneck analysis | 5 |
| [atmosphere/](atmosphere/) | Sky, fog, volumetric clouds, aerial perspective, transmittance LUTs | 17 |
| [dataflow/](dataflow/) | Data flow documents for multi-subsystem features (SDF GI, SSGI, radiance cache) | 5 |
| [engine-comparison/](engine-comparison/) | Cross-engine architecture analysis (Godot, Unity, Three.js) | 8+ |
| [gi/](gi/) | Global illumination: Lumen radiance cache, SDF GI, ground radiance | 3 |
| [lighting/](lighting/) | Shadows, sky light, local lights, reversed-Z depth | 5 |
| [materials/](materials/) | Material system architecture, parameter layouts, struct definitions | 12 |
| [parity/](parity/) | UE5 vs UzEngine rendering parity: gap analysis, property inventory, roadmap | 10 |
| [post-process/](post-process/) | Auto-exposure, histogram, tonemapping | 5 |
| [rendering-pipeline/](rendering-pipeline/) | Debug visualization, general pipeline analysis | 2+ |
| [rhi/](rhi/) | RHI abstraction: cbuffer layouts, bindless, SM5 patterns, Vulkan analysis | 12 |
| [upscaling/](upscaling/) | TSR, Godot 4K upscaling, temporal super-resolution | 8 |

## Key Documents

- **Component parity**: [parity/ue5-component-property-parity.md](parity/ue5-component-property-parity.md) — field-by-field comparison of 9 rendering components
- **Implementation checklist**: [parity/implementation-checklist.md](parity/implementation-checklist.md) — agent-ready tasks for missing properties
- **Rendering gap analysis**: [parity/ue5-vs-uzengine-rendering-gap-analysis.md](parity/ue5-vs-uzengine-rendering-gap-analysis.md) — full feature matrix

## Editor Gizmo Specifications (NEW 2026-03-26)

Complete UE5.6 transform gizmo specifications for visual parity:
- **[editor-gizmo-specifications.md](editor-gizmo-specifications.md)** — EXACT geometry, colors, dimensions, rendering styles (24 KB)
- **[gizmo-implementation-guide.md](gizmo-implementation-guide.md)** — Step-by-step C++ implementation with mesh generators and rendering pipeline (23 KB)
- **[gizmo-quick-reference.md](gizmo-quick-reference.md)** — One-page cheat sheet with all constants and copy-paste values (11 KB)

**All values extracted from UE5.6 source**:
- X/Y/Z axis colors: `#980503 / #226400 / #0624D9` (linear RGB)
- Translate axis length: 70 units with 22-unit cone tip
- Rotation radius: 70 units with 1.25 inner + 3.0 outer thickness
- Scale cube: 12×12×12 units
- Planar handles: 15×15×2 units, offset -15 units inward
- Screen-space sizing: Dynamic via projection matrix to maintain constant viewport appearance

## Conventions

- All filenames: `kebab-case.md` (no UPPERCASE, no snake_case)
- Max 800 lines per document
- Frontmatter `<!-- doc-meta -->` required
- Each subdirectory has its own `README.md` index
