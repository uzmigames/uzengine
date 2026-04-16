-- Mushroom character controller.
--
-- Reads keyboard input and drives the AnimationController attached to the
-- entity via state transitions. The mapping is intentionally game-layer logic:
-- the engine exposes generic APIs (`uz.animation.*`, `uz.input.*`) and this
-- script wires them to specific clip names + key bindings.
--
-- Bindings:
--   W          Mushroom_Walk
--   S          Mushroom_WalkBack
--   Q          Mushroom_Block01 (hold)
--   Space      Mushroom_Attack01 (one-shot)
--   E          Mushroom_Attack02 (one-shot)
--   R          Mushroom_Attack03 (one-shot)
--   F          Mushroom_Hit      (one-shot)
--   X          Mushroom_Death    (terminal state)
--   (none)     Mushroom_Idle

local BLEND_SHORT = 0.10
local BLEND_LONG  = 0.20

local one_shot_clips = {
    Mushroom_Attack01 = true,
    Mushroom_Attack02 = true,
    Mushroom_Attack03 = true,
    Mushroom_Hit      = true,
    Mushroom_Death    = true,
}

local dead = false

-- `self` is injected by the script system as the entity this script is attached
-- to; `dt` is the delta time in seconds.
function on_start(self)
    -- The initial clip was already set by the scene loader (initialClip).
    -- Nothing to prime here — keep the function defined so on_start hooks exist.
    dead = false
end

function on_update(self, dt)
    local current = uz.animation.get_current_clip(self) or ""

    -- Once dead, the controller stays in the death pose until someone resets.
    if dead then return end
    if current == "Mushroom_Death" then
        dead = true
        return
    end

    -- Death input has the highest priority.
    if uz.input.is_key_just_pressed("X") then
        uz.animation.crossfade(self, "Mushroom_Death", BLEND_SHORT, false, 1.0)
        return
    end

    -- If a one-shot clip is playing, let it finish before accepting new input.
    if one_shot_clips[current] then
        return
    end

    -- One-shot attacks / hit.
    if uz.input.is_key_just_pressed("SPACE") then
        uz.animation.crossfade(self, "Mushroom_Attack01", BLEND_SHORT, false, 1.0)
        return
    end
    if uz.input.is_key_just_pressed("E") then
        uz.animation.crossfade(self, "Mushroom_Attack02", BLEND_SHORT, false, 1.0)
        return
    end
    if uz.input.is_key_just_pressed("R") then
        uz.animation.crossfade(self, "Mushroom_Attack03", BLEND_SHORT, false, 1.0)
        return
    end
    if uz.input.is_key_just_pressed("F") then
        uz.animation.crossfade(self, "Mushroom_Hit", BLEND_SHORT, false, 1.0)
        return
    end

    -- Held locomotion / blocking.
    local q = uz.input.is_key_down("Q")
    local w = uz.input.is_key_down("W")
    local s = uz.input.is_key_down("S")

    local target
    if q then
        target = "Mushroom_Block01"
    elseif w then
        target = "Mushroom_Walk"
    elseif s then
        target = "Mushroom_WalkBack"
    else
        target = "Mushroom_Idle"
    end

    if target ~= current then
        uz.animation.crossfade(self, target, BLEND_LONG, true, 1.0)
    end
end

function on_stop(self)
    -- Nothing to tear down; the controller is owned by the subsystem.
end
