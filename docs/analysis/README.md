<!-- doc-meta
title: Engine Architecture Analysis Suite
author: UzEngine Architecture Team
date: 2026-04-04
-->

# Analysis Documentation Suite

This directory contains deep architectural analyses extracted from reference engine sources (UE5, Godot 4) to inform UzEngine's design and implementation.

## Documents

### 1. **ue5-rendering-cvars-reference.md** (PRIMARY REFERENCE)
   **Purpose**: Complete list of all rendering CVars from UE5 source
   
   **Contents**:
   - 1,000+ CVars organized by category (Tone Mapping, Bloom, GTAO, Shadows, TAA, SSR, SSGI, Atmosphere, Clouds, Fog, Hair, Ray Tracing, etc.)
   - Default values
   - Valid ranges
   - Descriptions extracted from UE5 source
   
   **How to Use**:
   - Find a parameter you want to implement (e.g., "bloom threshold")
   - Look it up in the reference (e.g., `r.Bloom.Threshold`)
   - Extract the default value, range, and description
   - Implement in UzEngine using the guide below

### 2. **uzengine-cvar-implementation-guide.md** (STEP-BY-STEP INSTRUCTIONS)
   **Purpose**: Practical guide for implementing CVars in UzEngine
   
   **Contents**:
   - Step 1: Define CVar in C++
   - Step 2: Wire into constant buffer structure
   - Step 3: Populate CB from CVars
   - Step 4: Wire CB into shader
   - Step 5: Validation & clamping
   - Step 6: Console command registration
   - Step 7: Runtime changes
   - Implementation roadmap (Phase 1-3)
   - Code examples for every step
   
   **How to Use**:
   - Pick a feature (e.g., bloom)
   - Follow all 7 steps with code examples
   - You now have a complete, working implementation

### 3. **cvar-architecture-antipatterns.md** (WHAT NOT TO DO)
   **Purpose**: Common mistakes to avoid when building CVar systems
   
   **Contents**:
   - 10 anti-patterns with wrong/right examples
   - Architecture overview showing correct data flow
   - Summary of UE5 pattern
   - Checklist for new features
   - When NOT to use CVars
   
   **How to Use**:
   - Before implementing a feature, read this
   - Verify your design against the anti-patterns
   - Use the checklist before shipping

## Key Statistics

| Category | CVar Count | Most Important |
|----------|-----------|---|
| Tone Mapping | 2 | r.Tonemapper.Sharpen |
| Bloom | 4 | r.Bloom.ScreenPercentage |
| GTAO | 8 | r.GTAO.FalloffEnd |
| Shadows | 25+ | r.Shadow.CSMDepthBias |
| TAA | 10 | r.TemporalAAFilterSize |
| SSR | 9 | r.SSR.Quality |
| SSGI | 6 | r.SSGI.Quality |
| Atmosphere | 30+ | r.SkyAtmosphere.SampleCountMax |
| Volumetric Clouds | 15+ | r.VolumetricCloud.ViewRaySampleMaxCount |
| Volumetric Fog | 7 | r.Fog |
| **Total** | **1,000+** | - |

### 4. **ue5-normal-smoothing-and-tangent-generation.md** (MESH IMPORT)
   **Purpose**: Complete analysis of mesh normal smoothing & tangent generation in UE5
   
   **Contents**:
   - Normal recomputation algorithm with weighted averaging (area + corner angle)
   - Hard edge detection (angle threshold + FBX smoothing groups)
   - Tangent generation via MikkTSpace and built-in methods
   - Import options (ComputeNormals, ImportNormals, etc.)
   - Normal storage formats (FPackedNormal 32-bit, FPackedRGBA16N 64-bit)
   - Runtime handling (no per-frame smoothing, all at import time)
   - Nanite-specific behavior (cluster normals, LOD transitions)
   - Complete source file references and line numbers from UE5 5.6
   - Implementation checklist for custom engines
   
   **How to Use**:
   - Fix faceted/flat shading on FBX imports
   - Implement weighted normal averaging
   - Add MikkTSpace tangent generation
   - Understand normal encoding/decoding in shaders

## Organization

All documents in this directory follow the same structure:
1. **Executive Summary** — key findings
2. **Technical Deep Dive** — algorithm, code, implementation details
3. **Comparison** — vs other engines / industry standards
4. **UzEngine Recommendations** — how to adapt insights

## Next Steps

### For Rendering Quality
1. Read ue5-rendering-cvars-reference.md — understand available CVars
2. Read uzengine-cvar-implementation-guide.md — learn the pattern
3. Read cvar-architecture-antipatterns.md — know what to avoid
4. Pick a feature and follow all 7 steps from the guide
5. Test at runtime — change CVar, verify result next frame

### For Mesh Import & Shading
1. Read godot4-normal-smoothing-analysis.md Section 1-3
2. Follow Section 11 (Recommendations) to implement smoothing groups
3. Integrate into FBX importer
4. Test with sample meshes

## Document Statistics

| Document | Lines | Focus | Industry Ref |
|----------|-------|-------|--------------|
| ue5-rendering-cvars-reference.md | 608 | UE5 1,000+ CVars | UE 5.6 |
| uzengine-cvar-implementation-guide.md | 482 | Implementation pattern | UE 5.6 |
| cvar-architecture-antipatterns.md | 577 | Anti-patterns | General |
| ue5-normal-smoothing-and-tangent-generation.md | 680+ | Mesh normal/tangent import | UE 5.6, MikkTSpace |
| ue5-ssr-architecture.md | 450+ | Screen-space reflection | UE 5.6 |
| **Total** | **2,797+** | Complete reference | Multiple |
