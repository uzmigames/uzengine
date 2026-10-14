-- Deterministic camera consumer: hold, orbit/translate, hold, cut, recovery.
local start_frame
local function place(relative)
    local phase = math.max(0, math.min(relative - 16, 64))
    if relative >= 112 then phase = 0 end
    local angle = math.rad(180 + phase * 0.5)
    local eye_y = phase * 1000
    uz.camera.set_position(0, eye_y, 200)
    uz.camera.look_at(math.cos(angle) * 906.3078,
                      eye_y + math.sin(angle) * 906.3078, 622.6183)
    if relative % 16 == 0 or relative == 112 then
        uz.log(string.format("CloudM3Motion frame=%d relative=%d yaw=%.2f eyeY=%.1f",
                             uz.frame_index, relative, 180 + phase * 0.5, eye_y))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("CloudM3Motion start_frame=%d", start_frame))
    place(0)
end
function on_update(entity, dt) place(uz.frame_index - start_frame) end
