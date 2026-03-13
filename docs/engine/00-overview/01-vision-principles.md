# Architecture Overview — Vision & Principles

> **Status**: Reference (foundational, rarely changes)
> **Source**: ARCHITECTURE.md §Vision, §Core Principles, §Engineering Principles

## Vision

AAA-quality game engine (visual parity with Unity, Godot, Unreal) fully embedded in VSCode, with open file formats designed for LLM manipulation. **The first engine built for autonomous AI-driven development.**

This means:
- Every file format is JSON (human-readable, LLM-parseable)
- Every parameter has semantic metadata (description, range, units)
- The engine ships a headless `--screenshot` mode for visual feedback loops
- MCP integration allows LLMs to manipulate scenes, materials, and code directly

## Core Principles

| Principle | What It Means |
|-----------|--------------|
| **AAA visual quality** | Rendering math and techniques based on UE5.6 (clean-room reimplementation, zero copied code) |
| **Open formats** | All scene/asset files are human-readable JSON, LLM-friendly |
| **Highly configurable** | Every subsystem is pluggable and data-driven via `.uzproject` |
| **Multi-backend rendering** | RHI abstraction: DX12 SM6.6 primary, Vulkan/Metal/DX11 fallbacks |
| **Runtime-first development** | Build and validate the runtime before extension/MCP integration |
| **Reference-driven development** | Always consult proven engine sources before writing code |
| **No garbage collector** | Deterministic memory via Rust-inspired ownership and borrow checking |

## Clean Code Rules

This engine is too large for sloppy code. Every subsystem must be readable, testable, and maintainable by both humans and AI agents.

1. **Meaningful names** — names reveal intent. No `tmp`, `buf`, `data`, `mgr`. Prefer `meshVertexBuffer` over `vb`.
2. **Small functions** — one function = one thing, max ~40 lines. If a block needs a comment, extract it.
3. **No side effects** — `GetX()` reads, `SetX()` writes. Never mix.
4. **No magic numbers** — `constexpr float kGravity = -9.81f;`, not inline literals.
5. **Fail early** — validate at system boundaries. `UZ_CHECK` for invariants, `UZ_ENSURE` for soft errors.
6. **Delete dead code** — git has history. No commented-out code.
7. **Max 1500 lines per file** — split proactively at ~1000. One file = one responsibility.

## SOLID Principles (adapted for C++ game engine)

| Principle | Application |
|-----------|------------|
| **Single Responsibility** | `InputSystem` owns input. `Config` owns config. No god classes. |
| **Open/Closed** | New RHI backends, components, and importers extend via new types, not modifying existing code. |
| **Liskov Substitution** | Any `rhi_backend_t` (DX12, Vulkan, Metal) is interchangeable at the vtable level. |
| **Interface Segregation** | Focused interfaces: `IRenderable`, `IUpdatable`, `ISerializable`. No fat base classes. |
| **Dependency Inversion** | Renderer depends on RHI abstraction, not DX12 directly. Inject, don't hardcode. |

## Observability Rules

An engine without observability is undebuggable. Every subsystem MUST be transparent:

1. **Structured logging** — `UZ_LOG(Category, Level, fmt, ...)` at every init/shutdown, external call, and error path
2. **CPU profiling** — `UZ_PROFILE_SCOPE("name")` on every function touching GPU, disk, or running per-frame
3. **Assertions** — `UZ_CHECK(expr)` for invariants (fatal), `UZ_ENSURE(expr)` for recoverable violations, `UZ_VERIFY(expr)` for side-effect expressions
4. **Stat counters** — `StatCounter` tracks allocations, draw calls, frame time, cache hits per group
5. **Error context** — always log what, why, and the input: `"Failed to load texture: path='{}', reason='{}'"` — never `"Load failed"`
