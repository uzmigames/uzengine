-- instancing_grid_setup.lua — Spawns 1000 cubes in a 10x10x10 grid
-- Migrated from runtime/demos/instancing_test/instancing_test.cpp
-- Tests GPU instancing: all cubes share the same mesh+material → 1-2 draw calls

local GRID_SIZE  = 10      -- 10x10x10 = 1000 cubes
local SPACING    = 250.0   -- cm between cube centers
local CUBE_SCALE = 40.0    -- cm per cube
local GRID_LIFT_Z = 200.0  -- vertical offset above ground

function on_start(entity)
    uz.log("=== Instancing Test: spawning " .. (GRID_SIZE^3) .. " cubes ===")

    local mesh_id = uz.mesh.load("meshes/cube.uzmesh")

    -- Pre-load textures once, reuse IDs for all 1000 cubes
    local tex_albedo    = uz.texture.load("textures/Stonebricks/stonebricks_albedo.jpg")
    local tex_normal    = uz.texture.load("textures/Stonebricks/stonebricks_normal.jpg")
    local tex_roughness = uz.texture.load("textures/Stonebricks/stonebricks_roughness.jpg")
    local tex_metallic  = uz.texture.load("textures/Stonebricks/stonebricks_metallic.jpg")
    local tex_ao        = uz.texture.load("textures/Stonebricks/stonebricks_ao.jpg")

    uz.log("Textures loaded: albedo=" .. tostring(tex_albedo) ..
           " normal=" .. tostring(tex_normal) ..
           " rough=" .. tostring(tex_roughness) ..
           " metal=" .. tostring(tex_metallic) ..
           " ao=" .. tostring(tex_ao))

    local half = (GRID_SIZE - 1) * SPACING * 0.5

    for ix = 0, GRID_SIZE - 1 do
        for iy = 0, GRID_SIZE - 1 do
            for iz = 0, GRID_SIZE - 1 do
                local x = ix * SPACING - half
                local y = iy * SPACING - half
                local z = iz * SPACING + GRID_LIFT_Z

                local e = uz.scene.create_entity("Cube_" .. ix .. "_" .. iy .. "_" .. iz)
                uz.scene.set_position(e, x, y, z)
                uz.scene.set_scale(e, CUBE_SCALE, CUBE_SCALE, CUBE_SCALE)
                uz.scene.set_mesh(e, mesh_id)
                uz.scene.set_material(e, {
                    albedo    = tex_albedo,
                    normal    = tex_normal,
                    roughness = tex_roughness,
                    metallic  = tex_metallic,
                    ao        = tex_ao
                })
            end
        end
    end

    uz.log("Instancing grid complete: " .. (GRID_SIZE^3) .. " cubes spawned")
end

function on_update(entity, dt)
end

function on_stop(entity)
    uz.log("Instancing test stopped")
end
