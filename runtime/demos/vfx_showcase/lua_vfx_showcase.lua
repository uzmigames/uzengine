--- VFX Showcase — comprehensive reference for the uz.vfx Lua API.
--- Demonstrates emitters, trails, decals, composites, and budget queries.
---
--- Attach to any scene node:  { "script": "demos/vfx_showcase/lua_vfx_showcase.lua" }
--- Replace asset paths with your project's .uzvfx / .uzmat files as needed.

local M = {}

-- ─────────────────────────────────────────────────────────────────────────────
-- Configuration
-- ─────────────────────────────────────────────────────────────────────────────

local EMITTER_ASSET   = "vfx/spark_fountain.uzvfx"
local COMPOSITE_ASSET = "vfx/impact_composite.uzvfx"
local TRAIL_MATERIAL  = "materials/trail_glow.uzmat"
local DECAL_MATERIAL  = "materials/scorch_mark.uzmat"

local EMITTER_POS     = { 0,  0, 100 }   -- world-space cm
local DECAL_POS       = { 0, -5,  95 }
local DECAL_NORMAL    = { 0,  1,   0 }

local EMISSION_BASE   = 200.0   -- particles / second at rest
local EMISSION_AMP    = 150.0   -- sine-wave amplitude
local EMISSION_FREQ   = 1.5     -- oscillation frequency (Hz)
local BURST_INTERVAL  = 5.0     -- seconds between auto-bursts
local COMPOSITE_TIME  = 10.0    -- spawn composite at this elapsed time
local DECAL_FADE_TIME = 15.0    -- begin decal fade at this elapsed time
local LOG_INTERVAL    = 2.0     -- live-count log cadence (seconds)

-- ─────────────────────────────────────────────────────────────────────────────
-- on_start
-- ─────────────────────────────────────────────────────────────────────────────

--- Called once when attached to the entity.  Spawns emitter, trail, and decal.
--- @param ctx table  Engine context.
function M:on_start(ctx)
    self.elapsed        = 0.0
    self.last_log       = 0.0
    self.last_burst     = 0.0
    self.hue            = 0.0
    self.composite_done = false
    self.decal_faded    = false

    -- ── Emitter ──────────────────────────────────────────────────────────────
    self.emitter = uz.vfx:spawn(EMITTER_ASSET, EMITTER_POS)
    if self.emitter then
        uz.vfx:set_parameter(self.emitter, "emission_rate", EMISSION_BASE)
        uz.vfx:set_parameter(self.emitter, "gravity", { 0, 0, -98 })           -- Vec3
        uz.vfx:set_parameter_vec4(self.emitter, "color_tint", { 1, 0.6, 0.1, 1 })
        uz.vfx:set_lod_distances(self.emitter, 50, 200, 800)
        uz.vfx:set_lod_enabled(self.emitter, true)
        uz.vfx:start(self.emitter)
        uz.log("VFX Showcase: emitter spawned — " .. EMITTER_ASSET)
    else
        uz.log_warn("VFX Showcase: emitter spawn failed")
    end

    -- ── Trail ─────────────────────────────────────────────────────────────────
    self.trail = uz.vfx:create_trail({
        material_path      = TRAIL_MATERIAL,
        max_points         = 128,
        width_start        = 4.0,
        width_end          = 0.5,
        point_lifetime     = 1.2,
        min_point_distance = 2.0,
        sort_order         = 0,
        is_active          = true,
    })
    if self.trail then
        uz.vfx:set_trail_width(self.trail, 4.0, 0.5)
        uz.vfx:set_trail_lifetime(self.trail, 1.2)
        uz.vfx:set_trail_uv_scroll(self.trail, 0.5)
        uz.log("VFX Showcase: trail created")
    else
        uz.log_warn("VFX Showcase: trail creation failed")
    end

    -- ── Decal (lifetime 0 = persistent until faded / destroyed) ──────────────
    self.decal = uz.vfx:spawn_decal(DECAL_MATERIAL, DECAL_POS, DECAL_NORMAL, 0)
    if self.decal then
        uz.vfx:set_decal_blend_mode(self.decal, "multiply")
        uz.vfx:set_decal_normal_intensity(self.decal, 0.8)
        uz.log("VFX Showcase: decal spawned")
    else
        uz.log_warn("VFX Showcase: decal spawn failed")
    end

    -- ── Budget snapshot ───────────────────────────────────────────────────────
    local b = uz.vfx:get_budget_stats()
    uz.log(string.format("VFX Showcase: budget — total=%d used=%d avail=%d util=%.1f%%",
        b.total, b.used, b.available, b.utilization_pct))
end

-- ─────────────────────────────────────────────────────────────────────────────
-- on_update
-- ─────────────────────────────────────────────────────────────────────────────

--- Called every frame.
--- @param ctx table   Engine context.
--- @param dt  number  Frame delta time in seconds.
function M:on_update(ctx, dt)
    self.elapsed = self.elapsed + dt
    self.hue     = (self.hue + dt * 0.3) % 1.0
    local t = self.elapsed

    -- ── Emitter: sine-wave emission rate (set_emitter_field) ─────────────────
    if self.emitter then
        local rate = EMISSION_BASE + EMISSION_AMP * math.sin(t * EMISSION_FREQ * math.pi * 2)
        uz.vfx:set_emitter_field(self.emitter, "emission_rate", rate)
    end

    -- ── Trail: hue-cycle color animation (set_trail_color) ───────────────────
    if self.trail then
        local r, g, b = M._hue_to_rgb(self.hue)
        uz.vfx:set_trail_color(self.trail, { r, g, b, 1.0 }, { r*0.3, g*0.3, b*0.3, 0.0 })
    end

    -- ── Periodic: live particle count + LOD level ─────────────────────────────
    if t - self.last_log >= LOG_INTERVAL then
        self.last_log = t
        local alive    = self.emitter and uz.vfx:get_alive_count(self.emitter) or 0
        local lod      = self.emitter and uz.vfx:get_lod_level(self.emitter)   or -1
        local trail_pt = self.trail   and uz.vfx:get_trail_point_count(self.trail) or 0
        uz.log(string.format("VFX Showcase [t=%.1fs]: alive=%d lod=%d trail_pts=%d total=%d emitters=%d",
            t, alive, lod, trail_pt, uz.vfx:get_total_alive(), uz.vfx:get_emitter_count()))
    end

    -- ── Periodic: burst every BURST_INTERVAL (emit_event with velocity scale) ─
    if t - self.last_burst >= BURST_INTERVAL then
        self.last_burst = t
        if self.emitter then
            uz.vfx:emit_event(self.emitter, "burst", 300, 1.5)
            uz.log("VFX Showcase: burst fired")
        end
    end

    -- ── One-shot: composite effect at t=10 ────────────────────────────────────
    if not self.composite_done and t >= COMPOSITE_TIME then
        self.composite_done    = true
        self.composite_handles = uz.vfx:spawn_composite(COMPOSITE_ASSET, EMITTER_POS)
        if self.composite_handles then
            uz.log(string.format("VFX Showcase: composite spawned (%d sub-effects)",
                #self.composite_handles))
        else
            uz.log_warn("VFX Showcase: composite spawn failed")
        end
    end

    -- ── One-shot: fade decal at t=15 ─────────────────────────────────────────
    if not self.decal_faded and t >= DECAL_FADE_TIME then
        self.decal_faded = true
        if self.decal then
            uz.vfx:fade_out_decal(self.decal, 2.0)
            uz.log("VFX Showcase: decal fade-out started (2 s)")
        end
    end

    -- ── Per-second: read-back, trail length, pause demo ─────────────────────
    if self.emitter and math.floor(t) ~= math.floor(t - dt) then
        local _ = uz.vfx:get_emitter_field(self.emitter, "emission_rate")
              and uz.vfx:get_parameter(self.emitter, "color_tint")
        -- Briefly pause then resume to demonstrate pause(handle, bool).
        uz.vfx:pause(self.emitter, true)
        uz.vfx:pause(self.emitter, false)
    end
    if self.trail then
        local _len = uz.vfx:get_trail_length(self.trail)     -- arc-length in cm
        uz.vfx:set_trail_active(self.trail, self.emitter ~= nil)
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- on_stop
-- ─────────────────────────────────────────────────────────────────────────────

--- Called on detach or entity destroy.  Releases all handles.
--- @param ctx table  Engine context.
function M:on_stop(ctx)
    if self.composite_handles then
        uz.vfx:stop_composite(self.composite_handles)
        self.composite_handles = nil
    end
    if self.emitter then
        uz.vfx:stop(self.emitter)
        self.emitter = nil
    end
    if self.trail then
        uz.vfx:clear_trail(self.trail)
        uz.vfx:destroy_trail(self.trail)
        self.trail = nil
    end
    if self.decal then
        uz.vfx:destroy_decal(self.decal)
        self.decal = nil
    end

    local b     = uz.vfx:get_budget_stats()
    local decal = uz.vfx:get_decal_stats()
    uz.log(string.format("VFX Showcase stopped: budget_used=%d decals=%d atlas_occ=%.2f",
        b.used, decal.active_count, decal.atlas_occupancy))
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Private helpers
-- ─────────────────────────────────────────────────────────────────────────────

--- HSV-to-RGB conversion (S=1, V=1) for hue animation.
--- @param  h number  Hue in [0, 1].
--- @return number, number, number  r, g, b each in [0, 1].
function M._hue_to_rgb(h)
    local i = math.floor(h * 6)
    local f = h * 6 - i
    local q, sector = 1 - f, i % 6
    if sector == 0 then return 1,   f,   0
    elseif sector == 1 then return q,   1,   0
    elseif sector == 2 then return 0,   1,   f
    elseif sector == 3 then return 0,   q,   1
    elseif sector == 4 then return f,   0,   1
    else                     return 1,   0,   q
    end
end

return M
