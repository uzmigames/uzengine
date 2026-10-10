-- QA consumer for phase0_sponza-followup-02: a single hard camera cut.
--
-- Holds Camera_A's pose for 60 frames, then jumps in ONE update to the
-- ground-floor corridor pose the old fly_camera.lua used. That jump is what
-- exposed the shadow-cascade lag: the first frame after it rendered the
-- corridor with the previous pose's cascades, i.e. unshadowed.
--
-- Absolute frame indices are logged so `--screenshot-frame` can target the
-- jump frame and the one after it (the preload consumes ~37 indices before
-- the first presented frame, so relative counting is not enough).

local start_frame = nil
local jumped = false

local HOLD_FRAMES = 60

-- Camera_A (scene pose)
local A_POS  = { 86.0, 881.0, 159.0 }
local A_LOOK = { 67.8, 784.6, 178.4 }

-- Jump target: the same atrium, shifted 400 cm along X. A stale cascade shows
-- up as a wrong shadow pattern on the sunlit floor, which a jump into the dark
-- corridor would hide. (The corridor pose fly_camera.lua used to teleport to
-- is 0,-250,130 looking at 0,734.8,-43.6 — measured too, see notes.)
local B_POS  = { 486.0, 881.0, 159.0 }
local B_LOOK = { 467.8, 784.6, 178.4 }

local function place(pos, look)
    uz.camera.set_position(pos[1], pos[2], pos[3])
    uz.camera.look_at(look[1], look[2], look[3])
end

function on_start(entity)
    start_frame = uz.frame_index
    uz.log(string.format(
        "TeleportCameraQA started base_frame=%d jump_frame=%d after_jump_frame=%d",
        start_frame, start_frame + HOLD_FRAMES, start_frame + HOLD_FRAMES + 1))
    place(A_POS, A_LOOK)
end

function on_update(entity, dt)
    local relative = uz.frame_index - start_frame
    if relative < HOLD_FRAMES then
        place(A_POS, A_LOOK)
        if relative % 30 == 0 then
            uz.log(string.format("TeleportCameraQA frame=%d relative=%d phase=hold", uz.frame_index, relative))
        end
    else
        place(B_POS, B_LOOK)
        if not jumped then
            jumped = true
            uz.log(string.format("TeleportCameraQA frame=%d relative=%d phase=jump position=(%.1f,%.1f,%.1f)",
                uz.frame_index, relative, B_POS[1], B_POS[2], B_POS[3]))
        elseif relative % 30 == 0 then
            uz.log(string.format("TeleportCameraQA frame=%d relative=%d phase=settled", uz.frame_index, relative))
        end
    end
end

function on_stop(entity)
    uz.log("TeleportCameraQA stopped")
end
