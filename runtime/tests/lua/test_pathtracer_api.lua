-- Headless API coverage, executed by LuaPathTracerAPITest.ScriptBindings.
local pt = uz.pathtracer
assert(type(pt) == "table")

local enabled = pt.is_enabled()
local max_bounces = uz.cvar.get("r.PathTracer.MaxBounces")
local samples_per_frame = uz.cvar.get("r.PathTracer.SamplesPerFrame")
local env_intensity = uz.cvar.get("r.PathTracer.EnvIntensity")

pt.enable(true)
assert(pt.is_enabled() == true)
assert(uz.cvar.get("r.PathTracer.Enable") == 1)
pt.enable(false)
assert(pt.is_enabled() == false)
assert(uz.cvar.get("r.PathTracer.Enable") == 0)

pt.set_max_bounces(8)
assert(uz.cvar.get("r.PathTracer.MaxBounces") == 8)
pt.set_max_bounces(100)
assert(uz.cvar.get("r.PathTracer.MaxBounces") == 16)
pt.set_samples_per_frame(3)
assert(uz.cvar.get("r.PathTracer.SamplesPerFrame") == 3)
pt.set_samples_per_frame(0)
assert(uz.cvar.get("r.PathTracer.SamplesPerFrame") == 1)
pt.set_env_intensity(2.5)
assert(uz.cvar.get("r.PathTracer.EnvIntensity") == 2.5)
pt.set_env_intensity(-1)
assert(uz.cvar.get("r.PathTracer.EnvIntensity") == 0)

pt.reset_accum()
local samples = pt.samples_accumulated()
assert(type(samples) == "number")
assert(samples >= 0 and samples == math.floor(samples))

pt.enable(enabled)
pt.set_max_bounces(max_bounces)
pt.set_samples_per_frame(samples_per_frame)
pt.set_env_intensity(env_intensity)
