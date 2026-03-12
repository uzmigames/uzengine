# Proposal: restructure-engine-docs

## Why
The engine documentation currently lives in 3 monolithic files totaling ~8000 lines (~360KB):
- `docs/ENGINE.md` (~5584 lines) — engine layer API, 25 sections covering everything from lifecycle to audio
- `docs/ARCHITECTURE.md` (~2300 lines) — overall architecture, build toolchain, 50+ third-party libs, AI pipeline
- `docs/DEVELOPER.md` (~169 lines) — developer-only tooling layer

These files are too large for effective LLM context windows, hard to navigate, impossible to work on in parallel, and mix implemented systems with speculative future plans. As we begin implementing engine systems, each subsystem needs a focused, detailed spec that serves as the implementation blueprint.

## What Changes
Split into ~138 focused files under `docs/engine/` organized into 30 numbered directories. The numbering reflects **implementation priority** — already-built systems (core, math, RHI) get low numbers; speculative future systems (networking, VFX, AI pipeline) get high numbers.

Each document will include:
- Detailed API design with C++ code examples
- Design rationale (why this approach was chosen)
- UE5/Godot architectural comparisons (clean-room, no copied code)
- Implementation status and dependencies
- Scenarios and edge cases

The original 3 files will be preserved as-is until migration is complete, then replaced with a redirect to docs/engine/README.md.

## Impact
- Affected specs: docs/ENGINE.md, docs/ARCHITECTURE.md, docs/DEVELOPER.md
- Affected code: None (documentation only)
- Breaking change: NO
- User benefit: Each subsystem has a focused, implementation-ready spec. LLM agents can load only the relevant doc instead of 5000+ lines. Parallel development across subsystems becomes possible.
