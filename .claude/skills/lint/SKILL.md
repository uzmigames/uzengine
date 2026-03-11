---
name: lint
description: Lint the UzEngine runtime C++ code with clang-tidy via Zig build. Use when checking code quality, style violations, or warnings.
allowed-tools: Bash
---

Lint the runtime code. The Zig build system runs clang-tidy with warnings-as-errors during compilation.

```bash
cd runtime && zig build 2>&1
```

Clang-tidy is integrated into the build — any warnings are treated as errors. If the build passes with no warnings, lint is clean.

**On success**: Report "Lint passed — zero warnings."

**On failure**: Show all clang-tidy warnings/errors with file:line. Suggest fixes for each violation.
