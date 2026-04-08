-- decal_setup.lua — Loads decal textures and creates decal entities
-- Decals require runtime texture loading to get SRV indices

function on_start(entity)
    uz.log("=== Decal Setup ===")

    -- Load decal textures
    local leak_albedo = uz.texture.load("textures/decals/leaking_stain_color.png")
    local leak_normal = uz.texture.load("textures/decals/leaking_stain_normal.png")

    local rust_albedo = uz.texture.load("textures/decals/rust_stain_color.png")
    local rust_normal = uz.texture.load("textures/decals/rust_stain_normal.png")

    local manhole_albedo = uz.texture.load("textures/decals/manhole_color.png")
    local manhole_normal = uz.texture.load("textures/decals/manhole_normal.png")

    uz.log("Decal textures loaded: leak=" .. tostring(leak_albedo)
        .. " rust=" .. tostring(rust_albedo)
        .. " manhole=" .. tostring(manhole_albedo))

    -- Decal on floor: leaking stain
    local d1 = uz.scene.create_entity("Decal_LeakFloor")
    uz.scene.set_position(d1, -150, 50, 1)
    uz.scene.set_scale(d1, 300, 300, 50)
    uz.scene.create_decal(d1)
    if leak_albedo then uz.scene.set_decal_albedo(d1, leak_albedo) end
    if leak_normal then uz.scene.set_decal_normal(d1, leak_normal) end

    -- Decal on floor: rust stain
    local d2 = uz.scene.create_entity("Decal_RustFloor")
    uz.scene.set_position(d2, 150, -50, 1)
    uz.scene.set_rotation(d2, 0, 45, 0)
    uz.scene.set_scale(d2, 250, 250, 50)
    uz.scene.create_decal(d2)
    if rust_albedo then uz.scene.set_decal_albedo(d2, rust_albedo) end
    if rust_normal then uz.scene.set_decal_normal(d2, rust_normal) end

    -- Decal on wall: rust stain
    local d3 = uz.scene.create_entity("Decal_RustWall")
    uz.scene.set_position(d3, 0, 398, 150)
    uz.scene.set_rotation(d3, 0, 0, 90)
    uz.scene.set_scale(d3, 300, 300, 50)
    uz.scene.create_decal(d3)
    if rust_albedo then uz.scene.set_decal_albedo(d3, rust_albedo) end
    if rust_normal then uz.scene.set_decal_normal(d3, rust_normal) end

    -- Decal on floor: manhole
    local d4 = uz.scene.create_entity("Decal_Manhole")
    uz.scene.set_position(d4, 0, 150, 1)
    uz.scene.set_scale(d4, 200, 200, 30)
    uz.scene.create_decal(d4)
    if manhole_albedo then uz.scene.set_decal_albedo(d4, manhole_albedo) end
    if manhole_normal then uz.scene.set_decal_normal(d4, manhole_normal) end

    uz.log("Decal setup: 4 decals created")
end

function on_update(entity, dt)
end

function on_stop(entity)
    uz.log("Decal setup stopped")
end
