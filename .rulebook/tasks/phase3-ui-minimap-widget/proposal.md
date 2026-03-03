# Proposal: phase3-ui-minimap-widget

## Why
Minimaps are required in virtually every open-world, action, and RPG game. No built-in minimap component exists in UzEngine. Building a minimap from scratch requires solving non-trivial problems: world-to-minimap coordinate projection, zoom levels, dynamic icon management, fog-of-war masking, and rotation relative to the player. Every game team that attempts this independently produces a different, incompatible solution with varying quality.

## What Changes
- Add `<uz-minimap>` custom HTML element registered by `uz_ui_html.dll`; placed in HTML like `<uz-minimap id="map" width="200" height="200" scale="0.02"></uz-minimap>`
- World-to-minimap coordinate system: `scale` attribute defines world units per pixel; `center_on` attribute accepts an entity ID to track (defaults to player entity tagged `player`)
- Background map texture: `map-texture` attribute points to a pre-baked top-down map image (PNG/DDS) aligned to world bounds declared in `[minimap]` section of `.uzscene`
- Icon layer: SVG-based icons rendered per-category (player, enemy, objective, waypoint, NPC); each icon type has a configurable color and size
- Fog-of-war: optional reveal mask texture updated at runtime as the player explores; unexplored areas are darkened; mask stored per-save-slot via save system integration
- Zoom level control: `zoom` attribute (float multiplier) and Lua `ctx.ui:minimap_set_zoom(id, factor)` with smooth interpolation
- Player rotation indicator: player icon rotates to match entity facing direction
- Lua API: `ctx.ui:minimap_add_icon(map_id, world_pos, icon_type, color, label)`, `ctx.ui:minimap_remove_icon(map_id, icon_id)`, `ctx.ui:minimap_reveal(map_id, world_pos, radius)`, `ctx.ui:minimap_set_zoom(map_id, factor)`
- Pulse animation on newly added icons (e.g., objective ping) via `pulse=true` icon option

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/save-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/scene-system.md`
- Affected code: `src/ui/minimap_widget.cpp`, `src/ui/minimap_widget.h`, `src/ui/ui_pass.cpp`, `src/lua/lua_ui_api.cpp`, `src/scene/scene_minimap.cpp`
- Breaking change: NO
- User benefit: Any game can add a working minimap with a single HTML element and a handful of Lua calls; fog-of-war, icons, and zoom are built-in rather than reimplemented per game
