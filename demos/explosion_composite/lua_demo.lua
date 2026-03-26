--- Explosion Composite -- Lua VFX scripting demo (full API).
---
--- Demonstrates the complete UzEngine VFX Lua API:
--- 1. Spawn particle effects by asset path
--- 2. Modify emitter parameters and structural fields each frame
--- 3. Trigger burst events with count and velocity_scale
--- 4. Respond to input events (spacebar burst)
--- 5. Chain multiple effects into a composite explosion sequence
--- 6. Create and animate ribbon trails behind debris
--- 7. Spawn a ground scorch decal at the explosion origin
--- 8. Configure LOD distances for distance-based quality
--- 9. Query budget statistics for live diagnostics
---
--- Uses the `uz.vfx` table provided by RegisterVFXLuaBindings,
--- RegisterTrailLuaBindings, and RegisterDecalLuaBindings.
---
--- Usage (in-engine):
---   Attach to any entity via inspector or scene file:
---     { "script": "demos/explosion_composite/lua_demo.lua" }

-- ─────────────────────────────────────────────────────────────────────────────
-- Configuration
-- ─────────────────────────────────────────────────────────────────────────────

local FIRE_ASSET       = "vfx/fire.uzvfx"
local SMOKE_ASSET      = "vfx/smoke.uzvfx"
local DEBRIS_ASSET     = "vfx/debris.uzvfx"
local COMPOSITE_ASSET  = "effects/explosion.uzvfx_composite"
local SCORCH_MATERIAL  = "materials/decals/scorch.uzmat"
local TRAIL_MATERIAL   = "materials/vfx/debris_trail.uzmat"

--- Spawn position for the composite effect (world space).
local SPAWN_POS = {0, 5, 0}

--- Smoke offset relative to the fire origin.
local SMOKE_OFFSET = {0, 0.5, 0}

--- Delay before smoke begins (seconds).
local SMOKE_DELAY = 0.2

--- Burst particle count when spacebar is pressed.
local BURST_COUNT = 500

--- Intensity oscillation frequency (Hz).
local INTENSITY_FREQ = 2.0

--- Intensity oscillation amplitude.
local INTENSITY_AMP = 0.5

--- LOD distance thresholds (metres).
local LOD_NEAR    = 30
local LOD_MID     = 80
local LOD_FAR     = 150

--- Budget stats print interval (seconds).
local STATS_INTERVAL = 3.0

-- ─────────────────────────────────────────────────────────────────────────────
-- State
-- ─────────────────────────────────────────────────────────────────────────────

local fire_handle       = nil
local smoke_handle      = nil
local debris_handle     = nil
local trail_handle      = nil
local scorch_handle     = nil
local composite_handles = nil

local elapsed           = 0
local smoke_spawned     = false
local composite_spawned = false
local scorch_fading     = false
local last_stats_time   = 0

-- ─────────────────────────────────────────────────────────────────────────────
-- Lifecycle: on_start
-- ─────────────────────────────────────────────────────────────────────────────

--- Called once when the script is first attached to the entity.
--- Spawns fire and debris immediately, creates a debris trail,
--- spawns a scorch decal, and configures LOD for fire.
function on_start()
    -- Spawn fire at the base position.
    fire_handle = uz.vfx:spawn(FIRE_ASSET, SPAWN_POS)

    -- Debris starts immediately alongside fire.
    debris_handle = uz.vfx:spawn(DEBRIS_ASSET, SPAWN_POS)

    -- Configure fire emitter structural fields.
    if fire_handle then
        uz.vfx:set_emitter_field(fire_handle, "emission_rate", 200.0)
        uz.vfx:set_emitter_field(fire_handle, "spread_angle", 45.0)
        uz.vfx:set_emitter_field(fire_handle, "damping", 0.95)

        -- Set LOD distances so the fire reduces quality at range.
        uz.vfx:set_lod_distances(fire_handle, LOD_NEAR, LOD_MID, LOD_FAR)
        uz.vfx:set_lod_enabled(fire_handle, true)

        -- Set a Vec4 color parameter for tinting.
        uz.vfx:set_parameter_vec4(fire_handle, "tint", {1.0, 0.6, 0.2, 1.0})
    end

    -- Create a ribbon trail to follow the debris emitter.
    trail_handle = uz.vfx:create_trail({
        material_path  = TRAIL_MATERIAL,
        max_points     = 64,
        width_start    = 0.3,
        width_end      = 0.05,
        point_lifetime = 1.5,
        min_point_distance = 0.1,
        sort_order     = 10,
        is_active      = true,
    })

    if trail_handle then
        -- Orange-to-transparent trail color.
        uz.vfx:set_trail_color(
            trail_handle,
            {1.0, 0.5, 0.1, 1.0},  -- start: bright orange
            {0.3, 0.1, 0.0, 0.0}   -- end: dim, transparent
        )
        uz.vfx:set_trail_uv_scroll(trail_handle, 0.3)
    end

    -- Spawn a ground scorch decal beneath the explosion.
    local ground_pos    = {SPAWN_POS[1], 0.01, SPAWN_POS[3]}
    local ground_normal = {0, 1, 0}
    scorch_handle = uz.vfx:spawn_decal(SCORCH_MATERIAL, ground_pos, ground_normal, 30.0)

    if scorch_handle then
        uz.vfx:set_decal_blend_mode(scorch_handle, "multiply")
        uz.vfx:set_decal_normal_intensity(scorch_handle, 0.6)
    end

    -- Print initial budget stats.
    local stats = uz.vfx:get_budget_stats()
    if stats then
        uz.log(string.format("VFX budget: %d/%d particles (%.1f%% used)",
            stats.used, stats.total, stats.utilization_pct))
    end

    uz.log("Explosion composite started: fire, debris, trail, scorch spawned")
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Lifecycle: on_update
-- ─────────────────────────────────────────────────────────────────────────────

--- Called every frame with the frame delta time.
---
--- @param dt number  Frame delta time in seconds.
function on_update(dt)
    elapsed = elapsed + dt

    -- Oscillate fire intensity using a sine wave.
    if fire_handle then
        local intensity = 1.0 + math.sin(elapsed * INTENSITY_FREQ * math.pi * 2) * INTENSITY_AMP
        uz.vfx:set_parameter(fire_handle, "intensity", intensity)
    end

    -- Spawn smoke after the configured delay (once).
    if not smoke_spawned and elapsed >= SMOKE_DELAY then
        local smoke_pos = {
            SPAWN_POS[1] + SMOKE_OFFSET[1],
            SPAWN_POS[2] + SMOKE_OFFSET[2],
            SPAWN_POS[3] + SMOKE_OFFSET[3],
        }
        smoke_handle = uz.vfx:spawn(SMOKE_ASSET, smoke_pos)
        smoke_spawned = true
        uz.log("Smoke emitter spawned after delay")
    end

    -- Trigger burst on all emitters when spacebar is pressed.
    -- Uses the extended emit_event with count AND velocity_scale.
    if ctx.input:pressed("space") then
        uz.log("Burst event triggered!")

        if fire_handle then
            uz.vfx:emit_event(fire_handle, "burst", BURST_COUNT, 1.5)
        end
        if smoke_handle then
            uz.vfx:emit_event(smoke_handle, "burst", BURST_COUNT, 0.8)
        end
        if debris_handle then
            uz.vfx:emit_event(debris_handle, "burst", BURST_COUNT, 2.0)
        end
    end

    -- Gradually reduce debris emission rate over time (debris settles).
    if debris_handle and elapsed > 1.0 then
        local falloff = math.max(0, 1.0 - (elapsed - 1.0) / 2.0)
        uz.vfx:set_emitter_field(debris_handle, "emission_rate", 300 * falloff)
    end

    -- Animate trail width based on elapsed time (pulse effect).
    if trail_handle then
        local pulse = 0.3 + 0.1 * math.sin(elapsed * 4.0)
        uz.vfx:set_trail_width(trail_handle, pulse, 0.05)
    end

    -- Spawn composite effect at t=10 (once).
    if not composite_spawned and elapsed >= 10.0 then
        local offset_pos = {SPAWN_POS[1] + 5, SPAWN_POS[2], SPAWN_POS[3]}
        composite_handles = uz.vfx:spawn_composite(COMPOSITE_ASSET, offset_pos)

        if composite_handles then
            uz.log(string.format("Composite spawned with %d sub-emitters",
                #composite_handles))
        end
        composite_spawned = true
    end

    -- Fade out the scorch decal at t=20.
    if not scorch_fading and elapsed >= 20.0 and scorch_handle then
        uz.vfx:fade_out_decal(scorch_handle, 5.0)
        scorch_fading = true
        uz.log("Scorch decal fading out over 5 seconds")
    end

    -- Print budget stats periodically.
    if elapsed - last_stats_time >= STATS_INTERVAL then
        last_stats_time = elapsed

        local alive = uz.vfx:get_total_alive()
        local count = uz.vfx:get_emitter_count()
        local stats = uz.vfx:get_budget_stats()

        uz.log(string.format("[t=%.1fs] %d alive particles, %d emitters, %.1f%% budget",
            elapsed, alive or 0, count or 0,
            stats and stats.utilization_pct or 0))

        -- Print per-emitter LOD levels.
        if fire_handle then
            local lod = uz.vfx:get_lod_level(fire_handle)
            local lod_names = { [0]="Full", [1]="Reduced", [2]="Minimal", [3]="Culled" }
            uz.log(string.format("  Fire LOD: %s (%d)", lod_names[lod] or "?", lod or -1))
        end

        -- Print trail point count.
        if trail_handle then
            local pts = uz.vfx:get_trail_point_count(trail_handle)
            uz.log(string.format("  Trail points: %d", pts or 0))
        end

        -- Print decal stats.
        local dstats = uz.vfx:get_decal_stats()
        if dstats then
            uz.log(string.format("  Decals: %d active, %d pool, %.1f%% atlas",
                dstats.active_count, dstats.pool_available, dstats.atlas_occupancy))
        end
    end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Lifecycle: on_stop
-- ─────────────────────────────────────────────────────────────────────────────

--- Called when the script is detached or the entity is destroyed.
--- Cleans up all spawned VFX handles.
function on_stop()
    -- Stop composite sub-emitters.
    if composite_handles then
        uz.vfx:stop_composite(composite_handles)
        composite_handles = nil
    end

    -- Destroy particle emitters.
    if fire_handle then
        uz.vfx:stop(fire_handle)
        fire_handle = nil
    end
    if smoke_handle then
        uz.vfx:stop(smoke_handle)
        smoke_handle = nil
    end
    if debris_handle then
        uz.vfx:stop(debris_handle)
        debris_handle = nil
    end

    -- Destroy trail.
    if trail_handle then
        uz.vfx:destroy_trail(trail_handle)
        trail_handle = nil
    end

    -- Destroy scorch decal.
    if scorch_handle then
        uz.vfx:destroy_decal(scorch_handle)
        scorch_handle = nil
    end

    uz.log("Explosion composite stopped: all effects cleaned up")
end
