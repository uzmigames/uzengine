-- hdri_setup.lua — Loads HDRI panorama and creates PBR material spheres
-- Migrated from runtime/demos/hdri_test/hdri_test.cpp
-- 7 spheres with varying metallic/roughness to showcase IBL reflections

local hdri_loaded = false

function on_start(entity)
    uz.log("=== HDRI Environment Test (Lua) ===")

    -- Load sphere mesh
    local sphere_id = uz.mesh.generate_sphere(64, 32)

    -- 7 material variations
    local materials = {
        { r=0.95, g=0.95, b=0.95, metal=1.0, rough=0.05 }, -- Chrome
        { r=0.85, g=0.85, b=0.87, metal=1.0, rough=0.30 }, -- Brushed metal
        { r=0.70, g=0.72, b=0.73, metal=1.0, rough=0.70 }, -- Rough metal
        { r=1.00, g=0.77, b=0.34, metal=1.0, rough=0.15 }, -- Gold
        { r=0.80, g=0.10, b=0.10, metal=0.0, rough=0.10 }, -- Smooth plastic (red)
        { r=0.20, g=0.30, b=0.80, metal=0.0, rough=0.60 }, -- Rough plastic (blue)
        { r=0.50, g=0.90, b=0.50, metal=0.5, rough=0.30 }, -- Semi-metallic (green)
    }

    local spacing = 250.0
    local count = #materials
    local half_width = (count - 1) * spacing * 0.5

    for i, mat in ipairs(materials) do
        local x = -half_width + (i - 1) * spacing
        local e = uz.scene.create_entity("Sphere_" .. i)
        uz.scene.set_position(e, x, 0, 100)
        uz.scene.set_scale(e, 100, 100, 100)
        uz.scene.set_mesh(e, sphere_id)
        uz.scene.set_material(e, {
            base_color_r = mat.r,
            base_color_g = mat.g,
            base_color_b = mat.b,
            metallic_multiplier = mat.metal,
            roughness_multiplier = mat.rough,
        })
    end

    uz.log("HDRI test: 7 spheres created, HDRI will load on first frame")
end

function on_update(entity, dt)
    -- Load HDRI on first update frame (GPU command list available)
    if not hdri_loaded then
        hdri_loaded = true
        local hdri_srv = uz.texture.load_hdr("textures/hdri/grasslands_sunset_4k.exr")
        if hdri_srv then
            uz.log("HDRI loaded: SRV=" .. tostring(hdri_srv))
            local ok = uz.renderer.set_hdri_environment(hdri_srv)
            uz.log("HDRI environment set: " .. tostring(ok))
        else
            uz.log("WARNING: Failed to load HDRI panorama")
        end
    end
end

function on_stop(entity)
    uz.log("HDRI test stopped")
end
