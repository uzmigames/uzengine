---
name: VFX AI Pipeline Research
description: Feasibility analysis of AI-automated VFX generation using Claude (shaders), ComfyUI+SDXL (textures), and TRELLIS.2 (3D meshes)
type: project
---

# VFX AI Pipeline — Research Report (2026-03-12)

## Executive Summary

AI-automated VFX creation is **FEASIBLE** with the proposed stack, but one component must change:
- **Claude Opus/Sonnet for shaders**: HIGHLY FEASIBLE — reliable HLSL generation, ~$0.01-0.10/effect
- **"Nano Banana" for textures**: DOES NOT EXIST as standalone model — it's Google Gemini Flash codename, cloud-only
- **Replacement: ComfyUI + SDXL**: FEASIBLE — local, open source, 10-12GB VRAM, generates VFX textures
- **TRELLIS.2 for 3D meshes**: FEASIBLE for offline/editor use — MIT license, 24GB+ VRAM, ~17s/mesh

## Component Analysis

### 1. Claude (Shaders/Materials/HLSL) — RECOMMENDED

**Strengths:**
- Generates particle shaders, PBR materials, post-process effects, flipbook animation shaders
- DXC compilation validates output immediately (tight feedback loop)
- Content-hash caching eliminates repeated API costs
- Natural language → VFX parameters → HLSL code mapping works well
- Cost: ~$0.01/effect (Sonnet), ~$0.05-0.10/effect (Opus for complex shaders)

**Architecture:**
- Prompt includes shader style guide + existing patterns + VFX parameters
- Generate → DXC compile → SPIRV-Reflect bindings → render preview → validate
- If compilation fails, feed error back to Claude (max 2 retries)
- Ship with pre-compiled cached shaders, Claude API only in dev builds

**Coverage:**
- Tier 1 (90%): Unlit billboard sprite, ~20 lines HLSL
- Tier 2 (8%): Additive glow, flipbook, ~50 lines HLSL
- Tier 3 (2%): Distortion, depth effects, ~100+ lines HLSL

### 2. Texture Generation — ComfyUI + SDXL (replaces "Nano Banana")

**Why not Nano Banana:**
- "Nano Banana" is Google DeepMind's codename for Gemini Flash image model
- Cloud-only, proprietary, not game-optimized, no local inference
- NOT suitable for a local game engine pipeline

**Recommended alternative: ComfyUI + Stable Diffusion XL**
- Open source, runs locally on RTX 3080+ (10-12GB VRAM)
- ControlNet for fine-grained control (depth maps, edge guides)
- Seamless tiling support via extensions
- Transparent PNG output for sprites
- Can generate fire, smoke, energy, spark textures
- REST API for engine integration
- Cache results by prompt hash (content-addressable)

**Other options considered:**
- Flux.1: Better quality but 32GB+ VRAM — not practical
- Scenario: Game-trained, PBR materials — but cloud/paid
- Material Maker: Procedural, open source — complementary tool

### 3. TRELLIS.2 (3D Mesh Generation) — FEASIBLE WITH CONSTRAINTS

**Strengths:**
- MIT license, fully open source
- Excellent PBR output (base color, roughness, metallic, opacity)
- Handles complex topology (holes, thin structures)
- GLB output format (UzEngine already uses glTF pipeline)

**Constraints:**
- **24GB+ VRAM minimum** (RTX 4090 or H100 required)
- **NOT for consumer GPUs** (RTX 3070/4070 cannot run it)
- ~17s per mesh at 1024³ — offline/editor only, not real-time
- Output is high-poly (100k-500k tris) — needs decimation for VFX
- Image-to-3D only (text requires intermediate image generation)

**Mitigation:**
- Run as local service on RTX 4090 machine or cloud (RunPod/Paperspace)
- Batch-generate VFX mesh library offline, cache as GLB
- Post-process: Blender scripting for decimation + cleanup
- Alternative for lower-end: InstantMesh (lower VRAM, geometry-only)

### 4. VFX Particle Systems — 85-95% Coverage

**What particles + sprites + simple meshes can handle:**
- Fire, smoke, explosions, magic spells, hit effects, trails, ambient particles, decals
- 98% of typical game VFX uses Tier 1-2 shaders

**What requires advanced techniques (5-15%):**
- Fluid simulation, volumetric clouds, advanced destruction
- These are Phase 2+ features

**JSON `.uzvfx` format is ideal for LLM generation:**
- Human-readable, version-controllable
- Schema-validatable
- LLM can generate complete effect definitions as structured JSON

## Revised Pipeline Architecture

```
User describes VFX effect (natural language or template)
    ↓
┌─────────────────────────────────────────────────┐
│ VFX AI Orchestrator                              │
│                                                  │
│  1. Parse description → VFX parameters (JSON)   │
│  2. Route to generation services:               │
│     ├─ Claude API → HLSL shaders + materials    │
│     ├─ ComfyUI/SDXL → particle textures/sprites│
│     └─ TRELLIS.2 → 3D mesh assets (if needed)  │
│  3. Validate all outputs:                       │
│     ├─ DXC compile shaders                      │
│     ├─ Import textures to engine format         │
│     └─ Decimate + import meshes                 │
│  4. Assemble .uzvfx effect definition          │
│  5. Hot-reload into viewport for preview        │
└─────────────────────────────────────────────────┘
    ↓
Live preview in editor viewport
    ↓
User iterates ("more fire", "bigger particles", "add smoke layer")
    ↓
Refinement loop (only regenerates changed components)
```

## Cost Estimates

| Component | Per-Effect Cost | Hardware Required |
|-----------|----------------|-------------------|
| Claude (shaders) | $0.01-0.10 | None (API) |
| ComfyUI+SDXL (textures) | $0 (local) | RTX 3080+ (10GB VRAM) |
| TRELLIS.2 (meshes) | $0 local / $0.01 cloud | RTX 4090 (24GB) or cloud |
| **Total per effect** | **$0.01-0.11** | — |
| **100 VFX effects** | **$1-11** | — |

## Implementation Phases

1. **Phase 1 (MVP)**: Claude generates `.uzvfx` JSON + HLSL particle shaders. Manual textures.
2. **Phase 2**: ComfyUI integration for texture generation. Automated compile+preview loop.
3. **Phase 3**: TRELLIS.2 for mesh VFX. Multi-emitter composition. Refinement UI.
4. **Phase 4**: Full natural language → complete VFX effect. Style transfer. Effect library.

## Key Decision: Replace "Nano Banana" with ComfyUI + SDXL

The user's original proposal included "Nano Banana" for textures. Research revealed this is NOT a real standalone model — it's Google's marketing name for Gemini Flash. The recommended replacement is **ComfyUI + SDXL** which is:
- Fully local and open source
- Runs on consumer GPUs (10-12GB VRAM)
- Has REST API for integration
- Can generate game-quality VFX textures
- Community has extensive VFX-specific workflows
