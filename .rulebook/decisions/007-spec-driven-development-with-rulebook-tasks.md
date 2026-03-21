# 7. Spec-Driven Development with Rulebook Tasks

**Status**: proposed
**Date**: 2026-03-15

## Context

A game engine with 165+ planned features needs a structured approach to prevent scope creep, ensure quality, and maintain AI agent context across sessions. Traditional issue trackers lack the structured spec format needed for AI-assisted development.

## Decision

Every feature follows spec-driven development via Rulebook tasks: (1) proposal.md — why/what/scope, (2) tasks.md — checklist with validation criteria, (3) specs/ — OpenSpec-compatible spec deltas with Given/When/Then scenarios. Tasks MUST be created BEFORE implementation. Task completion auto-saves learnings to persistent memory. 95% test coverage required. Visual validation (demo + screenshot) required for rendering features.

## Alternatives Considered

- GitHub Issues (no structured spec format)
- Linear (external tool, no AI memory integration)
- Notion (heavy, not version-controlled)
- Plain markdown TODOs (no enforcement or memory)

## Consequences

Pros: Structured specs prevent miscommunication, persistent memory enables session continuity, auto-archived tasks build knowledge base, AI agents can search past implementations. Cons: Higher upfront cost per feature, spec format must be maintained, Rulebook tooling dependency. Currently 45 active + 51 archived tasks.
