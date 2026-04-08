# Proposal: HDRI Skybox Completion (Phase 2)

## Why
HDRISkyboxPass exists as a stub but is non-functional. Scenes that use static HDRI environments (architectural viz, product shots, stylized skyboxes) cannot render their sky correctly — only the procedural Hillaire atmosphere works. This is a 1-2 day fix since the pass infrastructure exists; we just need to wire the cubemap loading and rendering.

## What Changes

### New Systems
1. Functional HDRI skybox rendering from equirectangular or cubemap HDR textures
2. SkyLight cubemap fallback — when no procedural sky is active, sample the HDRI for ambient

### Refactoring
- hdri_skybox_pass.cpp: implement cubemap binding, fullscreen quad rendering at depth=1.0
- skybox_cubemap_ps.hlsl: verify cubemap sampling and exposure application
- scene_renderer_init.cpp: register HDRI skybox pass in the render graph when SkyLight has cubemap_path

## Impact
- Affected specs: docs/specs/rendering/renderer.md
- Affected code: runtime/engine/renderer/passes/atmosphere/hdri_skybox_pass.cpp/.h, runtime/shaders/runtime/atmosphere/skybox_cubemap_ps.hlsl
- Breaking change: NO — activates previously stubbed feature
- User benefit: Static HDRI skybox support for non-procedural sky scenes
- Enables: Product visualization, architectural rendering, stylized environments
