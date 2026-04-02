# Cross-Engine Analysis Synthesis (2026-03-04)

## Context
Three AAA engines (UE5, Unity, Godot 4) analyzed at deep technical level. Synthesized into binding architecture document: `docs/architecture/cross-engine-analysis.md` (3800+ words).

## Five Universal Patterns (Consensus)
1. **Explicit Thread Ownership** — snapshots + command queues prevent race conditions
2. **Type Registration** — no type switches; decoupled via registry
3. **Arena Allocators** — bump allocation + RAII checkpoints for frame cleanup
4. **Command Queues** — lock-free FIFO for render thread work
5. **Immutable Snapshots** — cross-thread data via value copies, not pointers

## Eight Binding Rules (XE-1 to XE-8)
- **XE-1**: ModuleManager with reverse-order shutdown
- **XE-2**: Component registry + auto-registration (no switches)
- **XE-3**: Explicit vtables (no dynamic_cast in hot paths)
- **XE-4**: Lock-free RHI command queue
- **XE-5**: Arena allocators with checkpoint RAII
- **XE-6**: Immutable frame snapshots for render thread
- **XE-7**: Generational indices for entity references
- **XE-8**: ResourceUID for assets (decouple path from identity)

## Phased Implementation
- **Phase 1 (Weeks 1-6)**: XE-1, XE-2, XE-3, XE-5, XE-6, XE-7 (critical foundation)
- **Phase 2 (Weeks 7-14)**: XE-4, XE-8 (scalability + asset workflow)
- **Phase 3 (Weeks 15+)**: Job system, virtual instancing, GPU compute

## Anti-Patterns Documented
1. Type switches in core loops (use registry instead)
2. Dangling raw pointers (use generational indices)
3. Cross-thread pointer dereference (use immutable snapshots)
4. malloc() in hot paths (use arena allocators)

## Key File Updates Needed
- **CREATE**: `docs/specs/engine/module-system.md`
- **CREATE**: `docs/specs/engine/rhi-command-queue.md`
- **CREATE**: `docs/specs/engine/frame-snapshot.md`
- **CREATE**: `docs/specs/engine/entity-system.md`
- **CREATE**: `docs/specs/engine/asset-system.md`
- **UPDATE**: `docs/specs/engine/game-dll-abi.md` (add component registration ABI)
- **UPDATE**: `docs/specs/engine/memory-system.md` (arena patterns)

## Why This Matters
- **Convergence = Confidence**: Three independent engines arrived at same solutions
- **Binding Doctrine**: These 8 rules are NOT optional; they're architectural foundation
- **Risk Reduction**: Following proven patterns eliminates major classes of bugs
- **Extensibility**: Registry patterns enable C ABI plugins without recompilation

## Document Status
- **Status**: Active — Binds Phase 1+ implementation
- **Classification**: Architecture Standard
- **Owner**: Project Director
- **Review**: After Phase 1 completion (Week 6)
