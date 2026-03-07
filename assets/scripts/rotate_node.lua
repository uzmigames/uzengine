-- rotate_node.lua -- Demo script: continuously rotates the attached node.
--
-- Attach to a StaticMesh node via .uzscene:
--   script = assets/scripts/rotate_node.lua

local rotation_speed = 45.0  -- degrees per second

function on_update(dt)
    local rx, ry, rz = uz.scene.get_rotation(NODE_ID)
    ry = ry + rotation_speed * dt
    uz.scene.set_rotation(NODE_ID, rx, ry, rz)
end
