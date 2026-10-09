-- Switches SDF GI on at a fixed frame: the scene loads with `r.RadianceCache` 0, so no mesh
-- distance field exists, and the renderer must bake (or fetch from its cache) every field in the
-- background once the CVar flips (lighting block B, item 3: scripts/test_sdf_bake_cache.py,
-- gates C3 and C5). Attached to an entity of scenes/sponza-gi-runtime-enable.uzscene
-- (scripts/sponza_gi_runtime_enable.py writes that scene variant and its project).

local frame = 0
local ENABLE_AT = 60

function on_update(entity, dt)
    frame = frame + 1
    if frame == ENABLE_AT then
        uz.cvar.set("r.RadianceCache", 1)
        print("gi_enable_at_frame: r.RadianceCache=1 at frame " .. frame)
    end
end
