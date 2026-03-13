# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: Not specified
**Coverage Threshold**: 95%

## ⚠️ CRITICAL: NEVER Use `git stash`

**ABSOLUTELY FORBIDDEN**: No agent or conversation may run `git stash`, `git stash pop`, `git stash apply`, or any `git stash` subcommand. **No exceptions.**

### Why

`git stash` destroyed hours of uncommitted work on 2026-03-13. The stash pop failed with conflicts and all local changes were irrecoverably lost. This command is too dangerous for uncommitted work.

### What To Do Instead

- **Commit first**: Create a WIP commit (`git commit -m "wip: ..."`) before any operation that touches the working tree.
- **Use branches**: `git checkout -b temp-branch && git commit -am "wip"` if you need to test something on a clean tree.
- **Never touch uncommitted work**: If you need a clean working tree, ask the user to commit or stash manually.

## ⚠️ CRITICAL: File Editing Rules

**MANDATORY**: When editing multiple files, you MUST edit files **SEQUENTIALLY**, one at a time.

### Why Sequential Editing is Required

The Edit tool uses exact string matching for replacements. When multiple files are edited in parallel:
- The tool may fail to find the exact string in some files
- Race conditions can cause partial or corrupted edits
- Error recovery becomes impossible

### Correct Pattern

```
✅ CORRECT (Sequential):
1. Edit file A → Wait for confirmation
2. Edit file B → Wait for confirmation
3. Edit file C → Wait for confirmation

❌ WRONG (Parallel):
1. Edit files A, B, C simultaneously → Failures likely
```

### Implementation Rules

1. **NEVER call multiple Edit tools in parallel** for different files
2. **ALWAYS wait for each edit to complete** before starting the next
3. **Verify each edit succeeded** before proceeding
4. **If an edit fails**, retry that specific edit before moving on

## ⚠️ CRITICAL: Test Implementation Rules

**MANDATORY**: You MUST write **complete, production-quality tests**. Never simplify or reduce test coverage.

### Forbidden Test Patterns

```typescript
// ❌ NEVER do this - placeholder tests
it('should work', () => {
  expect(true).toBe(true);
});

// ❌ NEVER do this - skipped tests
it.skip('should handle edge case', () => {});

// ❌ NEVER do this - incomplete assertions
it('should return data', () => {
  const result = getData();
  expect(result).toBeDefined(); // Too weak!
});

// ❌ NEVER do this - "simplify" by removing test cases
// Original had 10 test cases, don't reduce to 3
```

### Required Test Patterns

```typescript
// ✅ CORRECT - complete test with proper assertions
it('should return user data with correct structure', () => {
  const result = getUserById(1);
  expect(result).toEqual({
    id: 1,
    name: 'John Doe',
    email: 'john@example.com',
    createdAt: expect.any(Date),
  });
});

// ✅ CORRECT - test edge cases and error paths
it('should throw NotFoundError when user does not exist', () => {
  expect(() => getUserById(999)).toThrow(NotFoundError);
});
```

### Test Implementation Rules

1. **NEVER simplify tests** - Implement the full, complete test as originally designed
2. **NEVER skip test cases** - Every test case in the spec must be implemented
3. **NEVER use placeholder assertions** - Each assertion must verify actual behavior
4. **ALWAYS test error paths** - Exceptions, edge cases, and failure modes
5. **ALWAYS maintain coverage** - Tests must achieve the project's coverage threshold (95%+)

## ⚠️ CRITICAL: File Size & Responsibility Rules

**MANDATORY**: No file may exceed **1500 lines**. Every file must have a **single, clear responsibility**.

### Why This Matters

Large files with mixed responsibilities degrade LLM comprehension. Smaller, focused files are easier to reason about, review, and test — both for humans and AI agents.

### Rules

1. **Max 1500 lines per file** — source, tests, headers, configs. No exceptions.
2. **Single Responsibility** — each file owns one concern (e.g., logging, input, config). If a file does two things, split it.
3. **Organize by topic directories** — group related files into directories by domain (e.g., `core/logging/`, `core/memory/`, `platform/windows/`). Prefer `topic/file.cpp` over a flat directory with many files.
4. **Tests mirror source structure** — `tests/core/test_logging.cpp`, `tests/core/test_memory.cpp`, etc. Never a monolithic `test_all.cpp`.
5. **Split proactively** — when a file approaches 1000 lines, plan the split. Don't wait until 1500.
6. **No generated artifacts in repo** — tests must clean up any files they create (logs, temp files). Never commit generated output.

### Examples

```
✅ CORRECT:
  runtime/core/logging/uz_log.h        (~150 lines)
  runtime/core/logging/uz_log.cpp      (~120 lines)
  runtime/tests/core/test_logging.cpp  (~100 lines)

❌ WRONG:
  runtime/tests/test_runtime.cpp       (400+ lines, tests everything)
  runtime/core/engine.cpp              (2000 lines, does logging + config + init)
```

## ⚠️ CRITICAL: Documentation Style

**MANDATORY**: All code headers, types, and public functions MUST use **Rust-style documentation comments**.

### Documentation Rules

1. **Triple-slash (`///`) for public items** — all public structs, functions, methods
   ```cpp
   /// Thin wrapper over DirectXMath for SIMD-accelerated vector operations.
   /// All types are DirectX-friendly: column-major matrices, XMVECTOR-compatible.
   struct Vec3 {
       /// Signed distance from point — positive if point is in front of plane.
       float Distance(const Vec3& p) const;
   }
   ```

2. **Double-slash sections (`// ───`)** — for logical groupings within files
   ```cpp
   // ─────────────────────────────────────────────────────────────────────────────
   // Vec3 Implementation — 3D float vector (primary SIMD vector)
   // ─────────────────────────────────────────────────────────────────────────────
   ```

3. **Inline comments ONLY for "why"** — never explain "what" the code does
   - ❌ `x += 1;  // increment x`
   - ✅ `x += 1;  // Skip sentinel value at index 0`

4. **No comments = self-explanatory code** — if you need to explain it, rename variables/functions

5. **Documentation MUST be detailed enough for auto-generated API docs** — every `///` comment must describe:
   - **What** the function/class does (purpose, not implementation)
   - **Parameters**: what each parameter means, valid ranges, ownership semantics
   - **Return value**: what it returns on success AND failure
   - **Side effects**: logging, file I/O, state mutations
   - **Thread safety**: if relevant
   - **Example**: for non-obvious usage patterns (use `/// ```cpp` blocks)

   ```cpp
   // ❌ WRONG — lazy one-liner that says nothing useful
   /// Reads a file.
   std::vector<uint8_t> Read(const std::string& path);

   // ✅ CORRECT — complete, generates useful API docs
   /// Reads an entire file into memory as a byte buffer.
   ///
   /// The path is resolved relative to the VFS root directory.
   /// Uses forward slashes as separators (e.g., "shaders/pbr.uzshader").
   ///
   /// # Returns
   /// - Non-empty vector containing the file bytes on success.
   /// - Empty vector if the file does not exist or an I/O error occurs.
   ///
   /// # Errors
   /// Logs `Asset::Error` on I/O failure with the resolved absolute path.
   std::vector<uint8_t> Read(const std::string& path) override;
   ```

6. **Override methods MUST have docs** — don't skip docs just because the base class has them. Each override should document its specific behavior/contract.

7. **Implementation files (.cpp) need section-level docs** — each function implementation should have a brief `///` comment if the behavior differs from or extends the header doc.

## ⚠️ CRITICAL: Clean Code & SOLID Principles

**MANDATORY**: All code must follow Clean Code and SOLID principles. This engine is large — without discipline, it becomes unmaintainable for both humans and AI.

### Clean Code Rules

1. **Meaningful names** — variables, functions, classes must reveal intent. No `tmp`, `data`, `handler`, `manager` unless truly generic. Prefer `meshVertexBuffer` over `buf`.
2. **Small functions** — each function does ONE thing. If you need a comment to explain a section, extract it into a named function. Max ~40 lines per function.
3. **No side effects** — a function named `GetX()` must not modify state. Separate queries from commands.
4. **No magic numbers** — use named constants. `constexpr float kGravity = -9.81f;` not `y += -9.81f`.
5. **Fail early, fail loud** — validate inputs at boundaries, use `UZ_CHECK`/`UZ_ENSURE` for invariants. Never silently swallow errors.
6. **Delete dead code** — don't comment out code "for later". Git has history.
7. **Consistent formatting** — follow `.clang-format`. No manual formatting overrides.

### SOLID Principles (adapted for C++/game engine)

1. **S — Single Responsibility**: One class/struct = one reason to change. `InputSystem` handles input, not window events. `Config` loads config, not file I/O.
2. **O — Open/Closed**: Extend via new types, not by modifying existing ones. Use virtual interfaces or templates for extension points (e.g., RHI backends).
3. **L — Liskov Substitution**: Any RHI backend (DX12, Vulkan, Metal) must be interchangeable through the `rhi_backend_t` vtable without the caller knowing which one.
4. **I — Interface Segregation**: Don't force consumers to depend on methods they don't use. Split fat interfaces into focused ones (e.g., `IRenderable` vs `IUpdatable` vs `ISerializable`).
5. **D — Dependency Inversion**: High-level modules (Renderer, Scene) depend on abstractions (RHI interface), not concrete backends (DX12). Inject dependencies, don't hardcode them.

### Observability Rules

1. **Log at boundaries** — every system init/shutdown, every external call (file I/O, GPU, OS API) gets a log entry with timing.
2. **Profile critical paths** — `UZ_PROFILE_SCOPE` on every function that touches GPU, disk, or runs per-frame. No guessing where time is spent.
3. **Assert invariants** — use `UZ_CHECK` for things that must never fail (corrupted state = crash). Use `UZ_ENSURE` for recoverable violations.
4. **Stat counters** — track allocations, draw calls, frame time, cache hits. Every system exposes its health via `StatCounter`.
5. **Error context** — log messages include what failed, why, and what input caused it. `"Failed to load texture: path='{}', reason='{}'"` not `"Load failed"`.

## ⚠️ CRITICAL: Engine Systems — Use Engine APIs, Not Raw Libraries

**MANDATORY**: Always use UzEngine's own abstractions. Never bypass them with raw library calls.

### Rules

1. **Logging**: Use `UZ_LOG(Category, Level, ...)` — NEVER use `spdlog::info/error/warn` directly
2. **Log categories**: Every subsystem MUST have a `UZ_DECLARE_LOG_CATEGORY` / `UZ_DEFINE_LOG_CATEGORY`. Add new ones when creating new subsystems.
3. **Assertions**: Use `UZ_CHECK` / `UZ_ENSURE` — NEVER use raw `assert()`
4. **Memory**: Use engine allocators — NEVER use raw `new`/`malloc` in engine code
5. **Paths**: Use `uz::GetExeDir()` / `uz::ResolveEngineResource()` — NEVER use hardcoded paths
6. **Profiling**: Use `UZ_PROFILE_SCOPE` — NEVER use raw Tracy macros

### Examples

```cpp
// ❌ WRONG — bypasses engine logging
spdlog::info("Loaded texture: {}", path);
spdlog::error("Failed to compile shader");

// ✅ CORRECT — uses engine log system with category
UZ_LOG(Asset, Info, "Loaded texture: path='{}'", path);
UZ_LOG(Asset, Error, "Failed to compile shader: reason='{}'", err);
```

## ⚠️ CRITICAL: Agent Delegation Rules

**MANDATORY**: You MUST delegate work to specialist agents instead of doing everything in the main conversation. This saves cost (haiku/sonnet vs opus) and produces better results from domain experts.

### When You MUST Delegate

| Task Type | Agent | Model | Why |
|-----------|-------|-------|-----|
| **Write/update docs** | docs-operator | haiku | 20x cheaper, docs don't need opus |
| **Explore/search codebase** | researcher | haiku | Read-only exploration is cheap |
| **Write C++ code** | cpp-core-expert | opus | Domain expert with quality checklist |
| **Write/fix shaders** | shader-engineer | opus | HLSL/SPIRV specialist |
| **RHI backend work** | rhi-engineer | opus | DX12/Vulkan/Metal specialist |
| **Render graph passes** | render-graph-engineer | opus | DAG/barrier specialist |
| **ECS/game systems** | ecs-systems-engineer | opus | EnTT/game loop specialist |
| **Write tests** | test-engineer | sonnet | Test patterns specialist |
| **Build system issues** | build-system-engineer | sonnet | Zig/vcpkg/DLL deploy specialist |
| **IPC/N-API work** | ipc-protocol-engineer | sonnet | Frame streaming/Electron specialist |
| **Performance work** | performance-engineer | sonnet | Tracy/GPU profiling specialist |
| **Analyze UE5 patterns** | unreal-architect | haiku | Reads W:\UE_5.6_Source |
| **Analyze Godot patterns** | godot-architect | haiku | Reads E:\godot |
| **Architecture synthesis** | engine-architect | sonnet | Combines multi-engine analysis |
| **TypeScript/extension** | typescript-vscode-extension | sonnet | TS/VSCode specialist |
| **Physics (Jolt)** | physics-engineer | sonnet | Jolt integration specialist |
| **Animation** | animation-engineer | sonnet | Skeletal/skinning/IK specialist |
| **Audio** | audio-engineer | sonnet | miniaudio/spatial specialist |
| **Lua scripting** | lua-scripting-engineer | sonnet | LuaJIT/sandbox specialist |
| **UI system** | ui-engineer | sonnet | HTML/CSS/data binding specialist |
| **Gameplay systems** | gameplay-systems-engineer | sonnet | Input/camera/save/BT/nav specialist |
| **Project coordination** | project-manager | haiku | Planning/status tracking |

### Delegation Rules

1. **NEVER write docs in main conversation** — ALWAYS delegate to docs-operator (haiku). This includes ENGINE.md migrations, new feature docs, doc audits.
2. **NEVER do broad codebase exploration in main conversation** — delegate to researcher (haiku) or use the Explore agent type.
3. **NEVER write C++ directly in main conversation** — delegate to the appropriate specialist agent (cpp-core-expert for general C++, rhi-engineer for RHI, etc.).
4. **NEVER analyze reference engines in main conversation** — delegate to architect agents (haiku).
5. **After implementing C++ code, ALWAYS launch docs-operator** to update relevant docs.
6. **After implementing C++ code, ALWAYS launch test-engineer** to write tests.
7. **Use skills as shortcuts**: `/docs`, `/research`, `/review`, `/analyze`, `/demo`, `/deps`, `/profile` trigger the right agents automatically.
8. **Parallel delegation**: Launch independent agents in parallel (e.g., docs-operator + test-engineer after code changes).
9. **Main conversation role**: The main conversation (opus) should ONLY do: coordination, user communication, planning, and delegating. It should NOT do bulk work.

### Cost Optimization

```
opus  = $$$  → Only for: C++ core, RHI, render graph, shaders, ECS (complex, critical)
sonnet = $$  → For: gameplay, physics, audio, animation, build, IPC, tests, TS
haiku  = $   → For: docs, research, project management, architecture analysis
```

**Rule of thumb**: If the task is read-only or text-generation (not complex code), use haiku. If it's code but not core engine, use sonnet. Only use opus for critical C++ systems.

## Critical Rules

1. **ALWAYS read AGENTS.md first** - Contains all project standards and patterns
2. **ALWAYS delegate to specialist agents** - See delegation table above
3. **Edit files sequentially** - One at a time, verify each edit
4. **Write complete tests** - No placeholders, no simplifications
5. **Tests required** - Minimum 95% coverage for all new code
6. **Quality checks before committing**:
   - Type check / Compiler check
   - Lint (zero warnings)
   - All tests passing
   - Coverage threshold met
7. **Documentation** - Update /docs/ when implementing features (via docs-operator agent)

## Persistent Memory

This project uses a **persistent memory system** via the Rulebook MCP server.
Memory persists across sessions — use it to maintain context between conversations.

**MANDATORY: You MUST actively use memory to preserve context across sessions.**

### Auto-Capture

Tool interactions (task create/update/archive, skill enable/disable) are auto-captured.
But you MUST also manually save important context:

- **Architectural decisions** — why you chose one approach over another
- **Bug fixes** — root cause and resolution
- **Discoveries** — codebase patterns, gotchas, constraints
- **Feature implementations** — what was built, key design choices
- **User preferences** — coding style, conventions, workflow preferences
- **Session summaries** — what was accomplished, what's pending

### Memory Commands (MCP)

```
rulebook_memory_save    — Save context (type, title, content, tags)
rulebook_memory_search  — Search past context (query, mode: hybrid/bm25/vector)
rulebook_memory_get     — Get full details by ID
rulebook_memory_timeline — Chronological context around a memory
rulebook_memory_stats   — Database stats
rulebook_memory_cleanup — Evict old memories
```

### Session Workflow

1. **Start of session**: `rulebook_memory_search` for relevant past context
2. **During work**: Save decisions, bugs, discoveries as they happen
3. **End of session**: Save a summary with `type: observation`

## Commands

```bash
# Quality checks
npm run type-check    # TypeScript type checking
npm run lint          # Run linter
npm test              # Run tests
npm run build         # Build project

# Task management (if using Rulebook)
rulebook task list    # List tasks
rulebook task show    # Show task details
rulebook validate     # Validate project structure
```

## File Structure

- `AGENTS.md` - Main project standards and AI directives
- `/rulebook/` - Modular rule definitions
- `/docs/` - Project documentation
- `/tests/` - Test files

When in doubt, check AGENTS.md for guidance.
