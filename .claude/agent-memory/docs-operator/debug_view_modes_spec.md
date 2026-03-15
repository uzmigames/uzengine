---
name: Debug View Modes Specification
description: Complete Rulebook task spec for debug visualization system (13 modes, keyboard shortcuts, CLI/Lua API)
type: reference
---

# Debug View Modes — Specification Reference

**Date**: 2026-03-14
**Phase**: 2 (Post-Processing & Advanced Rendering)
**Status**: Specification complete, ready for implementation
**Files**:
- Rulebook task: `e:/UzEngine/.rulebook/tasks/debug-view-modes/` (384-line checklist + 49-line proposal)
- Spec doc: `e:/UzEngine/docs/specs/rendering/debug-view-modes.md` (897 lines)

## 13 Visualization Modes

1. **Lit** (F1) — Normal PBR rendering (default, no visualization)
2. **Albedo** (F2) — Base color texture without lighting
3. **WorldNormal** (F3) — Surface normals (octahedral decoded, RGB visualization)
4. **Metallic** (F4) — Metallic parameter as grayscale
5. **Roughness** (F5) — Roughness parameter as grayscale
6. **AO** — Ambient occlusion as grayscale
7. **Depth** (F6) — Linearized depth (black=near, white=far)
8. **WorldPosition** (F7) — Absolute world coordinates as false color
9. **Lighting** (F8) — Direct lighting (Lo) without albedo multiply
10. **Shadows** (F7) — Shadow factor visualization (red=shadowed, white=lit)
11. **Unlit** (F11) — Alias for Albedo
12. **Wireframe** (F10) — Geometry edge detection overlay
13. **Overview** (F9) — Composite 4×2 grid of 8 channels with labels

## Control Methods

- **Keyboard**: F1-F11 (immediate mode switching)
- **CLI**: `--view-mode <name>` argument (for headless/batch testing)
- **Lua**: `ctx.debug.set_view_mode(string)` and `ctx.debug.get_view_mode()`

## Architecture Principles

- **No shader recompilation** — single `debug_view_ps.hlsl` with cbuffer-driven branching
- **Minimal overhead** — < 1ms on modern GPU (fullscreen post-process)
- **Zero cost for Lit mode** — pass is completely skipped (no performance regression)
- **Pass insertion** — After lighting, before tonemapping (after G-Buffer reads, before tone-mapping)
- **Reads existing G-Buffer** — No additional memory or uploads required

## Key Files to Create

```
NEW:
  runtime/engine/renderer/core/debug_view_mode.h           — Enum + parsing
  runtime/engine/renderer/passes/debug_view_pass.h/.cpp   — Render pass class
  runtime/shaders/runtime/debug/debug_view_ps.hlsl        — HLSL implementation

MODIFIED:
  scene_renderer.h/cpp                                     — SetDebugViewMode() + pass insertion
  demo_application.h/cpp                                   — --view-mode CLI arg
  pbr_test_demo.cpp                                        — F1-F11 keyboard input
  pipeline_library.h/cpp                                   — PSO caching
  lua_vm.cpp                                               — ctx.debug API bindings
```

## Task Checklist Structure

78 total items across 15 sections:

1. **Core Enum & Type Definitions** (3 items) — DebugViewMode enum, parsing, Lua API
2. **Shader Implementation** (15 items) — All 13 visualization modes + overview grid
3. **Render Pass Implementation** (4 items) — DebugViewPass class
4. **Pipeline & Resource Management** (4 items) — PSO, RT allocation, SRV management
5. **Integration: SceneRenderer** (3 items) — Getter/setter, pass insertion
6. **Integration: CLI** (3 items) — Argument parsing, help text
7. **Integration: Keyboard** (2 items) — F1-F11 shortcuts, overlay text
8. **Shader Library** (3 items) — Octahedral decoding, depth linearization
9. **Testing — Visual Verification** (7 items) — Per-mode screenshots, regression
10. **Lua API** (3 items) — ctx.debug implementation
11. **Overview Grid** (4 items) — UV subdivision, text labels, borders
12. **Documentation** (4 items) — Inline comments, API docs
13. **Build & Compilation** (3 items) — Shader pipeline, warnings, linking
14. **Final Integration & Polish** (3 items) — End-to-end flow, error handling
15. **Documentation & Changelog** (4 items) — Spec, guides, changelog

## Testing Requirements (Mandatory for Rendering Features)

Per CLAUDE.md requirement: **Visual verification over unit tests**.

- Screenshot each of 13 modes with standardized test scene
- Visual validation checklist: albedo correct, normals visualized correctly, depth gradient proper, etc.
- Keyboard shortcuts work (F1-F11)
- CLI arg works (`--view-mode albedo`)
- Lua API works (`ctx.debug.set_view_mode()`)
- Regression test: existing demos unaffected
- Performance test: overhead < 1ms

## Dependency Chain

✓ Deferred G-Buffer rendering (complete)
✓ Lighting pass (complete)
✓ Post-processing pipeline (complete)
→ Ready to implement

## Integration Pattern

```cpp
// In SceneRenderer::Render():
if (debugMode != Lit) {
    // Insert debug pass after lighting, before tonemapping
    m_debugViewPass->Execute(cmdList, desc);
    lastPassOutputRtv = debugOutputRtv; // Continue with this instead of lighting buffer
} else {
    lastPassOutputRtv = lightingBuffer; // Skip debug pass entirely (zero cost)
}
// Continue with tonemapping, bloom, etc. using lastPassOutputRtv
```

## Shader Mode Dispatcher Logic

```hlsl
switch (debugMode) {
    case 0:  return LightingBuffer;           // Lit (pass-through)
    case 1:  return VisualizeAlbedo(uv);      // Albedo
    case 2:  return VisualizeNormal(uv);      // Normal
    // ... etc ...
    case 12: return VisualizeOverview(uv);    // Overview grid
}
```

## Performance Characteristics

- **Debug pass overhead**: 0.2–0.5ms on RTX 3080+ (fullscreen quad with G-Buffer reads)
- **Keyboard input**: <0.1ms
- **Lua API call**: <0.1ms
- **Overlay text**: 0.1–0.2ms
- **Total**: <1ms on target hardware

## Future Expansions (Phase 2.5+)

- **Overdraw** — pixel overdraw counter (forward pass instrumentation)
- **LightComplexity** — lights affecting each pixel
- **MotionVectors** — TAA velocity buffer visualization
- **SSAO** — before-blend AO visualization
- **NaniteTriangles** — future (post-Nanite implementation)

## Documentation References

- Main spec: `e:/UzEngine/docs/specs/rendering/debug-view-modes.md`
- Rulebook task: `e:/UzEngine/.rulebook/tasks/debug-view-modes/`
- Related spec: `e:/UzEngine/docs/specs/1.3-renderer.md` (will add section 1.3.4)
- Related spec: `e:/UzEngine/docs/specs/1.1-math-containers.md` (enum location)

## Key Design Decisions

1. **Single shader with branching** over shader variants — faster iteration, one PSO
2. **Constant buffer mode flag** over dynamic branching — zero cost (cbuffer read is cached)
3. **Fullscreen post-process pass** over forward render pass modification — cleaner integration
4. **Godot's lightweight enum pattern** over UE5's heavyweight buffer visualization — simpler, still complete
5. **4×2 grid for Overview mode** over 4×3 — balance between channel coverage and readability

## Acceptance Criteria Summary

✓ 13 modes implemented with correct visualization logic
✓ Keyboard shortcuts (F1-F11) functional
✓ CLI argument parsing correct
✓ Lua API functional
✓ Text overlay showing current mode
✓ Overview grid composite functional
✓ < 1ms performance overhead
✓ All visual tests pass
✓ No regressions in existing code
✓ Zero cost for Lit mode (pass skipped)
✓ Complete documentation with code examples
✓ Production-ready error handling

## Implementation Notes for Agents

- Start with enum definition and parsing functions (section 1)
- Implement shader modes one at a time (section 2), verify visually each mode
- Create render pass class (section 3)
- Wire into SceneRenderer (section 5) — critical integration point
- Add keyboard input (section 7) — enables rapid testing
- Add CLI arg (section 6) — enables headless testing
- Implement Lua API (section 10) — enables script-based testing
- Overview grid (section 11) — most complex visual; do last
- Visual tests (section 9) — mandatory before marking complete

Use Rulebook task checklist to track progress through all 78 items.
