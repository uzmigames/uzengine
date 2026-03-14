-- hello.lua — Smoke test script for runtime standalone mode.
-- Logs a message on start and rotates the entity each frame.

function on_start(entity)
    uz.log("Hello from Lua! Entity loaded.")
end

function on_update(entity, dt)
    local rx, ry, rz = uz.scene.get_rotation(entity)
    uz.scene.set_rotation(entity, rx, ry + 45 * dt, rz)
end

function on_stop(entity)
    uz.log("Goodbye from Lua!")
end
