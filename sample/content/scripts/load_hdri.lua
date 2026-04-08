-- load_hdri.lua — Loads HDRI environment map for IBL reflections.
-- Attach to any entity in the scene. Loads on frame 3 to avoid descriptor ring issues.

local loaded = false
local frame = 0

function on_start(entity)
end

function on_update(entity, dt)
    if loaded then return end
    frame = frame + 1
    if frame >= 3 then
        loaded = true
        local srv = uz.texture.load_hdr("textures/hdri/grasslands_sunset_4k.hdr")
        if srv then
            uz.renderer.set_hdri_environment(srv)
            uz.log("HDRI environment loaded (SRV=" .. tostring(srv) .. ")")
        else
            uz.log("WARNING: Failed to load HDRI")
        end
    end
end

function on_stop(entity)
end
