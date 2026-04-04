<!-- doc-meta
title: UE5 Rendering Analysis — Documentation Index
author: UzEngine Architecture Team
date: 2026-04-04
-->

# UE5 Rendering Analysis — Complete CVar Reference & Implementation Guide

This directory contains comprehensive documentation extracted from UE5 5.6 source code for implementing a fully runtime-configurable rendering system in UzEngine.

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

## Next Steps

1. Read ue5-rendering-cvars-reference.md — understand available CVars
2. Read uzengine-cvar-implementation-guide.md — learn the pattern
3. Read cvar-architecture-antipatterns.md — know what to avoid
4. Pick a feature and follow all 7 steps from the guide
5. Test at runtime — change CVar, verify result next frame
