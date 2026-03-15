# UzEngine — Architecture References

Reference architecture analysis from UE5, Godot, and Unity. These documents inform UzEngine's design decisions for RHI abstraction, shader binding, memory management, and GPU architecture compatibility.

> For the main architecture overview, see [../architecture.md](../architecture.md).
> For engine system architecture, see [../engine/](../engine/).

## RHI & GPU Binding

Core reference documents for rendering hardware interface design:

| Document | Topic |
|----------|-------|
| [rhi-quick-reference.md](rhi-quick-reference.md) | Quick reference for RHI concepts and patterns |
| [rhi-pattern-rationale.md](rhi-pattern-rationale.md) | Rationale behind key RHI design patterns |
| [rhi-backend-compatibility-fix-guide.md](rhi-backend-compatibility-fix-guide.md) | Fixing RHI compatibility issues across backends |
| [shader-buffer-compatibility-directive.md](shader-buffer-compatibility-directive.md) | Ensuring shader/buffer layout compatibility (DX12/Vulkan) |
| [uniform-buffer-layout-guide.md](uniform-buffer-layout-guide.md) | Uniform/constant buffer layout guidelines |
| [cbuffer-quick-reference.md](cbuffer-quick-reference.md) | Constant buffer packing and alignment rules |

## UE5 RHI Analysis

Deep dives into UE5's RHI architecture and patterns:

| Document | Topic |
|----------|-------|
| [ue5-rhi-analysis-index.md](ue5-rhi-analysis-index.md) | Index of all UE5 RHI analysis documents |
| [ue5-rhi-binding-pattern-comparison.md](ue5-rhi-binding-pattern-comparison.md) | Comparison of UE5 vs UzEngine RHI binding patterns |
| [ue5-rhi-dx11-dx12-analysis.md](ue5-rhi-dx11-dx12-analysis.md) | Analysis of UE5's DX11 and DX12 backend implementations |
| [ue5-root-signature-analysis.md](ue5-root-signature-analysis.md) | Analysis of UE5's root signature design |
| [ue5-code-reference-snippets.md](ue5-code-reference-snippets.md) | Key UE5 RHI code snippets for reference |

## Cross-Engine Comparisons

Analysis of how reference engines solve RHI problems:

| Document | Topic |
|----------|-------|
| [dx11-dx12-quick-reference.md](dx11-dx12-quick-reference.md) | Quick reference: DX11 vs DX12 API differences |
| [unity-dx11-dx12-abstraction-analysis.md](unity-dx11-dx12-abstraction-analysis.md) | How Unity abstracts DX11/DX12 differences |
| [unity-rhi-abstraction-pattern.md](unity-rhi-abstraction-pattern.md) | Unity's RHI abstraction pattern and design |
| [godot-bindless-vulkan-analysis.md](godot-bindless-vulkan-analysis.md) | Bindless resources in Godot's Vulkan backend |
| [godot-backend-compatibility-analysis.md](godot-backend-compatibility-analysis.md) | Godot's cross-backend compatibility strategies |

## Migration & Implementation Guides

Practical guides for implementing RHI features:

| Document | Topic |
|----------|-------|
| [ue5-binding-migration-playbook.md](ue5-binding-migration-playbook.md) | Step-by-step playbook for migrating descriptor binding |
| [ue5-shader-model-compatibility-summary.md](ue5-shader-model-compatibility-summary.md) | Shader model compatibility summary (SM5/SM6) |
| [dx11-crash-fix-implementation-guide.md](dx11-crash-fix-implementation-guide.md) | Guide to fixing DX11-specific crashes and compatibility issues |
