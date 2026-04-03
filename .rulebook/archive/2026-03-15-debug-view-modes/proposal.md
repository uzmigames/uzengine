# Proposal: debug-view-modes

## Why

Rendering systems are opaque. Developers need to visualize individual G-Buffer channels, lighting contributions, shadow factors, and geometric data in real-time during development. Without this, debugging rendering issues is inefficient and guesswork-based. UE5 and Godot both ship debug view modes as essential developer tools.

UzEngine's PBR renderer currently produces no debug visualizations beyond fullscreen renders. We need a runtime-toggleable system that shows:
- Individual G-Buffer channels (albedo, normal, metallic, roughness, AO)
- Depth and linearized depth
- Lighting contributions and shadow factors
- Wireframe geometry overlay
- A composite grid showing all channels simultaneously

This enables rapid iteration on shader bugs, lighting setup, and material configuration.

## What Changes

**New Files**:
- `runtime/engine/renderer/core/debug_view_mode.h` — DebugViewMode enum (13 modes)
- `runtime/shaders/runtime/debug/debug_view_ps.hlsl` — shader implementing all 13 modes
- `runtime/engine/renderer/passes/debug_view_pass.h/.cpp` — fullscreen post-process pass
- `docs/specs/rendering/debug-view-modes.md` — complete system spec

**Modified Files**:
- `runtime/engine/renderer/core/scene_renderer.h/.cpp` — add SetDebugViewMode(mode), wire pass
- `runtime/engine/app/demo_application.h/.cpp` — add `--view-mode <name>` CLI arg
- `runtime/demos/pbr_test/pbr_test_demo.cpp` — add F1-F11 keyboard shortcuts
- `runtime/engine/renderer/core/pipeline_library.h/.cpp` — add GetDebugViewPSO()
- `runtime/engine/renderer/core/render_passes.h` — add debug_view_pass descriptor

**No spec changes** — system fits entirely within existing render graph + RHI abstraction. Integration is opt-in (disabled by default).

## Impact

- Affected specs: `1.3-renderer.md` (add 1.3.4 Debug Views subsection), `1.1-math-containers.md` (DebugViewMode enum)
- Affected code: scene_renderer (4 lines), demo_application (8 lines), pipeline_library (6 lines), pbr_test_demo (8 lines), render_passes (2 lines)
- Breaking change: **NO** — system is additive, no API changes
- User benefit: **Essential** — enables visual debugging of PBR/deferred rendering without shader recompilation. Unlocks rapid iteration on material/lighting bugs. Foundation for future profiling UI (overdraw, light complexity, etc.)

## Phase

**Phase 2** — Post-processing & advanced rendering features. Depends on: deferred G-Buffer (complete), lighting pass (complete), post-process pipeline (complete).

## Complexity

**Medium** (3-4 days):
- Shader logic is straightforward (sample textures, output channel)
- Overview grid is most complex part (UV subdivisions, text rendering)
- Integration is minimal (pass insertion, keyboard input, CLI arg)
