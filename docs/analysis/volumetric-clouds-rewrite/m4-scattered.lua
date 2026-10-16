-- Fixed for static/GBV captures, then move the sun, then the camera, then hold.
local first_frame
local function place(relative)
    local sun_phase = math.max(0, math.min(relative - 40, 40))
    local camera_phase = math.max(0, math.min(relative - 88, 32))
    local sun = uz.scene.find("Sun")
    if sun then uz.scene.set_rotation(sun, -40 - sun_phase * 0.5, 0, 0) end
    uz.camera.set_position(camera_phase * 2000, -150000, 200000)
    uz.camera.look_at(0, 200000, 0)
    if relative % 16 == 0 then
        uz.log(string.format("CloudM4Scattered relative=%d sunPitch=%.1f cameraX=%.1f",
            relative, -40 - sun_phase * 0.5, camera_phase * 2000))
    end
end
function on_start(entity)
    first_frame = uz.frame_index
    place(0)
end
function on_update(entity, dt) place(uz.frame_index - first_frame) end
