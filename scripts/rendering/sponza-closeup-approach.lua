-- QA consumer: slow approach toward the mesh the user reported as "balancando quando a
-- camera chega perto" (P pose (-223.3,-599.6,576.6) -> (-418.6,-1577.7,648.7)). Starts
-- 300 cm behind that pose along the view direction, holds 60 frames, then advances 400 cm
-- over 200 frames (2 cm/frame) through the reported pose, then holds. Logs every 20 frames.
local start_frame = nil
local HOLD, SWEEP, DIST = 60, 200, 400.0
local PX, PY, PZ = -223.3, -599.6, 576.6
local DX, DY, DZ = -0.1950, -0.9768, 0.0720   -- normalized view direction
local BACK = 300.0
local function apply(relative)
    local t = 0.0
    if relative > HOLD and relative <= HOLD + SWEEP then t = (relative - HOLD) * DIST / SWEEP
    elseif relative > HOLD + SWEEP then t = DIST end
    local s = -BACK + t
    local x, y, z = PX + DX * s, PY + DY * s, PZ + DZ * s
    uz.camera.set_position(x, y, z)
    uz.camera.look_at(x + DX * 1000.0, y + DY * 1000.0, z + DZ * 1000.0)
    if relative == HOLD + 1 or (relative > HOLD and relative <= HOLD + SWEEP and relative % 20 == 0) or relative == HOLD + SWEEP + 1 then
        uz.log(string.format("CloseupApproachQA frame=%d relative=%d s=%.1f pos=(%.1f, %.1f, %.1f)", uz.frame_index, relative, s, x, y, z))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("CloseupApproachQA started base_frame=%d", start_frame))
    apply(0)
end
function on_update(entity, dt) apply(uz.frame_index - start_frame) end
function on_stop(entity) uz.log("CloseupApproachQA stopped") end
