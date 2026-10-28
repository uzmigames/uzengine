function on_update(entity, dt)
    local x = uz.frame_index >= 64 and 50 or 0
    uz.camera.set_position(x, 0, 150)
    uz.camera.look_at(x, 600, 150)
    if uz.frame_index == 64 then uz.log("UNIFIED_DENOISER_TRANSLATION frame=64 x=50") end
end
