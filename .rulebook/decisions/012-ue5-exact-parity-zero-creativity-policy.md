# 12. UE5 Exact Parity — Zero Creativity Policy

**Status**: proposed
**Date**: 2026-03-16

## Context

Multiple rendering features (SH irradiance, fog parameters, shadow cascades, aerial perspective) were implemented using invented approximations, analytical fallbacks, and guessed coefficients instead of reading UE5 source code. Every time this happened, the visual result was visibly wrong compared to UE5. The user has provided full UE5 5.6 source at W:\UE_5.6_Source specifically for exact replication.

## Decision

ALL engine code MUST use the EXACT same calculations, formulas, coefficients, and parameter values as UE5. Zero creativity, zero approximations, zero invented fallbacks. Before writing ANY rendering/lighting/fog/shadow/physics code, the UE5 source MUST be read first via unreal-architect agent. The math must be identical — only variable/function names are changed to avoid copyright. If a GPU feature (like cubemap readback) fails, FIX the feature instead of inventing an analytical fallback.

## Alternatives Considered

- Allow analytical approximations when GPU readback fails — REJECTED: produces visibly wrong results every time
- Use Godot as primary reference instead of UE5 — REJECTED: UE5 is the quality target, Godot is secondary
- Allow creative improvements over UE5 — REJECTED: Claude lacks the decades of expertise UE5 devs have

## Consequences

1. Every rendering function must document which UE5 file/line it mirrors. 2. No more 'analytical fallback' code paths — broken features must be fixed. 3. Agents must always read UE5 source before implementing. 4. Visual parity with UE5 becomes achievable. 5. Slower initial implementation but correct results on first try.
