<!-- doc-meta
title: Mesh Import & Processing Analysis
description: Deep dives into normal smoothing, tangent generation, and vertex data processing in UE5, Godot 4, and industry standards.
keywords: mesh-import, normals, tangents, smoothing-groups, vertex-data, FBX, glTF
tags: analysis, mesh, import-pipeline
-->

# Mesh Import & Processing Analysis

This subdirectory contains comprehensive analyses of how game engines (UE5, Godot 4) handle mesh normal computation, tangent generation, and vertex data processing during import.

---

## Documents Overview

### 1. ue5-normal-smoothing-and-tangent-generation.md (PRIMARY - READ FIRST)

**Size:** 680+ lines | **Time to read:** 30-40 minutes

**What it covers:**
- Complete UE5 5.6 normal smoothing algorithm with source code references
- Weighted normal averaging by triangle area AND corner angle
- Hard edge detection from angle threshold and FBX smoothing groups
- MikkTSpace tangent space generation (industry standard)
- Built-in tangent averaging as fallback
- Normal encoding formats (FPackedNormal, FPackedRGBA16N)
- Import options and their visual effects
- Nanite-specific behavior
- Shader normal decoding

**Key takeaways:**
```
UE5 approach:
- Weights by BOTH area and angle (not just area)
- Preserves hard edges (no smoothing across sharp transitions)
- Uses MikkTSpace for production tangent generation
- Stores normals as signed int8×4 (4 bytes)
- All computation at import time (zero runtime cost)
```

**Best for:**
- Understanding correct weighted normal implementation
- Implementing normal smoothing in custom engines
- Shader normal decoding

---

### 2. godot4-normal-smoothing-analysis.md

**Size:** 550+ lines | **Time to read:** 25-30 minutes

**What it covers:**
- Godot 4's simpler unweighted normal averaging
- Smoothing groups handling
- MikkTSpace integration
- Normal import pipeline (FBX, glTF, OBJ)
- Vertex format (32-bit packed)
- Comparison with UE5 (differences and trade-offs)

**Key difference from UE5:**
```
Godot approach:
- Simple unweighted average (each face contributes equally)
- No corner angle weighting
- Faster import, but visually lower quality on thin/complex geometry
```

**Best for:**
- Understanding simpler implementations
- Learning trade-offs between speed and quality
- Comparative analysis

---

### 3. normal-smoothing-tangent-comparison.md

**Size:** 450+ lines | **Time to read:** 20-25 minutes

**What it covers:**
- Side-by-side comparison: UE5 vs Godot 4 vs Industry Standard
- Weighted normal algorithms (area-only vs area+angle)
- Tangent generation methods (built-in vs MikkTSpace)
- Performance metrics
- Visual quality differences
- Hard edge detection strategies
- Recommendations for custom engines

**Decision table:**
```
Feature              | UE5          | Godot 4      | Recommended
--------------------|--------------|--------------|---------------
Normal Weighting     | Area + Angle | Unweighted   | Area + Angle
Tangent Generation   | MikkTSpace   | MikkTSpace   | MikkTSpace
Hard Edge Detection  | Angle-based  | Explicit     | Both
Performance (1M tri) | ~200ms       | ~80ms        | Start with 200ms
Visual Quality       | Excellent    | Good         | Match UE5
```

**Best for:**
- Deciding which approach to implement
- Understanding trade-offs
- Performance planning

---

### 4. godot4-normal-pipeline-dataflow.md

**Size:** 320+ lines | **Time to read:** 15-20 minutes

**What it covers:**
- Complete data flow from FBX file → GPU vertex buffer
- Godot 4's `ImporterMesh` class architecture
- Normal computation pipeline steps
- Constant buffer (cbuffer) integration
- Shader binding and decoding

**Data flow diagram:**
```
FBX file
  ↓
[FBX SDK Import]
  ↓ Raw triangles + per-face smoothing groups
[Mesh Description Converter]
  ↓ Unified mesh format
[Normal Computation]
  ↓ Per-vertex averaged normals
[Tangent Generation (MikkTSpace)]
  ↓ Tangent + bitangent sign
[Vertex Packing]
  ↓ FPackedNormal format
[GPU Upload]
  ↓
Vertex Buffer (GPU)
  ↓ Shader reads and unpacks
Pixel Shader
```

**Best for:**
- Understanding the full pipeline flow
- Identifying integration points in your engine
- Debugging import problems

---

## Key Concepts

### Normal Weighting

UE5 uses **area + angle** weighting:
```
weight = triangle_area × corner_angle

Why two weights?
- Area: large triangles should influence the normal more
- Angle: at sharp corners, contribution naturally smaller
- Combined: prevents spike-like triangles from dominating
```

Example:
```
Unweighted: thin spike + large surface = faceted seam
Weighted:   thin spike has less influence = smooth transition
```

### Hard Edges

Edges marked as "hard" **prevent normal smoothing**:
```
Soft edge:  normals average across
            → smooth transition between faces
            → good for organic surfaces

Hard edge:  normals separate
            → sharp transition between faces
            → good for hard surface modeling

Detection methods:
1. Angle: if angle > 60°, mark hard (UE5 + industry)
2. Smoothing groups: FBX explicit groups (legacy Autodesk format)
```

### Tangent Space

Essential for normal mapping:
```
Normal Map provides: local surface detail (bumps, dents)

To apply normal map correctly:
- Need tangent frame (T, B, N) in world space
- T = tangent (along UV.x direction)
- B = binormal (along UV.y direction)
- N = vertex normal

MikkTSpace: generates T aligned to UV flow
            → consistent texel mapping → no seams
```

---

## Implementation Priority

### If you have 1 week: Minimum Viable
1. Simple unweighted normal averaging (Godot 4 style)
2. Basic hard edge detection (angle threshold)
3. Built-in tangent averaging (no MikkTSpace yet)

**Result:** Smooth meshes, working but lower quality than UE5

### If you have 2 weeks: Production Quality (Recommended)
1. Weighted normal averaging (area + angle)
2. Hard edge detection (angle + smoothing groups)
3. MikkTSpace tangent generation
4. Normal packing (FPackedNormal)

**Result:** Matches UE5 quality, 27% smaller vertex buffers

### If you have 4 weeks: Premium Quality
1. All of above
2. Nanite-style cluster normals (for very high poly)
3. LOD-specific normal handling
4. GPU-side normal reconstruction

**Result:** Exceeds UE5 quality, complex but unmatched results

---

## Quick Start (For Implementation)

### Step 1: Read ue5-normal-smoothing-and-tangent-generation.md

Pay special attention to:
- Section 1: Normal Recomputation (algorithm with weights)
- Section 2: Tangent Generation (MikkTSpace integration)
- Section 4: Import Options (what matters for users)
- Section 8: Best Practices (implementation roadmap)

### Step 2: Read docs/guides/mesh-normal-import-guide.md

This document (in docs/guides/) provides:
- Step-by-step code for all phases
- Unit test examples
- Integration with your FBX importer
- Shader changes needed

### Step 3: Compare with godot4-normal-smoothing-analysis.md

Understand the simpler approach, recognize trade-offs with UE5.

### Step 4: Implement in this order
1. Phase 1: Basic normal averaging (2-3 hours)
2. Phase 2: Area+angle weighting (1-2 hours)
3. Phase 3: Hard edge detection (1-2 hours)
4. Phase 4: MikkTSpace integration (3-4 hours)
5. Phase 5: FBX importer integration (2-3 hours)
6. Phase 6: Shader decoding + vertex format (1-2 hours)

---

## File Organization

```
docs/analysis/mesh/
  ├── README.md (← you are here)
  ├── ue5-normal-smoothing-and-tangent-generation.md      [PRIMARY]
  ├── godot4-normal-smoothing-analysis.md                 [REFERENCE]
  ├── normal-smoothing-tangent-comparison.md              [DECISION]
  └── godot4-normal-pipeline-dataflow.md                  [ARCHITECTURE]

docs/guides/
  └── mesh-normal-import-guide.md                         [IMPLEMENTATION]
```

---

## Source Code References

All line numbers refer to source code versions:
- **UE5:** Engine/Source/Runtime/StaticMeshDescription/Private/StaticMeshOperations.cpp (UE 5.6)
- **Godot:** core/io/image.cpp, scene/resources/importer_mesh.cpp (Godot 4.x)

---

## Common Questions

**Q: Do I need to implement normal smoothing?**
A: Yes. Without it, all imported meshes look faceted (each triangle visible as a flat surface). This is usually the first complaint about custom engines.

**Q: Should I use MikkTSpace?**
A: If you support normal maps, yes. It's the industry standard and eliminates UV-seam artifacts. Library integration is simple (~3 hours).

**Q: Does weighted normal averaging matter?**
A: Depends on mesh complexity. Simple models: no. Complex models with thin/spike triangles: yes, huge visual improvement.

**Q: What's the performance cost?**
A: Import-time only (~100-200ms for 1M triangles). Zero runtime cost. Worth the one-time expense.

**Q: Can I keep my current vertex format?**
A: If you use float3 for normals (12 bytes). Better approach: pack to int8×4 (4 bytes), save 66% memory per normal.

---

## Related Documentation

- **Vertex Format Architecture:** docs/architecture/vertex-format.md
- **Shader Math:** docs/guides/shader-math-reference.md
- **Import Pipeline:** docs/guides/asset-import-guide.md
- **Rendering Quality Baseline:** docs/engine/rendering-quality-requirements.md
