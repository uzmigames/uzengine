<!-- doc-meta: analysis-directory-index; date: 2026-03-27 -->

# Analysis Documents

Reference analysis documents organized by subsystem. Used for architectural research, gap analysis, and implementation planning.

## Directory Structure

| Directory | Contents | Files |
|-----------|----------|-------|
| [architecture/](architecture/) | Engine architecture decisions and patterns | 2+ |
| [asset-pipeline/](asset-pipeline/) | Asset loading architecture, texture benchmarks, bottleneck analysis | 5 |
| [atmosphere/](atmosphere/) | Sky, fog, volumetric clouds, aerial perspective, transmittance LUTs | 17 |
| [backend-parity/](backend-parity/) | DX11/SM6.0 vs SM6.6 rendering parity: gap analysis, fix tracking | 2 |
| [dataflow/](dataflow/) | Data flow documents for multi-subsystem features (SDF GI, SSGI, radiance cache) | 5 |
| [dx12-descriptors/](dx12-descriptors/) | DX12 descriptor management: UE5/Godot patterns, non-bindless issues | 5 |
| [engine-comparison/](engine-comparison/) | Cross-engine architecture analysis (Godot, Unity, Three.js) | 8+ |
| [gi/](gi/) | Global illumination: Lumen radiance cache, SDF GI, ground radiance | 3 |
| [gizmo/](gizmo/) | Editor gizmo specifications: UE5 geometry, colors, interaction, gap analysis | 6 |
| [lighting/](lighting/) | Shadows, sky light, local lights, reversed-Z depth | 5 |
| [materials/](materials/) | Material system architecture, parameter layouts, struct definitions | 12 |
| [parity/](parity/) | UE5 vs UzEngine rendering parity: gap analysis, property inventory, roadmap | 10 |
| [post-process/](post-process/) | Auto-exposure, histogram, tonemapping | 5 |
| [renderer/](renderer/) | Debug visualization, general pipeline analysis, mesh winding | 3+ |
| [rendering-pipeline/](rendering-pipeline/) | Debug visualization, general pipeline analysis | 2+ |
| [rhi/](rhi/) | RHI abstraction: cbuffer layouts, bindless, SM5 patterns, Vulkan analysis | 12 |
| [upscaling/](upscaling/) | TSR, Godot 4K upscaling, temporal super-resolution | 8 |
| [vfx/](vfx/) | VFX system gaps, implementation roadmap, architectural analysis | 5 |

## Key Documents

- **Backend parity**: [backend-parity/dx11-sm6-parity-report.md](backend-parity/dx11-sm6-parity-report.md) — 29 issues across DX11/SM6.0 vs SM6.6
- **DX12 descriptors**: [dx12-descriptors/dx12-non-bindless-rendering-issues.md](dx12-descriptors/dx12-non-bindless-rendering-issues.md) — root cause analysis of staging block race condition
- **Component parity**: [parity/ue5-component-property-parity.md](parity/ue5-component-property-parity.md) — field-by-field comparison of 9 rendering components
- **Gizmo specs**: [gizmo/editor-gizmo-specifications.md](gizmo/editor-gizmo-specifications.md) — UE5.6 exact geometry, colors, dimensions

## Conventions

- All filenames: `kebab-case.md` (no UPPERCASE, no snake_case)
- Max 800 lines per document
- Frontmatter `<!-- doc-meta -->` required
- Each subdirectory has its own `README.md` index
