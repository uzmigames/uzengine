---
name: build
description: Build the UzEngine runtime with Zig. Use when compiling, checking for build errors, or verifying code changes compile.
allowed-tools: Bash
---

Build the UzEngine runtime and report results.

```bash
cd runtime && zig build 2>&1
```

**On success**: Report "Build passed" with a one-line summary.

**On failure**: Show the FULL error output. Identify the failing file and line. Suggest a fix.
