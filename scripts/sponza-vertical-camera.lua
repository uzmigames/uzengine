-- QA consumer for phase0_sponza-followup-06 item 4.5: a vertical camera sweep that
-- crosses a radiance-cache grid cell on Z (spacing = 2*GridExtent/15 ~ 266.7 cm at the
-- Sponza project's GridExtent 2000). Pose reported by the user with fly_camera's P key:
-- position (-33.2, 55.8, 282.9) yaw -107 pitch -12.7, flicker while rising. Holds the
-- pose at z=159 for 60 frames, then rises 300 cm over 100 frames (3 cm/frame; the grid
-- recentre lands on absolute frame 132 with base_frame 37), then holds. Each crossing used to retrace every probe
-- with first-write semantics (no history, no multi-bounce feedback) -> a dark blink that
-- recovers over ~20 frames. Logs every 5 frames of motion.
local start_frame = nil
local HOLD, SWEEP, DIST = 60, 100, 300.0
local PX, PY, PZ = -33.2, 55.8, 159.0
local DX, DY, DZ = -285.1, -933.1, -219.3  -- look direction offset (yaw -107, pitch -12.7)
local function apply(relative)
    local offset = 0.0
    if relative > HOLD and relative <= HOLD + SWEEP then offset = (relative - HOLD) * DIST / SWEEP
    elseif relative > HOLD + SWEEP then offset = DIST end
    local z = PZ + offset
    uz.camera.set_position(PX, PY, z)
    uz.camera.look_at(PX + DX, PY + DY, z + DZ)
    if relative == HOLD + 1 or (relative > HOLD and relative <= HOLD + SWEEP and relative % 5 == 0) or relative == HOLD + SWEEP + 1 then
        uz.log(string.format("VerticalSweepQA frame=%d relative=%d z=%.1f", uz.frame_index, relative, z))
    end
end
function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format("VerticalSweepQA started base_frame=%d sweep=%d..%d", start_frame, start_frame + HOLD + 1, start_frame + HOLD + SWEEP))
    apply(0)
end
function on_update(entity, dt) apply(uz.frame_index - start_frame) end
function on_stop(entity) uz.log("VerticalSweepQA stopped") end
