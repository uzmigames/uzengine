-- pbr_controller.lua — FPS camera + renderer controls (full Lua, no C++)
--
-- Controls:
--   WASD        — move camera
--   Mouse       — look around (hold right-click)
--   F           — toggle deferred/forward render path
--   0           — disable debug mode
--   1-7         — GBuffer debug channels

local camera_speed = 5.0
local mouse_sensitivity = 0.15
local yaw = 0.0
local pitch = 15.0
local cam_x, cam_y, cam_z = 0, 3, -8
local render_path = "deferred"

function on_start(entity)
    uz.log("PBR Controller started — WASD to move, right-click + mouse to look")
end

function on_update(entity, dt)
    -- Toggle render path
    if uz.input.is_key_just_pressed("F") then
        if render_path == "deferred" then
            render_path = "forward"
        else
            render_path = "deferred"
        end
        uz.renderer.set_render_path(render_path)
        uz.log("Render path: " .. render_path)
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

    -- Mouse look (only when right button held)
    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        yaw   = yaw   - dx * mouse_sensitivity
        pitch = pitch - dy * mouse_sensitivity
        if pitch >  89 then pitch =  89 end
        if pitch < -89 then pitch = -89 end
    end

    -- Camera movement in view-relative axes
    local rad = math.pi / 180.0
    local cy = math.cos(yaw * rad)
    local sy = math.sin(yaw * rad)
    local cp = math.cos(pitch * rad)

    -- Forward = into screen
    local fwd_x = sy * cp
    local fwd_y = math.sin(pitch * rad)
    local fwd_z = cy * cp

    -- Right = perpendicular on XZ plane
    local right_x = cy
    local right_z = -sy

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
        cam_z = cam_z - right_z * speed
    end
    if uz.input.is_key_down("D") then
        cam_x = cam_x + right_x * speed
        cam_z = cam_z + right_z * speed
    end

    -- Apply camera transform
    uz.camera.set_position(cam_x, cam_y, cam_z)

    -- Look-at target: camera position + forward direction
    local look_dist = 10.0
    uz.camera.look_at(
        cam_x + fwd_x * look_dist,
        cam_y + fwd_y * look_dist,
        cam_z + fwd_z * look_dist
    )
end

function on_stop(entity)
    uz.log("PBR Controller stopped")
end
