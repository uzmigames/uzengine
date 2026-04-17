-- mushroom_controller.lua — 3rd-person Mushroom character controller.
--
-- Drives both the character (translation + rotation + animation) AND a
-- 3rd-person follow camera. The script is attached to the MushroomMonster
-- entity, so `self` IS the character.
--
-- Coordinates: Z-up, X=forward, Y=right, left-handed.
--
-- Movement bindings:
--   W           Walk forward
--   S           Walk backward
--   A           Turn left  (yaw -)
--   D           Turn right (yaw +)
--   Mouse       Hold RMB and drag to orbit camera around character
--   Scroll      Zoom in/out
--
-- Action bindings (override movement when fired):
--   Q           Mushroom_Block01 (held)
--   Space       Mushroom_Attack01 (one-shot)
--   E           Mushroom_Attack02 (one-shot)
--   R           Mushroom_Attack03 (one-shot)
--   F           Mushroom_Hit      (one-shot)
--   X           Mushroom_Death    (terminal state)
--   Escape      Quit

local MOVE_SPEED       = 220.0   -- cm/s, walk
local MOVE_SPEED_BACK  = 140.0
local TURN_SPEED       = 160.0   -- deg/s
local BLEND_SHORT      = 0.10
local BLEND_LONG       = 0.20

-- 3rd-person camera tuning ---------------------------------------------------
local CAM_DIST_DEFAULT = 380.0   -- cm behind character
local CAM_DIST_MIN     = 120.0
local CAM_DIST_MAX     = 1200.0
local CAM_HEIGHT       = 160.0   -- cm above character pivot
local CAM_LOOK_HEIGHT  = 110.0   -- look-at point above character pivot
local CAM_FOLLOW_LERP  = 8.0     -- bigger = stiffer
local CAM_PITCH_DEFAULT = -10.0  -- deg, downward (negative looks down)
local CAM_PITCH_MIN    = -75.0
local CAM_PITCH_MAX    =  35.0
local MOUSE_SENS       = 0.18

-- Logical -> ECS rotation: character yaw is around Z.
-- yaw=0 means facing +X (forward); +yaw rotates toward +Y (right).
local char_yaw = 0.0

-- Camera orbit state (yaw is around character; pitch is up/down).
local cam_yaw   = 180.0  -- deg; 180 = behind the character (camera at -X relative)
local cam_pitch = CAM_PITCH_DEFAULT
local cam_dist  = CAM_DIST_DEFAULT

-- Smoothed actual camera position (so it lerps to target).
local smooth_x, smooth_y, smooth_z = nil, nil, nil

-- One-shot animation tracking.
local one_shot_clips = {
    Mushroom_Attack01 = true,
    Mushroom_Attack02 = true,
    Mushroom_Attack03 = true,
    Mushroom_Hit      = true,
    Mushroom_Death    = true,
}

local dead = false

local function deg2rad(d) return d * math.pi / 180.0 end

-- Polynomial smooth lerp: factor = 1 - exp(-rate * dt) is frame-rate independent.
local function smooth(a, b, rate, dt)
    local f = 1.0 - math.exp(-rate * dt)
    return a + (b - a) * f
end

function on_start(self)
    dead = false
    -- Read the character's authored yaw so the camera starts behind it.
    -- The scene's authored rotation is (0,0,0), so char_yaw stays 0.
    -- Force initial camera placement on the next on_update.
    smooth_x, smooth_y, smooth_z = nil, nil, nil
    uz.log("=== Mushroom Controller ===")
    uz.log("WASD: move | Mouse RMB+drag: orbit | Scroll: zoom")
    uz.log("Space/E/R: attack | F: hit | Q: block | X: death | Esc: quit")
end

function on_update(self, dt)
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

    local current = uz.animation.get_current_clip(self) or ""

    -- Once dead, the controller stays in the death pose.
    if not dead then
        if current == "Mushroom_Death" then
            dead = true
        elseif uz.input.is_key_just_pressed("X") then
            uz.animation.crossfade(self, "Mushroom_Death", BLEND_SHORT, false, 1.0)
            return
        end
    end

    -- ── Camera input (always active, even when dead) ──────────────────────
    local _, scroll_y = uz.input.get_scroll_delta()
    if scroll_y ~= 0 then
        cam_dist = cam_dist - scroll_y * 30.0
        if cam_dist < CAM_DIST_MIN then cam_dist = CAM_DIST_MIN end
        if cam_dist > CAM_DIST_MAX then cam_dist = CAM_DIST_MAX end
    end

    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        cam_yaw   = cam_yaw   + dx * MOUSE_SENS
        cam_pitch = cam_pitch - dy * MOUSE_SENS
        if cam_pitch > CAM_PITCH_MAX then cam_pitch = CAM_PITCH_MAX end
        if cam_pitch < CAM_PITCH_MIN then cam_pitch = CAM_PITCH_MIN end
    end

    -- ── Read current character position ───────────────────────────────────
    local px, py, pz = uz.scene.get_position(self)
    if px == nil then
        -- Entity not ready yet (rare race during scene load).
        return
    end

    -- ── Movement / animation logic (skipped when dead or one-shot active) ─
    local target_clip = current
    if not dead and not one_shot_clips[current] then
        -- One-shot attacks / hit.
        if uz.input.is_key_just_pressed("SPACE") then
            uz.animation.crossfade(self, "Mushroom_Attack01", BLEND_SHORT, false, 1.0)
        elseif uz.input.is_key_just_pressed("E") then
            uz.animation.crossfade(self, "Mushroom_Attack02", BLEND_SHORT, false, 1.0)
        elseif uz.input.is_key_just_pressed("R") then
            uz.animation.crossfade(self, "Mushroom_Attack03", BLEND_SHORT, false, 1.0)
        elseif uz.input.is_key_just_pressed("F") then
            uz.animation.crossfade(self, "Mushroom_Hit", BLEND_SHORT, false, 1.0)
        else
            -- Held movement / blocking.
            local q = uz.input.is_key_down("Q")
            local w = uz.input.is_key_down("W")
            local s = uz.input.is_key_down("S")
            local a = uz.input.is_key_down("A")
            local d = uz.input.is_key_down("D")

            -- Turning is independent of forward movement.
            if a then char_yaw = char_yaw - TURN_SPEED * dt end
            if d then char_yaw = char_yaw + TURN_SPEED * dt end

            -- Translate along the character's facing direction.
            local rad = deg2rad(char_yaw)
            local fx, fy = math.cos(rad), math.sin(rad)
            local moved = false
            if q then
                target_clip = "Mushroom_Block01"
            elseif w then
                px = px + fx * MOVE_SPEED * dt
                py = py + fy * MOVE_SPEED * dt
                target_clip = "Mushroom_Walk"
                moved = true
            elseif s then
                px = px - fx * MOVE_SPEED_BACK * dt
                py = py - fy * MOVE_SPEED_BACK * dt
                target_clip = "Mushroom_WalkBack"
                moved = true
            else
                target_clip = "Mushroom_Idle"
            end

            -- Even pure turning should look "walking-in-place" if the user is
            -- holding A/D without W/S. Use Walk when turning, Idle when truly idle.
            if not moved and not q and (a or d) then
                target_clip = "Mushroom_Walk"
            end

            if target_clip ~= current then
                uz.animation.crossfade(self, target_clip, BLEND_LONG, true, 1.0)
            end

            -- Apply movement + facing to the entity transform.
            uz.scene.set_position(self, px, py, pz)
            uz.scene.set_rotation_yaw(self, deg2rad(char_yaw))
        end
    end

    -- ── 3rd-person camera follow ──────────────────────────────────────────
    -- Camera position = character + offset built from (cam_yaw, cam_pitch, cam_dist).
    -- cam_yaw=0 puts camera in front of character (+X). cam_yaw=180 puts it behind.
    local yaw_rad   = deg2rad(cam_yaw)
    local pitch_rad = deg2rad(cam_pitch)
    local cp = math.cos(pitch_rad)
    local sp = math.sin(pitch_rad)
    local cy = math.cos(yaw_rad)
    local sy = math.sin(yaw_rad)

    -- Offset from character to camera (camera looks back toward character).
    local off_x = cy * cp * cam_dist
    local off_y = sy * cp * cam_dist
    local off_z = sp     * cam_dist + CAM_HEIGHT

    local target_x = px + off_x
    local target_y = py + off_y
    local target_z = pz + off_z

    if smooth_x == nil then
        smooth_x, smooth_y, smooth_z = target_x, target_y, target_z
    else
        smooth_x = smooth(smooth_x, target_x, CAM_FOLLOW_LERP, dt)
        smooth_y = smooth(smooth_y, target_y, CAM_FOLLOW_LERP, dt)
        smooth_z = smooth(smooth_z, target_z, CAM_FOLLOW_LERP, dt)
    end

    uz.camera.set_position(smooth_x, smooth_y, smooth_z)
    uz.camera.look_at(px, py, pz + CAM_LOOK_HEIGHT)
end

function on_stop(self)
    -- Nothing to tear down; the controller is owned by the subsystem.
end
