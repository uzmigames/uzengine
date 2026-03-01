# Phase 2 — Cascaded Shadow Maps

## Context

Part of the PBR renderer phase. Separated into its own task because CSM is complex enough to warrant independent tracking. DirectionalLight nodes cast shadows via 3-cascade shadow maps rendered to a shared atlas.

## Goal

- 3-cascade CSM for DirectionalLight
- Shadow atlas 2048×2048 (D32_FLOAT)
- PCF 3×3 kernel for soft shadows
- No shadow acne (depth bias tuned)
- Cascade debug overlay (toggle via IPC)

## Dependencies

- **phase2-pbr-renderer** (deferred lighting pass consumes shadows)
