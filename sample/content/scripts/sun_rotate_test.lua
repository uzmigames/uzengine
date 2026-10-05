-- Rotates the entity this script is attached to (the Sun) once, at frame 100, to a yaw of 90
-- degrees about Z. uz.scene.set_rotation_yaw replaces the whole rotation, so the sun also drops to
-- the horizon: a large, unambiguous change of the sky. Used by scripts/test_sky_ibl_rebake.py to
-- check that the sky IBL (cubemap capture, SH, prefiltered maps) is re-baked when the sun moves
-- (block A.5, 2026-10-05).

local frame = 0

function on_update(entity, dt)
    frame = frame + 1
    if frame == 100 then
        uz.scene.set_rotation_yaw(entity, math.rad(90))
        print("sun_rotate_test: sun rotated at frame " .. frame)
    end
end
