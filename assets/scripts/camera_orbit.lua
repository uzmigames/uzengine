-- camera_orbit.lua -- Demo script: orbits the camera around the scene center.
--
-- Attach to any node via .uzscene:
--   script = assets/scripts/camera_orbit.lua
--
-- The camera circles at a fixed radius and height, always looking at the origin.

local radius = 25
local height = 12
local speed  = 0.4  -- radians per second

function on_start()
    uz.log("camera_orbit: started")
end

function on_update(dt)
    local t = uz.total_time()
    local angle = t * speed

    local x = math.sin(angle) * radius
    local z = math.cos(angle) * radius
    local y = height

    uz.camera.set_position(x, y, z)
    uz.camera.look_at(0, 2, 0)
end

function on_stop()
    uz.log("camera_orbit: stopped")
end
