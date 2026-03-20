-- cornell_box_setup.lua — Cornell Box GI test scene
-- Migrated from runtime/demos/cornell_box/cornell_box.cpp
-- Z-UP coordinate system: X=forward, Y=right, Z=up
--
-- Classic Cornell box: white room with red left wall, green right wall,
-- and a white diffuse sphere in the center.

local camera_speed = 500.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = 0.0
local cam_x, cam_y, cam_z = 0, -500, 100

-- Solid color material helper (no textures, just base color tint)
local function solid_material(r, g, b)
    return {
        base_color_r = r,
        base_color_g = g,
        base_color_b = b,
        roughness_multiplier = 0.9,
        metallic_multiplier = 0.0,
    }
end

function on_start(entity)
    uz.log("=== Cornell Box (Lua) ===")

    local white = solid_material(0.9, 0.9, 0.9)
    local red   = solid_material(0.8, 0.05, 0.05)
    local green = solid_material(0.05, 0.8, 0.05)

    -- Load cube mesh
    local cube_id = uz.mesh.load("meshes/cube.uzmesh")

    -- Generate sphere mesh
    local sphere_id = uz.mesh.generate_sphere(64, 32)

    -- ── Floor (Z=0, white) ──
    local floor = uz.scene.create_entity("Floor")
    uz.scene.set_position(floor, 0, 0, -5)
    uz.scene.set_scale(floor, 500, 500, 10)
    uz.scene.set_mesh(floor, cube_id)
    uz.scene.set_material(floor, white)

    -- ── Ceiling (Z=500, white) ──
    local ceiling = uz.scene.create_entity("Ceiling")
    uz.scene.set_position(ceiling, 0, 0, 505)
    uz.scene.set_scale(ceiling, 500, 500, 10)
    uz.scene.set_mesh(ceiling, cube_id)
    uz.scene.set_material(ceiling, white)

    -- ── Back wall (Y=250, white) ──
    local back = uz.scene.create_entity("BackWall")
    uz.scene.set_position(back, 0, 255, 250)
    uz.scene.set_scale(back, 500, 10, 500)
    uz.scene.set_mesh(back, cube_id)
    uz.scene.set_material(back, white)

    -- ── Left wall (X=-250, red) ──
    local left = uz.scene.create_entity("LeftWall")
    uz.scene.set_position(left, -255, 0, 250)
    uz.scene.set_scale(left, 10, 500, 500)
    uz.scene.set_mesh(left, cube_id)
    uz.scene.set_material(left, red)

    -- ── Right wall (X=250, green) ──
    local right = uz.scene.create_entity("RightWall")
    uz.scene.set_position(right, 255, 0, 250)
    uz.scene.set_scale(right, 10, 500, 500)
    uz.scene.set_mesh(right, cube_id)
    uz.scene.set_material(right, green)

    -- ── Center sphere (white diffuse) ──
    local sphere = uz.scene.create_entity("Sphere")
    uz.scene.set_position(sphere, 0, 0, 100)
    uz.scene.set_scale(sphere, 80, 80, 80)
    uz.scene.set_mesh(sphere, sphere_id)
    uz.scene.set_material(sphere, white)

    -- ── Sun light ──
    local sun = uz.scene.create_entity("Sun")
    uz.scene.set_position(sun, 0, 0, 920)
    uz.scene.set_rotation(sun, 50, -30, 0)
    uz.scene.add_directional_light(sun, 1.0, 1.0, 1.0, 10.0)
    uz.scene.set_atmosphere_sun_light(sun, true)

    -- ── Sky atmosphere ──
    local sky = uz.scene.create_entity("Sky")
    uz.scene.set_position(sky, 0, 0, 1020)
    uz.scene.create_sky_atmosphere(sky)
    uz.scene.set_sky_rayleigh_scale(sky, 1.0)
    uz.scene.set_sky_mie_scale(sky, 1.0)
    uz.scene.set_sky_multi_scattering(sky, 1.0)
    uz.scene.set_sky_ground_albedo(sky, 0.4, 0.4, 0.4)

    -- ── Sky light ──
    local skylight = uz.scene.create_entity("SkyLight")
    uz.scene.set_position(skylight, 0, 0, 690)
    uz.scene.create_sky_light(skylight)
    uz.scene.set_sky_light_intensity(skylight, 1.0)
    uz.scene.set_sky_light_color(skylight, 1.0, 1.0, 1.0)

    -- ── Height fog (minimal, pushed far below) ──
    local fog = uz.scene.create_entity("Fog")
    uz.scene.set_position(fog, 0, 0, -5000)
    uz.scene.create_fog(fog)
    uz.scene.set_fog_density(fog, 0.0436)
    uz.scene.set_fog_height_falloff(fog, 0.2)
    uz.scene.set_fog_base_height(fog, -5000)
    uz.scene.set_fog_max_opacity(fog, 1.0)
    uz.scene.set_fog_color(fog, 0, 0, 0)

    uz.log("Cornell Box: 5 walls + sphere + sun + sky + fog")
end

function on_update(entity, dt)
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

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
    uz.log("Cornell Box stopped")
end
