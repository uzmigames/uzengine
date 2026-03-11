---
name: fix-docs
description: Find and fix poor/missing documentation across all runtime C++ files and /docs/. Scans headers, sources, and user docs in parallel using multi-agent dispatch.
---

# Fix Documentation — Full Audit & Repair

Scan ALL runtime C++ source files and `/docs/` for poor, missing, or outdated documentation. Fix everything found using parallel agents for speed.

## Phase 1: Audit (run in parallel)

Launch **3 agents in parallel** to scan different areas:

### Agent 1 — Header Audit (`runtime/**/*.h`)

Scan every `.h` file under `runtime/`. For each file, check:

1. **File-level doc comment** — MUST have a `///` block before `#pragma once` or first `#include` explaining what the file provides
2. **Every public struct/class** — MUST have `///` doc with:
   - One-line summary
   - `# Memory Layout` (size, alignment, HLSL/shader compatibility if relevant)
   - `# Thread Safety` (thread-safe? main-thread-only? value type?)
   - `# Examples` (at least one usage example in ```cpp block)
3. **Every public function/method** — MUST have `///` doc with:
   - One-line summary of what it does
   - `# Arguments` — describe each parameter (name, type, constraints)
   - `# Returns` — what is returned, edge cases (nullptr? empty?)
   - `# Panics` / `# Safety` — if it can crash or has preconditions
4. **Every public enum** — MUST have `///` on the enum itself AND on each enumerator
5. **Every macro** — MUST have `///` explaining usage, parameters, and expansion behavior
6. **Every constant** (`constexpr`, `static const`) — MUST have `///` explaining its value and purpose

**Score each file**: `(lines with ///) / (total lines)`. Flag any file below 15% as "poor".
Also flag files where doc comments exist but are **shallow** (just restating the name, e.g., `/// The width` on a field named `width`).

### Agent 2 — Source Audit (`runtime/**/*.cpp`)

Scan every `.cpp` file under `runtime/`. For each file, check:

1. **File-level doc header** — MUST have a `///` block at line 1 explaining:
   - What this file implements
   - Key design decisions or algorithms used
   - Thread safety model (if relevant)
2. **Non-trivial static/internal functions** — MUST have `///` doc if longer than 10 lines
3. **Complex logic blocks** — any block >20 lines should have a `// Why:` comment or be refactored into a named function

### Agent 3 — User Docs Audit (`docs/**/*.md`)

Scan every `.md` file under `docs/`. Cross-reference with actual code:

1. **Completeness** — every major system in `runtime/core/` should have corresponding docs in `/docs/user/`
2. **Accuracy** — check that documented APIs, types, and function signatures match actual code
3. **Broken links** — find any `[text](path)` links pointing to files that don't exist
4. **Missing guides** — identify new systems added to runtime that have NO documentation in `/docs/`
5. **Outdated content** — find docs referencing old file names, removed functions, or renamed types

## Phase 2: Fix (run in parallel)

Based on audit results, launch **up to 5 agents in parallel**, each handling a different module group:

### Fix Agent Groups (split by directory)

- **Group A**: `runtime/core/math/`, `runtime/core/containers/`, `runtime/core/memory/`
- **Group B**: `runtime/core/rhi/`, `runtime/core/rhi/dx12/`
- **Group C**: `runtime/core/shader/`, `runtime/core/asset/`
- **Group D**: `runtime/core/logging/`, `runtime/core/assertions/`, `runtime/core/config/`, `runtime/core/paths/`, `runtime/core/platform/`, `runtime/tools/`, `runtime/demos/`
- **Group E**: `docs/**/*.md` — update/create missing user documentation

Each fix agent MUST follow these rules:

### Documentation Standard (Rust-style — NO EXCEPTIONS)

```cpp
/// Thin wrapper over IDXGISwapChain4 with FLIP_DISCARD presentation model.
///
/// Uses triple-buffering by default. Supports `DXGI_PRESENT_ALLOW_TEARING`
/// for variable refresh rate (VRR/FreeSync/G-Sync) when vsync is disabled.
///
/// # Lifecycle
/// 1. Construct -> `Init(device, desc)` -> use in render loop -> destructor releases
/// 2. `Resize()` releases and recreates back buffers (invalidates existing handles)
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the main/render thread only.
///
/// # Memory Layout
/// Contains COM pointers (ComPtr<IDXGISwapChain4>), back buffer resources, and RTV handles.
/// Total size depends on kMaxBackBuffers (default 3).
///
/// # Examples
/// ```cpp
/// DX12SwapChain swapchain;
/// swapchain.Init(&device, desc);
/// auto backbuffer = swapchain.GetCurrentBackBuffer();
/// swapchain.Present(vsync);
/// ```
class DX12SwapChain final : public IRHISwapChain {
```

**For functions:**

```cpp
/// Compiles HLSL source to DXGI bytecode using DXC.
///
/// Invokes the DirectX Shader Compiler with the specified target profile
/// and entry point. Includes are resolved via the shader VFS.
///
/// # Arguments
/// * `source` — HLSL source code as UTF-8 string
/// * `entryPoint` — Shader entry function name (e.g., "VSMain", "PSMain")
/// * `profile` — Target shader model (e.g., "vs_6_6", "ps_6_6")
/// * `defines` — Preprocessor defines to pass to DXC
///
/// # Returns
/// `ShaderBlob` containing compiled DXGI bytecode on success.
/// Empty blob with error message on compilation failure.
///
/// # Panics
/// Asserts if DXC is not initialized (call `Init()` first).
///
/// # Examples
/// ```cpp
/// auto blob = compiler.Compile(hlslSource, "VSMain", "vs_6_6", {});
/// if (blob.IsValid()) { /* use bytecode */ }
/// ```
ShaderBlob Compile(std::string_view source, const char* entryPoint,
                   const char* profile, std::span<const ShaderDefine> defines);
```

### What counts as "bad" documentation (FIX ALL OF THESE):

1. **Missing entirely** — no `///` at all on public items
2. **Name-restating** — `/// The width` on `uint32_t m_width` (USELESS — explain what width means in context)
3. **No sections** — has a summary but missing `# Arguments`, `# Returns`, etc.
4. **Wrong info** — doc says one thing, code does another
5. **Shallow** — `/// Initialize the device` on a 50-line Init() function (explain WHAT it initializes, HOW, and failure modes)
6. **Missing examples** — public types and key functions MUST have `# Examples`
7. **No file header** — every `.h` and `.cpp` MUST have a `///` block at the top explaining the file

### /docs/ Update Rules:

1. Every system in `runtime/core/` MUST have a corresponding page in `docs/user/`
2. New systems without docs get a new guide/reference page created
3. Existing docs with wrong API signatures or removed types get updated
4. Index pages (`docs/user/index.md`, `docs/user/reference/*/index.md`) must link all pages
5. Follow existing doc style — see `docs/user/guides/rendering/shader-pipeline.md` as reference

## Phase 3: Verify

After all fix agents complete:

1. Run `cd runtime && zig build 2>&1` — documentation changes must not break compilation
2. Report summary: files audited, files fixed, files created in /docs/, any remaining issues

## Output Format

```
=== Documentation Audit & Fix Report ===

HEADERS: X/Y files audited, Z fixed
SOURCES: X/Y files audited, Z fixed
DOCS:    X pages audited, Z updated, W created

Files fixed:
  - runtime/core/math/engine_math.h (added 23 doc comments)
  - runtime/core/rhi/rhi.h (rewrote 5 shallow docs)
  - docs/user/reference/core/memory.md (CREATED)
  ...

Build: PASSED / FAILED
```
