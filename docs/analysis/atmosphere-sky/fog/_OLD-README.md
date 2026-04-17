<!-- doc-meta
title: "Volumetric Fog Energy Balance Analysis - Index"
description: "Index and navigation guide for volumetric fog research documents"
tags: ["rendering", "fog", "volumetric", "energy-conservation", "index"]
status: "complete"
author: "claude-code"
-->

# Volumetric Fog Energy Balance Analysis - Complete Research

This directory contains a comprehensive analysis of how UE5 implements volumetric fog without over-darkening the scene. Three complementary documents provide different levels of detail and focus.

## Documents

### 1. [volumetric-fog-energy-balance.md](./volumetric-fog-energy-balance.md) — **COMPREHENSIVE REFERENCE**

**Length**: ~800 lines | **Sections**: 12 | **Focus**: Complete technical analysis

Best for: Understanding the WHAT and WHY of UE5's approach.

**Contents**:
- Executive summary of three-tier energy balance system
- Complete parameter documentation (every default value from source)
- Mathematical equations from rendering shaders
- Height fog + volumetric fog interaction model
- Directional light and sky atmosphere contributions
- Energy conservation mechanisms (5 detailed mechanisms)
- Practical tuning guide (when scene is too dark/too bright)
- Frostbite/Hillaire 2016 comparison
- Key files reference with line numbers

**When to read this**: 
- Understanding the architecture
- Learning why certain parameters matter
- Comparing with published papers (Hillaire 2016)
- Making design decisions

**Critical sections**:
- Part 1: Component Parameters (all UPROPERTYs documented)
- Part 2: Energy Balance Equation (the math)
- Part 8: Anti-Over-Darkening Mechanisms (5 layers of protection)
- Part 10: Practical Tuning Guide (artist perspective)

---

### 2. [volumetric-fog-implementation-guide.md](./volumetric-fog-implementation-guide.md) — **QUICK REFERENCE**

**Length**: ~600 lines | **Sections**: 12 | **Focus**: Implementation-ready guide

Best for: Implementing volumetric fog in UzEngine.

**Contents**:
- Parameter mapping (C++ struct + defaults)
- Energy balance math in pseudocode
- Implementation checklist (6 phases)
- Critical default values (do not change)
- Tuning parameters for artists
- Complete algorithms (density computation, light injection, composition)
- Henyey-Greenstein phase function
- Common mistakes (what to avoid)
- Testing checklist
- Performance notes

**When to read this**:
- Before implementing any fog system
- As a checklist during development
- When debugging incorrect behavior
- When tuning for artistic goals

**Critical sections**:
- "Implementation Checklist" (step-by-step phases)
- "Energy Balance: The Math" (4-step pipeline)
- "Algorithm: Material Setup" (density computation)
- "Algorithm: Composition" (final blending)
- "Common Mistakes to Avoid" (critical gotchas)

---

### 3. [dataflow/volumetric-fog-complete-pipeline.md](./dataflow/volumetric-fog-complete-pipeline.md) — **DATA FLOW TRACE**

**Length**: ~500 lines | **Sections**: 6 stages + diagrams | **Focus**: Complete pipeline from component to pixel

Best for: Understanding data flow and integration points.

**Contents**:
- Stage 1: Component properties (editor values)
- Stage 2: CPU-side setup (InitFogConstants function)
- Stage 3: Constant buffer upload (FFogUniformParameters)
- Stage 4a: Material setup shader (density/extinction computation)
- Stage 4b: Light injection shader (per-light scattering)
- Stage 5: Integration shader (raymarching accumulation)
- Stage 6: Composition shader (final blending formula)
- Parameter flow diagram (end-to-end)
- Energy conservation checks at each stage
- Key values for implementation

**When to read this**:
- Implementing a new render pass
- Understanding how data flows through the pipeline
- Debugging integration issues
- Checking which files to modify

**Critical sections**:
- "Stage 4a: Material Setup Shader" (the anti-darkening split)
- "Stage 6: Composition Shader" (the final formula)
- "Complete Parameter Flow Diagram" (end-to-end visualization)
- "Energy Conservation Check" (validation at each stage)

---

## Quick Navigation

### I want to understand...

**...why fog doesn't over-darken:**
→ Read [volumetric-fog-energy-balance.md, Part 8](./volumetric-fog-energy-balance.md#part-8-anti-over-darkening-mechanisms-summary)

**...what parameters I need to implement:**
→ Read [volumetric-fog-implementation-guide.md, Parameter Mapping](./volumetric-fog-implementation-guide.md#quick-reference-parameter-mapping)

**...how to implement volumetric fog step-by-step:**
→ Read [volumetric-fog-implementation-guide.md, Implementation Checklist](./volumetric-fog-implementation-guide.md#implementation-checklist)

**...what goes wrong with naive implementations:**
→ Read [volumetric-fog-implementation-guide.md, Common Mistakes](./volumetric-fog-implementation-guide.md#common-mistakes-to-avoid)

**...how data flows from component to pixel:**
→ Read [dataflow/volumetric-fog-complete-pipeline.md](./dataflow/volumetric-fog-complete-pipeline.md)

**...the mathematical equations:**
→ Read [volumetric-fog-energy-balance.md, Part 2](./volumetric-fog-energy-balance.md#part-2-the-energy-balance-equation)

**...how to compare with Hillaire/Frostbite:**
→ Read [volumetric-fog-energy-balance.md, Part 7](./volumetric-fog-energy-balance.md#part-7-the-frostbite--hillaire-approach-in-ue5)

**...how artists should tune parameters:**
→ Read [volumetric-fog-energy-balance.md, Part 10](./volumetric-fog-energy-balance.md#part-10-practical-tuning-guide)

---

## The Three-Tier Anti-Darkening System (Executive Summary)

UE5 prevents volumetric fog from over-darkening the scene using three complementary strategies:

### Tier 1: Conservative Defaults
- `FogDensity = 0.02f` (very low by default)
- Fog is subtle unless artist explicitly increases density

### Tier 2: Decoupled Albedo Parameter
- **Extinction** = how much light is absorbed per unit distance
- **Scattering** = Albedo × Extinction
- Key: When Albedo = 1.0 (default), all absorbed light scatters back
- Result: No net absorption, just directional filtering

### Tier 3: Emissive Compensation
- Emissive light **scales with density**
- Where extinction is high, emissive is also high
- Compensates for light lost to extinction
- Net darkening = 0

**Bonus mechanisms**:
- **Transmittance composition** — Height fog only shows through transparent vfog
- **Light accumulation** — Multiple lights inject scattering (fog is illuminated)

---

## Key Files in UE5 Source

All line numbers refer to UE5 5.6.0:

| File | Purpose | Critical Lines |
|------|---------|-----------------|
| `ExponentialHeightFogComponent.h` | Component properties | 18-25, 142-176 |
| `ExponentialHeightFogData.h` | Data structure with defaults | 17-25 |
| `VolumetricFog.usf` | Material setup + light injection | 135-149, 146, 242-249 |
| `HeightFogCommon.ush` | Height fog + composition | 174-180, 375, 469 |
| `FogRendering.cpp` | CPU setup | 208-304 |
| `FogRendering.h` | Fog parameters uniform struct | — |

---

## Implementation Priorities

### Phase 1 (REQUIRED FOR ENERGY BALANCE):
- [ ] `FogDensity` parameter (default 0.02)
- [ ] `VolumetricFogAlbedo` parameter (default 1.0)
- [ ] `VolumetricFogExtinctionScale` parameter (default 1.0)
- [ ] Decoupled calculation: `Scattering = Albedo × Extinction`

### Phase 2 (REQUIRED FOR NO OVER-DARKENING):
- [ ] `VolumetricFogEmissive` parameter (default 0.0)
- [ ] Density-scaled emissive: `EmissiveOutput = Emissive × Density`

### Phase 3 (REQUIRED FOR PROPER COMPOSITION):
- [ ] Height fog integration
- [ ] Transmittance-based blending: `Final = VFog + HeightFog × VFog.alpha`

### Phase 4 (REQUIRED FOR ILLUMINATION):
- [ ] Directional light injection
- [ ] Sky light contribution
- [ ] Henyey-Greenstein phase function

---

## Critical Equations (Copy-Paste Ready)

### Density Computation
```
GlobalDensity = FogDensity × exp2(-FogHeightFalloff × (worldZ - heightOffset))
GlobalDensity *= 0.5f  // Voxel representation correction factor
```

### Energy Balance Split
```
Extinction = GlobalDensity × ExtinctionScale
Scattering = Albedo × Extinction
// Key: Scattering ≤ Extinction (Albedo ≤ 1.0)
```

### Emissive Compensation
```
EmissiveToAdd = Emissive
if (PROJECT_EXPFOG_MATCHES_VFOG):
    EmissiveToAdd *= GlobalDensity  // Scale by density!
```

### Final Composition
```
FinalColor = VolumetricFogRGB + HeightFogRGB × VolumetricFogAlpha
FinalAlpha = VolumetricFogAlpha × HeightFogAlpha
```

---

## Common Pitfalls (Do Not Repeat)

1. **Making fog too dense by default** → Use 0.02f
2. **Forgetting albedo constraint** → Keep Albedo ≤ 1.0
3. **Constant emissive** → Must scale with density
4. **Additive blending** → Use transmittance-based composition
5. **No light injection** → Fog becomes invisible/dark without lights
6. **Wrong composition order** → Height fog must be modulated by vfog transmittance

---

## References & Citations

### Academic Papers
- Hillaire 2016, "Physically Based & Unified Volumetric Rendering in Real-Time" (GDC)
  - Foundation for UE5's scattering/extinction model
  - Describes Henyey-Greenstein phase function
  - Energy conservation via transmittance-based composition

### Engine Sources
- UE5 5.6.0 Engine source (used for all code citations)
- Falcor reference renderer (available in `E:/Falcor`)

---

## Document Metadata

**Created**: April 5, 2026
**Status**: Complete and ready for reference
**Format**: Markdown with code blocks and tables
**Total Content**: ~1900 lines across 3 documents
**UE5 Version**: 5.6.0
**Target Engine**: UzEngine (C++23, custom rendering)

---

## Next Steps for UzEngine

1. **Read Phase 1**: volumetric-fog-energy-balance.md (understand the problem)
2. **Read Phase 2**: volumetric-fog-implementation-guide.md (implement the solution)
3. **Read Phase 3**: dataflow/volumetric-fog-complete-pipeline.md (trace every stage)
4. **Implement Phase 1**: Density + Albedo + Extinction (basic parity)
5. **Implement Phase 2**: Emissive compensation (energy balance)
6. **Implement Phase 3**: Composition + Height fog (final integration)
7. **Test**: Run with Falcor reference comparison (`E:/Falcor/build/.../Mogwai.exe`)

---

**Questions?** Refer to the specific document for the level of detail you need.
