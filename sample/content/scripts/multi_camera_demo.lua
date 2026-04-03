-- multi_camera_demo.lua — Demonstrates CameraManager multi-camera switching
--
-- Creates 3 cameras at different positions and cycles between them every 3 seconds.
-- Attach this script to any entity in a scene to activate.
--
-- Controls:
--   Automatic cycling every 3 seconds
--   Press 1, 2, 3 to manually switch cameras
--   Press F5 to toggle editor free-cam (independent of this script)

local cameras = {}
local current_index = 1
local switch_interval = 3.0
local timer = 0.0

-- Camera definitions: {name, x, y, z, yaw_deg, pitch_deg}
local camera_defs = {
    { name = "Front",   x = 0,    y = -800, z = 300, yaw = 90,  pitch = -15 },
    { name = "Top",     x = 0,    y = 0,    z = 1500, yaw = 90,  pitch = -80 },
    { name = "Side",    x = -800, y = 0,    z = 300, yaw = 0,   pitch = -10 },
}

function on_start(entity)
    uz.log("Multi-Camera Demo: creating " .. #camera_defs .. " cameras")

    for i, def in ipairs(camera_defs) do
        local id = uz.camera.mgr_create(def.name)
        cameras[i] = { id = id, def = def }
        uz.log("  Camera '" .. def.name .. "' created (id=" .. id .. ")")
    end

    -- Switch to the first custom camera
    if #cameras > 0 then
        uz.camera.mgr_switch(cameras[1].id)
        apply_camera_transform(cameras[1].def)
        current_index = 1
    end

    uz.log("Multi-Camera Demo: press 1/2/3 to switch, or wait 3s for auto-cycle")
end

function apply_camera_transform(def)
    -- Use mgr_set_position/mgr_look_at to act directly on the CameraManager's
    -- active camera, bypassing ECS CameraComponent (which fly_camera.lua overwrites).
    uz.camera.mgr_set_position(def.x, def.y, def.z)

    -- Compute look direction from yaw/pitch
    local deg2rad = 3.14159265 / 180.0
    local yaw_rad = def.yaw * deg2rad
    local pitch_rad = def.pitch * deg2rad
    local cos_p = math.cos(pitch_rad)
    local target_x = def.x + math.cos(yaw_rad) * cos_p * 1000
    local target_y = def.y + math.sin(yaw_rad) * cos_p * 1000
    local target_z = def.z + math.sin(pitch_rad) * 1000
    uz.camera.mgr_look_at(target_x, target_y, target_z)
end

function switch_to(index)
    if index < 1 or index > #cameras then return end
    current_index = index
    local cam = cameras[current_index]
    uz.camera.mgr_switch(cam.id)
    apply_camera_transform(cam.def)
    uz.log("Camera switched to '" .. cam.def.name .. "' (id=" .. cam.id .. ")")
end

function on_update(entity, dt)
    -- Auto-cycle
    timer = timer + dt
    if timer >= switch_interval then
        timer = 0.0
        local next_idx = (current_index % #cameras) + 1
        switch_to(next_idx)
    end

    -- Manual switch: keys 1, 2, 3
    if uz.input and uz.input.is_key_just_pressed then
        if uz.input.is_key_just_pressed(0x31) then switch_to(1) end  -- '1'
        if uz.input.is_key_just_pressed(0x32) then switch_to(2) end  -- '2'
        if uz.input.is_key_just_pressed(0x33) then switch_to(3) end  -- '3'
    end
end

function on_stop(entity)
    uz.log("Multi-Camera Demo: cleaning up cameras")
    -- Switch back to Main camera (id=0) before destroying
    uz.camera.mgr_switch(0)
    for _, cam in ipairs(cameras) do
        uz.camera.mgr_destroy(cam.id)
    end
    cameras = {}
end
