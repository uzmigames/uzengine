---
name: test
description: Run all UzEngine runtime unit tests. Use when running tests, checking test results, or verifying code changes pass tests. Supports zig (default), msvc, and clang.
---

Run all runtime unit tests and report results.

## Default (Zig)

```bash
cd runtime && zig build test 2>&1
```

## MSVC

```bash
cd runtime && cmake --preset msvc-debug 2>&1 && cmake --build build/msvc-debug 2>&1 && build/msvc-debug/Debug/test_runtime.exe 2>&1
```

## Clang

```bash
cd runtime && cmake --preset clang-debug 2>&1 && cmake --build build/clang-debug 2>&1 && build/clang-debug/test_runtime.exe 2>&1
```

Use the compiler the user specifies. If not specified, use Zig.

**Report**:
- Show the summary line (PASSED / FAILED / SKIPPED counts)
- If any test FAILED: show full failure output with test name, expected vs actual, and file:line
- If all passed: report "All tests passed" with count
- SKIPPED tests are acceptable — mention them but don't treat as failures
