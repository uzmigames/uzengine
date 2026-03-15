-- cube_rotator.lua — Slowly rotates all cubes in the scene
--
-- Finds all entities whose name starts with "Cube_" and applies
-- a gentle Z-axis rotation each frame (Z-up coordinate system).
-- Each cube gets a different rotation speed based on its index.

local cubes = {}
local angles = {}  -- accumulated angle per cube (degrees)
local base_speed = 30.0  -- degrees per second

function on_start(entity)
    -- Collect all cube entities
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        if name and name:sub(1, 5) == "Cube_" then
            cubes[#cubes + 1] = e
            angles[#cubes] = 0.0
        end
    end
    uz.log("CubeRotator: found " .. #cubes .. " cubes to rotate")
end

function on_update(entity, dt)
    for i = 1, #cubes do
        local e = cubes[i]
        -- Each cube rotates at a slightly different speed
        local speed = base_speed * (0.5 + i * 0.3)
        angles[i] = angles[i] + speed * dt
        if angles[i] > 360 then angles[i] = angles[i] - 360 end
        -- Rotate around Z axis (Z-up): set_rotation(pitch, yaw, roll)
        -- For Z-up rotation around world Z, we set roll (3rd param)
        uz.scene.set_rotation(e, 0, 0, angles[i])
    end
end

function on_stop(entity)
    uz.log("CubeRotator stopped")
end
