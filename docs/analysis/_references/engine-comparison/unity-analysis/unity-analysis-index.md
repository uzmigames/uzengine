# Unity Application/Engine Abstraction — Analysis Index

**Date**: 2026-03-14  
**Analyst**: Claude Code (Haiku 4.5)  
**Scope**: How Unity hides GPU/rendering boilerplate from game developers  
**Target Audience**: UzEngine architects, render engineers, game developers

## Three-Document Suite

This analysis consists of three complementary documents, each addressing different audiences and use cases:

### 1. **unity-application-abstraction.md** (20+ pages)
**For**: Architects, render engineers, technical decision-makers  
**Purpose**: Deep architectural analysis of Unity's five-layer abstraction model  
**Key Sections**:
- How PlayerLoop subsystem registration works (no manual scheduling)
- How global settings hide implementation details (Application.targetFrameRate, QualitySettings)
- How Camera.Render() abstracts deferred vs forward rendering
- How RenderPipelineAsset enables runtime pipeline swaps
- Two-level rendering abstraction (declarative vs procedural)
- SceneManager async loading with progress tracking
- UzEngine translation for each pattern
- Honest UE5 comparison (what's better/worse)

**When to read**: Before implementing any Application/Engine subsystem  
**Time**: 30-45 minutes

---

### 2. **UNITY-PATTERNS-SUMMARY.md** (10 pages)
**For**: Everyone (quick reference format)  
**Purpose**: Visual summary of the five-layer model with examples  
**Key Sections**:
- ASCII diagram of all five layers (game code → GPU)
- FAQ: "How does X work without Y knowledge?"
- What game code NEVER sees (device, command lists, texture upload, etc.)
- UzEngine correspondence table (what we have ✓ vs need ✗)
- Copy-paste examples (Unity → UzEngine translation)
- Implementation priority (8-12 days)
- Success metrics (how to validate we got it right)

**When to read**: Before starting Phase 1 implementation  
**Time**: 10-15 minutes

---

### 3. **unity-application-abstraction-uzengine-roadmap.md** (12 pages)
**For**: Engineers implementing the feature (game-code-facing abstraction)  
**Purpose**: Concrete implementation tasks with code sketches  
**Key Sections**:
- Current state: what UzEngine already has ✓ vs lacks ✗
- **Phase 1**: ApplicationSettings singleton (2-3 days)
  - Task 1.1: Create ApplicationSettings class
  - Task 1.2: Create ApplicationSettingsSubsystem
  - Task 1.3: Expose to Lua
- **Phase 2**: Pluggable render pipeline (3-4 days)
  - Task 2.1: Define IRenderPipeline interface
  - Task 2.2: Implement DeferredRenderPipeline
  - Task 2.3: Implement ForwardRenderPipeline
  - Task 2.4: Create RenderPipelineSubsystem
  - Task 2.5: Remove old SceneRenderer enum-based logic
  - Task 2.6: Update demos
- **Phase 3**: Async scene loading (2-3 days)
  - Task 3.1: AsyncSceneLoad struct
  - Task 3.2: SceneLoaderSubsystem
  - Task 3.3: Lua API
- **Phase 4**: Docs & tests (1-2 days)
- Integration checklist
- Effort estimate (8-12 days total)
- Success criteria

**When to use**: As a Rulebook task/sprint specification  
**Time**: 60-90 minutes to implement each phase

---

## Reading Recommendations

### Scenario 1: "I need to understand the big picture"
1. Start: **UNITY-PATTERNS-SUMMARY.md** (15 min)
2. Deep dive: **unity-application-abstraction.md** (45 min)
3. Result: You understand all five layers + how game code avoids GPU knowledge

### Scenario 2: "I need to implement this in UzEngine"
1. Start: **UNITY-PATTERNS-SUMMARY.md** (15 min)
2. Reference: **unity-application-abstraction-uzengine-roadmap.md** (90 min)
3. Code: Use roadmap tasks as Rulebook entries
4. Validate: Check success criteria against implementation

### Scenario 3: "I'm writing game code for UzEngine"
1. Read: **UNITY-PATTERNS-SUMMARY.md** (15 min)
2. Example translation section (5 min)
3. Result: You know what APIs to call, how to avoid GPU code

### Scenario 4: "I'm reviewing render code"
1. Check: **unity-application-abstraction.md** section on RenderPipelineAsset
2. Pattern: Should use `IRenderPipeline`, not if-statements on enum
3. Anti-pattern: Game code should never import `rhi.h`

---

## Key Takeaways (TL;DR)

### The Problem Unity Solves
Game developers should write **game logic only**. They should never:
- Create GPU devices
- Record command lists
- Upload textures/meshes manually
- Register render passes
- Manage shadows or G-Buffers
- Call barrier/state-transition code

### The Solution: Five Layers
1. **Game Code** — `Update()`, move camera, nothing about rendering
2. **Components** — `MeshRenderer`, `Camera`, `Light` (declared once, auto-scheduled)
3. **Subsystems** — Ordered execution via PlayerLoop (topologically sorted)
4. **Settings** — Global config via `Application.targetFrameRate`, `QualitySettings`
5. **Render Pipeline** — Pluggable via `RenderPipelineAsset` (swap deferred ↔ forward)

### How It Works
- Game code declares what to render (MeshRenderer component)
- Engine auto-schedules rendering (no registration calls)
- Settings control quality (no per-subsystem tweaks)
- Pipelines are swappable (no game code changes)
- Lua gets full parity (game designers use same APIs as C++ code)

### UzEngine Status
- **Have**: Subsystem registration, DemoApplication, TimeManager ✓
- **Need**: ApplicationSettings, IRenderPipeline, AsyncSceneLoad (8-12 days)

---

## How to Use These Docs in the Project

### For Architects
- Reference in design documents: "See `/docs/analysis/rendering/unity-application-abstraction.md`"
- Use when evaluating feature proposals: "Does this expose GPU details to game code?"

### For Render Engineers
- Implement based on roadmap: "I'm implementing Task 2.1 (IRenderPipeline interface)"
- Cross-check pattern: "This should NOT be an if-statement; use polymorphism"

### For Game Developers
- Bookmark UNITY-PATTERNS-SUMMARY.md
- Example translations (Unity → UzEngine code)
- Know what APIs to call vs avoid

### For PR Reviewers
- Game code importing `rhi.h`? → Anti-pattern, reference Summary
- Enum-based render path selection? → Should use IRenderPipeline
- Settings scattered across subsystems? → Should be ApplicationSettings

---

## Metadata

| Aspect | Value |
|--------|-------|
| **Analysis Date** | 2026-03-14 |
| **Analyst** | Claude Code (Haiku model) |
| **Unity Version Analyzed** | 6000.0+ (2023+) |
| **Reference Engines** | Unity, UE5.6, Godot 4 |
| **UzEngine Target** | C++23, DX12-first, LuaJIT scripting |
| **Total Documentation** | ~1400 lines across 3 files |
| **Implementation Effort** | 8-12 days (4 phases) |
| **Priority** | P1 (Phase 1-2) + P2 (Phase 3) |

---

## Links to Related Documentation

- **Engine Vision**: `docs/engine-vision.md`
- **Subsystem Architecture**: `runtime/engine/app/subsystem.h` (already correct)
- **Demo Template**: `runtime/demos/common/demo_application.h` (model for game code)
- **Rendering Specs**: `docs/specs/1.3-renderer.md`
- **Lua API Spec**: `docs/specs/lua-scripting.md`
- **Architecture Docs**: `docs/architecture/`

---

## Feedback & Updates

If you have questions or disagree with any pattern:
1. Check if there's a more recent version of this analysis
2. Review the reference engine source code (W:\UE_5.6_Source, E:\godot)
3. Create a Rulebook task to discuss with team

Last updated: 2026-03-14
