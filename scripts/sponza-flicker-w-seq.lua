-- QA consumer: continuous single-run sequence at pose W (shadowed wall beside the barred
-- window). Holds the pose for HOLD frames (temporal history converges, static section),
-- then moves forward slowly (SPEED cm/frame) for SWEEP frames, then holds again.
-- Meant to be captured with `--screenshot-frames A-B` (every frame of ONE process), so
-- consecutive frames share history, timing and loading state.
local start_frame = nil
local HOLD, SWEEP, SPEED = 300, 200, 1.0
local PX, PY, PZ = 175.0, 1130.0, 165.0
local LX, LY, LZ = 246.0, 1420.0, 175.0
local DX, DY, DZ = 0.2380, 0.9713, 0.0335  -- normalized (look - pos)
local function apply(relative)
    local s = 0.0
    if relative > HOLD and relative <= HOLD + SWEEP then s = (relative - HOLD) * SPEED
    elseif relative > HOLD + SWEEP then s = SWEEP * SPEED end
    local x, y, z = PX + DX * s, PY + DY * s, PZ + DZ * s
    uz.camera.set_position(x, y, z)
    uz.camera.look_at(LX + DX * s, LY + DY * s, LZ + DZ * s)
    if relative == HOLD + 1 or (relative > HOLD and relative <= HOLD + SWEEP and relative % 20 == 0) or relative == HOLD + SWEEP + 1 then
        uz.log(string.format("FlickerSeqQA frame=%d relative=%d s=%.1f", uz.frame_index, relative, s))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("FlickerSeqQA started base_frame=%d static=%d..%d motion=%d..%d", start_frame, start_frame, start_frame + HOLD, start_frame + HOLD + 1, start_frame + HOLD + SWEEP))
    apply(0)
end
function on_update(entity, dt) apply(uz.frame_index - start_frame) end
function on_stop(entity) uz.log("FlickerSeqQA stopped") end
