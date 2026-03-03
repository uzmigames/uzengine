## 1. uz-minimap Custom HTML Element

- [ ] 1.1 Register `<uz-minimap>` as a custom HTML element in `uz_ui_html.dll` via the browser's custom element registry; element is valid with attributes `id`, `width`, `height`, `scale`, `center_on`, `map-texture`, `zoom`
- [ ] 1.2 Create `src/ui/minimap_widget.h` declaring `MinimapWidget` with methods `set_center(Vec3 world_pos)`, `set_zoom(float)`, `add_icon(MinimapIconDesc)`, `remove_icon(uint32_t icon_id)`, `reveal(Vec3 world_pos, float radius)`
- [ ] 1.3 Create `src/ui/minimap_widget.cpp` implementing the world-to-minimap coordinate projection: `minimap_pos = (world_pos - map_origin) * scale * zoom + minimap_center`
- [ ] 1.4 Parse the `scale` attribute as world units per pixel; parse `center_on` as an entity ID to track each frame

## 2. Background Map Texture

- [ ] 2.1 Load the PNG/DDS texture referenced by `map-texture` attribute using the existing texture loader
- [ ] 2.2 Draw the map texture as the base layer of the minimap canvas, clipped to `width x height` pixels
- [ ] 2.3 Read the world bounds `[minimap] origin_x, origin_z, extent_x, extent_z` from the `.uzscene` file for correct map alignment

## 3. Icon Layer

- [ ] 3.1 Define `struct MinimapIconDesc` in `minimap_widget.h` with fields: `icon_id` (uint32_t), `world_pos` (Vec3), `icon_type` (enum: Player, Enemy, Objective, Waypoint, NPC), `color` (RGBA), `label` (optional string), `pulse` (bool)
- [ ] 3.2 Render each icon as an inline SVG element positioned at the projected minimap coordinate
- [ ] 3.3 Rotate the player icon to match the entity's facing direction (yaw angle from the entity's forward vector)
- [ ] 3.4 Implement `pulse=true` icon animation: CSS keyframe animation scaling the icon from 1.0 to 1.5 and back over 1 second, stopping after 3 cycles

## 4. Fog-of-War

- [ ] 4.1 Maintain a per-minimap reveal mask texture (grayscale, same resolution as `width x height`) initialized to all zeros (unexplored)
- [ ] 4.2 In `MinimapWidget::reveal(world_pos, radius)`, paint a circle of white pixels at the projected minimap position with projected `radius`; blend additively (already-revealed areas stay revealed)
- [ ] 4.3 Composite the reveal mask over the map texture: unexplored areas (mask == 0) are darkened by 70% opacity black overlay; explored areas are fully visible
- [ ] 4.4 Persist the reveal mask to the save slot via the save system: serialize as a compressed binary blob keyed by `(scene_name, minimap_id)`

## 5. Zoom Control

- [ ] 5.1 Update the `MinimapWidget::zoom_` member when the `zoom` attribute changes or `ctx.ui:minimap_set_zoom()` is called
- [ ] 5.2 Apply smooth zoom interpolation: lerp the current displayed zoom toward the target zoom at rate `8.0 * delta_time` per frame

## 6. Lua API

- [ ] 6.1 Add `ctx.ui:minimap_add_icon(map_id, world_pos, icon_type, color, label)` Lua binding calling `MinimapWidget::add_icon()` and returning the new `icon_id`
- [ ] 6.2 Add `ctx.ui:minimap_remove_icon(map_id, icon_id)` Lua binding
- [ ] 6.3 Add `ctx.ui:minimap_reveal(map_id, world_pos, radius)` Lua binding
- [ ] 6.4 Add `ctx.ui:minimap_set_zoom(map_id, factor)` Lua binding with smooth interpolation

## 7. Testing

- [ ] 7.1 Create `tests/ui/test_minimap_widget.cpp` testing `MinimapWidget::set_center()`: verify world position (0,0,0) with `scale=0.02` maps to minimap center pixel
- [ ] 7.2 Test world-to-minimap projection: verify a world point 50 units east of center maps to 50*0.02 = 1 pixel right of center
- [ ] 7.3 Test zoom: at `zoom=2.0`, the same world point maps to 2 pixels right of center
- [ ] 7.4 Test `add_icon()` increases icon count; `remove_icon()` with a valid ID decreases icon count; `remove_icon()` with an invalid ID is a no-op (no crash)
- [ ] 7.5 Test fog-of-war `reveal()`: after revealing a circle at origin, the mask pixel at the center is > 0
- [ ] 7.6 Test reveal does not clear previously revealed pixels (additive compositing)
- [ ] 7.7 Test player icon rotation: entity with yaw=90 degrees produces a 90-degree-rotated player icon SVG transform
- [ ] 7.8 Test smooth zoom interpolation: zoom does not instantly snap but approaches target over multiple update ticks
- [ ] 7.9 Test Lua bindings: `ctx.ui:minimap_add_icon()` returns a non-zero `icon_id`; subsequent `ctx.ui:minimap_remove_icon()` with that ID succeeds
- [ ] 7.10 Achieve >= 95% line and branch coverage across `minimap_widget.cpp` and `scene_minimap.cpp`

## 8. Documentation

- [ ] 8.1 Update `docs/specs/ui-system.md` documenting `<uz-minimap>` element attributes, icon types, fog-of-war behavior, and Lua API
- [ ] 8.2 Update `docs/specs/save-system.md` documenting fog-of-war mask persistence format and save slot key schema
- [ ] 8.3 Update `docs/specs/scene-system.md` documenting `[minimap]` scene section fields: `origin_x`, `origin_z`, `extent_x`, `extent_z`
