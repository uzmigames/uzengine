-- Orbit/translate, then hold while cloud wind continues moving the shadows.
local first_frame
local function place(relative)
    local phase = math.max(0, math.min(relative - 32, 48))
    local angle = math.rad(phase * 0.4)
    local y = -150000 + phase * 1000
    uz.camera.set_position(math.sin(angle) * 150000, y, 200000)
    uz.camera.look_at(0, 200000, 0)
    if relative % 16 == 0 then
        uz.log(string.format("CloudM4Motion frame=%d relative=%d angle=%.2f eyeY=%.1f",
            uz.frame_index, relative, phase * 0.4, y))
    end
end
function on_start(entity)
    first_frame = uz.frame_index
    uz.log(string.format("CloudM4Motion start_frame=%d", first_frame))
    place(0)
end
function on_update(entity, dt) place(uz.frame_index - first_frame) end
