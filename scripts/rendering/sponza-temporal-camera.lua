-- QA consumer: preserve Camera_A lens and move its active ECS camera deterministically.
local start_frame = nil
local last_phase = nil

local function apply(relative_frame)
    local offset, phase = 0, "stationary"
    if relative_frame > 120 and relative_frame <= 180 then
        offset = (relative_frame - 120) * 160 / 60
        phase = "sweep"
    elseif relative_frame > 180 and relative_frame <= 210 then
        offset = 160
        phase = "shifted"
    elseif relative_frame > 210 then
        phase = "returned"
    end
    local x, y, z = 86 + offset, 881, 159
    uz.camera.set_position(x, y, z)
    uz.camera.look_at(67.8 + offset, 784.6, 178.4)
    local actual_x, actual_y, actual_z = uz.camera.get_position()
    assert(actual_x and math.abs(actual_x - x) < 0.001
        and math.abs(actual_y - y) < 0.001 and math.abs(actual_z - z) < 0.001,
        "TemporalCameraQA active camera readback differs")
    if phase ~= last_phase or relative_frame % 30 == 0 or relative_frame == 212 then
        uz.log(string.format("TemporalCameraQA frame=%d relative=%d phase=%s position=(%.3f,%.3f,%.3f)",
            uz.frame_index, relative_frame, phase, actual_x, actual_y, actual_z))
    end
    last_phase = phase
end

function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("TemporalCameraQA started base_frame=%d sweep=%d return=%d settled=%d",
        start_frame, start_frame + 121, start_frame + 211, start_frame + 300))
    apply(0)
end

function on_update(entity, dt)
    apply(uz.frame_index - start_frame)
end

function on_stop(entity)
    uz.log("TemporalCameraQA stopped")
end
