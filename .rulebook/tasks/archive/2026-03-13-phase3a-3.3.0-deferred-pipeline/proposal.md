# Proposal: 3.3.0 — Deferred Pipeline

## Why
Multi-light scenes require deferred shading to decouple geometry complexity from light count. G-Buffer enables efficient per-pixel lighting with dozens of point/spot lights.

## What Changes
- G-Buffer: Albedo (RGBA8), Normal (R10G10B10A2), MetalRough (RGBA8), Depth (D32)
- Geometry pass: render all opaque → G-Buffer via render graph
- Lighting pass: fullscreen quad, reconstruct position from depth, Cook-Torrance PBR
- Directional + point light evaluation
- Forward transparent pass after deferred resolve
- G-Buffer visualization mode (toggle individual buffers)

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/core/renderer/deferred/`, shaders
- Breaking change: YES — rendering pipeline switches from forward-only to deferred+forward
- User benefit: Multi-light scenes, foundation for shadows and post-processing
