---
name: fix-docs
description: Detect and fix C++ files with documentation that violates the Rust-style standard. Scans for missing ///, shallow docs, missing # sections, undocumented public items, and auto-fixes using parallel agents.
---

# Fix Documentation — Deep Quality Audit & Repair

Scan ALL runtime C++ files for documentation that does NOT meet the project's Rust-style
standard (defined in CLAUDE.md). **This is NOT a simple grep for `///`** — you must READ files
and evaluate if docs are COMPLETE and CORRECT. Detect specific violations, then fix them.

## The Standard (Reference)

Every public item MUST have `///` documentation following this structure. These are the
**exemplar files** — use them as the gold standard when judging quality:

- `runtime/engine/containers/handle_map.h` — Best per-method docs
- `runtime/engine/rhi/rhi_types.h` — Best enum + struct docs
- `runtime/engine/ecs/entity.h` — Concise but complete
- `runtime/engine/asset/vfs.h` — Best abstract interface docs
- `runtime/engine/logging/log.h` — Best macro documentation
- `runtime/engine/app/demo_application.h` — Best class hierarchy docs
- `runtime/engine/math/basis.h` — Best math function docs
- `runtime/engine/shader/shader_compiler.h` — Best factory/builder docs

## Phase 1: Deep Quality Audit (1 agent, thorough read-through)

### Agent 1 — Deep Header Quality Auditor (`runtime/**/*.h`)

**CRITICAL**: You must READ each file completely. Don't grep for `///` — actually evaluate if docs meet the standard.

For each `.h` file, detect these **specific violation patterns**:

#### V1: Missing File Header
```cpp
// ❌ VIOLATION — file starts with #pragma once, no module doc
#pragma once
#include <cstdint>

// ✅ COMPLIANT — /// block before or after #pragma once
#pragma once
/// UzEngine Logging System — Category-based structured logging.
/// ...
```
**Detection**: First non-blank, non-`#pragma once` line is NOT `///`.

#### V2: Undocumented Public Struct/Class
```cpp
// ❌ VIOLATION — no /// before class
class MyRenderer {

// ✅ COMPLIANT
/// Renders the scene using deferred shading pipeline.
class MyRenderer {
```
**Detection**: `class X` or `struct X` preceded by a line that is NOT `///`.
Exclude: forward declarations (`class X;`), `private`/`protected` nested types.

#### V3: Undocumented Public Method
```cpp
// ❌ VIOLATION — public method with no ///
public:
    void Init(IRHIDevice* device);

// ✅ COMPLIANT
public:
    /// Initializes the renderer with the given device.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this renderer.
    ///
    /// # Returns
    /// `true` on success, `false` if device is null or init fails.
    bool Init(IRHIDevice* device);
```
**Detection**: Line declaring a function (not `= delete`, `= default`, destructor)
inside a `public:` section, where the preceding line is NOT `///`.

#### V4: Shallow One-Liner (Missing # Sections)
```cpp
// ❌ VIOLATION — has summary but no # Arguments / # Returns
/// Initializes the device.
bool Init(IRHIDevice* device, const Config& cfg);

// ❌ VIOLATION — name-restating (adds no info beyond name)
/// The width.
uint32_t m_width;

/// Group name (e.g., "Render", "Physics")
StatGroup* group;

// ✅ COMPLIANT — has sections AND elaboration
/// Initializes the device with the given configuration.
///
/// # Arguments
/// * `device` — RHI device, must outlive this object.
/// * `cfg` — Configuration parameters (resolution, vsync, etc.).
///
/// # Returns
/// `true` on success, `false` on initialization failure (error logged).
bool Init(IRHIDevice* device, const Config& cfg);

/// Current window/swapchain width in pixels. Updated on resize events.
uint32_t m_width;

/// Parent group for UI filtering. Set by UZ_DEFINE_STAT; must never be nullptr.
StatGroup* group;
```
**Detection rules for when # sections are REQUIRED:**
- **`# Arguments`** — REQUIRED when function has ≥1 parameter (excluding `this`)
- **`# Returns`** — REQUIRED when return type is NOT `void`
- **`# Thread Safety`** — REQUIRED when method/class manages GPU/OS resources, uses atomics, or is called from multiple threads
- **`# Side Effects`** — REQUIRED if method logs, writes files, mutates state, or calls external APIs
- **`# Examples`** — REQUIRED on public classes/structs (not on every method, but on the type itself)
- **Elaboration** — REQUIRED beyond one-liner on most types. Don't just restate the identifier name.

**How to detect shallow**: Read the doc and ask: "Does this add value beyond the name?"
- "/// The width" → Name-restating (violation)
- "/// Current window width in pixels" → Adds units and context ✅
- "/// Initializes device" + function has params → Missing # Arguments section (violation)
- "/// Resets counter" on a method that's called from multiple threads → Missing # Thread Safety (violation)

#### V5: Undocumented Enum Values
```cpp
// ❌ VIOLATION — enum values have no ///
enum class Format {
    Unknown,
    RGBA8,
    RGBA16F,
};

// ✅ COMPLIANT
enum class Format {
    Unknown = 0,  ///< Invalid/uninitialized format.
    RGBA8,        ///< 4-channel, 8-bit unsigned normalized [0,1].
    RGBA16F,      ///< 4-channel, 16-bit half-precision float.
};
```
**Detection**: Enum value line without `///<` trailing doc.

#### V6: Undocumented Macro
```cpp
// ❌ VIOLATION
#define UZ_PROFILE_SCOPE(name) ...

// ✅ COMPLIANT
/// Instruments a scope for CPU profiling via Tracy.
///
/// # Arguments
/// * `name` — Static string literal identifying this scope in the profiler.
///
/// # Examples
/// ```cpp
/// void Render() { UZ_PROFILE_SCOPE("Render"); /* ... */ }
/// ```
#define UZ_PROFILE_SCOPE(name) ...
```
**Detection**: `#define` line where previous line is NOT `///`.
Exclude: include guards, simple constants.

#### V7: Wrong Comment Style
```cpp
// ❌ VIOLATION — block comment instead of ///
/* Initializes the renderer. */
void Init();

// ❌ VIOLATION — //! instead of ///
//! Module documentation
```
**Detection**: Lines matching `^/\*` or `^//!` before public declarations.

#### V8: Name-Restating Doc
```cpp
// ❌ VIOLATION — just restates the name
/// The width.
uint32_t m_width;

/// Get the device.
IRHIDevice* GetDevice();

// ✅ COMPLIANT — adds context
/// Current window/swapchain width in pixels. Updated on resize.
uint32_t m_width;

/// Returns the RHI device. Valid after `Init()` has been called.
[[nodiscard]] IRHIDevice* GetDevice() const;
```
**Detection**: Doc comment where the text (lowercased, stripped of articles) is
essentially the function/variable name split by camelCase/underscores.
Heuristic: if removing "the/a/an/get/set/is" from the doc leaves only the identifier name → VIOLATION.

### Agent 2 — Source Violation Scanner (`runtime/**/*.cpp`)

1. **V1 (File Header)** — Same as headers. First content line must be `///`.
2. **V9: Undocumented Internal Function >10 lines** — Static/anonymous-namespace functions
   with body >10 lines that have no `///` doc.
3. **V10: Magic Numbers** — Numeric literals in code that aren't `0`, `1`, `-1`, `nullptr`,
   `true`, `false` and don't have a `constexpr` name or inline comment explaining them.

### Agent 3 — Docs Audit (`docs/**/*.md`)

1. **Completeness** — Every directory under `runtime/engine/` should have docs in `/docs/`
2. **Broken links** — `[text](path)` where path doesn't exist
3. **Stale API refs** — Function/class names in docs that don't exist in code anymore

## Phase 2: Fix (up to 5 agents in parallel)

Split files-to-fix into groups by directory:

- **Group A**: `runtime/engine/math/`, `runtime/engine/containers/`, `runtime/engine/memory/`
- **Group B**: `runtime/engine/renderer/` (all subdirs)
- **Group C**: `runtime/engine/rhi/`, `runtime/engine/shader/`, `runtime/engine/asset/`
- **Group D**: `runtime/engine/logging/`, `runtime/engine/app/`, `runtime/engine/platform/`,
  `runtime/engine/ecs/`, `runtime/engine/scene/`, `runtime/engine/job/`,
  `runtime/engine/event/`, `runtime/engine/property/`, `runtime/demos/`
- **Group E**: `docs/**/*.md`, `runtime/tests/**/*.cpp`

### Fix Rules

Each fix agent MUST:

1. **Read the file** before editing
2. **Read at least one exemplar file** from the same category to match the style
3. **Fix only violations** — do NOT rewrite docs that are already compliant
4. **Edit sequentially** — one file at a time, verify each edit
5. **Preserve existing good docs** — never downgrade quality

### Fix Templates by Violation Type

**V2/V3 fix (missing doc on class/method):**
```cpp
/// [One-line summary — WHAT it does, not HOW].
///
/// [Optional 1-2 line elaboration with design context].
///
/// # Arguments                    ← only if function has params
/// * `param1` — [description].
/// * `param2` — [description].
///
/// # Returns                      ← only if non-void
/// [Success case]. [Failure case].
///
/// # Side Effects                 ← only if logs, writes files, mutates global state
/// Logs `[Category]::Error` on failure.
///
/// # Thread Safety                ← only on classes with resources
/// [Thread-safe / NOT thread-safe / Main-thread-only].
///
/// # Examples                     ← required on public classes, optional on methods
/// ```cpp
/// [2-5 lines showing typical usage]
/// ```
```

**V4 fix (shallow → complete):**
Add the missing `# Arguments`, `# Returns`, etc. sections. Read the function body
to understand what it actually does — don't just expand the existing one-liner.

**V5 fix (enum values):**
Add `///< [description]` on each enum value line. Include:
- Value meaning in context
- Valid ranges if numeric
- Mapping to GPU/API concepts if relevant

**V8 fix (name-restating):**
Replace with description that adds information NOT in the name:
- For getters: state when valid, what null/invalid means
- For members: units, valid range, update frequency, who owns it

## Phase 3: Verify

1. Run `cd runtime && zig build 2>&1` — MUST compile
2. Re-run violation scanner on fixed files to confirm 0 violations
3. Report:

```
=== Documentation Fix Report ===

Files scanned: X headers, Y sources, Z docs
Violations found: N total
  V1 (missing file header):     A
  V2 (undocumented class):      B
  V3 (undocumented method):     C
  V4 (shallow/missing sections): D
  V5 (undocumented enum values): E
  V6 (undocumented macro):      F
  V7 (wrong comment style):     G
  V8 (name-restating):          H
  V9 (undocumented internal fn): I
  V10 (magic numbers):          J

Files fixed: N
  - path/to/file.h (V2 x3, V4 x5 → fixed)
  - path/to/file.cpp (V1, V9 x2 → fixed)
  ...

Files already compliant: M (skipped)
Build: PASSED / FAILED
```
