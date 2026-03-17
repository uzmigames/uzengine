# Proposal: UE5 Visual Parity Roadmap — Master Task

## Why

UzEngine's renderer is at ~35% visual parity with UE5 5.6. The core deferred PBR pipeline works (GGX+Burley+SmithJoint, CSM, IBL, Bloom, ACES FilmToneMap, Sky Atmosphere, POM, DFS), but it lacks the post-processing stack, reflections, advanced lighting, and scene management features that make UE5 look photorealistic.

Without this work: metallic surfaces look flat (no reflections), geometry edges are jagged (no TAA), objects float (no contact shadows), scenes look sterile (no film grain/vignette), interiors are pitch black (no GI), and only 1 directional + N point lights are supported.

With this work (~60 days): ~80% visual parity with UE5 default lit, competitive with modern indie/AA engines.

## What Changes

This master task consolidates ALL rendering gaps into a single prioritized roadmap. It references 22 existing sub-tasks and adds 6 new feature areas (Decals, Emissive, PCSS fix, GTAO fix, SSGI, GPU Occlusion Culling) that had no task. Implementation follows ROI order: highest visual impact per day of work first.

## Impact
- Affected specs: renderer.md, screen-space-effects.md, shader-library.md, all post-process specs
- Affected code: runtime/engine/renderer/, runtime/shaders/runtime/
- Breaking change: NO (additive features only)
- User benefit: Dramatically improved visual quality, approaching UE5 default lit parity
