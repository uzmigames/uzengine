# Architecture Overview — Reference Sources & Clean-Room Rules

> **Status**: Reference (foundational directive)
> **Source**: architecture.md §Development Directives, §Clean-Room Rules

## Reference-Driven Development

When implementing any subsystem, **always consult reference engine sources** before writing code. Never guess or brute-force an implementation.

| Priority | Engine | Path | Usage |
|----------|--------|------|-------|
| 1 (Primary) | **Unreal Engine 5.6** | `W:\UE_5.6_Source` | Parameters, math, algorithms, architecture patterns |
| 2 (Secondary) | **Godot 4.x** | `E:\godot` | Alternative approaches, simpler implementations for comparison |

### How to Use References

1. **Identify the subsystem** in the reference engine (e.g., UE5's `FStaticMeshSceneProxy` for render proxies)
2. **Study the approach** — understand the math, the data flow, the architecture pattern
3. **Understand why** — what problem does it solve? What tradeoffs were made?
4. **Reimplement from scratch** — same parameters and calculations, original code
5. **Document the rationale** — explain why this approach was chosen in the doc

### When to Deviate

Only deviate from the reference architecture when a more efficient alternative exists, and **document why**. Examples:
- UE5 uses a custom TArray; we use `std::vector` (sufficient for our scale, simpler)
- UE5 uses a custom hash map; we use `ankerl::unordered_dense` (faster in benchmarks)
- UE5 uses Chaos Physics; we use Jolt (MIT license, faster, simpler)

## Clean-Room Rules

These rules are **non-negotiable** and exist for legal protection:

1. **Study the approach, understand the math, reimplement from scratch** — same parameters and calculations, original code
2. **NEVER copy source code** from reference engines — not even a single function
3. **NEVER reference engine names in code comments** — no "this is like UE5's FooBar" in source files. Documentation is fine, source code is not.
4. Only deviate from the reference when a more efficient alternative exists, and document why

### What IS Allowed

- Reading UE5/Godot source to understand algorithms
- Using the same mathematical formulas (math is not copyrightable)
- Following the same architectural patterns (patterns are not copyrightable)
- Documenting comparisons in `/docs/` (educational, fair use)

### What IS NOT Allowed

- Copy-pasting code, even with variable renames
- Referencing engine names in C++ source comments
- Using proprietary data structures without reimplementation
