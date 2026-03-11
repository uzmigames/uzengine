---
name: test
description: Run all UzEngine runtime unit tests via Zig. Use when running tests, checking test results, or verifying code changes pass tests.
allowed-tools: Bash
---

Run all runtime unit tests and report results.

```bash
cd runtime && zig build test 2>&1
```

**Report**:
- Show the summary line (PASSED / FAILED / SKIPPED counts)
- If any test FAILED: show full failure output with test name, expected vs actual, and file:line
- If all passed: report "All tests passed" with count
- SKIPPED tests are acceptable — mention them but don't treat as failures
