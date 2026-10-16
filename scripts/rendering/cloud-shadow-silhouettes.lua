-- Hold below the cloud layer, with the ground filling the frame.
local function place()
    uz.camera.set_position(0, -100000, 250000)
    uz.camera.look_at(0, 0, 0)
end
function on_start(entity) place() end
function on_update(entity, dt) place() end
