---
name: fix-docs
description: Detect and fix C++ files with documentation that violates the Rust-style standard. Scans for missing ///, shallow docs, missing # sections, undocumented public items, and auto-fixes using parallel agents.
---

# Fix Documentation — Automated Audit & Agent Repair

Scan runtime C++ files for documentation violations using `scripts/audit_docs.py`,
then fix violations using parallel agents grouped by directory.

## The Standard (Reference)

Every public item MUST have `///` documentation following Rust-style conventions.
**Exemplar files** (gold standard):

- `runtime/engine/containers/handle_map.h` — Best per-method docs
- `runtime/engine/rhi/rhi_types.h` — Best enum + struct docs
- `runtime/engine/ecs/entity.h` — Concise but complete
- `runtime/engine/asset/vfs.h` — Best abstract interface docs
- `runtime/engine/logging/log.h` — Best macro documentation
- `runtime/engine/app/demo_application.h` — Best class hierarchy docs
- `runtime/engine/math/basis.h` — Best math function docs
- `runtime/engine/shader/shader_compiler.h` — Best factory/builder docs

## Violation Types (detected by audit_docs.py)

| Code | Description |
|------|-------------|
| V1 | Missing file header (first content line is not `///`) |
| V2 | Undocumented public struct/class |
| V3 | Undocumented public method (in `public:` section) |
| V4 | Shallow one-liner (missing `# Arguments`, `# Returns` sections) |
| V5 | Undocumented enum values (no `///<` trailing doc) |
| V6 | Undocumented macro (no `///` before `#define`) |
| V7 | Wrong comment style (`/* */` or `//!` instead of `///`) |
| V8 | Name-restating doc (adds no value beyond the identifier name) |
| V9 | Undocumented internal function >10 lines (.cpp only) |
| V10 | Magic numbers without constexpr name or comment (.cpp only) |

## Phase 1: Automated Audit (Python script — NO agents)

Run the audit script and capture the JSON report:

```bash
cd e:/UzEngine && python scripts/audit_docs.py --json --severity V1,V2,V3,V5,V6 2>/dev/null
```

This scans ALL `runtime/engine/**/*.h` and `runtime/engine/**/*.cpp` files automatically.
The script returns a JSON object with per-file violations including line numbers.

**Parse the JSON output** to build a list of files-to-fix grouped by directory.

For the full scan (all violation types including V4, V10):
```bash
cd e:/UzEngine && python scripts/audit_docs.py --json 2>/dev/null
```

For human-readable output with the worst offenders:
```bash
cd e:/UzEngine && python scripts/audit_docs.py --top 20 2>&1
```

**IMPORTANT**: Do NOT use agents for the audit phase. The script is faster, more accurate,
and deterministic. Agents previously hallucinated "0 violations" on a codebase with 1000+.

## Phase 2: Fix (up to 5 agents in parallel)

Based on the audit JSON, split files-to-fix into groups by directory:

- **Group A**: `runtime/engine/math/`, `runtime/engine/containers/`, `runtime/engine/memory/`
- **Group B**: `runtime/engine/renderer/` (all subdirs)
- **Group C**: `runtime/engine/rhi/`, `runtime/engine/shader/`, `runtime/engine/asset/`
- **Group D**: `runtime/engine/logging/`, `runtime/engine/app/`, `runtime/engine/platform/`,
  `runtime/engine/ecs/`, `runtime/engine/scene/`, `runtime/engine/job/`,
  `runtime/engine/event/`, `runtime/engine/property/`, `runtime/demos/`
- **Group E**: `docs/**/*.md`, `runtime/tests/**/*.cpp`

Only spawn agents for groups that have violations. Each agent receives:
1. The exact list of files + line numbers + violation types from the JSON
2. Instructions to read the file, read one exemplar, then fix

### Fix Rules

Each fix agent MUST:

1. **Read the file** before editing
2. **Read at least one exemplar file** from the same category to match the style
3. **Fix only violations** — do NOT rewrite docs that are already compliant
4. **Edit sequentially** — one file at a time, verify each edit
5. **Preserve existing good docs** — never downgrade quality

### Fix Templates by Violation Type

**V1 fix (missing file header):**
Add `///` block after `#pragma once` describing the module's purpose.

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
/// # Thread Safety                ← only on classes with resources
/// [Thread-safe / NOT thread-safe / Main-thread-only].
```

**V4 fix (shallow → complete):**
Add the missing `# Arguments`, `# Returns`, etc. sections. Read the function body
to understand what it actually does — don't just expand the existing one-liner.

**V5 fix (enum values):**
Add `///< [description]` on each enum value line.

**V6 fix (undocumented macro):**
Add `///` block before `#define` with `# Arguments` and `# Examples`.

**V8 fix (name-restating):**
Replace with description that adds information NOT in the name.

## Phase 3: Verify

1. Re-run the audit script on fixed files to confirm violations are resolved:
   ```bash
   cd e:/UzEngine && python scripts/audit_docs.py --severity V1,V2,V3,V5,V6 2>&1 | head -25
   ```
2. Run `cd e:/UzEngine/runtime && zig build 2>&1` — MUST compile
3. Report the before/after violation counts

## Useful Script Flags

```bash
# Scan specific subdirectory
python scripts/audit_docs.py runtime/engine/rhi

# Headers only
python scripts/audit_docs.py --headers-only

# Sources only
python scripts/audit_docs.py --sources-only

# Filter specific violation types
python scripts/audit_docs.py --severity V2,V3

# Machine-readable JSON
python scripts/audit_docs.py --json

# Show worst N files
python scripts/audit_docs.py --top 10
```