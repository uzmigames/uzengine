local function place()
    uz.camera.set_position(0, -150000, 200000)
    uz.camera.look_at(0, 200000, 0)
end
function on_start(entity) place() end
function on_update(entity, dt) place() end
