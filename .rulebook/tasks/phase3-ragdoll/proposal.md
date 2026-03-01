# Phase 3 — Ragdoll & Physical Animation

## Goal

Jolt Physics ragdoll for character death/knockback. Physics asset (`.uzphys`) drives per-bone collider shapes and joint constraints. Active ragdoll (motor-driven blending toward animated pose). Seamless transition from animated to ragdoll.

## Spec Reference

[Skeletal Mesh — Ragdoll](../../../docs/specs/skeletal-mesh.md#6-ragdoll-integration-jolt-physics)

## Dependencies

- phase3-skeletal-mesh, phase3-animation-system, phase2-jolt-physics (base Jolt integration)
