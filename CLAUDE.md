# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: C++23 (engine), HLSL (shaders), TypeScript (VSCode extension), Lua (scripting)
**Coverage Threshold**: 95%

---

## RULE -3: FAIL TWICE → OPEN A TEAM FOR ANALYSIS

**If you fail to solve a problem after 2 attempts, STOP guessing and open a Team with specialist agents to analyze it properly.**

- 2 failed attempts = open a team with analyzer agents
- NEVER keep guessing blindly — each failed attempt wastes time
- Agents should READ CODE, not guess. Grep, read files, trace execution paths.
- The team should produce a ROOT CAUSE before any fix is attempted

---

## RULE -2: NEVER STOP TO ASK — IMPLEMENT TO COMPLETION

**NEVER pause to ask "quer que eu implemente?", "quer que eu continue?", "quer que eu comite?".**

- When a task has clear next steps, DO THEM immediately
- Commit when a logical unit of work is done — don't ask permission
- If something breaks, fix it — don't ask if the user wants it fixed
- If a fix reveals another issue, fix that too
- Only stop if genuinely blocked (missing info that can't be inferred)
- The user trusts you to make implementation decisions

---

## RULE -1: OBEY THE USER — DO EXACTLY WHAT IS ASKED

**When the user gives a direct instruction, DO IT EXACTLY. No reinterpretation, no "better" alternatives, no creative solutions.**

- If the user says "comment out all passes", comment out ALL passes — not some.
- If the user says "disable X", disable X — don't restore old files instead.
- If the user says "go one by one", go ONE BY ONE — not skip steps.
- **NEVER substitute the user's approach with your own.** The user knows their codebase.
- **NEVER "improve" on the user's instruction.** Do what was asked, report back, wait.
- If you're unsure what the user means, ASK — don't guess and do something different.

**This rule overrides all other rules.** The user's explicit instruction is always the highest priority.

---

## TOP 5 RULES (HIGHEST PRECEDENCE — READ THESE FIRST)

These are the most important rules. They override everything else. If you remember nothing else, remember these:

### RULE 0: Falcor is the Rendering Quality Baseline

**NVIDIA Falcor (E:/Falcor) is UzEngine's ground truth renderer.** Every BRDF, tone mapping, IBL, and lighting shader MUST be compared against Falcor's path-traced output. The goal: UzEngine's rasterized result must be **perceptually indistinguishable** from Falcor's ground truth.

**After implementing or modifying any rendering shader:**
1. Run `python scripts/falcor/test_bsdf_sphere.py` to generate Falcor reference
2. Capture UzEngine output at the same scene/camera
3. Compare via PSNR (>= 35 dB = pass) or visual inspection
4. If UzEngine output diverges, **adjust UzEngine to match Falcor**, not the other way around

**Falcor binary**: `E:/Falcor/build/windows-vs2022/bin/Release/Mogwai.exe`
**Test scripts**: `scripts/falcor/`
**Reference images**: `.sandbox/brdf_comparison/` (gitignored, local only)

### RULE 1: Industry-Standard Rendering Parity — Components, Parameters, Defaults, Logic

Every UzEngine rendering component, system, and feature MUST implement the **correct behavior, ALL relevant parameters, ALL standard defaults, and ALL computation logic** as described in published papers and industry-standard implementations.

**ABSOLUTELY FORBIDDEN:**
- **Simplifying components** by removing parameters that the standard technique requires
- **Inventing defaults** without researching the published reference
- **Replacing runtime computation with hardcoded values** (e.g., auto sky color → hardcoded RGB)
- Inventing parameter values without a published reference
- Analytical approximations where the technique uses a LUT/texture
- Compensation factors (`* 2.0f` to "match reference")
- Minimum clamps (`max(value, 0.15f)` unless the reference technique does the same)
- Empirically tuned constants ("was tuned to approximate...")
- **Visual workarounds** — if a technique auto-computes a value, IMPLEMENT THAT SYSTEM
- **Guessing** at what a parameter does based on its name — READ THE PAPER

**WHAT TO DO:**
1. Research the published paper/technique FIRST
2. Identify ALL parameters and their default values from the reference
3. Implement ALL properties with the correct defaults
4. Implement ALL behavior — same computation, same update triggers
5. Cite the paper/standard with `@ref Author Year "Title"` for every property and function
6. Use UzEngine-specific naming (NEVER copy identifiers from proprietary codebases)

See `.claude/rules/ue5-component-parity.md` for full details and examples.

### RULE 2: ALWAYS Research Published References BEFORE Writing Rendering Code

Before writing ANY shader, render pass, lighting, atmosphere, shadow, or post-process code:
1. Research the published paper or technique (SIGGRAPH, GDC, etc.)
2. Use the `unreal-architect` agent for architectural pattern analysis (NOT for copying code)
3. Only then write UzEngine code based on the published reference

**NEVER rely on model knowledge** for rendering math — it is frequently wrong.

### RULE 3: Shader Source Citation Template (MANDATORY)

Every shader function MUST start with this header:

```hlsl
/// @ref Author Year "Paper Title" — technique description
/// @constants <list every numeric constant and its published value>
```

Every C++ rendering function MUST have:
```cpp
/// @ref Author Year "Paper Title" — technique description
```

**NEVER cite proprietary source files.** Always cite published papers, standards, or techniques.

### RULE 3.5: Incremental Implementation — Step by Step, Test Each Step

**NEVER implement a complex feature all at once.** Break it into small steps, test each one visually before proceeding:

1. **Step 1**: Absolute minimum (solid color output → verify pixel on screen)
2. **Step 2**: Add ONE thing (ray intersection → verify correct area)
3. **Step 3**: Build + test after EACH step — do NOT proceed if current step fails
4. **Step 4**: If stuck for 2-3 attempts, **DELETE everything and restart from scratch**

**The line between persistence and stupidity is thin.** Rewriting correctly is faster than debugging wrong code.

See `.claude/rules/incremental-implementation.md` for details.

### RULE 4: NEVER Use Stubs, TODOs, Placeholders, or Approximations

- No `// TODO`, `// HACK`, `// FIXME`, `// TEMP`, `// placeholder`
- No `return 0; // fixme`, no empty function bodies
- No simplified algorithms where the correct one is known
- If GPU readback fails, FIX the readback — don't invent a fallback

### RULE 5: Implement Directly — Agents Only When User Requests

Implement C++, shaders, and docs **directly in the main conversation**. Do NOT delegate to agents unless the user explicitly asks for it. Agents are available but opt-in only.

### RULE 6: File Locations — NEVER create new directories

- **Analysis files** → `docs/analysis/` (NEVER `.claude/analysis/`)
- **Guides/reference docs** → `docs/guides/`
- **Specs** → `docs/specs/`
- **NEVER** create new top-level directories (no `.ue5-defaults/`, no `.claude/analysis/`)
- Tell ALL agents to save output in `docs/analysis/` or `docs/guides/`

---

## Secondary Rules (see .claude/rules/ for details)

The following rules are equally important but less frequently violated. Full details are in `.claude/rules/`:

- **Data Flow First** — multi-subsystem tasks (3+ files) require a data flow document BEFORE implementation. See `.claude/rules/data-flow-first.md`
- **Task Decomposition** — break multi-file tasks into 1-2 file sub-tasks per subsystem. See `.claude/rules/task-decomposition.md`
- **Rendering Parity** — every component replicates ALL standard parameters, defaults, and logic. No simplification. See `.claude/rules/ue5-component-parity.md`
- **Visual Verification** — rendering features need demo + screenshot. **Prefer `--screenshot-stdout`** (base64 to stdout, no disk I/O) over `--screenshot <path>`. See `.claude/rules/visual-verification.md`
- **Shader Testing Pipeline** — 3-tier validation: (1) CPU math parity tests for every HLSL function, (2) visual regression via PSNR against golden references, (3) FLIP perceptual comparison. See `.claude/rules/shader-testing-pipeline.md`
- **Pass Dump FIRST** — for ANY visual bug, run `--pass-dump` BEFORE anything else. Inspect every pass output. See `.claude/rules/pass-dump-first.md`
- **GPU Debugging** — never guess, use `--diag` / `--pixel-debug` / `--vis`. See `.claude/rules/gpu-debugging.md`
- **Rendering Diagnostics** — MUST run diagnostic tools BEFORE editing any shader. See `.claude/rules/rendering-diagnostics.md`
- **Demo Build Order** — always rebuild runtime static lib before demo. See `.claude/rules/build-order.md`
- **File Editing** — edit files sequentially, one at a time. See `.claude/rules/editing.md`
- **Architecture** — Z-up, cm units, engine APIs (UZ_LOG/UZ_CHECK), SOLID. See `.claude/rules/architecture.md`
- **Documentation** — Rust-style `///`, kebab-case filenames, TEMPLATE.md. See `.claude/rules/documentation.md`
- **Lua API Parity** — every C++ feature gets a Lua binding. See `.claude/rules/lua-parity.md`
- **No Shortcuts** — quality over speed, complete implementations only. See `.claude/rules/quality.md`
- **Git Safety** — never `git stash`, never switch branches, never force push. See `.claude/rules/git-safety.md`
- **Thirdparty** — never manually clone/download; use `scripts/setup.ps1`. See `.claude/rules/thirdparty.md`

## Agent Delegation Table

| Task Type | Agent | Model |
|-----------|-------|-------|
| Write/fix shaders | shader-engineer | opus |
| Write C++ code | cpp-core-expert | opus |
| RHI backend work | rhi-engineer | opus |
| Render graph passes | render-graph-engineer | opus |
| ECS/game systems | ecs-systems-engineer | opus |
| Analyze architecture patterns | unreal-architect | haiku |
| Analyze Godot patterns | godot-architect | haiku |
| Architecture synthesis | engine-architect | sonnet |
| Write/update docs | docs-operator | haiku |
| Explore/search codebase | researcher | haiku |
| Write tests | test-engineer | sonnet |
| Build system issues | build-system-engineer | sonnet |
| TypeScript/extension | typescript-vscode-extension | sonnet |
| Lua scripting | lua-scripting-engineer | sonnet |
| Physics (Jolt) | physics-engineer | sonnet |
| Audio | audio-engineer | sonnet |
| Animation | animation-engineer | sonnet |
| UI system | ui-engineer | sonnet |
| Gameplay systems | gameplay-systems-engineer | sonnet |
| Multi-subsystem integration | systems-integration | opus |
| Post-merge/implementation audit | implementation-auditor | sonnet |
| Project coordination | project-manager | haiku |

## Persistent Memory

This project uses a **persistent memory system** via the Rulebook MCP server and `.claude/projects/` memory files.

### Session Workflow
1. **Start of session**: `rulebook_memory_search` for relevant past context
2. **During work**: Save decisions, bugs, discoveries as they happen
3. **End of session**: Save a summary with `type: observation`

## MCP Documentation Server (uzengine-docs)

An MCP server indexes all C++ and HLSL code documentation. Use it to understand the codebase without reading every file.

### Available Tools

| Tool | Purpose | Example |
|------|---------|---------|
| `search_docs` | Hybrid BM25 + semantic search across all code entities | `search_docs({ query: "fog density" })` |
| `get_entity` | Get full docs for a specific entity by ID | `get_entity({ id: "runtime/src/engine/renderer/passes/fog_pass.h::FogPass" })` |
| `get_data_flow` | Trace a field through component → renderer → cbuffer → shader | `get_data_flow({ field: "fogDensity" })` |
| `get_relationships` | Find related entities (inheritance, bindings, etc.) | `get_relationships({ id: "SkyAtmosphereComponent", depth: 2 })` |
| `get_subsystem_map` | List all subsystems with entity counts | `get_subsystem_map()` |
| `get_refs` | List academic paper references cited in code | `get_refs({ author: "Bruneton" })` |

### When to Use

- **Before implementing** — `search_docs` to find existing code for the feature area
- **Understanding data flow** — `get_data_flow` to trace a field across the pipeline (replaces manual grep across 4+ files)
- **Finding related code** — `get_relationships` to discover what depends on or extends an entity
- **Checking citations** — `get_refs` to verify which papers are already referenced
- **Exploring subsystems** — `get_subsystem_map` for a high-level view of the codebase structure

### Search Filters

`search_docs` supports optional filters:
- `kind`: `file`, `class`, `struct`, `enum`, `function`, `method`, `field`, `macro`, `shader_fn`, `shader_cb`, `typedef`
- `subsystem`: filter by subsystem name (use `get_subsystem_map` to list them)
- `tags`: array of tags (AND logic)
- `limit`: max results (default 10)

### Example Workflow

```
# 1. Find all fog-related entities
search_docs({ query: "exponential height fog", kind: "struct" })

# 2. Get full docs for the component
get_entity({ id: "height_fog_component.h::HeightFogComponent" })

# 3. Trace how fogDensity flows through the pipeline
get_data_flow({ field: "fogDensity" })

# 4. Find what references this component
get_relationships({ id: "height_fog_component.h::HeightFogComponent", kind: "binding" })
```

## Logging & Crash Reports

All logs and crash reports are centralized in the user's OS data directory:

| Type | Location (Windows) | Format |
|------|-------------------|--------|
| Runtime logs | `%LOCALAPPDATA%/UzEngine/logs/` | `uzengine_YYYY-MM-DD_HH-MM-SS.log` |
| Crash reports | `{engine_dir}/crashes/{YYYY-MM-DD_HH-MM-SS}/` | `crash.dmp`, `crash.txt`, `crash.json` |
| Worker logs | `extension/native/worker.log` | Appended per-session (N-API worker process) |

- `LogInit()` with no args auto-resolves to `%LOCALAPPDATA%/UzEngine/logs/` with a timestamped filename
- `LogInit(path)` with an explicit path still works (used by tests)
- `LogGetFilePath()` returns the resolved log file path at runtime
- Rotating file sink: 10MB max per file, keeps 3 rotated files

## Commands

```bash
# Build & test (Zig)
cd runtime && zig build          # Build runtime
cd runtime && zig build -Ddll=true  # Build runtime DLL (for VSCode extension)
cd demos/<name> && zig build     # Build demo

# Deploy DLL to extension
cp runtime/build/bin/uzengine_runtime.dll extension/native/uzengine_runtime.dll

# Quality checks
npm run type-check    # TypeScript type checking
npm run lint          # Run linter
npm test              # Run tests
```

## Rendering Diagnostics (MANDATORY before any shader debug)

When a rendering bug is reported, run these commands BEFORE editing any code.
Full details in `.claude/rules/rendering-diagnostics.md`.

```bash
# 1. Which render target is broken? (JSON, auto-exits 120 frames)
uzengine.exe --diag --project ./sample/projects/<name>.uzproject --editor

# 2. Exact GBuffer values at a pixel (13 values: depth, AO, albedo, HDR, roughness, metallic, emissive)
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 --project ... --editor

# 3. Visualize a target / GBuffer channel
uzengine.exe --vis shadow.atlas --screenshot-stdout --frames 8 --project ... --editor
uzengine.exe --view-mode overview --screenshot-stdout --frames 8 --project ... --editor

# 4. Dump all pass outputs as PNGs
uzengine.exe --pass-dump --frames 120 --project ... --editor

# 5. Disable a pass to isolate the problem
uzengine.exe --pass-toggle bloom --screenshot-stdout --frames 120 --project ... --editor
```

Parse stdout for `DIAG_JSON:{...}` and `PIXEL_DEBUG_JSON:{...}`.
**NEVER edit shaders to add debug output** — the tools above replace that workflow entirely.

When in doubt, check AGENTS.md for guidance.
