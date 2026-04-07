-- day_night_cycle.lua — Lua-driven day/night cycle
--
-- Rotates the Sun directional light through a full 24h cycle while
-- modulating its color and intensity to simulate dawn, noon, dusk and
-- night. Also adjusts SkyLight intensity so ambient lighting follows
-- the sun.
--
-- Time model:
--   time_of_day in [0, 24) hours
--   6h  = sunrise  (sun on horizon, east)
--   12h = noon     (sun overhead)
--   18h = sunset   (sun on horizon, west)
--   0h  = midnight (sun below horizon)
--
-- Z-up, X-forward. Sun pitch: 0 = horizon, 90 = straight down (noon).

local sun = nil
local skylight = nil

-- Cycle configuration
local time_of_day  = 12.0   -- start at noon
local day_length   = 60.0   -- seconds for a full 24h cycle (1 min)

-- Color presets (linear RGB)
local color_noon    = { 1.00, 0.98, 0.95 }
local color_golden  = { 1.00, 0.65, 0.35 }  -- dawn/dusk warm
local color_night   = { 0.20, 0.30, 0.55 }  -- cool moonlight tint

-- Intensity presets
local intensity_noon  = 18.0
local intensity_dusk  = 6.0
local intensity_night = 0.2

local skylight_noon  = 0.35
local skylight_night = 0.05

-- ── Helpers ─────────────────────────────────────────────────────────
local function lerp(a, b, t)
    return a + (b - a) * t
end

local function lerp_color(a, b, t)
    return lerp(a[1], b[1], t), lerp(a[2], b[2], t), lerp(a[3], b[3], t)
end

local function clamp01(x)
    if x < 0 then return 0 end
    if x > 1 then return 1 end
    return x
end

-- Returns (color r,g,b, intensity, skylight_intensity) for a given hour.
local function eval_cycle(hour)
    -- Sun height factor: 1 at noon (12h), 0 at horizon (6h/18h), -1 at midnight
    local angle = (hour - 6.0) / 12.0 * math.pi  -- 0 at 6h, pi at 18h
    local height = math.sin(angle)               -- [-1..1]

    if height <= 0.0 then
        -- Night
        return color_night[1], color_night[2], color_night[3],
               intensity_night, skylight_night
    end

    -- Day: blend golden (low sun) → noon (high sun)
    local t = clamp01(height)           -- 0 at horizon, 1 at noon
    local warm = 1.0 - t                -- warmth weight

    local r, g, b = lerp_color(color_noon, color_golden, warm)
    local intensity = lerp(intensity_dusk, intensity_noon, t)
    local sky_i     = lerp(skylight_night * 2.0, skylight_noon, t)
    return r, g, b, intensity, sky_i
end

-- ── Lifecycle ───────────────────────────────────────────────────────
function on_start(entity)
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local n = uz.scene.get_name(e)
        if n == "Sun" then
            sun = e
        elseif n == "SkyLight" then
            skylight = e
        end
    end

    if sun == nil then
        uz.log("DayNight: Sun entity not found")
    end
    if skylight == nil then
        uz.log("DayNight: SkyLight entity not found")
    end

    -- Force a much bigger sun disk (JSON loader ignores light_source_angle).
    if sun ~= nil then
        uz.scene.set_light_source_angle(sun, 2.5)
    end

    -- Crank bloom + enable lens flare so the sun gets a proper glow/corona
    -- instead of looking like a flat white disk. The project-wide defaults
    -- are calibrated for subtle bloom; this scene needs a real sun.
    uz.cvar.set("r.Bloom.Intensity", 0.35)
    uz.cvar.set("r.Bloom.Threshold", 1.0)
    uz.cvar.set("r.Bloom.SizeScale", 8.0)
    uz.cvar.set("r.Bloom.Radius", 2.0)
    uz.cvar.set("r.LensFlare", 1)
    uz.cvar.set("r.LensFlare.Intensity", 1.5)
    uz.cvar.set("r.LensFlare.HaloRadius", 0.8)
    uz.cvar.set("r.LensFlare.ChromaticShift", 0.04)

    uz.log(string.format("DayNight: cycle started (day_length=%.0fs)", day_length))
end

function on_update(entity, dt)
    -- Advance time
    time_of_day = time_of_day + (24.0 / day_length) * dt
    while time_of_day >= 24.0 do
        time_of_day = time_of_day - 24.0
    end

    -- Sun elevation (rotation around Y in DX convention) — Forward = +X.
    -- 0 at 6h (east horizon), 90 at 12h (overhead), 180 at 18h,
    -- 270 at 0h (underground). Rotating +X around Y moves it into the
    -- XZ plane, giving vertical sun motion.
    local elev = (time_of_day - 6.0) / 24.0 * 360.0
    if sun ~= nil then
        -- Tilt a bit on roll so the sun traces an arc instead of
        -- passing exactly overhead (looks more natural).
        uz.scene.set_rotation(sun, 0, elev, 15)
    end

    -- Color / intensity
    local r, g, b, intensity, sky_i = eval_cycle(time_of_day)
    if sun ~= nil then
        uz.scene.set_light_color(sun, r, g, b)
        uz.scene.set_light_intensity(sun, intensity)
    end
    if skylight ~= nil then
        uz.scene.set_sky_light_intensity(skylight, sky_i)
    end
end

function on_stop(entity)
    uz.log("DayNight: stopped")
end
