# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: C++23 (engine), HLSL (shaders), TypeScript (VSCode extension), Lua (scripting)
**Coverage Threshold**: 95%

---

## TOP 5 RULES (HIGHEST PRECEDENCE — READ THESE FIRST)

These are the most important rules. They override everything else. If you remember nothing else, remember these:

### RULE 1: UE5 COMPLETE Behavioral Parity — Components, Parameters, Defaults, Logic

Every UzEngine component, system, and feature that has a UE5 equivalent MUST replicate the **exact same behavior, ALL parameters, ALL defaults, and ALL logic**. This is NOT limited to numeric constants — it covers the ENTIRE component.

**If UE5's component has 47 properties, ours has 47 properties. Not 12. Not "the important ones". ALL of them.**

**ABSOLUTELY FORBIDDEN:**
- **Simplifying components** by removing parameters UE5 has
- **Inventing defaults** without reading UE5 source (constructor, `GetDefaultObject()`)
- **Replacing runtime computation with hardcoded values** (e.g., auto sky color → hardcoded RGB)
- Inventing parameter values (no `0.90f`, `0.5f` without UE5 source)
- Analytical approximations where UE5 uses a LUT/texture
- Compensation factors (`* 2.0f` to "match UE5")
- Minimum clamps (`max(value, 0.15f)` unless UE5 does the same)
- Empirically tuned constants ("was tuned to approximate...")
- **Visual workarounds** ("sky horizon blue-white", "warm sun glow") — if UE5 has a system that auto-computes a value, IMPLEMENT THAT SYSTEM instead of hardcoding a guess
- **Supposing corrections** — if the render looks wrong, find the ROOT CAUSE in UE5 source, don't patch symptoms with invented numbers
- **Creating alternatives** — if UE5 does X, do X. Don't invent Y because "it's simpler"
- **Adding parameters UE5 doesn't have** (no "artistic controls", no "UzEngine enhancements")
- **Guessing** at what a parameter does based on its name — READ THE CODE

**WHAT TO DO:**
1. Read UE5 source FIRST — find the `.h` file, list EVERY `UPROPERTY`
2. Read the constructor — note EVERY default value
3. Read the `.cpp` — understand ALL update/recalculation logic
4. Implement ALL properties with EXACT same defaults
5. Implement ALL behavior — same computation, same update triggers
6. Cite `@source UE5 <file>:<line>` for every property and function
7. Rewrite in your own code style (different names, structure) to respect Epic's IP — but the MATH and BEHAVIOR must be identical

See `.claude/rules/ue5-component-parity.md` for full details and examples.

### RULE 2: ALWAYS Read UE5 Source BEFORE Writing Rendering Code

Before writing ANY shader, render pass, lighting, atmosphere, shadow, or post-process code:
1. Launch `unreal-architect` agent to find the exact UE5 implementation
2. Read the actual UE5 `.usf`/`.ush`/`.cpp` file
3. Only then write UzEngine code based on the UE5 reference

**NEVER rely on model knowledge** for rendering math — it is frequently wrong.

### RULE 3: Shader Source Citation Template (MANDATORY)

Every shader function MUST start with this header:

```hlsl
/// @source UE5 <file.usf>:<line> — <function name>
/// @constants <list every numeric constant and its UE5 value>
/// @deviations NONE
```

Every C++ rendering function MUST have:
```cpp
/// @source UE5 <file.cpp>:<line> — <function name>
```

If you cannot fill in `@source`, you MUST launch `unreal-architect` BEFORE writing the code.

### RULE 4: NEVER Use Stubs, TODOs, Placeholders, or Approximations

- No `// TODO`, `// HACK`, `// FIXME`, `// TEMP`, `// placeholder`
- No `return 0; // fixme`, no empty function bodies
- No simplified algorithms where the correct one is known
- If GPU readback fails, FIX the readback — don't invent a fallback

### RULE 5: ALWAYS Delegate to Specialist Agents

Never write C++/shaders/docs directly in the main conversation. Delegate to the appropriate agent:
- **Shaders** → `shader-engineer` (opus)
- **C++ engine code** → `cpp-core-expert` (opus)
- **RHI backends** → `rhi-engineer` (opus)
- **UE5 analysis** → `unreal-architect` (haiku)
- **Docs** → `docs-operator` (haiku)
- **Tests** → `test-engineer` (sonnet)

### RULE 6: File Locations — NEVER create new directories

- **Analysis files** → `docs/analysis/` (NEVER `.claude/analysis/`)
- **Guides/reference docs** → `docs/guides/`
- **Specs** → `docs/specs/`
- **NEVER** create new top-level directories (no `.ue5-defaults/`, no `.claude/analysis/`)
- Tell ALL agents to save output in `docs/analysis/` or `docs/guides/`

---

## Secondary Rules (see .claude/rules/ for details)

The following rules are equally important but less frequently violated. Full details are in `.claude/rules/`:

- **UE5 Component Parity** — every component replicates ALL UE5 parameters, defaults, and logic. No simplification. See `.claude/rules/ue5-component-parity.md`
- **Visual Verification** — rendering features need demo + screenshot. **Prefer `--screenshot-stdout`** (base64 to stdout, no disk I/O) over `--screenshot <path>`. See `.claude/rules/visual-verification.md`
- **GPU Debugging** — never guess, use readback + RenderDoc/PIX. See `.claude/rules/gpu-debugging.md`
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
| Analyze UE5 patterns | unreal-architect | haiku |
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
| Project coordination | project-manager | haiku |

## Persistent Memory

This project uses a **persistent memory system** via the Rulebook MCP server and `.claude/projects/` memory files.

### Session Workflow
1. **Start of session**: `rulebook_memory_search` for relevant past context
2. **During work**: Save decisions, bugs, discoveries as they happen
3. **End of session**: Save a summary with `type: observation`

## Commands

```bash
# Build & test (Zig)
cd runtime && zig build          # Build runtime
cd demos/<name> && zig build     # Build demo

# Quality checks
npm run type-check    # TypeScript type checking
npm run lint          # Run linter
npm test              # Run tests
```

When in doubt, check AGENTS.md for guidance.
