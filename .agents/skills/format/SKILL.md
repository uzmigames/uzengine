---
name: format
description: Format UzEngine runtime C++ code with clang-format. Use when formatting code, checking style, or before committing.
disable-model-invocation: true
---

Format all C++ source files in the runtime using the project's `.clang-format` config.

```bash
cd runtime && find src core platform demos tools tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

After formatting, check if any files changed:

```bash
cd runtime && git diff --stat
```

**If files changed**: List the formatted files.
**If no changes**: Report "All files already formatted."
