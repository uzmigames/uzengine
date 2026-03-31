-- fly_camera.lua — Universal FPS fly camera controller
-- Z-UP coordinate system: X=forward, Y=right, Z=up
--
-- Controls:
--   WASD        — move camera (XY plane)
--   E/Q         — move up/down (Z axis)
--   Mouse       — look around (hold right-click)
--   Scroll      — zoom (adjust speed)
--   F1          — deferred render path
--   F2          — forward render path
--   F           — toggle deferred/forward
--   0           — disable debug mode (normal lit)
--   1-7         — GBuffer debug channels
--   Tab         — cycle debug view modes
--   Escape      — quit

local camera_speed = 500.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = -20.0
local cam_x, cam_y, cam_z = 0, -500, 200
local render_path = "deferred"

local debug_views = {
    "Lit", "Albedo", "Normal", "Metallic", "Roughness",
    "AO", "Depth", "WorldPos", "Velocity", "Wireframe", "Overdraw"
}
local debug_view_index = 1

function on_start(entity)
    uz.log("=== Fly Camera ===")
    uz.log("WASD: move | QE: up/down | Right-click+Mouse: look | Scroll: speed")
    uz.log("F/F1/F2: render path | Tab: cycle views | 0-7: debug channels")
end

function on_update(entity, dt)
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

    -- Render path toggle
    if uz.input.is_key_just_pressed("F") then
        if render_path == "deferred" then
            render_path = "forward"
        else
            render_path = "deferred"
        end
        uz.renderer.set_render_path(render_path)
        uz.log("Render path: " .. render_path)
    end
    if uz.input.is_key_just_pressed("F1") then
        render_path = "deferred"
        uz.renderer.set_render_path(render_path)
        uz.log("Render path: Deferred")
    end
    if uz.input.is_key_just_pressed("F2") then
        render_path = "forward"
        uz.renderer.set_render_path(render_path)
        uz.log("Render path: Forward")
    end

    -- Debug mode keys 0-7
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

    -- Cycle debug view with Tab
    if uz.input.is_key_just_pressed("Tab") then
        debug_view_index = debug_view_index + 1
        if debug_view_index > #debug_views then
            debug_view_index = 1
        end
        uz.renderer.set_debug_view(debug_views[debug_view_index])
        uz.log("Debug view: " .. debug_views[debug_view_index])
    end

    -- Camera speed via scroll
    local _, scroll_y = uz.input.get_scroll_delta()
    if scroll_y ~= 0 then
        camera_speed = camera_speed + scroll_y * 50.0
        if camera_speed < 50.0 then camera_speed = 50.0 end
        if camera_speed > 5000.0 then camera_speed = 5000.0 end
    end

    -- Mouse look (right button held)
    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        yaw   = yaw   + dx * mouse_sensitivity
        pitch = pitch - dy * mouse_sensitivity
        if pitch >  89 then pitch =  89 end
        if pitch < -89 then pitch = -89 end
    end

    -- Z-up camera movement
    local rad = math.pi / 180.0
    local cy = math.cos(yaw * rad)
    local sy = math.sin(yaw * rad)
    local cp = math.cos(pitch * rad)
    local sp = math.sin(pitch * rad)

    local fwd_x = cy * cp
    local fwd_y = sy * cp
    local fwd_z = sp

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
    if uz.input.is_key_down("E") then cam_z = cam_z + speed end
    if uz.input.is_key_down("Q") then cam_z = cam_z - speed end

    uz.camera.set_position(cam_x, cam_y, cam_z)
    local look_dist = 1000.0
    uz.camera.look_at(
        cam_x + fwd_x * look_dist,
        cam_y + fwd_y * look_dist,
        cam_z + fwd_z * look_dist
    )
end

function on_stop(entity)
    uz.log("Fly Camera stopped")
end
