-- Moves the entity this script is attached to (the box of scenes/gdf-incremental-reference) 400 cm
-- along +x between Lua frames 100 and 160, then leaves it there. Used by
-- scripts/test_gdf_regional.py (lighting block B, item 2): the global distance field and the GI
-- consumers must follow the box without a global invalidation.
--
-- The position is the entity's OFFSET from its authored place: the mesh is written at x -250..-150
-- (scripts/gdf_incremental_reference.py), so the box rests there until frame 100 and ends at
-- x +150..+250, the place the static variant scene `gdf-incremental-reference-end` puts it.

local frame = 0
local START_X, END_X = 0.0, 400.0
local FIRST, LAST = 100, 160

function on_update(entity, dt)
    frame = frame + 1
    if frame >= FIRST and frame <= LAST then
        local t = (frame - FIRST) / (LAST - FIRST)
        uz.scene.set_position(entity, START_X + (END_X - START_X) * t, 0.0, 0.0)
        if frame == FIRST then print("gdf_box_move: start at frame " .. frame) end
        if frame == LAST then print("gdf_box_move: end at frame " .. frame) end
    end
end
