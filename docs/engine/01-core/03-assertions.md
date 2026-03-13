# Core — Assertion System

> **Status**: Done
> **Source**: `runtime/engine/assertions/engine_assert.h`, `runtime/engine/assertions/engine_assert.cpp`
> **Depends on**: Nothing (standalone)

## Purpose

Assertions catch programming errors at the point of violation, not downstream where symptoms appear. The engine provides three assertion types covering the spectrum from "this must never happen" to "this failed but we can recover".

## Assertion Types

| Macro | Severity | Shipping Build | Returns | Use For |
|-------|----------|---------------|---------|---------|
| `UZ_CHECK(expr)` | Fatal | **Stripped** | — (aborts) | Invariants that must never be violated |
| `UZ_CHECKF(expr, fmt, ...)` | Fatal | **Stripped** | — (aborts) | Same, with formatted message |
| `UZ_ENSURE(expr)` | Non-fatal | **Stripped** | `bool` | Recoverable conditions — log and continue |
| `UZ_VERIFY(expr)` | Fatal | **Kept** | — (aborts) | Expressions with side effects that must always execute |

### Engine Comparison

| Engine | Fatal | Non-Fatal | Always-Evaluated |
|--------|-------|-----------|-----------------|
| **UE5** | `check(expr)` / `checkf(expr, fmt, ...)` | `ensure(expr)` / `ensureMsgf(expr, fmt, ...)` | `verify(expr)` |
| **Godot** | `CRASH_COND(expr)` | `ERR_FAIL_COND(expr)` + `ERR_FAIL_COND_V(expr, ret)` | — (no equivalent) |
| **UzEngine** | `UZ_CHECK(expr)` / `UZ_CHECKF(expr, fmt, ...)` | `UZ_ENSURE(expr)` | `UZ_VERIFY(expr)` |

We follow UE5's philosophy exactly: three tiers with the same semantics. Godot's `ERR_FAIL_*` macros serve a similar purpose to `UZ_ENSURE` but are more verbose and lack the "always execute" tier.

## API

### UZ_CHECK — Fatal Invariant

Use when the condition must always be true. A violation means the engine is in a corrupted state and continuing would cause worse problems.

```cpp
UZ_CHECK(device != nullptr);            // null device = corrupted RHI
UZ_CHECK(index < array.size());         // out of bounds = memory corruption
UZ_CHECKF(size > 0, "Buffer size must be positive, got %d", size);
```

**Behavior:**
- **Debug/Development**: Evaluates `expr`. If false → prints expression, file, line, stack trace → calls `std::abort()`
- **Shipping**: Entire macro is `((void)0)` — zero cost, zero codegen

### UZ_ENSURE — Recoverable Check

Use when failure is unexpected but the engine can continue safely. Returns `bool` so callers can handle the failure.

```cpp
if (!UZ_ENSURE(texture != nullptr)) {
    return;  // use fallback texture or skip rendering
}

if (!UZ_ENSURE(file.is_open())) {
    UseDefaultConfig();  // graceful degradation
}
```

**Behavior:**
- **Debug/Development**: Evaluates `expr`. If false → logs expression, file, line, stack trace to stderr → returns `false`
- **Shipping**: Evaluates to `!!(expr)` — the boolean result is preserved (for the `if` check), but no logging occurs

### UZ_VERIFY — Side-Effect Expression

Use when the expression itself has a necessary side effect (file close, handle release, mutex unlock) and you also want to assert the return value. Unlike `UZ_CHECK`, this is NEVER stripped.

```cpp
UZ_VERIFY(CloseHandle(handle));     // must close, must succeed
UZ_VERIFY(allocator.Free(ptr));     // must free, must succeed
```

**Behavior:**
- **All builds**: Always evaluates `expr`. If false → `FatalAssert` → `std::abort()`

## Failure Reporting

Both `AssertionFailed()` (non-fatal) and `FatalAssert()` (fatal) report:
- The failed expression as a string (e.g., `"device != nullptr"`)
- Source file and line number
- Optional formatted message
- Stack trace (when available)

`FatalAssert` integrates with the crash reporting system to generate a minidump before aborting, capturing the full engine state at the point of failure.

## When to Use Which

```
Is the condition always true in correct code?
├── YES → Is recovery possible?
│   ├── YES → UZ_ENSURE (log + continue)
│   └── NO  → Does the expression have side effects?
│       ├── YES → UZ_VERIFY (always execute, assert result)
│       └── NO  → UZ_CHECK (fatal, stripped in shipping)
└── NO (expected failure path) → Don't use assertions. Use normal error handling.
```

## Rules

1. **NEVER** use raw `assert()` — always use `UZ_CHECK`, `UZ_ENSURE`, or `UZ_VERIFY`
2. **NEVER** put side effects inside `UZ_CHECK` — it's stripped in shipping
3. **ALWAYS** use `UZ_VERIFY` when the expression must execute in all builds
4. **ALWAYS** use `UZ_ENSURE` for conditions that callers can handle (return, fallback, skip)
5. **ALWAYS** use `UZ_CHECKF` with context when the expression alone isn't diagnostic enough
