-- QA consumer for phase0_sponza-followup-06 item 4.4: a camera sweep long enough
-- to cross radiance-cache grid cells (spacing = 2*GridExtent/15 ~ 267 cm at the
-- Sponza project's GridExtent 2000). Holds Camera_A for 60 frames, then moves
-- 900 cm along +X over 150 frames (6 cm/frame), then holds. Each cell crossing
-- used to drop the world gather for one frame and reset its history — the
-- "whole scene flickers while flying" report. Logs every 10 frames of motion.
local start_frame = nil
local HOLD, SWEEP, DIST = 60, 150, 900.0
local function apply(relative)
    local offset = 0.0
    if relative > HOLD and relative <= HOLD + SWEEP then offset = (relative - HOLD) * DIST / SWEEP
    elseif relative > HOLD + SWEEP then offset = DIST end
    uz.camera.set_position(86.0 + offset, 881.0, 159.0)
    uz.camera.look_at(67.8 + offset, 784.6, 178.4)
    if relative == HOLD + 1 or (relative > HOLD and relative <= HOLD + SWEEP and relative % 10 == 0) or relative == HOLD + SWEEP + 1 then
        uz.log(string.format("LongSweepQA frame=%d relative=%d x=%.1f", uz.frame_index, relative, 86.0 + offset))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("LongSweepQA started base_frame=%d sweep=%d..%d", start_frame, start_frame + HOLD + 1, start_frame + HOLD + SWEEP))
    apply(0)
end
function on_update(entity, dt) apply(uz.frame_index - start_frame) end
function on_stop(entity) uz.log("LongSweepQA stopped") end
