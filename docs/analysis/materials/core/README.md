<!-- doc-meta
type: index
category: analysis
theme: materials
title: Material System Analysis — PBR, Parameters, Parallax
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Materials & Shading Analysis

Complete analysis of UE5 material system, material instances, shader parameters, parallax occlusion mapping, and material authoring patterns.

## Files

- [quick-reference-material-data.md](quick-reference-material-data.md) — Quick reference data structures for material system
- [quick-reference-material-system.md](quick-reference-material-system.md) — Quick reference for material system concepts and parameters
- [readme-material-system.md](readme-material-system.md) — Material system overview and architecture entry point
- [ue5-material-parameter-system-extraction.md](ue5-material-parameter-system-extraction.md) — Exact extraction of UE5 material and material instance parameter data model
- [ue5-material-patterns-uzengine-adaptation.md](ue5-material-patterns-uzengine-adaptation.md) — How to adapt UE5 material patterns to UzEngine architecture
- [ue5-material-struct-layouts-concrete.md](ue5-material-struct-layouts-concrete.md) — Concrete C++ struct layouts for UE5 materials
- [ue5-material-system-architecture.md](ue5-material-system-architecture.md) — Complete UE5 material system architecture and component relationships
- [ue5-material-system-directory-structure.md](ue5-material-system-directory-structure.md) — How UE5 organizes material files, references, and asset management
- [ue5-material-system-patterns.md](ue5-material-system-patterns.md) — Common patterns and idioms in UE5 material system
- [ue5-material-system-tree.md](ue5-material-system-tree.md) — Material inheritance tree and master material patterns
- [ue5/README.md](ue5/README.md) — UE5 material analysis detailed entry point
- [ue5/godot-pom-heightmap-analysis.md](ue5/godot-pom-heightmap-analysis.md) — Godot's parallax occlusion mapping heightmap implementation
- [ue5/godot-pom-hlsl-reference.md](ue5/godot-pom-hlsl-reference.md) — HLSL reference code for Godot-style POM
- [ue5/parallel-ox-mapping-analysis.md](ue5/parallel-ox-mapping-analysis.md) — Parallel occlusion mapping technique analysis
- [ue5/ue5-pom-code-reference.md](ue5/ue5-pom-code-reference.md) — UE5 POM implementation code snippets

## Quick Navigation

### Understanding Material System Architecture?
1. [ue5-material-system-architecture.md](ue5-material-system-architecture.md) — Start with architecture overview
2. [ue5-material-system-directory-structure.md](ue5-material-system-directory-structure.md) — Asset organization
3. [ue5-material-system-tree.md](ue5-material-system-tree.md) — Inheritance and master materials

### Material Data Model?
- [ue5-material-parameter-system-extraction.md](ue5-material-parameter-system-extraction.md) — Exact parameter data model
- [ue5-material-struct-layouts-concrete.md](ue5-material-struct-layouts-concrete.md) — C++ struct definitions
- [quick-reference-material-data.md](quick-reference-material-data.md) — Quick reference

### Parallax Occlusion Mapping (POM)?
- [ue5/ue5-pom-code-reference.md](ue5/ue5-pom-code-reference.md) — UE5 POM code
- [ue5/godot-pom-hlsl-reference.md](ue5/godot-pom-hlsl-reference.md) — Godot HLSL implementation
- [ue5/parallel-ox-mapping-analysis.md](ue5/parallel-ox-mapping-analysis.md) — Algorithm details

### Adapting to UzEngine?
- [ue5-material-patterns-uzengine-adaptation.md](ue5-material-patterns-uzengine-adaptation.md) — Adaptation guidance

## Key Concepts

### Material System Components
- **UMaterial**: Base material graph (parameters, expressions, compiled state)
- **UMaterialInstance**: Runtime parameter overrides of a parent material
- **Material Parameters**: Scalar (float), Vector (color/position), Texture, Static (compile-time)
- **Material Graph**: Nodes for math, texture sampling, lighting calculations

### Parameter System
- **Scalar Parameters**: Float ranges, defaults, UI hints
- **Vector Parameters**: Color (0-1) vs. raw vector (unbounded)
- **Texture Parameters**: Texture object references, compression hints
- **Static Switches**: Compile-time branches, shader variant generation

### Parallax Occlusion Mapping (POM)
- **Heightmap displacement**: Steep parallax with multiple ray steps
- **Ray stepping**: Linear or binary search through height layers
- **Parallax correction**: View-dependent offset for per-pixel displacement
- **Surface interaction**: Occluded areas darken, peak areas brighten (contact shadows + highlights)
