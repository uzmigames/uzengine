# Demo Build Order (Static Library Dependency)

Demos link against a static library built from the runtime. Build order is strict.

## Steps

```bash
# 1. Clean ALL caches
cd runtime && zig build --clean
cd demos/<demo_name> && zig build --clean

# 2. Build runtime FIRST
cd runtime && zig build

# 3. THEN build demo
cd demos/<demo_name> && zig build
```

## Rules

1. ALWAYS clean + rebuild runtime BEFORE rebuilding any demo when runtime code changed
2. ALWAYS clean the demo cache too — Zig incremental build may not detect lib changes
3. Build order is strict: runtime → demo. Never reverse.
4. If demo shows stale behavior, first check if runtime lib was rebuilt
