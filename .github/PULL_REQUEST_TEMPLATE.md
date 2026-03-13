## Summary

<!-- One paragraph: what does this PR do and why? -->

## Subsystem(s) Affected

<!-- Check all that apply -->
- [ ] RHI (DX12 / Vulkan / Metal)
- [ ] Renderer (passes, shaders, lighting)
- [ ] ECS / Tick system
- [ ] Platform layer
- [ ] Build system (Zig)
- [ ] VSCode Extension
- [ ] MCP server
- [ ] Math / Core
- [ ] Docs only

## Type of Change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor (no behavior change)
- [ ] Performance improvement
- [ ] Test coverage
- [ ] Documentation
- [ ] Build / CI

## Breaking Changes

<!-- Does this change any public API, file format, or build interface? -->
- [ ] No breaking changes
- [ ] Yes — describe below:

<!-- breaking change description -->

---

## Testing

<!-- How was this tested? Which test files cover the new code? -->

**Test files added / modified:**

```
runtime/tests/…
```

**Manual verification steps (if applicable):**

1.
2.

---

## Pre-Merge Checklist

- [ ] `zig build test` passes locally — zero failures.
- [ ] New code has `///` doc comments on all exported types and functions.
- [ ] No file exceeds 1 500 lines; new files have a single clear responsibility.
- [ ] Engine APIs used throughout (`UZ_LOG`, `UZ_CHECK`, engine allocators — no raw `spdlog::`/`assert()`/`new`).
- [ ] No hardcoded paths — uses `uz::GetExeDir()` / `uz::ResolveEngineResource()`.
- [ ] Tests clean up any files they create (no generated artifacts in repo).
- [ ] CI is green (all workflows passing).
- [ ] PR description is complete — I have not left placeholder text above.
