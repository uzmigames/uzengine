-- pbr_showcase_controller.lua — FPS camera + renderer controls + debug views
-- Z-UP coordinate system: X=forward, Y=right, Z=up
--
-- Controls:
--   WASD        — move camera (XY plane)
--   E/Q         — move up/down (Z axis)
--   Mouse       — look around (hold right-click)
--   Scroll      — zoom (adjust speed)
--   F           — toggle deferred/forward render path
--   0           — disable debug mode (normal lit)
--   1-7         — GBuffer debug channels
--   Tab         — cycle debug view modes
--   Escape      — quit

local camera_speed = 5.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = -15.0
local cam_x, cam_y, cam_z = 0, -8, 3
local render_path = "deferred"

-- Debug view mode names (matches DebugViewMode enum)
local debug_views = {
    "Lit", "Albedo", "Normal", "Metallic", "Roughness",
    "AO", "Depth", "WorldPos", "Velocity", "Wireframe", "Overdraw"
}
local debug_view_index = 1  -- 1 = Lit (normal rendering)

function on_start(entity)
    uz.log("=== PBR Showcase (Z-up) ===")
    uz.log("WASD: move | QE: up/down | Right-click+Mouse: look | F: render path | Tab: cycle views")
    uz.log("Render path: " .. render_path)
end

function on_update(entity, dt)
    -- ── Quit ────────────────────────────────────────────────────────────
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

    -- ── Toggle render path ──────────────────────────────────────────────
    if uz.input.is_key_just_pressed("F") then
        if render_path == "deferred" then
            render_path = "forward"
        else
            render_path = "deferred"
        end
        uz.renderer.set_render_path(render_path)
        uz.log("Render path: " .. render_path)
    end

    -- ── Debug mode keys 0-7 ─────────────────────────────────────────────
    for i = 0, 7 do
        if uz.input.is_key_just_pressed(tostring(i)) then
            uz.renderer.set_debug_mode(i == 0 and 8 or (i - 1))
            if i == 0 then
                uz.log("Debug mode: off")
            else
                uz.log("Debug mode: " .. i)
            end
        end
    end

    -- ── Cycle debug view with Tab ───────────────────────────────────────
    if uz.input.is_key_just_pressed("Tab") then
        debug_view_index = debug_view_index + 1
        if debug_view_index > #debug_views then
            debug_view_index = 1
        end
        uz.renderer.set_debug_view(debug_views[debug_view_index])
        uz.log("Debug view: " .. debug_views[debug_view_index])
    end

    -- ── Camera speed adjust via scroll ──────────────────────────────────
    local _, scroll_y = uz.input.get_scroll_delta()
    if scroll_y ~= 0 then
        camera_speed = camera_speed + scroll_y * 0.5
        if camera_speed < 0.5 then camera_speed = 0.5 end
        if camera_speed > 50.0 then camera_speed = 50.0 end
    end

    -- ── Mouse look (only when right button held) ────────────────────────
    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        yaw   = yaw   + dx * mouse_sensitivity
        pitch = pitch - dy * mouse_sensitivity
        if pitch >  89 then pitch =  89 end
        if pitch < -89 then pitch = -89 end
    end

    -- ── Camera movement in Z-up view-relative axes ──────────────────────
    -- Z-up: X=forward, Y=right, Z=up
    -- yaw rotates around Z axis, pitch tilts up/down
    local rad = math.pi / 180.0
    local cy = math.cos(yaw * rad)
    local sy = math.sin(yaw * rad)
    local cp = math.cos(pitch * rad)
    local sp = math.sin(pitch * rad)

    -- Forward direction in Z-up (projects onto XY plane, Z from pitch)
    local fwd_x = cy * cp
    local fwd_y = sy * cp
    local fwd_z = sp

    -- Right direction (perpendicular to forward on XY plane)
    -- Must match FPSCameraController: right = (-sin(yaw), cos(yaw), 0)
    local right_x = -sy
    local right_y = cy

    local speed = camera_speed * dt

    if uz.input.is_key_down("W") then
        cam_x = cam_x + fwd_x * speed
        cam_y = cam_y + fwd_y * speed
        cam_z = cam_z + fwd_z * speed
    end
    if uz.input.is_key_down("S") then
        cam_x = cam_x - fwd_x * speed
        cam_y = cam_y - fwd_y * speed
        cam_z = cam_z - fwd_z * speed
    end
    if uz.input.is_key_down("A") then
        cam_x = cam_x - right_x * speed
        cam_y = cam_y - right_y * speed
    end
    if uz.input.is_key_down("D") then
        cam_x = cam_x + right_x * speed
        cam_y = cam_y + right_y * speed
    end
    -- Vertical movement (Z axis)
    if uz.input.is_key_down("E") then
        cam_z = cam_z + speed
    end
    if uz.input.is_key_down("Q") then
        cam_z = cam_z - speed
    end

    -- ── Apply camera ────────────────────────────────────────────────────
    uz.camera.set_position(cam_x, cam_y, cam_z)

    local look_dist = 10.0
    uz.camera.look_at(
        cam_x + fwd_x * look_dist,
        cam_y + fwd_y * look_dist,
        cam_z + fwd_z * look_dist
    )
end

function on_stop(entity)
    uz.log("PBR Showcase Controller stopped")
end
