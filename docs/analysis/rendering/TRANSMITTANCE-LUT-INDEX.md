<!-- doc-meta
title: Transmittance LUT Analysis — Document Index
author: Architecture Team
date: 2026-03-15
tags: [index, navigation, transmittance-lut]
status: complete
version: 1.0
-->

# Transmittance LUT Analysis — Complete Document Index

This document provides an overview and navigation guide for the complete transmittance LUT analysis series for UzEngine.

## Document Overview

### Core Analysis Documents (in e:\UzEngine\docs\analysis\rendering\)

#### 1. **ue5-sky-atmosphere-transmittance-lut.md** — PRIMARY REFERENCE
- **Purpose**: Complete technical analysis of UE5's transmittance LUT system
- **Audience**: Architects, engineers, anyone wanting full understanding
- **Length**: ~8,400 words, 40+ sections
- **Contains**:
  - Executive summary
  - Problem statement (why LUT?)
  - Complete LUT parameterization (UV → parameters bidirectional math)
  - Texture specifications (256×64, R32G32B32F format)
  - Complete compute shader code with line-by-line explanation
  - Shader parameters and constant buffers
  - Dispatch configuration (8×8 groups, 32×8 grid)
  - Runtime usage patterns
  - Architecture pattern analysis
  - Performance characteristics
  - UzEngine adaptation guidance
  - File references with exact line numbers
- **Start here if**: You want to understand the complete system
- **Key sections**:
  - "LUT Parameterization" — detailed math explanation
  - "Compute Shader Implementation" — algorithm walkthrough
  - "UzEngine Adaptation Guidance" — what to copy vs. adapt

#### 2. **ue5-transmittance-lut-hlsl-snippets.md** — CODE REFERENCE
- **Purpose**: Direct copy-paste ready HLSL code from UE5 source
- **Audience**: Implementers, shader programmers
- **Length**: ~1,200 words
- **Contains**:
  - Complete UV mapping functions (decode + encode)
  - Full compute shader kernel (production-ready)
  - Runtime lookup functions
  - Helper structures and constant buffer layouts
  - Optional sub-texel adjustment code
  - C++ dispatcher example
  - Implementation checklist
- **Start here if**: You want actual working code immediately
- **Key sections**:
  - "Complete UV Mapping Functions" — copy directly
  - "Complete Compute Shader" — ready to adapt
  - "Key Differences for UzEngine Porting" — what to change

#### 3. **TRANSMITTANCE-LUT-ANALYSIS-SUMMARY.md** — EXECUTIVE SUMMARY
- **Purpose**: High-level overview and synthesis of all findings
- **Audience**: Project leads, architects making go/no-go decisions
- **Length**: ~3,500 words
- **Contains**:
  - Document guide (what each doc covers)
  - Key findings summary
  - What is transmittance (plain language)
  - Why use LUT (cost/benefit)
  - Technical decisions breakdown
  - Porting approach (copy vs. adapt)
  - Integration points
  - Critical success factors
  - Testing strategy
  - Risk assessment
  - Timeline estimate (17 hours total)
  - Next steps
- **Start here if**: You need executive briefing or 1-page summary
- **Key sections**:
  - "Key Findings" — TL;DR facts
  - "Porting to UzEngine" — what to copy vs. adapt
  - "Timeline Estimate" — effort breakdown

#### 4. **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** — CHEAT SHEET
- **Purpose**: One-page reference card for implementers
- **Audience**: Shader programmers, GPU engineers
- **Length**: ~900 words
- **Contains**:
  - One-paragraph summary
  - Key numbers table (LUT size, format, dispatch grid, timing)
  - Core equations (UV decode/encode math)
  - Compute shader template
  - Runtime lookup pattern
  - DX12 integration checklist
  - Performance tips
  - Expected output reference
  - Validation tests
  - Common mistakes (do's and don'ts)
- **Start here if**: You need a quick lookup while implementing
- **Typical use**: Pinned next to keyboard during coding

### Implementation Guide (in e:\UzEngine\docs\guides\)

#### 5. **ue5-transmittance-lut-porting.md** — STEP-BY-STEP GUIDE
- **Purpose**: Walkthrough for implementing transmittance LUT in UzEngine
- **Audience**: C++ and shader engineers
- **Length**: ~2,000 words
- **Contains**:
  - Step 1: HLSL UV mapping functions (copy + paste)
  - Step 2: Compute shader (with comments)
  - Step 3: C++ texture creation and dispatch (DX12 API)
  - Step 4: Integration into sky renderer
  - Step 5: Configuration (CVars)
  - Implementation checklist
  - Performance tips
  - References
- **Start here if**: You're ready to implement now
- **Key sections**:
  - "Step-by-Step" sections — follow in order
  - "Integration into Sky Renderer" — how it fits
  - "Checklist" — ensure nothing is missed

### Summary Document (this file)
#### 6. **TRANSMITTANCE-LUT-INDEX.md** — NAVIGATION (YOU ARE HERE)
- **Purpose**: Index and reading guide for the entire analysis series
- **Contains**:
  - This overview
  - Document selection guide
  - Reading order recommendations
  - Key numbers quick lookup
  - FAQ

## Reading Recommendations by Role

### "I'm the architect deciding if we should implement this"
1. Read **TRANSMITTANCE-LUT-ANALYSIS-SUMMARY.md** sections:
   - "Key Findings"
   - "Porting to UzEngine"
   - "Risk Assessment"
   - "Timeline Estimate"
2. Time: 15 minutes
3. Decision: Based on effort (17 hours) and confidence level

### "I'm implementing this in HLSL"
1. Read **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** (cheat sheet)
2. Reference **ue5-transmittance-lut-hlsl-snippets.md** (copy code)
3. Read **ue5-transmittance-lut-porting.md** step 2 (compute shader)
4. Follow **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** validation tests
5. Time: 4-6 hours

### "I'm implementing this in C++/DX12"
1. Read **ue5-transmittance-lut-porting.md** steps 1-5
2. Reference **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** DX12 checklist
3. Read **ue5-sky-atmosphere-transmittance-lut.md** section "Dispatch Configuration"
4. Follow **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** validation tests
5. Time: 6-8 hours

### "I need to understand the math"
1. Read **ue5-sky-atmosphere-transmittance-lut.md** section "LUT Parameterization"
2. Read **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** "Core Equations"
3. Reference **ue5-transmittance-lut-hlsl-snippets.md** "Complete UV Mapping Functions"
4. Time: 1-2 hours

### "I'm reviewing someone's implementation"
1. Read **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** sections:
   - "DX12 Integration Checklist"
   - "Expected Output"
   - "Common Mistakes"
2. Reference **ue5-transmittance-lut-porting.md** for design decisions
3. Check against **ue5-sky-atmosphere-transmittance-lut.md** spec
4. Time: 30 minutes per code review

## Key Numbers (Quick Lookup)

| Aspect | Value |
|--------|-------|
| **LUT Resolution** | 256 × 64 pixels |
| **Texture Format** | R32G32B32_FLOAT (or RGBA32F on mobile) |
| **Memory per LUT** | ~192 KB |
| **Thread Group Size** | 8 × 8 |
| **Dispatch Grid** | 32 × 8 groups |
| **Ray Samples per Pixel** | 10 (configurable) |
| **Precomputation Time** | 10–50 ms |
| **Runtime Lookup Cost** | ~2 GPU cycles |
| **Precomputation Frequency** | Once per scene |
| **Paper Reference** | Bruneton et al., SIGGRAPH 2017 |

## Document Dependency Tree

```
TRANSMITTANCE-LUT-INDEX.md (you are here)
├─ ue5-sky-atmosphere-transmittance-lut.md (detailed reference)
│  └─ Explains why everything works
├─ ue5-transmittance-lut-hlsl-snippets.md (code reference)
│  └─ Extract code from main reference
├─ ue5-transmittance-lut-porting.md (implementation guide)
│  └─ Depends on snippets + sky renderer understanding
├─ TRANSMITTANCE-LUT-ANALYSIS-SUMMARY.md (executive summary)
│  └─ Synthesizes main reference
└─ TRANSMITTANCE-LUT-QUICK-REFERENCE.md (cheat sheet)
   └─ Summarizes main reference
```

## FAQ

**Q: Which document should I start with?**
A: Depends on your role (see "Reading Recommendations by Role" above). Most engineers should start with the Quick Reference, then the porting guide.

**Q: Can I just copy-paste the HLSL and it will work?**
A: Almost. The UV mapping functions and compute shader kernel can be copied directly. You'll need to adapt the ray-march integration and DX12 state management to UzEngine's architecture.

**Q: How long will this take to implement?**
A: 17 hours estimated (range 12–24 hours depending on familiarity with DX12 and atmospheric models). Breakdown: 2h HLSL, 4h ray-march, 3h DX12 setup, 2h dispatch, 2h integration, 4h testing.

**Q: What's the performance impact?**
A: Precomputation is one-time (~20 ms). Runtime cost per lookup is negligible (~2 cycles = 0.1 ns on modern GPU). Texture memory is ~200 KB. No per-frame cost.

**Q: Do I need to understand the Bruneton paper?**
A: Not required for implementation. The analysis documents explain the parameterization enough to port it. The paper is useful if you want to extend the system later.

**Q: Can I use a different LUT resolution?**
A: Yes. 256×64 is UE5's default. You can use 128×32 (mobile), 512×128 (high-quality), etc. See **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** "Resolution trade-off".

**Q: What if my atmosphere model is different from UE5's?**
A: The UV mapping and precomputation framework is generic. You only need to adapt the ray-marching integration (how you compute optical depth). The LUT parameterization and dispatch are reusable.

**Q: How do I validate the implementation?**
A: See **TRANSMITTANCE-LUT-ANALYSIS-SUMMARY.md** "Validation Checklist" and **TRANSMITTANCE-LUT-QUICK-REFERENCE.md** "Validation Tests".

## File Locations

All documents are in the UzEngine repository:

```
e:\UzEngine\docs\analysis\rendering\
├─ ue5-sky-atmosphere-transmittance-lut.md (main reference)
├─ ue5-transmittance-lut-hlsl-snippets.md (code reference)
├─ TRANSMITTANCE-LUT-ANALYSIS-SUMMARY.md (executive summary)
├─ TRANSMITTANCE-LUT-QUICK-REFERENCE.md (cheat sheet)
└─ TRANSMITTANCE-LUT-INDEX.md (this file)

e:\UzEngine\docs\guides\
└─ ue5-transmittance-lut-porting.md (implementation guide)
```

## Source References (UE5 5.6)

- **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf**
  - Lines 1099–1138: `RenderTransmittanceLutCS` compute shader
  - Lines 346–356: `GetTransmittance()` runtime lookup

- **W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush**
  - Lines 156–172: `fromTransmittanceLutUVs()` (decode)
  - Lines 175–192: `getTransmittanceLutUvs()` (encode)
  - Lines 233–254: `GetAtmosphereTransmittance()` integration

- **W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SkyAtmosphereRendering.cpp**
  - Lines 151–174: CVar definitions
  - Lines 1148–1164: Format selection
  - Lines 1364–1379: Compute dispatch

## Next Steps

1. **Select a document** from "Reading Recommendations by Role"
2. **Implement in phases** (HLSL → C++ → integration → testing)
3. **Use Quick Reference** as your daily checklist
4. **Reference main analysis** when questions arise
5. **Check validation tests** before marking done

---

**Last Updated**: 2026-03-15
**Status**: Complete analysis, ready for implementation
**Questions?** Refer to the appropriate document for your question type.

