-- instancing_grid_setup.lua — Spawns 1000 cubes in a 10x10x10 grid
-- Migrated from runtime/demos/instancing_test/instancing_test.cpp
-- Tests GPU instancing: all cubes share the same mesh+material → 1-2 draw calls

local GRID_SIZE = 10       -- 10x10x10 = 1000 cubes
local SPACING   = 250.0    -- cm between cube centers
local CUBE_SCALE = 40.0    -- cm per cube
local GRID_LIFT_Z = 1200.0 -- vertical offset so grid floats above ground

function on_start(entity)
    uz.log("=== Instancing Test: spawning " .. (GRID_SIZE^3) .. " cubes ===")

    local mesh_id = uz.mesh.load("meshes/cube.uzmesh")

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
                    albedo = "textures/Stonebricks/stonebricks_albedo.jpg",
                    normal = "textures/Stonebricks/stonebricks_normal.jpg",
                    roughness = "textures/Stonebricks/stonebricks_roughness.jpg",
                    metallic = "textures/Stonebricks/stonebricks_metallic.jpg",
                    ao = "textures/Stonebricks/stonebricks_ao.jpg"
                })
            end
        end
    end

    uz.log("Instancing grid complete: " .. (GRID_SIZE^3) .. " cubes spawned")
end

function on_update(entity, dt)
    -- Static scene — no per-frame updates
end

function on_stop(entity)
    uz.log("Instancing test stopped")
end
