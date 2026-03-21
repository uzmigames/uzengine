# Static Library Build Order (Runtime → Demo)

**Category**: architecture
**Tags**: build, static-library, demos, zig, stale-code

## Description

Demos link against runtime.lib (static library). When runtime C++ code changes, you MUST: (1) clean + rebuild runtime first, (2) clean + rebuild the demo. Zig's incremental build may not detect the lib changed, causing demos to link stale code. This is the #1 source of 'I changed the code but nothing happened' bugs.

## Example

# Full rebuild sequence:\ncd runtime && zig build --clean && zig build\ncd demos/<name> && zig build --clean && zig build

## When to Use

Every time runtime engine code is modified and you need to test via a demo. When debugging demo behavior that doesn't match expected code changes.

## When NOT to Use

When only changing demo-specific code (main.cpp in the demo dir) — demo-only rebuild is sufficient.
