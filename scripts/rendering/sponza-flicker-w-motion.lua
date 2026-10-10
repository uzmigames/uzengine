-- QA consumer for the flicker elimination programme (followup-10 item 1.7, followup-14 item 1.4).
--
-- One continuous run at pose W (the barred window) covering the cases a candidate TAA change has to
-- survive, so a "fix" that only works on a still camera is caught. The strafe is slow enough that
-- the window stays inside the measurement rectangle, and the cut returns to the ORIGINAL pose, so
-- the recovery can be measured against the converged image of phase 1 — the same content, with a
-- history that belongs to somewhere else.
--
--   phase 1  HOLD    relative 0..119     still at pose W, history converges
--   phase 2  STRAFE  relative 120..239   0.5 cm/frame to the right (60 cm): the bars sweep across
--                                        the pane, so bright pixels are covered and uncovered
--   phase 3  HOLD    relative 240..299   still at the strafed pose
--   phase 4  CUT     relative 300        one-frame jump back to pose W: the history is now wrong
--   phase 5  HOLD    relative 301..380   still at pose W: recovery against the phase-1 image
--
-- Absolute frame indices are logged at every boundary (the preload consumes ~37 indices).

local start_frame = nil

local HOLD1, STRAFE, HOLD2, HOLD3 = 120, 120, 60, 80
local SPEED = 0.5

local PX, PY, PZ = 175.0, 1130.0, 165.0
local LX, LY, LZ = 246.0, 1420.0, 175.0

local function place(offset)
    uz.camera.set_position(PX + offset, PY, PZ)
    uz.camera.look_at(LX + offset, LY, LZ)
end

local function offset_for(relative)
    if relative <= HOLD1 then return 0.0 end
    if relative <= HOLD1 + STRAFE then return (relative - HOLD1) * SPEED end
    if relative <= HOLD1 + STRAFE + HOLD2 then return STRAFE * SPEED end
    return 0.0                      -- the cut: straight back to the phase-1 pose
end

function on_start(entity)
    start_frame = uz.frame_index
    local b1 = start_frame + HOLD1
    local b2 = b1 + STRAFE
    local b3 = b2 + HOLD2
    uz.log(string.format(
        "FlickerMotionQA base_frame=%d hold1=%d..%d strafe=%d..%d hold2=%d..%d cut=%d hold3=%d..%d speed=%.2f",
        start_frame, start_frame, b1, b1 + 1, b2, b2 + 1, b3, b3 + 1, b3 + 2, b3 + 1 + HOLD3, SPEED))
    place(0.0)
end

function on_update(entity, dt)
    local relative = uz.frame_index - start_frame
    place(offset_for(relative))
    if relative == HOLD1 or relative == HOLD1 + STRAFE or relative == HOLD1 + STRAFE + HOLD2
       or relative == HOLD1 + STRAFE + HOLD2 + 1 then
        uz.log(string.format("FlickerMotionQA phase boundary frame=%d relative=%d offset=%.1f",
                             uz.frame_index, relative, offset_for(relative)))
    end
end

function on_stop(entity) uz.log("FlickerMotionQA stopped") end
