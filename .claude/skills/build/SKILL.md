---
name: build
description: Build the UzEngine runtime. Use when compiling, checking for build errors, or verifying code changes compile. Supports zig (default), msvc, and clang.
---

Build the UzEngine runtime and report results.

## Default (Zig)

```bash
cd runtime && zig build 2>&1
```

## MSVC

```bash
cd runtime && cmake --preset msvc-debug 2>&1 && cmake --build build/msvc-debug 2>&1
```

## Clang

```bash
cd runtime && cmake --preset clang-debug 2>&1 && cmake --build build/clang-debug 2>&1
```

Use the compiler the user specifies. If not specified, use Zig.

**On success**: Report "Build passed (compiler)" with a one-line summary.

**On failure**: Show the FULL error output. Identify the failing file and line. Suggest a fix.
