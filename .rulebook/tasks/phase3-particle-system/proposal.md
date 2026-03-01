# Phase 3 — Particle System (VFX)

## Goal

Full GPU-driven particle system: compute-shader simulation (no CPU readback), `.uzfx` plain-text effect format, stacked behavior modules, billboard quad rendering via `DrawIndexedInstanced`, ECS `CParticleEmitter` component, Lua `ctx.vfx` API, and AnimNotify integration so animations can automatically spawn VFX at specific frames.

## Spec Reference

[Particle System Spec](../../../docs/specs/particle-system.md)

## Dependencies

- phase3-animation-system (AnimNotify `spawn_vfx` event routing)
- phase2-pbr-renderer (DX12 compute pipeline infrastructure, StructuredBuffer, indirect draw)
- phase2-asset-pak (`.uzfx` file registered as an asset type, packed in `.uzpak`)
