# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

This project uses @hivehub/rulebook standards. All code generation should follow the rules defined in AGENTS.md.

**Languages**: Not specified
**Coverage Threshold**: 95%

## ⚠️ CRITICAL: Reference Engine Research Before Implementation

**MANDATORY**: Before implementing any new system or solving a non-trivial problem, you MUST research how UE5, Godot, and Unity solve it — never rely solely on model knowledge.

### Why

Model knowledge can be outdated, incomplete, or subtly wrong about engine internals. Real engine source code is the ground truth. UE5, Godot, and Unity have battle-tested solutions for nearly every game engine problem. Using their patterns avoids reinventing wheels and produces production-quality results.

### What To Do

1. **Before any new feature/system**: Launch architect agents (unreal-architect, godot-architect, unity-architect) in parallel to analyze how each engine solves the problem.
2. **Before debugging a hard problem**: Check if reference engines have the same pattern and how they handle edge cases.
3. **Synthesize findings**: Use engine-architect to combine the 3 analyses into UzEngine-specific directives.
4. **Document the decision**: Save which patterns were adopted and why in the architecture docs.

### Rules

- **NEVER skip reference research** for new systems — always check at least UE5 + one other engine.
- **NEVER copy-paste** from reference engines — adapt patterns to UzEngine's identity (C++23, DX12-first, lean architecture).
- **For rendering**: UE5 (W:\UE_5.6_Source) is primary reference. Godot for lean alternatives.
- **For ECS/gameplay**: Unity DOTS + Godot Node system for comparison. UE5 for Actor/Component.
- **For scripting**: Godot GDExtension + Unity C# patterns adapted for LuaJIT.
- **Delegate cheaply**: Architect agents run on haiku — cost is minimal, value is high.

### Example Workflow

```
User: "Implement clustered shading"
1. Launch unreal-architect: "How does UE5 implement clustered/tiled shading?"
2. Launch godot-architect: "How does Godot 4 handle light culling?"
3. Launch unity-architect: "How does Unity URP/HDRP handle light clustering?"
4. Launch engine-architect: Synthesize → UzEngine directive
5. Implement based on synthesized directive
```

## ⚠️ CRITICAL: Visual Verification Over Unit Tests for Rendering

**MANDATORY**: Unit tests are NOT sufficient to validate rendering features. A test can pass while the visual result is completely wrong.

### Why

This is a rendering engine. Math tests and API tests can all pass green while the actual GPU output is a black screen, wrong colors, or broken pipeline. The only real validation is **seeing the pixels**.

### What To Do

1. **Every rendering feature MUST have a demo** — either create a new demo under `demos/` or modify an existing one.
2. **Run the demo with `--screenshot`** to capture the output.
3. **View the screenshot** (using the Read tool on the PNG) and verify the visual result is correct.
4. **Delete the screenshot** after verification — `rm` the PNG file to avoid accumulating garbage in the repo.
5. **Only after visual confirmation** is the feature considered implemented.

### Rules

- Unit tests are still useful for math utilities, data structures, and non-visual logic — keep writing them.
- But for any shader, render pass, post-process effect, or GPU pipeline change: **demo + screenshot = the real test**.
- Never mark a rendering task as complete based solely on unit tests passing.
- If the screenshot shows wrong output (black screen, wrong colors, artifacts), the feature is **NOT done** regardless of test results.

### Compare Grid for Multi-Backend/Multi-Config Testing

When testing the same feature across multiple RHI backends or configurations, use the **CompareGrid** system to compose all screenshots into a **single labeled grid image**. This saves context window tokens (1 image instead of N) and makes visual comparison easier.

#### How to Use

```bash
# 1. Capture individual screenshots (one per config)
demo.exe --backend dx12 --sm 6.6 --screenshot e:/UzEngine/.screenshots/dx12_sm66.png
demo.exe --backend dx11 --sm 5.0 --screenshot e:/UzEngine/.screenshots/dx11_sm50.png

# 2. Compose into a single grid image
demo.exe --compose-grid e:/UzEngine/.screenshots/compare.png --cell-size 384 \
  e:/UzEngine/.screenshots/dx12_sm66.png "DX12 SM6.6" "Frame 5" \
  e:/UzEngine/.screenshots/dx11_sm50.png "DX11 SM5.0" "Frame 5"

# 3. View the grid (single image with all configs labeled)
# Use the Read tool on e:/UzEngine/.screenshots/compare.png

# 4. Clean up all temporary PNGs
rm e:/UzEngine/.screenshots/*.png
```

#### Rules
- **Use CompareGrid** whenever comparing 2+ screenshots — never send multiple separate images to the LLM.
- **Always include labels**: RHI backend, shader model, frame number, and what's being tested.
- **Cell size 384x384** is the recommended default — large enough to see detail, small enough to fit 4+ in one grid.
- **Delete all PNGs** (individual + grid) after visual verification.
- **ALWAYS use `e:/UzEngine/.screenshots/` as the output directory** — never use `/tmp`, `AppData`, or any other path. This directory is in `.gitignore` and is always visible to the user.

#### Diff Mode — Detect Rendering Differences Between RHIs

Use `--diff` to compare two screenshots pixel-by-pixel and produce a 3-panel image:
**[Image A | Image B | Diff Heatmap]** with stats (% diff, PSNR).

```bash
# Compare DX12 vs DX11 rendering
demo.exe --diff e:/UzEngine/.screenshots/diff_output.png \
  e:/UzEngine/.screenshots/dx12.png "DX12 SM6.6" \
  e:/UzEngine/.screenshots/dx11.png "DX11 SM5.0" \
  --cell-size 384

# View the diff (heatmap shows: black=identical, red=different, magenta=size mismatch)
# Use the Read tool on e:/UzEngine/.screenshots/diff_output.png

# Clean up
rm e:/UzEngine/.screenshots/*.png
```

Heatmap color scale:
- **Black** = identical pixels
- **Red** (dark→bright) = small→medium difference
- **Yellow** = large difference
- **White** = maximum difference
- **Magenta** = size mismatch (one image larger)

The diff panel label shows: `X.X% diff, PSNR XX.X dB`
- **PSNR > 40 dB** = visually identical (minor float rounding)
- **PSNR 30-40 dB** = subtle differences (usually acceptable across RHIs)
- **PSNR < 30 dB** = visible differences (investigate)

#### Programmatic Usage (C++)
```cpp
#include "compare_grid.h"

// Grid mode
uz::CompareGrid grid;
grid.SetCellSize(384, 384);
grid.AddCellFromPNG("dx12.png", "DX12 SM6.6", "Frame 42");
grid.AddCellFromPNG("dx11.png", "DX11 SM5.0", "Frame 42");
grid.SaveToPNG("compare.png");

// Diff mode
uz::ImageDiff::DiffToPNG("dx12.png", "DX12", "dx11.png", "DX11", "diff.png");

// Just stats (no image output)
auto result = uz::ImageDiff::Compare(pixelsA, wA, hA, pixelsB, wB, hB);
// result.diffPercentage, result.psnr, result.identical, etc.
```

## ⚠️ CRITICAL: GPU Debugging — Never Guess, Always Diagnose

**MANDATORY**: When a rendering feature produces wrong output (black screen, wrong colors, white sky, artifacts), you MUST follow a systematic diagnostic process. NEVER iterate blindly by changing code and hoping it works.

### Why

Rendering bugs are GPU-side. The CPU code can be "correct" while the GPU sees completely different data. The only way to diagnose is to inspect what the GPU actually received and produced. Guessing wastes time and money.

### Diagnostic Workflow (in order)

1. **CPU Readback First** — Before anything else, read back the GPU resource to CPU and log actual values:
   - For LUTs/textures: read back a few diagnostic pixels and compare against known-good values
   - For constant buffers: log every field as uploaded to GPU
   - If values are wrong → fix the upload code. If values are correct → the shader logic is wrong.

2. **Use RenderDoc/PIX** — Capture a frame and inspect:
   - Is the correct shader bound?
   - Are CBV/SRV/UAV descriptors pointing to the right resources?
   - What are the actual constant buffer values as decoded by the tool?
   - What does the UAV output texture look like?
   - Step through the shader line-by-line if needed (compile with `-Zi -Od`)

3. **Isolate the Pass** — Render ONLY the resource under test to screen:
   - For a LUT: display it as a fullscreen quad
   - For a render pass: disable all other passes and view only its output
   - This eliminates all downstream bugs from consideration

4. **Validate Against Known-Good Values** — Every LUT/texture has mathematically predictable values:
   - Transmittance LUT: zenith from ground → T ≈ (0.97, 0.98, 0.99); top of atmosphere → T ≈ (1,1,1)
   - BRDF LUT: corner (0,0) ≈ (1,0); corner (1,1) ≈ (0.5, 0.5)
   - If values don't match → the integration/math is wrong

### Required Engine Infrastructure

1. **PIX Event Markers**: Every render/compute pass MUST have `PIXBeginEvent`/`PIXEndEvent` markers
2. **Debug Readback Path**: Every compute pass that generates a LUT MUST have a cvar-gated readback that logs diagnostic pixel values
3. **Debug Shader Compilation**: Must support `-Zi -Od` flags for shader step-through in RenderDoc/PIX
4. **DX12 Debug Layer**: Debug builds MUST enable `ID3D12Debug::EnableDebugLayer()` + GPU-Based Validation
5. **DRED Breadcrumbs**: Enable `SetAutoBreadcrumbsEnablement(FORCED_ON)` for device-removed crash diagnosis

### Atmosphere-Specific Validation

| Check | Expected | If Wrong |
|-------|----------|----------|
| Units | All km (bottomRadius=6360, topRadius=6420) | Divide by 1000 if in meters |
| Rayleigh β | (5.802e-3, 13.558e-3, 33.1e-3) km^-1 | Check unit conversion |
| Mie β | 3.996e-3 km^-1 | Check unit conversion |
| Scale heights | Rayleigh=8km, Mie=1.2km | Check CB upload |
| Integration direction | Toward top of atmosphere | Reverse = black LUT |
| UV mapping | Bruneton 2017 non-linear | Linear cosine = banding |
| ClampCosine | clamp(mu, -1, 1) | Missing = NaN artifacts |

### Skill Reference

Use `/gpu-debug` for the full diagnostic workflow with step-by-step instructions.

## ⚠️ CRITICAL: Demo Build Order (Static Library Dependency)

**MANDATORY**: Demos link against a **static library** built from the runtime. You MUST follow this exact build order or demos will link against stale code.

### Why

Each demo under `demos/` has its own `build.zig` that depends on the runtime static lib. If you change runtime code and only rebuild the demo, it will still link the old `.lib` — your changes won't appear. This is a common source of "I changed the code but nothing happened" bugs.

### Build Steps (Current Architecture)

```bash
# 1. Clean ALL caches (runtime + demo)
cd runtime && zig build --clean
cd demos/<demo_name> && zig build --clean

# 2. Build the runtime static lib FIRST
cd runtime && zig build

# 3. THEN build the demo
cd demos/<demo_name> && zig build
```

### Rules

1. **ALWAYS clean + rebuild runtime BEFORE rebuilding any demo** when runtime code changed.
2. **ALWAYS clean the demo cache too** — Zig's incremental build may not detect the lib changed.
3. **Build order is strict**: runtime → demo. Never reverse.
4. **If a demo shows stale behavior** after code changes, the first thing to check is whether you rebuilt the runtime lib.

### Future Migration

The current static-lib architecture is temporary. The plan is to migrate demos to use **Lua scripting** (LuaJIT) so they become lightweight scripts that load the engine as a runtime — no recompilation needed. Until that migration is complete, follow the manual build order above.

## ⚠️ CRITICAL: NEVER Manually Clone/Download Thirdparty Libraries

**ABSOLUTELY FORBIDDEN**: No agent or conversation may run `git clone` targeting `runtime/thirdparty/`, manually download files into `runtime/thirdparty/`, or use `curl`/`wget` to fetch thirdparty sources. **No exceptions.**

### Why

All thirdparty dependencies are managed by `scripts/setup.ps1`. Manual clones create inconsistent state, wrong versions, missing patches, and break the build. The setup script handles cloning, cleanup, patching, and verification — manual intervention bypasses all of that.

### What To Do Instead

1. **Edit `scripts/setup.ps1`** — update the version/tag/URL in the script
2. **Delete the target directory** — `rm -rf runtime/thirdparty/<lib>` to force re-download
3. **Run the setup script** — `powershell -ExecutionPolicy Bypass -File scripts/setup.ps1`
4. **Verify the build** — `cd runtime && zig build`

### Rules

- NEVER run `git clone` into `runtime/thirdparty/`
- NEVER use `curl`/`wget`/`Download-File` to manually place files in thirdparty
- ALWAYS update `scripts/setup.ps1` first, then run it
- If setup.ps1 fails, fix the SCRIPT, don't work around it manually

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

## ⚠️ CRITICAL: Unit & Coordinate Conventions

**MANDATORY**: All code must follow these conventions. Mismatched units cause visual bugs that are hard to diagnose.

### Coordinate System
- **Z-up, X-forward, left-handed** — same as UE5
- `Vec3::Forward() = (1, 0, 0)`, `Vec3::Up() = (0, 0, 1)`, `Vec3::Right() = (0, 1, 0)`
- NEVER use Y-up when porting code from external sources (DirectXMath defaults, tutorials, etc.)

### Units
| Category | Convention | Notes |
|----------|-----------|-------|
| **Distances** | **Meters** | UE5 uses centimeters — divide by 100 when porting |
| **Angles (ECS components)** | **Degrees** | SpotLight cones, FOV, sun disk angle — all in degrees |
| **Angles (GPU/shaders)** | **Radians** | Convert deg→rad in proxy sync / CB upload |
| **Atmosphere distances** | **Kilometers** | SkyAtmosphereComponent follows UE5 km convention |
| **Color space** | **Linear RGB** | sRGB conversion only at final tonemap shader output |
| **Temperature** | **Kelvin** | Blackbody: 3000K=warm, 6500K=daylight, 10000K=cool |
| **Time** | **Seconds** | Delta time, total time, animation time |

### Rules
- Component stores degrees → `OnProxyCreated()` converts to radians for the proxy
- Shaders NEVER convert units — they receive pre-converted values from CBs
- When porting UE5 default values: distances ÷ 100, angles keep as degrees

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
| **Review/organize docs** | docs-reviewer | haiku | Naming, indexing, template compliance |

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

## ⚠️ CRITICAL: Documentation Standards

**MANDATORY**: All documentation in `/docs/` MUST follow the template at `/docs/TEMPLATE.md`.

### Naming Convention

- **kebab-case ONLY** for filenames: `ue5-rhi-analysis.md` NOT `UE5_RHI_ANALYSIS.md`
- No UPPERCASE filenames, no snake_case, no spaces, no mixed case
- Numbered prefixes OK for ordering: `01-overview.md`, `02-details.md`

### File Rules

1. **Max 800 lines** per document — split by subtopic if longer
2. **Frontmatter is MANDATORY** — every doc needs the `<!-- doc-meta ... -->` block from TEMPLATE.md
3. **No `.txt` files** — all docs are `.md` only
4. **No orphan docs** — every file must be listed in its directory's `README.md`
5. **Code blocks need language tags** — ` ```cpp `, ` ```hlsl `, ` ```bash `, etc.

### Analysis Directory Structure

Analysis docs MUST be organized by theme subdirectories:

```
docs/analysis/
  rendering/   ← shaders, post-processing, lighting, PBR
  rhi/         ← RHI, backend abstraction, GPU API, bindless
  ecs/         ← ECS, scene, components
  platform/    ← platform abstraction, window, input
  general/     ← cross-cutting comparisons, gap analysis
```

### When Creating Documentation

1. **Read `/docs/TEMPLATE.md` first** — copy the structure
2. **Use kebab-case filename** — derive from the title
3. **Add frontmatter** — fill in all required fields
4. **Update the directory's README.md** — add an entry for the new doc
5. **After bulk doc creation, run docs-reviewer agent** to audit compliance

## ⚠️ CRITICAL: Lua API Parity — Every C++ Feature Gets a Lua Binding

**MANDATORY**: Every engine feature implemented in C++ MUST also be exposed to Lua. Both languages must have access to the same capabilities. No exceptions.

### Why

UzEngine is a scripted engine — Lua is the primary authoring language for gameplay. If a feature exists in C++ but not in Lua, it's invisible to game developers. Feature parity between C++ and Lua is a core design principle: **if C++ can do it, Lua must be able to do it too**.

### What To Do

1. **After implementing any C++ system/feature**, immediately create the corresponding Lua API bindings.
2. **Lua API files go in the themed subdirectories**:
   - `engine/scripting/api/scene/` — entities, transforms, lights, meshes, materials
   - `engine/scripting/api/core/` — camera, input, time, math, physics, audio
   - `engine/scripting/api/renderer/` — render path, debug modes, stats, window
3. **Registration pattern**: Each API module has a `LuaRegister*API(lua_State* L)` function called from `LuaVM::Init()`.
4. **Follow existing patterns**: Extend `uz.*` tables (uz.scene, uz.camera, uz.input, uz.renderer, uz.math, uz.time, uz.window). Never create new top-level globals.
5. **Test both sides**: Unit tests for C++ logic + unit tests for Lua bindings.

### Checklist (apply to every new feature)

```
☐ C++ implementation complete and tested
☐ Lua binding created in correct api/ subdirectory
☐ Registration function added to LuaVM::Init()
☐ Lua unit tests written (test via embedded Lua scripts)
☐ Lua API documented in docs/engine/15-scripting/06-lua-api.md
```

### Examples

```
Feature: "Add point light shadows"
  ✅ C++ ShadowPass renders point light shadow maps
  ✅ uz.scene.set_light_shadow(entity, enabled) — Lua can toggle shadows per light
  ✅ uz.scene.set_shadow_resolution(entity, res) — Lua can control shadow quality

Feature: "Add physics raycasts"
  ✅ C++ PhysicsWorld::Raycast() implementation
  ✅ uz.physics.raycast(ox,oy,oz, dx,dy,dz, max_dist) → hit, px,py,pz, nx,ny,nz, entity
  ✅ uz.physics.overlap_sphere(x,y,z, radius) → {entity, ...}
```

### Rules

- **NEVER mark a C++ feature as complete** without its Lua binding
- **NEVER create Lua-only features** — always implement in C++ first, then bind
- **Delegate Lua binding work** to `lua-scripting-engineer` agent (sonnet) after C++ is done
- **API naming**: Lua uses `snake_case` (uz.scene.get_position), C++ uses `PascalCase` (GetPosition)

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
