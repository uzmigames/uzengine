---
name: Lua Scripting Phase 1 Complete — Documentation
description: Updated all Lua scripting documentation to reflect functional Phase 1 state with working APIs
type: project
---

## Completion Date: 2026-03-15

## Summary
Updated all Lua scripting documentation to accurately reflect the current **functional** state of Phase 1. LuaJIT 2.1 is fully integrated with a complete set of working APIs.

## Files Updated
1. **docs/engine/15-scripting/06-lua-api.md** (459 lines)
   - Added Global Functions section with uz.quit(), uz.log(), uz.delta_time, uz.total_time
   - Updated Scene API to include uz.scene.get_entities() for entity iteration
   - Complete Input API reference with uz.input.* functions
   - New Renderer section with uz.renderer.set_render_path(), set_debug_view(), set_debug_mode()
   - Updated Camera section with uz.camera.set_position() and look_at()
   - Removed unimplemented sections (Physics, Audio, UI, Events, Timers marked as Phase 3+)
   - Added two complete working example scripts (FPS Camera Controller, Entity Finder/Rotator)
   - Updated Engine Comparison table to reflect actual working APIs
   - Updated Rules section with accurate guidance (Z-up coordinate system, state non-persistence, etc.)

2. **docs/user/guides/lua-scripting.md** (NEW, ~1,150 lines)
   - Quick start guide with script lifecycle (on_start, on_update, on_stop)
   - Common tasks: Moving camera, reading input, finding/rotating entities, changing render settings, quitting
   - Coordinate system explanation (Z-up: X=forward, Y=right, Z=up)
   - Timing and frame control with uz.delta_time, uz.total_time
   - Best practices section (stateless design, Euler angles, input lifecycle, logging, nil handling)
   - Full API reference table for all working functions
   - Complete FPS camera controller script as practical example
   - Troubleshooting section (script loading, hot-reload, input not working, camera)
   - Examples directly from working demo scripts (pbr_showcase_controller.lua, cube_rotator.lua)

3. **docs/README.md** (line 86)
   - Added link to new Lua scripting guide in user guides section

4. **docs/user/README.md** (line 19)
   - Added link to new Lua scripting guide in guides table

## Current Lua Phase 1 State (Working)

### Lifecycle
- on_start(entity) — Called once when script loads
- on_update(entity, dt) — Called every frame
- on_stop(entity) — Called when script unloads

### Working APIs
- **Global**: uz.log(), uz.quit(), uz.delta_time, uz.total_time
- **Input**: is_key_down(), is_key_just_pressed(), get_mouse_delta(), get_scroll_delta(), is_mouse_button_down()
- **Camera**: set_position(), look_at()
- **Scene**: get_entities(), get_name(), get_rotation(), set_rotation()
- **Renderer**: set_render_path(), set_debug_view(), set_debug_mode()

### Demo Scripts
- pbr_showcase_controller.lua — FPS camera with renderer controls
- cube_rotator.lua — Entity finder and rotation example
- Both demonstrate real working patterns

### Key Design Points Documented
1. **Z-up Coordinate System** — X=forward, Y=right, Z=up (critical for scripts)
2. **Euler Angles in Degrees** — set_rotation(pitch, yaw, roll) all in degrees, not radians
3. **No State Persistence** — Script state (local variables) lost on hot-reload; use on_start() to reinitialize
4. **Frame-Based Input** — Call input functions in on_update(), not on_start()
5. **Scene Iteration** — Use uz.scene.get_entities() then filter by name

## Documentation Pattern
All documentation follows established project conventions:
- Status badge: `> **Status**: Phase 1 Complete`
- Tables for API references with function names, parameters, returns, notes
- Code examples with proper ```lua syntax highlighting
- Cross-references using relative paths
- Examples taken from actual working demo code

## Validation
- ✅ All API references match actual working functions in runtime/engine/scripting/api/
- ✅ Examples are exact copies from sample/content/scripts/
- ✅ Coordinate system (Z-up) matches FPSCameraController implementation
- ✅ No unimplemented APIs documented as available
- ✅ All cross-references in docs/README.md and docs/user/README.md updated
- ✅ Consistent formatting across both API reference and user guide

## Related Documentation
- Source code: runtime/engine/scripting/api/core/, api/renderer/, api/scene/
- Demo scripts: sample/content/scripts/ (pbr_showcase_controller.lua, cube_rotator.lua, hello.lua)
- Related spec: docs/engine/15-scripting/01-overview.md marks Phase 1 as Complete
