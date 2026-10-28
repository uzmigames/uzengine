-- Phase D stage 4 relight responsiveness: step the Sun's intensity once, at a logged frame.
-- Attached to the "Sun" directional entity of surface-cache-stage4-sunstep.uzscene; the static
-- reference (surface-cache-stage4-sunstatic.uzscene) starts at the post-step intensity.
local STEP_RELATIVE = 100     -- frames after on_start
local AFTER = 6.75            -- the scene starts at 13.5
local base_frame = nil

function on_start(entity)
    base_frame = uz.frame_index
    uz.log(string.format("SunStepQA base_frame=%d", base_frame))
end

function on_update(entity, dt)
    local relative = uz.frame_index - base_frame
    if relative == STEP_RELATIVE then
        uz.scene.set_light_intensity(entity, AFTER)
        uz.log(string.format("SunStepQA step frame=%d relative=%d intensity=%.3f",
                             uz.frame_index, relative, AFTER))
    end
end
