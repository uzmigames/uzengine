-- QA consumer for followup-13: clipmap-boundary crossings, one axis at a time.
--
-- The global distance field's clipmap 0 recentres whenever the camera crosses a voxel of
-- 19.53 cm (extent 2500 cm, resolution 256). This trajectory crosses that grid deliberately and
-- separately on each axis and in each direction, with a stop before and a stop after every
-- crossing, so a step in the image can be tied to the event that caused it instead of to the
-- motion in general:
--
--   per segment (140 frames): 40 still, 60 moving at 1 cm/frame (60 cm = about 3 voxels), 40 still
--   segments in order: +X, -X, +Y, -Y, +Z, -Z
--
-- The camera looks at a fixed point in the atrium, so the same surfaces stay on screen and a
-- world-space comparison has something to track. Absolute frame indices are logged for every
-- segment boundary (the preload consumes ~37 indices).

local start_frame = nil

local HOLD_A, MOVE, HOLD_B = 40, 60, 40
local SEGMENT = HOLD_A + MOVE + HOLD_B
local SPEED = 1.0

local PX, PY, PZ = 175.0, 1130.0, 165.0
local LX, LY, LZ = 246.0, 1420.0, 175.0

-- axis index and sign per segment
local SEGMENTS = {
    { axis = 1, sign = 1 }, { axis = 1, sign = -1 },
    { axis = 2, sign = 1 }, { axis = 2, sign = -1 },
    { axis = 3, sign = 1 }, { axis = 3, sign = -1 },
}

local function offsets_for(relative)
    local off = { 0.0, 0.0, 0.0 }
    for i = 1, #SEGMENTS do
        local seg = SEGMENTS[i]
        local base = (i - 1) * SEGMENT
        local t = relative - base
        local travelled
        if t <= HOLD_A then
            travelled = 0.0
        elseif t <= HOLD_A + MOVE then
            travelled = (t - HOLD_A) * SPEED
        else
            travelled = MOVE * SPEED
        end
        if t > 0 then
            off[seg.axis] = off[seg.axis] + seg.sign * travelled
        end
    end
    return off
end

local function place(relative)
    local o = offsets_for(relative)
    uz.camera.set_position(PX + o[1], PY + o[2], PZ + o[3])
    uz.camera.look_at(LX + o[1], LY + o[2], LZ + o[3])
end

function on_start(entity)
    start_frame = uz.frame_index
    local msg = string.format("GdfBoundaryQA base_frame=%d segment=%d hold=%d move=%d speed=%.1f",
                              start_frame, SEGMENT, HOLD_A, MOVE, SPEED)
    for i = 1, #SEGMENTS do
        local base = start_frame + (i - 1) * SEGMENT
        msg = msg .. string.format(" seg%d(axis=%d sign=%d)=%d..%d move=%d..%d",
                                   i, SEGMENTS[i].axis, SEGMENTS[i].sign,
                                   base, base + SEGMENT - 1, base + HOLD_A + 1, base + HOLD_A + MOVE)
    end
    uz.log(msg)
    place(0)
end

function on_update(entity, dt)
    local relative = uz.frame_index - start_frame
    place(relative)
    if relative % SEGMENT == 0 and relative > 0 then
        uz.log(string.format("GdfBoundaryQA segment boundary frame=%d relative=%d", uz.frame_index, relative))
    end
end

function on_stop(entity) uz.log("GdfBoundaryQA stopped") end
