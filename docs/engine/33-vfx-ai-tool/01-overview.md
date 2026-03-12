# VFX AI Tool — Fully Automated AI-Driven VFX Creation Pipeline

> **Status**: Planned (Phase 3–4)
> **Source**: `editor/vfx-ai-tool/`, VFX orchestrator runtime layer
> **References**: UE5 Niagara (node graph reference), Anthropic API (Claude), ComfyUI (texture generation), TRELLIS.2 (mesh generation)

## Purpose

The VFX AI Tool is the flagship differentiator of UzEngine — a fully automated AI-driven pipeline that generates complete visual effects from natural language descriptions. Users describe what they want ("bright blue energy ball explosion") and the system generates everything:

- **HLSL Shaders** via Claude Opus/Sonnet (shader code + material parameters)
- **Particle Textures** via ComfyUI + Stable Diffusion XL (locally, no cloud dependencies)
- **3D Meshes** via TRELLIS.2 (for mesh-particle effects, optional, requires RTX 4090)
- **.uzvfx JSON Definitions** (the effect graph — spawn rates, curves, physics, rendering)

This enables artists and game designers with **zero shader knowledge** to create production-quality VFX in minutes instead of hours. Cost: **$0.01–0.11 per effect** (API + local GPU). Coverage: **85–95% of typical game VFX achievable**.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Input Layer                         │
│  (VSCode Extension: "VFX AI Tool" panel or command palette)     │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│              VFX AI Orchestrator (TypeScript + C++)              │
│  ├── Input parsing (natural language → structured parameters)   │
│  ├── Service routing (decide what to generate)                  │
│  ├── Parallel dispatch (shaders + textures + meshes)            │
│  └── Assembly + validation (compile → import → validate schema) │
└────────────────────────┬────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
    ┌─────────┐    ┌──────────┐    ┌──────────┐
    │  Claude │    │ ComfyUI  │    │TRELLIS.2 │
    │ HLSL    │    │Textures  │    │ Meshes   │
    │Generator│    │Generator │    │Generator │
    └────┬────┘    └────┬─────┘    └────┬─────┘
         │              │               │
         └──────────────┼───────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────────────┐
│             Asset Assembly & Cache Layer                        │
│  ├── Shader compilation (DXC → SPIR-V → cache)                 │
│  ├── Texture import (.png → .uztexture, hashed cache)           │
│  ├── Mesh import (.glb → .uzmesh, decimation + cleanup)         │
│  └── .uzvfx JSON assembly (combines all components)             │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│               Viewport Preview & Iteration                      │
│  ├── Hot-load .uzvfx into viewport                              │
│  ├── Render effect at origin with play/stop controls            │
│  ├── User feedback ("more fire", "bigger", "add smoke")         │
│  └── Regenerate only changed components (iterative refinement)  │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Save to Project (Phase 3+)                     │
│  ├── Effect stored as standard .uzvfx + textures + shaders      │
│  ├── Fully editable manually via inspector                      │
│  └── Can be remixed/combined into multi-emitter compositions    │
└─────────────────────────────────────────────────────────────────┘
```

## The 3 AI Pillars

### Pillar 1: Claude HLSL Shader Generation

**What it does**: Claude Opus/Sonnet receives a prompt like "bright blue energy ball explosion" and generates valid HLSL pixel/compute shaders.

**How**:
- Curated prompt engineering: shader style guide + existing patterns + VFX parameters
- Output validated by DXC compiler (errors fed back to Claude for retry)
- Auto-detects shader tier (simple billboard vs. complex distortion)
- Tier 1 (90%): simple billboard shaders, ~$0.01/effect
- Tier 2 (8%): glow, flipbook animation, ~$0.05/effect
- Tier 3 (2%): distortion, complex effects, ~$0.10/effect (Opus)

**Cost**: Sonnet ~$0.001–0.01 per shader, Opus ~$0.01–0.10 (larger effects).

**Shipping**: All generated shaders pre-compiled and cached in `.uzpak`. Claude API only active in dev builds (`#if UZ_SHADER_DEV`).

### Pillar 2: ComfyUI + SDXL Texture Generation

**What it does**: Stable Diffusion XL runs locally in ComfyUI (Python service) and generates particle textures: fire, smoke, sparks, energy, sprites.

**Why ComfyUI + SDXL** (not "Nano Banana"):
- Nano Banana was misidentified branding for Google Gemini Flash (cloud-only, proprietary)
- ComfyUI + SDXL runs 100% locally, no cloud, no API keys, full control
- Supports seamless tiling, ControlNet for guided generation, fast iteration
- RTX 3080+ sufficient (10–12 GB VRAM)

**How**:
- Engine sends prompt + control image → ComfyUI REST API
- SDXL generates image in ~2–5 seconds
- Result returned as PNG, imported as `.uztexture`
- Cached by prompt hash (same particles always reuse texture)

**Quality**: SDXL textures comparable to hand-painted (when prompted well). Tool includes common presets: "fire", "smoke", "electric", "holy", "water", "ice".

**Parallel generation**: Multiple textures batch-generated during asset cooking.

### Pillar 3: TRELLIS.2 3D Mesh Generation

**What it does**: Microsoft's TRELLIS.2 (MIT license, open-source) generates 3D meshes from reference images.

**When to use**: For unique mesh-based VFX (debris, crystals, projectiles, environmental meshes in effects).

**Constraints**:
- GPU: RTX 4090 minimum (24 GB+ VRAM), ~17s per mesh at 1024³
- Offline/editor-only (not real-time)
- Requires reference image (concept art or AI-generated)

**How**:
- User provides reference image OR Claude generates one first
- TRELLIS.2 generates GLB with PBR textures
- Post-process: mesh simplification (0.80–0.85 ratio), remove internals
- Result imported as `.uzmesh`

**Alternatives**: InstantMesh (lower VRAM), Hunyuan 3D (if available). Fallback: use primitive shapes from engine library.

**Deployment options**:
- Local RTX 4090 service (fastest, control)
- Cloud: RunPod/Paperspace (~$0.60/hr, scales)

## Coverage: What VFX Can Be Generated

The system covers 85–95% of typical game VFX:

| Category | Examples | Achievable | Notes |
|----------|----------|-----------|-------|
| **Fire/Heat** | Campfire, explosion, muzzle flash, lava | 100% | Particles + sprites, easy prompt engineering |
| **Smoke/Gas** | Smoke clouds, fog, dust, steam | 100% | Texture variety via SDXL, curves via Claude |
| **Energy** | Magic attacks, electricity, force field | 95% | Shader complexity for distortion/glow |
| **Water** | Splash, rain, waterfalls | 90% | Simpler via particles, complex sims need hand-tuning |
| **Light Effects** | Halos, lens flares, bloom | 98% | Shader + material parameters |
| **Trails/Beams** | Sword slash, laser beam, jetpack | 95% | Uses dedicated trail system + shader |
| **Decals** | Bullet holes, scorch marks, blood | 100% | Sprites + decal system |
| **Particles (mesh-based)** | Debris, shrapnel, destruction | 80% | TRELLIS.2 for unique meshes, otherwise spheres/cubes |
| **Ambient (looped)** | Ambient glow, environmental, looped fire | 95% | JSON parameter tuning for seamless loops |

**Out of scope** (hand-authored or procedural plugins):
- Fluid sims (water, cloth) — too expensive for real-time
- Skeletal mesh deformation — animation system (separate)
- Complex physics interactions (softbodies) — requires Jolt extensions

## VFX Categories & Templates

Pre-built templates for common effect types (user can customize):

| Template | Description | Emitters | Estimated Cost |
|----------|-------------|----------|-----------------|
| **Fire Ball** | Orange/yellow fire, small explosion | 2 (main + embers) | $0.02 |
| **Explosion** | Shockwave + smoke + fire + sparks | 4 | $0.08 |
| **Magic Attack** | Glow + distortion + particles | 2 | $0.05 |
| **Smoke Cloud** | Large diffuse smoke plume | 1 | $0.01 |
| **Electric Beam** | Crackling energy + glow | 2 | $0.04 |
| **Holy Light** | Ascending particles + lens flare | 2 | $0.03 |
| **Water Splash** | Simple splash + droplets | 2 | $0.02 |
| **Custom** | User-described effect | 1–4 | $0.01–0.11 |

**Cost breakdown**: API calls (Claude $0.001–$0.01) + GPU (local, ~$0.001 per texture if counted).

## User Workflow — High-Level Steps

1. **Open VFX AI Tool** — VSCode sidebar panel or command palette (`VFX AI: Create Effect`)
2. **Describe or Select** — Type natural language or choose template
3. **Configure** — (Optional) Set emitter count, intensity, duration
4. **Generate** — System routes to Claude/ComfyUI/TRELLIS.2, shows progress
5. **Preview** — Effect renders in viewport, user plays/stops/loops
6. **Iterate** — User types feedback ("more glow", "bigger", "add smoke")
7. **System regenerates** — Only changed components (fast iteration)
8. **Save** — Effect stored as `.uzvfx` + supporting assets in project

**Advanced**:
- **Multi-emitter composition** — "fire + smoke + sparks" auto-creates 3 emitters with parameters
- **Gallery** — Browse previously generated effects, modify/remix
- **Export** — Effects are standard `.uzvfx` + textures + shaders — fully editable by hand

## Comparison Table

| Feature | Unreal 5 | Godot 4 | Unity VFX Graph | **UzEngine VFX AI** |
|---------|----------|---------|-----------------|-------------------|
| **Authoring** | Niagara node graph (visual) | Inspector params (manual) | Node graph (visual) | **Natural language (AI)** |
| **Creation time** | 30–120 min / effect | 20–60 min / effect | 40–180 min / effect | **2–5 min / effect (AI)** |
| **Required skill** | VFX artist expertise | Intermediate | Advanced | **None (AI does it)** |
| **Shader generation** | Manual HLSL | Manual GLSL | Manual HLSL | **Claude auto-generates** |
| **Texture creation** | Purchased/hand-authored | Purchased/hand-authored | Procedural | **SDXL auto-generates** |
| **Mesh creation** | Modeled/sculpted | Modeled/sculpted | N/A | **TRELLIS.2 auto-generates** |
| **Cost per effect** | $50–200 (artist time) | $30–100 (artist time) | $40–150 (artist time) | **$0.01–0.11 (API)** |
| **GPU requirement** | RTX 2080+ | RTX 2080+ | RTX 2080+ | **RTX 3080+ (SDXL), 4090 optional (TRELLIS.2)** |
| **Real-time iteration** | Yes (visual editor) | Limited | Yes (visual editor) | **Yes (natural language)** |
| **Template library** | 50+ built-in | 20+ built-in | 30+ built-in | **50+ templates** |
| **Comparison** | Industry standard, expensive, requires expertise | Community-driven, limited features | Proprietary, requires subscription | **AI-first, cheap, accessible** |

## Implementation Phases

| Phase | What Ships | Timeline |
|-------|-----------|----------|
| **Phase 3** | Claude HLSL shader generator (Tier 1–2), basic `.uzvfx` assembly, preview + iteration loop, 10 templates | Q2 2026 |
| **Phase 3.5** | ComfyUI + SDXL texture generation (locally), batch asset cooking, gallery + remix | Q3 2026 |
| **Phase 4** | TRELLIS.2 mesh generation (optional, requires RTX 4090), advanced templates, multi-emitter composition | Q4 2026 |
| **Phase 5+** | Flux.1 texture generation (if available), motion capture for trails, cloud backend option | 2027+ |

## Benefits for AI-Driven Development

VFX AI Tool is core to UzEngine's vision of "first engine for autonomous AI development":

- **LLM-native workflow**: Describe effects in natural language, Claude handles details
- **Zero artist bottleneck**: VFX no longer requires hiring specialist artists
- **Iteration speed**: Change effect description → 2–5 sec regeneration vs. manual re-authoring
- **Cost-effective**: $0.01–0.11 per effect vs. $50–200 artist hours
- **Accessible**: Game designers without VFX knowledge can create polish
- **Testable**: Generated effects are standard JSON/shaders — can be version-controlled and tested in CI

## Related Documentation

- `docs/engine/25-vfx/` — VFX System (GPU particles, emitters, modules)
- `docs/engine/05-shader-pipeline/` — Shader compilation and caching
- `docs/engine/28-ipc-protocol/` — Extension-to-runtime IPC for effect generation
- `docs/engine/08-resource-system/` — Asset import and `.uz*` formats
- `docs/engine/40-editor-framework/` — VSCode editor framework (panel hosting, layout)
