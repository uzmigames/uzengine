-- camera_test_controller.lua — FPS camera with render path toggle
-- Migrated from runtime/demos/camera_test/camera_test.cpp
-- Z-UP coordinate system: X=forward, Y=right, Z=up
--
-- Controls:
--   WASD        — move camera (XY plane)
--   E/Q         — move up/down (Z axis)
--   Mouse       — look around (hold right-click)
--   Scroll      — zoom (adjust speed)
--   F1          — deferred render path
--   F2          — forward render path
--   Escape      — quit

local camera_speed = 500.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = -20.0
-- Camera starts at Y=-500 (behind), Z=200 (above), looking toward origin
local cam_x, cam_y, cam_z = 0, -500, 200

function on_start(entity)
    uz.log("=== Camera Test (Lua) ===")
    uz.log("WASD: move | QE: up/down | Right-click+Mouse: look | F1/F2: render path")
end

function on_update(entity, dt)
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

    -- Render path toggle
    if uz.input.is_key_just_pressed("F1") then
        uz.renderer.set_render_path("deferred")
        uz.log("Render path: Deferred")
    end
    if uz.input.is_key_just_pressed("F2") then
        uz.renderer.set_render_path("forward")
        uz.log("Render path: Forward")
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
    uz.log("Camera Test stopped")
end
