# Proposal: phase4d_4.26.0-vfx-gpu-readback-framework

## Why
Multiple VFX features need GPU→CPU data transfer: light renderer reads top-N emissive
particles, dynamic AABB reads bounds, death events need readback for sub-emitter spawning,
and future features (gameplay queries, audio reactivity) will need it too. Currently each
feature implements ad-hoc readback with its own staging buffer and fence logic. A unified
framework reduces boilerplate, avoids redundant staging buffers, and ensures correct
multi-frame latency handling across all consumers.

## What Changes
- New `VFXReadbackManager` class: manages a ring of staging buffers (3-frame latency)
- Consumers register "readback requests" (source UAV, byte offset, size, callback)
- Each frame: (1) copy requested regions to staging, (2) map N-2 frame staging, (3) invoke callbacks
- Migrate LightRenderModule's existing readback to use the framework
- Migrate BoundsBuffer readback (4.25.0) to use the framework

## Impact
- Affected specs: particle-system.md (add readback framework section)
- Affected code: vfx_readback.h/.cpp (new), light_render_module.cpp, frame_particles.cpp
- Breaking change: NO (internal refactor, same external behavior)
- User benefit: Enables future GPU→CPU features without per-feature plumbing
