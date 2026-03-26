# Contributing to UzEngine

UzEngine is a AAA-grade game engine embedded in VSCode, built in C++ with a Zig build system and TypeScript extension layer. Contributions are welcome across all subsystems.

---

## Table of Contents

- [Before You Start](#before-you-start)
- [Project Structure](#project-structure)
- [Building](#building)
- [Code Standards](#code-standards)
- [Testing Requirements](#testing-requirements)
- [Commit Conventions](#commit-conventions)
- [Pull Request Process](#pull-request-process)
- [Specialist Areas](#specialist-areas)

---

## Before You Start

1. **Search open issues and PRs** before filing a new one — duplicates slow everyone down.
2. **For large changes**, open a discussion issue first. Architectural work that lands without prior agreement will likely be asked to be redesigned.
3. **Read the docs** in `/docs/` — especially `docs/ARCHITECTURE.md` — before touching core systems.
4. **Agree to the Code of Conduct** (see `CODE_OF_CONDUCT.md`). All contributors are expected to uphold it.

---

## Project Structure

```
runtime/          C++ engine library (RHI, ECS, Renderer, Platform, etc.)
  engine/         Core engine systems
  platform/       OS abstraction layers (Windows, macOS, Linux)
  shaders/        HLSL source files
  tests/          Tests mirror source structure (tests/engine/, tests/platform/, …)
  build.zig       Zig build script — builds engine lib, tests, and tools only
extension/        TypeScript VSCode extension
mcp/              Node.js MCP server
docs/             All documentation (never put docs inside runtime/)
.github/          CI workflows and issue/PR templates
```

---

## Building

### Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| Zig  | 0.14.x  | Build system |
| MSVC / Clang | Latest | C++20 compiler |
| DXC  | Agility SDK 614+ | Shader compilation |
| Node.js | 22.x | Extension and MCP |

### Build Commands

```bash
# Build the runtime library
zig build

# Build and run all tests
zig build test

# Build a specific demo (each demo is standalone)
cd runtime/demos/<demo_name>
zig build

# Extension
cd extension
npm install
npm run build

# MCP server
cd mcp
npm install
npm run build
```

> **Note**: Demos are standalone — each `demos/<name>/` has its own `build.zig`. Do **not** register demos in the root `runtime/build.zig`.

### First-Time Setup

1. Deploy `dxcompiler.dll` + `dxil.dll` to `zig-out/bin/`
2. Deploy `D3D12Core.dll` to `zig-out/bin/D3D12/`
3. Verify with `zig build test` — all tests must pass before your first PR.

---

## Code Standards

### Language: C++ (engine)

- **Standard**: C++20, no compiler extensions.
- **Max file size**: 1 500 lines. Split proactively at ~1 000.
- **Single responsibility**: one file = one concern. If a file does two things, split it.
- **Naming**: `snake_case` for files (`gbuffer_pass.h`), `PascalCase` for types, `camelCase` for locals, `kConstantName` for `constexpr` constants.
- **No raw `new`/`malloc`** in engine code — use engine allocators.
- **No raw `assert()`** — use `UZ_CHECK` / `UZ_ENSURE`.
- **No `spdlog::` calls** — use `UZ_LOG(Category, Level, ...)`.
- **No hardcoded paths** — use `uz::GetExeDir()` / `uz::ResolveEngineResource()`.

### Documentation Comments

All exported types and functions **must** carry Rust-style doc comments:

```cpp
/// Resolves a VFS-relative path to an absolute filesystem path.
///
/// The path uses forward slashes (e.g., "shaders/pbr.uzshader").
/// Resolution is relative to the engine's resource root, not CWD.
///
/// # Returns
/// - Absolute path string on success.
/// - Empty string if the path escapes the resource root.
///
/// # Errors
/// Logs `Asset::Error` with the rejected path on failure.
std::string ResolveEngineResource(std::string_view vfs_path);
```

One-liner descriptions that say nothing useful ("Reads a file.") are rejected in review.

### Sections within files

Use separator comments to group logical sections:

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// GBuffer Pass — writes albedo/normal/roughness/metallic to MRT
// ─────────────────────────────────────────────────────────────────────────────
```

### Profiling and observability

- Wrap per-frame and per-draw functions with `UZ_PROFILE_SCOPE`.
- Log every system init/shutdown with timing.
- Expose subsystem health via `StatCounter`.

### Language: TypeScript (extension)

- Strict mode (`"strict": true`).
- Explicit return types on all exported functions.
- No `any` — use `unknown` with narrowing.
- Follow existing patterns in `extension/src/`.

---

## Testing Requirements

- **Minimum coverage**: 95 % for all new code.
- Tests live in `runtime/tests/` and **mirror** the source tree:
  - `runtime/engine/renderer/passes/gbuffer_pass.cpp` →
    `runtime/tests/engine/renderer/test_gbuffer_pass.cpp`
- **No monolithic test files** (`test_all.cpp` is forbidden).
- Tests must **clean up** any files they create — no generated artifacts committed.
- No placeholder tests (`EXPECT_TRUE(true)`), skipped tests, or weak assertions.
- Every error path must have a test case.

Run tests:

```bash
zig build test          # all runtime tests
npm test                # extension tests
```

All tests must be green before a PR is opened.

---

## Commit Conventions

UzEngine follows [Conventional Commits](https://www.conventionalcommits.org/).

```
<type>(<scope>): <short summary>

[optional body]

[optional footer]
```

**Types**: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`, `perf`, `build`

**Scopes** (use the subsystem name): `rhi`, `renderer`, `ecs`, `platform`, `shader`, `extension`, `mcp`, `build`, `docs`

**Examples**:

```
feat(renderer): add deferred lighting pass with clustered shading
fix(rhi): correct IID for IDxcUtils — was using CLSID value
refactor(ecs): split tick_system into update and fixed_update phases
test(platform): add file system UTF-8 path coverage
docs(renderer): document GBuffer layout and slot assignments
```

Rules:
- Summary is lowercase, no period at end.
- Body explains **why**, not what — the diff shows the what.
- Breaking changes: add `!` after type/scope (`feat(rhi)!: ...`) and a `BREAKING CHANGE:` footer.
- Keep commits focused — one logical change per commit.

---

## Acceptance Criteria (Mandatory)

Contributions are **only accepted** when all of the following are met. PRs that fail any criterion will be rejected without detailed review.

### 1. Follow the existing directory structure

Every subsystem in UzEngine has a well-defined home. New code **must** follow the same layout as existing code — never create parallel hierarchies.

| File type | Where it goes | Example |
|-----------|---------------|---------|
| ECS components | `engine/ecs/<category>/` | `engine/ecs/environment/height_fog_component.h` |
| Render passes | `engine/renderer/passes/<category>/` | `engine/renderer/passes/vfx/particle_render_pass.h` |
| Lua bindings | `engine/scripting/<category>/` | `engine/scripting/scene/lua_fog_api.cpp` |
| Asset formats/loaders | `engine/asset/formats/` | `engine/asset/formats/uzfx_loader.h` |
| Core subsystem logic | `engine/<subsystem>/` | `engine/vfx/core/gpu_particle_buffer.h` |

**How to check**: before creating a file, search for existing files of the same type (`grep -r "passes/" runtime/build.zig`, look at peer directories). If 15 render passes live under `renderer/passes/`, yours does too — not under your subsystem directory.

> **What went wrong (VFX)**: The VFX implementation placed render passes, Lua bindings, and asset formats under `engine/vfx/passes/`, `engine/vfx/scripting/`, and `engine/vfx/asset/` respectively, ignoring the established locations. This required a full reorganization of 23 files and 17 include-path updates after the fact.

### 2. Document everything

- Every `.h` and `.cpp` file **must** start with a `///` file header describing its purpose.
- Every public class, struct, method, enum, and macro **must** have `///` documentation.
- One-liner descriptions that restate the name ("Gets the value." on `GetValue()`) are rejected.
- Run `python scripts/audit_docs.py --severity V1,V2,V3` and fix all violations before opening your PR.

**How to check**: run the audit script. Zero violations = ready for review.

```bash
python scripts/audit_docs.py --severity V1,V2,V3,V5,V6
# Target: 0 violations on your changed files
```

> **What went wrong (VFX)**: 40 source files were committed without `///` file headers, requiring a separate doc-fix pass across the entire subsystem.

### 3. Respect file size limits

- **Max 1500 lines** per source file. Split proactively at ~1000 lines.
- Each file has **single responsibility**. If a file handles directional lights AND point lights AND spot lights extended properties, it is doing too much.

**How to check**: `wc -l runtime/engine/**/*.cpp | awk '$1 > 1500'` — must return nothing for your files.

> **What went wrong (VFX)**: `lua_light_api.cpp` grew to 1669 lines, requiring a post-hoc split into two files.

### 4. Tests are mandatory

- 95% coverage minimum on all new code.
- Tests mirror the source tree: `engine/vfx/core/foo.cpp` → `tests/engine/vfx/test_foo.cpp`.
- Every error path, edge case, and public API function needs a test.
- No placeholder assertions, no skipped tests.

### 5. Do not break existing functionality

- `zig build` must pass with zero errors and zero new warnings.
- `zig build test` — all existing tests must still pass.
- Include paths in `build.zig` must be updated if you move or add directories.
- If your change touches `build.zig`, verify that **all** demos still build: `cd runtime/demos/<each> && zig build`.

### 6. Update build.zig correctly

When adding a new directory:
1. Add it to the **include paths array** (alphabetically within its section).
2. Add the corresponding `addCppDir()` call (in the same relative position).
3. **Never** duplicate an entry that already exists — search first.

## Pre-PR Checklist

Copy this into your PR description and check every box:

```markdown
- [ ] Files placed in correct directories (matches existing patterns)
- [ ] `python scripts/audit_docs.py --severity V1,V2,V3,V5,V6` reports 0 violations on my files
- [ ] No file exceeds 1500 lines (`wc -l` check)
- [ ] `cd runtime && zig build` passes with zero errors
- [ ] `cd runtime && zig build test` — all tests pass
- [ ] New code has 95%+ test coverage
- [ ] `build.zig` include paths updated (no duplicates, no stale entries)
- [ ] No existing functionality broken (ran affected demos)
```

---

## Pull Request Process

1. **Fork** the repository and create a feature branch from `main`.
2. **Complete the pre-PR checklist** above — PRs without a checked-off checklist are closed.
3. **Keep PRs small** — large PRs stall. Prefer a series of focused PRs.
4. **Fill in the PR template** completely. Incomplete templates are closed.
5. **CI must be green** before requesting review. Do not ask reviewers to look at failing builds.
6. **One approving review** required from a maintainer before merge.
7. **Squash or rebase** — no merge commits. The maintainer merges with squash if the branch is messy.
8. **Delete your branch** after merge.

### What Gets Reviewed

- **Directory placement** — files in the wrong location are rejected before code is read.
- **Documentation** — undocumented public APIs are rejected.
- Correctness and test coverage.
- Adherence to code standards above (doc comments, engine APIs, file size).
- Performance implications (allocations, draw calls, frame time).
- Cross-platform compatibility (Windows/macOS/Linux where applicable).

---

## Specialist Areas

Large subsystems have elevated review standards. Tag the relevant area in your PR description so the right person is looped in:

| Area | Scope tag | Notes |
|------|-----------|-------|
| RHI (DX12/Vulkan/Metal) | `rhi` | Must pass barrier validation and all 102+ RHI tests |
| Render graph / passes | `renderer` | DAG correctness, resource lifetime, no hazards |
| ECS / game systems | `ecs` | EnTT patterns, tick-budget compliance |
| Shaders (HLSL) | `shader` | SM6.6 primary; SM5 fallback where noted |
| Platform layer | `platform` | Win32/Cocoa/POSIX — no leaks, correct cleanup |
| Build system | `build` | Zig build changes must not break any existing target |
| Extension / IPC | `extension` | N-API ABI must match Electron version in use |

---

## Questions

Open a [Discussion](../../discussions) for anything that doesn't fit an issue — design questions, "how do I…", or general feedback.
