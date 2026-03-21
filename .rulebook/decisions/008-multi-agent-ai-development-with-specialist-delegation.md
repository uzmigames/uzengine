# 8. Multi-Agent AI Development with Specialist Delegation

**Status**: proposed
**Date**: 2026-03-15

## Context

Building a game engine with AI assistance requires different levels of expertise for different subsystems. Using a single expensive model (opus) for everything is wasteful. Different tasks have different complexity: docs are simple, RHI code is complex, codebase exploration is read-only.

## Decision

Three-tier agent cost model: opus ($$$) for critical C++ systems (RHI, render graph, shaders, ECS), sonnet ($$) for gameplay/physics/audio/tests/TypeScript, haiku ($) for docs/research/architecture analysis. Main conversation (opus) does ONLY coordination/planning/delegation — never bulk work. Mandatory delegation rules: docs → docs-operator (haiku), C++ → cpp-core-expert (opus), exploration → researcher (haiku). Parallel agent launch for independent tasks.

## Alternatives Considered

- Single model for everything (expensive, no specialization)
- Human developers only (slow for game engine scale)
- Code generation without review (quality issues)
- Fixed agent teams (inflexible, wasteful)

## Consequences

Pros: 20x cost reduction for docs/research, domain expertise per subsystem, parallel work on independent systems. Cons: Coordination overhead, context must be passed to agents, agent results need verification. 25+ specialist agent types defined in AGENTS.md.
