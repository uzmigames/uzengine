# Phase 2 — PBR Renderer (Deferred + G-Buffer)

## Context

The current renderer draws a hard-coded single-material pyramid. This task upgrades it to a full deferred PBR renderer: G-Buffer pass, lighting pass (GGX BRDF), IBL from a skybox, cascaded shadow maps, and a basic post-process stack.

## Goal

A physically-based renderer supporting:
- Deferred shading (G-Buffer: albedo, normal, roughness, metallic, depth)
- GGX Cook-Torrance BRDF
- Image-Based Lighting (IBL) from HDR equirectangular skybox
- Directional light with cascaded shadow maps (3 cascades)
- Point/spot lights (up to 256 per scene, tile-based culling)
- Tone mapping (ACES Filmic)
- Bloom (dual-threshold)
- Basic SSAO

## Spec Reference

[Renderer Spec](../../../docs/specs/renderer.md)

## Dependencies

- **phase1-scene-system** (scene nodes provide render data)
- **phase1-shader-hot-reload** (shaders are external `.hlsl` files)
- **phase2-gltf-import** (meshes needed to see results)
