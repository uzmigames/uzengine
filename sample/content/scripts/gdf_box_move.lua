-- Moves the entity this script is attached to (the box of scenes/gdf-incremental-reference) 400 cm
-- along +x between Lua frames 100 and 160, then leaves it there. Used by
-- scripts/test_gdf_incremental.py (lighting block B, item 2): the global distance field and the GI
-- consumers must follow the box without a global invalidation.

local frame = 0
local START_X, END_X = -200.0, 200.0
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
