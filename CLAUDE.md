# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: C++23 (engine), HLSL (shaders), TypeScript (VSCode extension), Lua (scripting)
**Coverage Threshold**: 95%

---

## TOP 5 RULES (HIGHEST PRECEDENCE — READ THESE FIRST)

These are the most important rules. They override everything else. If you remember nothing else, remember these:

### RULE 1: UE5 Exact Parity — Every Number Must Cite UE5 file:line

Every numeric constant, formula, and algorithm in rendering/lighting/physics code MUST come from UE5 source (`W:\UE_5.6_Source`) with an explicit file path and line number citation.

**ABSOLUTELY FORBIDDEN:**
- Inventing parameter values (no `0.90f`, `0.5f`, `512.0f` without UE5 source)
- Analytical approximations where UE5 uses a LUT/texture
- Compensation factors (`* 2.0f` to "match UE5")
- Minimum clamps (`max(value, 0.15f)` unless UE5 does the same)
- Empirically tuned constants ("was tuned to approximate...")

**WHAT TO DO:** Read UE5 source FIRST → copy the exact math → cite the file:line.

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

---

## Secondary Rules (see .claude/rules/ for details)

The following rules are equally important but less frequently violated. Full details are in `.claude/rules/`:

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
