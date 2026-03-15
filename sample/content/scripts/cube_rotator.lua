-- cube_rotator.lua — Slowly rotates all cubes in the scene
--
-- Finds all entities whose name starts with "Cube_" and applies
-- a gentle Y-axis rotation each frame. Each cube gets a different
-- rotation speed based on its index for visual variety.

local cubes = {}
local base_speed = 30.0  -- degrees per second

function on_start(entity)
    -- Collect all cube entities
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        if name and name:sub(1, 5) == "Cube_" then
            cubes[#cubes + 1] = e
        end
    end
    uz.log("CubeRotator: found " .. #cubes .. " cubes to rotate")
end

function on_update(entity, dt)
    for i = 1, #cubes do
        local e = cubes[i]
        -- Each cube rotates at a slightly different speed
        local speed = base_speed * (0.5 + i * 0.3)
        local px, py, pz = uz.scene.get_rotation(e)
        py = py + speed * dt
        if py > 360 then py = py - 360 end
        uz.scene.set_rotation(e, px, py, pz)
    end
end

function on_stop(entity)
    uz.log("CubeRotator stopped")
end
