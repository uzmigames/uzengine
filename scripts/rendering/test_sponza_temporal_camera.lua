-- Runs the real QA script lifecycle under LuaJIT with camera API spies.
-- This verifies scheduling and API use; actual renderer movement requires native QA logs/images.
local position = {86, 881, 159}
local target = nil
uz = {frame_index=37, log=print, camera={}}
uz.camera.set_position = function(x,y,z) position={x,y,z} end
uz.camera.get_position = function() return unpack(position) end
uz.camera.look_at = function(x,y,z) target={x,y,z} end
assert(loadfile("scripts/rendering/sponza-temporal-camera.lua"))()
on_start(nil)
local expected = {[120]=86, [121]=86+160/60, [150]=166, [180]=246, [210]=246, [211]=86, [212]=86, [300]=86}
local checked=0
for relative=1,300 do
    uz.frame_index=37+relative
    on_update(nil, 1/60)
    if expected[relative] then
        assert(math.abs(position[1]-expected[relative])<0.00001, "Wrong movement at "..relative)
        assert(math.abs((target[1]-position[1])-(67.8-86))<0.00001, "View direction changed")
        checked=checked+1
    end
end
on_stop(nil)
print("PASS TemporalCameraQA schedule/readback: "..checked.." checkpoints; native run pending")
