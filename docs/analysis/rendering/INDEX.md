<!-- doc-meta
title: Rendering Architecture Analysis Index
category: Rendering
tags: [Architecture, RHI, Shaders, Analysis]
---

# Rendering Architecture Analysis Index

This directory contains deep architectural analyses of modern rendering systems, with focus on UE5 patterns and UzEngine adaptations.

## Core Analysis Documents

### 1. UE5 Uniform Buffer & Shader Binding System (PRIMARY REFERENCE)
**File:** `ue5-uniform-buffer-shader-binding-system.md` (900 lines)

**Summary:** Complete analysis of how UE5 handles shader parameter binding — the critical insight being that textures/samplers are NOT packed into constant buffers, but rather stored as pointers in the C++ struct and bound separately via metadata-driven RHI calls.

**Key Sections:**
- Pattern 1: The C++ Struct Illusion
- Pattern 2: How Resources Bind Separately on DX11 vs DX12
- Pattern 3: The HLSL Side — Texture Inside cbuffer Syntax
- Pattern 4: Metadata-Driven Binding
- Pattern 5: Actual Source Files & Key Functions
- Complete answers to all 4 original questions
- UzEngine adaptation strategy (4 phases)

**Key Insight:**
- Textures go into the C++ struct as POINTERS (8 bytes), NOT GPU buffer
- Metadata tagged with `bIsStoredInConstantBuffer=false` prevents CB packing
- SetShaderParameters() reads metadata to separate CB data from resources
- Same C++ code works on DX11 and DX12 via metadata abstraction

**Start here for understanding the ARCHITECTURE and WHY.**

---

### 2. Shader Parameter Binding Implementation Guide
**File:** `docs/guides/shader-parameter-binding-guide.md` (700 lines)

**Summary:** Step-by-step implementation guide for bringing UE5's pattern to UzEngine, with complete C++ code and examples.

**Key Sections:**
- Step 1: Define Metadata Structure
- Step 2: Macro System for Struct Definition
- Step 3: Parameter Binding Implementation
- Step 4: HLSL Code Generation
- Step 5: Usage Example
- DX11 vs DX12 Implementation Differences
- Testing Examples
- Migration Checklist

**Contains:**
- Complete `ParameterStructMetadata` struct
- Full macro implementation
- `SetShaderParameters()` template code
- HLSL generation code
- Google Test examples
- Performance analysis

**Start here for IMPLEMENTATION CODE.**

---

### 3. Before & After: Code Comparison
**File:** `shader-parameters-before-after.md` (600 lines)

**Summary:** Side-by-side comparison of current manual approach vs. UE5 metadata-driven approach, with concrete scenarios.

**Key Sections:**
- The Problem: Manual Parameter Binding (current UzEngine)
- The Solution: Metadata-Driven Binding (UE5 pattern)
- Scenario 1: Shader Hot-Reload
- Scenario 2: Adding a New Sampler
- Scenario 3: Struct Alignment (The Footgun)
- Code Size Comparison (~200 → ~50 lines per pass)
- DX11/DX12 Comparison (platform forks eliminated)
- Summary Table: Feature comparison

**Start here for MOTIVATION and BUSINESS CASE.**

---

## Quick Reference: Original Questions Answered

### Q1: How are textures packed into the uniform buffer?
**Answer:** They aren't. C++ struct stores **pointers** (8 bytes each). Metadata marks them with `bIsStoredInConstantBuffer=false`. GPU never sees the pointers — only actual texture data via SRV binding.

**Location:** `ue5-uniform-buffer-shader-binding-system.md` → Pattern 1 & Pattern 4

### Q2: How does HLSL access struct members?
**Answer:** TextureCube inside `cbuffer` is legal HLSL syntax but syntactic sugar. Compiler produces:
- CB register (b0) with scalar/vector/matrix data only
- SRV slots (t0, t1, ...) assigned by shader reflection
- SetShaderParameters() maps C++ pointers to descriptor slots

**Location:** `ue5-uniform-buffer-shader-binding-system.md` → Pattern 3

### Q3: What's the DX11 implementation?
**Answer:**
- CB data → RHICreateUniformBuffer() → PSSetConstantBuffers()
- Resource pointers → PSSetShaderResources() (slots from reflection)
- Sampler pointers → PSSetSamplers()

**Location:** `ue5-uniform-buffer-shader-binding-system.md` → Pattern 2 (DX11 subsection)

### Q4: What's the DX12 implementation?
**Answer:**
- CB data → UploadBuffer → SetGraphicsRootConstantBufferView(0, ...)
- Resources → Descriptor heap → SetGraphicsRootDescriptorTable(1, ...)
- Slot assignments pre-computed from metadata

**Location:** `ue5-uniform-buffer-shader-binding-system.md` → Pattern 2 (DX12 subsection)

---

## The Core Pattern (Visual)

```
┌─────────────────────────────────────────────────────────────┐
│                    C++ Struct (Single Definition)           │
├─────────────────────────────────────────────────────────────┤
│ SHADER_PARAMETER(float, Roughness)       ← CB data          │
│ SHADER_PARAMETER(float4, BaseColor)      ← CB data          │
│ SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)  ← SRV ptr  │
│ SHADER_PARAMETER_SAMPLER(SamplerState, LinearSamp) ← ptr   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│       Metadata (Separates Intent from Mechanism)             │
├──────────────────────────────────────────────────────────────┤
│ cb_members:       [Roughness (offset 0, size 4)            │
│                    BaseColor (offset 16, size 16)]          │
│ resource_members: [DiffuseMap (offset 32),                  │
│                    LinearSampler (offset 40)]               │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│         SetShaderParameters() (Platform Agnostic)            │
├──────────────────────────────────────────────────────────────┤
│ 1. Extract CB data (offsets 0-32)                            │
│ 2. Upload to GPU constant buffer                             │
│ 3. Extract resource pointers (offsets 32-48)                 │
│ 4. Bind via RHI backend                                      │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│      RHI Backend (API-Specific Implementation)               │
├──────────────────────────────────────────────────────────────┤
│ DX12:  SetGraphicsRootConstantBufferView(0, cb)             │
│        SetGraphicsRootDescriptorTable(1, heap)              │
│                                                              │
│ DX11:  PSSetConstantBuffers(0, 1, &cb)                     │
│        PSSetShaderResources(0, 2, &srvs)                    │
│        PSSetSamplers(0, 1, &samplers)                       │
└──────────────────────────────────────────────────────────────┘
```

**The magic:** Metadata lets the **same C++ code** work on both DX11 and DX12.

---

## UzEngine Implementation Plan (4 Phases)

| Phase | Task | Duration | Complexity | Blocker? |
|-------|------|----------|-----------|----------|
| 1 | Metadata struct + member collection | 1-2 days | Low | No |
| 2 | Macro system (BEGIN/END/PARAMETER) | 2-3 days | Medium | No |
| 3 | SetShaderParameters() + DX12 binding | 3-5 days | High | No |
| 4 | HLSL generation + DX11 binding (opt) | 1-2 days | Medium | No |
| Test | Integration testing + hot-reload | 2-3 days | Medium | Yes |
| **Total** | | **~2 weeks** | | |

**Recommended Timeline:**
1. Phase 1 + 2 → Proof of concept (1 week)
2. Phase 3 → DX12 binding (1 week)
3. Testing + Phase 4 → Production ready (1 week)

---

## Benefits vs. Effort

| Metric | Value |
|--------|-------|
| Implementation time | ~2 weeks |
| Code reduction | 2-4x per render pass |
| Manual errors eliminated | 100% (padding, slot management) |
| Shader hot-reload safety | Safe (metadata-driven) |
| Platform transparency | DX11/DX12 unified |
| Type safety | Compile-time checks |
| Boilerplate reduction | ~200 → ~50 lines/pass |
| **ROI** | **EXTREMELY HIGH** |

---

## Reading Guide by Role

### For Architects/Decision Makers
1. Read: `shader-parameters-before-after.md` — see the cost-benefit
2. Skim: `ue5-uniform-buffer-shader-binding-system.md` → Pattern 1 (intro) + UzEngine Adaptation Strategy
3. Check: Effort estimate (Phase 1-4 timeline)

**Time:** 30 minutes

### For Engineers Implementing Phase 1-2 (Metadata + Macros)
1. Read: `ue5-uniform-buffer-shader-binding-system.md` → Pattern 1, 4, 5
2. Read: `docs/guides/shader-parameter-binding-guide.md` → Steps 1-2, Testing
3. Start implementation with Step 1-2 code as template

**Time:** 1-2 hours

### For Engineers Implementing Phase 3 (DX12 Binding)
1. Read: `ue5-uniform-buffer-shader-binding-system.md` → Pattern 2 (DX12 subsection)
2. Read: `docs/guides/shader-parameter-binding-guide.md` → Step 3, DX12 Implementation Differences
3. Reference: UE5 source `ShaderParameterStruct.cpp` (lines 247-294)

**Time:** 2-3 hours

### For Engineers Porting Shaders
1. Read: `docs/guides/shader-parameter-binding-guide.md` → Step 5 (Usage Example)
2. Use macro templates to define parameter structs
3. Replace manual RHI calls with `SetShaderParameters()`

**Time:** 1 hour per shader (first time), 10 mins (subsequent)

---

## Related Documentation

### UzEngine Architecture
- `docs/specs/shader-system.md` — Shader compilation & hot-reload
- `docs/specs/renderer.md` — Render passes & infrastructure
- `docs/architecture/rhi-abstraction.md` — RHI layer design
- `docs/architecture/render-pipeline.md` — Render graph & pass execution

### UE5 Source Code
**All files at:** `W:\UE_5.6_Source\Engine\Source\Runtime\`

Primary:
- `RenderCore/Public/ShaderParameterMacros.h` (1700+ lines)
- `RenderCore/Public/ShaderParameterStruct.h` (361 lines)
- `RenderCore/Public/ShaderParameters.h` (271 lines)
- `RenderCore/Public/ShaderParameterMetadata.h`
- `RHI/Public/RHIShaderParameters.h` (444 lines)
- `RenderCore/Public/GlobalShader.h` (454 lines)

See full list in: `ue5-uniform-buffer-shader-binding-system.md` → Related UE5 Files section

---

## Checklist for Implementation Approval

- [ ] **Decision Made:** Implement metadata-driven parameter binding?
- [ ] **Timeline:** Allocated 2 weeks of engineering time?
- [ ] **Phase 1 Owner:** Assigned engineer for metadata system?
- [ ] **Phase 2 Owner:** Assigned engineer for macro system?
- [ ] **Phase 3 Owner:** Assigned engineer for DX12 binding?
- [ ] **Phase 4 Owner:** (Optional) Assigned engineer for HLSL generation?
- [ ] **Testing Plan:** Defined test strategy (unit + integration)?
- [ ] **Shader Migration:** Planned shader porting schedule?
- [ ] **Documentation:** Allocated time for migration guide + examples?

---

**Analysis Date:** March 16, 2026
**UE5 Version Analyzed:** 5.6 Source
**Status:** Analysis complete, ready for implementation planning

**Last Updated:** 2026-03-16
