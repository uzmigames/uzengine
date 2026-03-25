# Proposal: phase3a-3.69.0-vfx-lua-api-parity

## Why
Per project rules, every C++ feature must have a complete Lua binding. The existing uz.vfx Lua API has two known bugs (spawn() ignores graph path, emit_event() hardcodes count/velocity) and is missing bindings for trails, decals, LOD control, budget queries, composite effects, and emitter structural field get/set. Without complete Lua parity, designers cannot script VFX-driven gameplay in Lua.

## What Changes
- Fix uz.vfx:spawn() to match by graph path on the ParticleEmitter component
- Fix uz.vfx:emit_event() to expose emission_count and velocity_scale parameters
- Add get/set bindings for all emitter structural fields (emission_rate, lifetime, velocity, etc.)
- Add uz.vfx:set_parameter_vec4() and get_parameter() for read-back
- New trail Lua API: create_trail, destroy_trail, set_trail_active, clear_trail, configure width/color/lifetime/uv_scroll
- New decal Lua API: spawn_decal, fade_out_decal, destroy_decal, blend_mode, normal_intensity, get_decal_stats
- LOD control bindings: set_lod_distances, get_lod_level, set_lod_enabled
- Budget query bindings: get_budget_stats, get_total_alive, get_emitter_count
- Composite effects bindings: spawn_composite, stop_composite

## Impact
- Affected specs: vfx-gpu-particle-core, vfx-trail-ribbon-renderer, vfx-decal-system, vfx-advanced-modules-lod-budget
- Affected code: runtime/engine/scripting/api/vfx/, runtime/engine/vfx/
- Breaking change: YES — spawn() behavior changes (now requires graphPath field on emitter); existing scripts must be updated
- User benefit: Full scriptable VFX control from Lua — designers can spawn, configure, and animate all VFX types without touching C++
