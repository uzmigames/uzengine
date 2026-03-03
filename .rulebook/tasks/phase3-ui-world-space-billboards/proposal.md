# Proposal: phase3-ui-world-space-billboards

## Why
Health bars, name tags, quest markers, floating damage numbers, and interaction prompts need to appear in 3D world space above or beside entities, always facing the camera. No world-space UI system exists in UzEngine. The current `uz_ui_html.dll` only renders in screen space. Implementing this manually requires projecting world positions to screen coordinates every frame in Lua, which is fragile, does not occlude correctly against scene geometry, and does not scale with camera distance.

## What Changes
- Add `CWorldSpaceUI` ECS component: fields `html_path` (relative path to HTML file), `width_m` / `height_m` (physical size in world meters), `offset` (Vec3 local offset from entity origin), `face_camera` (bool, default true), `depth_test` (bool, default true), `scale_by_distance` (bool, default false)
- UIPass billboard rendering: each `CWorldSpaceUI` entity gets an offscreen render target for its HTML document; the target texture is applied to a camera-facing quad placed at the entity's world position + offset
- Depth testing against scene geometry: billboard quad participates in the depth buffer so it is occluded by walls and terrain when `depth_test = true`
- Auto-scale by distance: when enabled, apparent screen size is held constant (quad grows with distance); when disabled, size is physically accurate (shrinks with distance)
- Lua API: `ctx.ui:attach_to_entity(entity_id, html_path, width_m, height_m)`, `ctx.ui:detach_from_entity(entity_id)`, `ctx.ui:update_billboard(entity_id, html_content)` for dynamic content injection
- HTML documents attached to billboards share the same JS/Lua bridge as screen-space UI; `ctx.ui:set_content(element_id, value)` works identically
- Editor gizmo: selected entity with `CWorldSpaceUI` shows a yellow wire-frame rectangle in the Editor Pass indicating billboard bounds

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/renderer.md`, `docs/specs/editor-gizmos.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/ui/ui_pass.cpp`, `src/ui/world_space_ui.h`, `src/ui/world_space_ui.cpp`, `src/renderer/billboard_renderer.cpp`, `src/lua/lua_ui_api.cpp`, `src/editor/gizmos.cpp`
- Breaking change: NO
- User benefit: Health bars, name tags, and damage numbers can be authored as plain HTML files and attached to entities with two lines of Lua; depth occlusion and camera facing are handled automatically
