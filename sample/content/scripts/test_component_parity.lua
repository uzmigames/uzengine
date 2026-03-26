-- test_component_parity.lua
-- Round-trip property test for all ECS component properties added in
-- phase2-2.22.0-ecs-component-parity (tasks 7.1 – 7.5).
--
-- Each property is:
--   1. Set to a non-default test value.
--   2. Read back via the getter.
--   3. Compared to the value that was set.
--   4. Restored to the documented default.
--
-- Run once from on_start(entity) so it executes exactly one frame
-- and produces a clear PASS/FAIL summary in the log.

-- ─────────────────────────────────────────────────────────────────────────────
-- Pass / fail accounting
-- ─────────────────────────────────────────────────────────────────────────────

local pass = 0
local fail = 0

local function check(name, expected, got)
    if got == expected then
        pass = pass + 1
    else
        fail = fail + 1
        uz.log("[FAIL] " .. name
            .. " | expected=" .. tostring(expected)
            .. " got=" .. tostring(got))
    end
end

-- Scalar round-trip: set → get → verify → restore.
local function test(name, set_fn, get_fn, test_val, default_val)
    set_fn(test_val)
    local got = get_fn()
    check(name, test_val, got)
    set_fn(default_val)
end

-- Boolean variant (booleans compare by identity in Lua).
local function test_bool(name, set_fn, get_fn, test_val, default_val)
    set_fn(test_val)
    local got = get_fn()
    -- lua_toboolean returns 1/0 but lua_pushboolean pushes a real bool;
    -- coerce both sides so we compare the same type.
    check(name, test_val and true or false, got and true or false)
    set_fn(default_val)
end

-- Vec3 variant: getter returns three separate numbers.
local function test_vec3(name, set_fn, get_fn, tx, ty, tz, dx, dy, dz)
    set_fn(tx, ty, tz)
    local rx, ry, rz = get_fn()
    check(name .. ".x", tx, rx)
    check(name .. ".y", ty, ry)
    check(name .. ".z", tz, rz)
    set_fn(dx, dy, dz)
end

-- Set-only: exercises the setter but has no getter to round-trip.
-- Merely verifies that calling the setter does not raise an error.
-- Records a single PASS if no error is thrown; does not test readback.
local function test_set_only(name, set_fn, test_val, default_val)
    local ok, err = pcall(set_fn, test_val)
    if ok then
        pass = pass + 1
    else
        fail = fail + 1
        uz.log("[FAIL] " .. name .. " setter threw: " .. tostring(err))
    end
    pcall(set_fn, default_val)  -- restore; ignore errors (already counted)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Entity setup helpers
-- ─────────────────────────────────────────────────────────────────────────────

-- Bind a single-argument closure that prepends the entity handle.
-- This lets us write test() / test_bool() / test_vec3() without
-- repeating the entity handle in every call.
local function bind(fn, entity)
    return function(...) return fn(entity, ...) end
end

local function bind_get(fn, entity)
    return function() return fn(entity) end
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Entry point
-- ─────────────────────────────────────────────────────────────────────────────

function on_start(entity)
    uz.log("=== test_component_parity: running ECS property round-trip tests ===")

    -- ── 1. Create a dedicated test entity for DirectionalLightComponent ──────
    local dir_entity = uz.scene.create_entity("_test_dir_light")
    if dir_entity == nil then
        uz.log("[FAIL] could not create DirectionalLight test entity")
        fail = fail + 1
    else
        uz.scene.add_directional_light(dir_entity, 1.0, 1.0, 1.0, 1.0)
        test_directional_light(dir_entity)
    end

    -- ── 2. HeightFogComponent ─────────────────────────────────────────────────
    local fog_entity = uz.scene.create_entity("_test_fog")
    if fog_entity == nil then
        uz.log("[FAIL] could not create HeightFog test entity")
        fail = fail + 1
    else
        uz.scene.create_fog(fog_entity)
        test_fog(fog_entity)
    end

    -- ── 3. VolumetricCloudsComponent ──────────────────────────────────────────
    local clouds_entity = uz.scene.create_entity("_test_clouds")
    if clouds_entity == nil then
        uz.log("[FAIL] could not create VolumetricClouds test entity")
        fail = fail + 1
    else
        uz.scene.create_clouds(clouds_entity)
        test_clouds(clouds_entity)
    end

    -- ── 4. SkyLightComponent ─────────────────────────────────────────────────
    local skylight_entity = uz.scene.create_entity("_test_skylight")
    if skylight_entity == nil then
        uz.log("[FAIL] could not create SkyLight test entity")
        fail = fail + 1
    else
        uz.scene.create_sky_light(skylight_entity)
        test_sky_light(skylight_entity)
    end

    -- ── 5. SkyAtmosphereComponent ─────────────────────────────────────────────
    local atmo_entity = uz.scene.create_entity("_test_atmosphere")
    if atmo_entity == nil then
        uz.log("[FAIL] could not create SkyAtmosphere test entity")
        fail = fail + 1
    else
        uz.scene.create_sky_atmosphere(atmo_entity)
        test_atmosphere(atmo_entity)
    end

    -- ── Summary ───────────────────────────────────────────────────────────────
    local total = pass + fail
    uz.log("=== test_component_parity: " .. pass .. "/" .. total .. " passed ===")
    if fail > 0 then
        uz.log("[RESULT] FAIL — " .. fail .. " test(s) failed")
    else
        uz.log("[RESULT] PASS — all " .. total .. " tests passed")
    end
end

function on_update(entity, dt)
    -- intentionally empty — all work is done once in on_start
end

function on_stop(entity)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Section 1 — DirectionalLightComponent (phase2-2.22.0, task 7.1)
--
-- Properties added by the parity pass and their C++ defaults:
--   atmosphereSunLightIndex             int   0
--   lightSourceAngle                    float 0.5357
--   lightSourceSoftAngle                float 0.0
--   shadowCascades                      int   4
--   cascadeDistributionExponent         float 3.0
--   cascadeTransitionFraction           float 0.1
--   shadowDistanceFadeoutFraction       float 0.1
--   dynamicShadowDistance               float 40000.0
--   contactShadowLength                 float 0.0
--   farShadowCascadeCount               uint  0
--   farShadowDistance                   float 300000.0
--   atmosphereSunDiskColorScale         Vec3  (1,1,1)
--   castCloudShadows                    bool  true
--   castShadowsOnClouds                 bool  true
--   castShadowsOnAtmosphere             bool  true
--   cloudShadowStrength                 float 1.0
--   cloudShadowOnAtmosphereStrength     float 1.0
--   cloudShadowOnSurfaceStrength        float 1.0
--   cloudShadowDepthBias                float 0.0
--   cloudShadowExtent                   float 128.0
--   cloudShadowMapResolutionScale       float 1.0
--   cloudShadowRaySampleCountScale      float 1.0
--   cloudScatteredLuminanceScale        Vec3  (1,1,1)
--   enableLightShaftOcclusion           bool  true
--   occlusionMaskDarkness               float 0.5
--   occlusionDepthRange                 float 10000.0
--   lightShaftOverrideDirection         Vec3  (0,0,0)
--   contactShadowLengthInWorldSpace     bool  false
--   contactShadowCastingIntensity       float 1.0
--   contactShadowNonCastingIntensity    float 0.0
--   shadowSourceAngleFactor             float 1.0
--   shadowCascadeBiasDistribution       float 1.0
--   distanceFieldShadowDistance         float 0.0
--   traceDistance                       float 100000.0
--   forwardShadingPriority              int   0
-- ─────────────────────────────────────────────────────────────────────────────

function test_directional_light(e)
    uz.log("--- DirectionalLightComponent ---")

    test("atmosphere_sun_index",
        bind(uz.scene.set_light_atmosphere_sun_index, e),
        bind_get(uz.scene.get_light_atmosphere_sun_index, e),
        1, 0)

    test("light_source_angle",
        bind(uz.scene.set_light_source_angle, e),
        bind_get(uz.scene.get_light_source_angle, e),
        1.2, 0.5357)

    test("light_source_soft_angle",
        bind(uz.scene.set_light_source_soft_angle, e),
        bind_get(uz.scene.get_light_source_soft_angle, e),
        0.3, 0.0)

    test("shadow_cascades",
        bind(uz.scene.set_light_shadow_cascades, e),
        bind_get(uz.scene.get_light_shadow_cascades, e),
        3, 4)

    test("cascade_distribution_exponent",
        bind(uz.scene.set_light_cascade_distribution_exponent, e),
        bind_get(uz.scene.get_light_cascade_distribution_exponent, e),
        2.5, 3.0)

    test("cascade_transition_fraction",
        bind(uz.scene.set_light_cascade_transition_fraction, e),
        bind_get(uz.scene.get_light_cascade_transition_fraction, e),
        0.2, 0.1)

    test("shadow_distance_fadeout_fraction",
        bind(uz.scene.set_light_shadow_distance_fadeout_fraction, e),
        bind_get(uz.scene.get_light_shadow_distance_fadeout_fraction, e),
        0.05, 0.1)

    test("dynamic_shadow_distance",
        bind(uz.scene.set_light_dynamic_shadow_distance, e),
        bind_get(uz.scene.get_light_dynamic_shadow_distance, e),
        20000.0, 40000.0)

    test("contact_shadow_length",
        bind(uz.scene.set_light_contact_shadow_length, e),
        bind_get(uz.scene.get_light_contact_shadow_length, e),
        0.08, 0.0)

    test("far_shadow_cascade_count",
        bind(uz.scene.set_light_far_shadow_cascade_count, e),
        bind_get(uz.scene.get_light_far_shadow_cascade_count, e),
        2, 0)

    test("far_shadow_distance",
        bind(uz.scene.set_light_far_shadow_distance, e),
        bind_get(uz.scene.get_light_far_shadow_distance, e),
        500000.0, 300000.0)

    test_vec3("atmosphere_sun_disk_color_scale",
        bind(uz.scene.set_light_atmosphere_sun_disk_color_scale, e),
        bind_get(uz.scene.get_light_atmosphere_sun_disk_color_scale, e),
        0.9, 0.8, 0.7,
        1.0, 1.0, 1.0)

    test_bool("cast_cloud_shadows",
        bind(uz.scene.set_light_cast_cloud_shadows, e),
        bind_get(uz.scene.get_light_cast_cloud_shadows, e),
        false, true)

    test_bool("cast_shadows_on_clouds",
        bind(uz.scene.set_light_cast_shadows_on_clouds, e),
        bind_get(uz.scene.get_light_cast_shadows_on_clouds, e),
        false, true)

    test_bool("cast_shadows_on_atmosphere",
        bind(uz.scene.set_light_cast_shadows_on_atmosphere, e),
        bind_get(uz.scene.get_light_cast_shadows_on_atmosphere, e),
        false, true)

    test("cloud_shadow_strength",
        bind(uz.scene.set_light_cloud_shadow_strength, e),
        bind_get(uz.scene.get_light_cloud_shadow_strength, e),
        0.75, 1.0)

    test("cloud_shadow_on_atmosphere_strength",
        bind(uz.scene.set_light_cloud_shadow_on_atmosphere_strength, e),
        bind_get(uz.scene.get_light_cloud_shadow_on_atmosphere_strength, e),
        0.6, 1.0)

    test("cloud_shadow_on_surface_strength",
        bind(uz.scene.set_light_cloud_shadow_on_surface_strength, e),
        bind_get(uz.scene.get_light_cloud_shadow_on_surface_strength, e),
        0.4, 1.0)

    test("cloud_shadow_depth_bias",
        bind(uz.scene.set_light_cloud_shadow_depth_bias, e),
        bind_get(uz.scene.get_light_cloud_shadow_depth_bias, e),
        0.001, 0.0)

    test("cloud_shadow_extent",
        bind(uz.scene.set_light_cloud_shadow_extent, e),
        bind_get(uz.scene.get_light_cloud_shadow_extent, e),
        256.0, 128.0)

    test("cloud_shadow_map_resolution_scale",
        bind(uz.scene.set_light_cloud_shadow_map_resolution_scale, e),
        bind_get(uz.scene.get_light_cloud_shadow_map_resolution_scale, e),
        2.0, 1.0)

    test("cloud_shadow_ray_sample_count_scale",
        bind(uz.scene.set_light_cloud_shadow_ray_sample_count_scale, e),
        bind_get(uz.scene.get_light_cloud_shadow_ray_sample_count_scale, e),
        0.5, 1.0)

    test_vec3("cloud_scattered_luminance_scale",
        bind(uz.scene.set_light_cloud_scattered_luminance_scale, e),
        bind_get(uz.scene.get_light_cloud_scattered_luminance_scale, e),
        0.8, 0.9, 0.95,
        1.0, 1.0, 1.0)

    test_bool("enable_light_shaft_occlusion",
        bind(uz.scene.set_light_enable_light_shaft_occlusion, e),
        bind_get(uz.scene.get_light_enable_light_shaft_occlusion, e),
        false, true)

    test("occlusion_mask_darkness",
        bind(uz.scene.set_light_occlusion_mask_darkness, e),
        bind_get(uz.scene.get_light_occlusion_mask_darkness, e),
        0.3, 0.5)

    test("occlusion_depth_range",
        bind(uz.scene.set_light_occlusion_depth_range, e),
        bind_get(uz.scene.get_light_occlusion_depth_range, e),
        5000.0, 10000.0)

    test_vec3("light_shaft_override_direction",
        bind(uz.scene.set_light_shaft_override_direction, e),
        bind_get(uz.scene.get_light_shaft_override_direction, e),
        0.0, 0.0, 1.0,
        0.0, 0.0, 0.0)

    test_bool("contact_shadow_length_in_world_space",
        bind(uz.scene.set_light_contact_shadow_length_in_world_space, e),
        bind_get(uz.scene.get_light_contact_shadow_length_in_world_space, e),
        true, false)

    test("contact_shadow_casting_intensity",
        bind(uz.scene.set_light_contact_shadow_casting_intensity, e),
        bind_get(uz.scene.get_light_contact_shadow_casting_intensity, e),
        0.7, 1.0)

    test("contact_shadow_non_casting_intensity",
        bind(uz.scene.set_light_contact_shadow_non_casting_intensity, e),
        bind_get(uz.scene.get_light_contact_shadow_non_casting_intensity, e),
        0.2, 0.0)

    test("shadow_source_angle_factor",
        bind(uz.scene.set_light_shadow_source_angle_factor, e),
        bind_get(uz.scene.get_light_shadow_source_angle_factor, e),
        2.0, 1.0)

    test("shadow_cascade_bias_distribution",
        bind(uz.scene.set_light_shadow_cascade_bias_distribution, e),
        bind_get(uz.scene.get_light_shadow_cascade_bias_distribution, e),
        0.5, 1.0)

    test("distance_field_shadow_distance",
        bind(uz.scene.set_light_distance_field_shadow_distance, e),
        bind_get(uz.scene.get_light_distance_field_shadow_distance, e),
        5000.0, 0.0)

    test("trace_distance",
        bind(uz.scene.set_light_trace_distance, e),
        bind_get(uz.scene.get_light_trace_distance, e),
        50000.0, 100000.0)

    test("forward_shading_priority",
        bind(uz.scene.set_light_forward_shading_priority, e),
        bind_get(uz.scene.get_light_forward_shading_priority, e),
        1, 0)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Section 2 — HeightFogComponent (phase2-2.22.0, task 7.2)
--
-- New properties and C++ defaults:
--   enableVolumetricFog                              bool  false
--   volumetricFogScatteringDistribution              float 0.2
--   volumetricFogAlbedo                              Vec3  (1,1,1)
--   volumetricFogEmissive                            Vec3  (0,0,0)
--   volumetricFogExtinctionScale                     float 1.0
--   volumetricFogDistance                            float 6400.0
--   volumetricFogStartDistance                       float 0.0
--   volumetricFogNearFadeInDistance                  float 300.0
--   volumetricFogStaticLightingScatteringIntensity   float 1.0
--   overrideLightColorsWithFogInscatteringColors     bool  false
--   inscatteringColorCubemapId                       uint  0
--   inscatteringColorCubemapAngle                    float 0.0
--   inscatteringTextureTint                          Vec3  (1,1,1)
--   fullyDirectionalInscatteringColorDistance        float 100000.0
--   nonDirectionalInscatteringColorDistance          float 50000.0
--   skyAtmosphereAmbientContributionColorScale       Vec3  (1,1,1)
-- ─────────────────────────────────────────────────────────────────────────────

function test_fog(e)
    uz.log("--- HeightFogComponent (volumetric / inscattering) ---")

    -- enableVolumetricFog has no getter in the current Lua API (set-only binding).
    -- test_set_only verifies the setter does not raise an error and restores default.
    test_set_only("fog_volumetric",
        bind(uz.scene.set_fog_volumetric, e),
        true, false)

    test("fog_volumetric_scattering_distribution",
        bind(uz.scene.set_fog_volumetric_scattering_distribution, e),
        bind_get(uz.scene.get_fog_volumetric_scattering_distribution, e),
        0.5, 0.2)

    test_vec3("fog_volumetric_albedo",
        bind(uz.scene.set_fog_volumetric_albedo, e),
        bind_get(uz.scene.get_fog_volumetric_albedo, e),
        0.8, 0.9, 1.0,
        1.0, 1.0, 1.0)

    test_vec3("fog_volumetric_emissive",
        bind(uz.scene.set_fog_volumetric_emissive, e),
        bind_get(uz.scene.get_fog_volumetric_emissive, e),
        0.1, 0.05, 0.02,
        0.0, 0.0, 0.0)

    test("fog_volumetric_extinction_scale",
        bind(uz.scene.set_fog_volumetric_extinction_scale, e),
        bind_get(uz.scene.get_fog_volumetric_extinction_scale, e),
        2.0, 1.0)

    test("fog_volumetric_distance",
        bind(uz.scene.set_fog_volumetric_distance, e),
        bind_get(uz.scene.get_fog_volumetric_distance, e),
        3200.0, 6400.0)

    test("fog_volumetric_start_distance",
        bind(uz.scene.set_fog_volumetric_start_distance, e),
        bind_get(uz.scene.get_fog_volumetric_start_distance, e),
        100.0, 0.0)

    test("fog_volumetric_near_fade_in_distance",
        bind(uz.scene.set_fog_volumetric_near_fade_in_distance, e),
        bind_get(uz.scene.get_fog_volumetric_near_fade_in_distance, e),
        600.0, 300.0)

    test("fog_volumetric_static_lighting_scattering_intensity",
        bind(uz.scene.set_fog_volumetric_static_lighting_scattering_intensity, e),
        bind_get(uz.scene.get_fog_volumetric_static_lighting_scattering_intensity, e),
        0.5, 1.0)

    test_bool("fog_override_light_colors_with_fog_inscattering_colors",
        bind(uz.scene.set_fog_override_light_colors_with_fog_inscattering_colors, e),
        bind_get(uz.scene.get_fog_override_light_colors_with_fog_inscattering_colors, e),
        true, false)

    test("fog_inscattering_color_cubemap_id",
        bind(uz.scene.set_fog_inscattering_color_cubemap_id, e),
        bind_get(uz.scene.get_fog_inscattering_color_cubemap_id, e),
        42, 0)

    test("fog_inscattering_color_cubemap_angle",
        bind(uz.scene.set_fog_inscattering_color_cubemap_angle, e),
        bind_get(uz.scene.get_fog_inscattering_color_cubemap_angle, e),
        45.0, 0.0)

    test_vec3("fog_inscattering_texture_tint",
        bind(uz.scene.set_fog_inscattering_texture_tint, e),
        bind_get(uz.scene.get_fog_inscattering_texture_tint, e),
        0.9, 0.85, 0.8,
        1.0, 1.0, 1.0)

    test("fog_fully_directional_inscattering_color_distance",
        bind(uz.scene.set_fog_fully_directional_inscattering_color_distance, e),
        bind_get(uz.scene.get_fog_fully_directional_inscattering_color_distance, e),
        200000.0, 100000.0)

    test("fog_non_directional_inscattering_color_distance",
        bind(uz.scene.set_fog_non_directional_inscattering_color_distance, e),
        bind_get(uz.scene.get_fog_non_directional_inscattering_color_distance, e),
        25000.0, 50000.0)

    test_vec3("fog_sky_atmosphere_ambient_contribution_color_scale",
        bind(uz.scene.set_fog_sky_atmosphere_ambient_contribution_color_scale, e),
        bind_get(uz.scene.get_fog_sky_atmosphere_ambient_contribution_color_scale, e),
        0.5, 0.6, 0.7,
        1.0, 1.0, 1.0)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Section 3 — VolumetricCloudsComponent (phase2-2.22.0, task 7.3)
--
-- New properties and C++ defaults:
--   viewSampleCountScale                                float 0.5
--   reflectionViewSampleCountScale                      float 0.1
--   shadowViewSampleCountScale                          float 0.5
--   shadowReflectionViewSampleCountScale                float 0.1
--   shadowTracingDistance                               float 10.0
--   stopTracingTransmittanceThreshold                   float 0.01
--   aerialPerspectiveRayleighScatteringStartDistance    float 0.0
--   aerialPerspectiveRayleighScatteringFadeDistance     float 10.0
--   aerialPerspectiveMieScatteringStartDistance         float 0.0
--   aerialPerspectiveMieScatteringFadeDistance          float 10.0
--   skyLightCloudBottomOcclusion                        float 0.2
--   tracingMaxDistanceMode                              int   0
--   tracingStartMaxDistance                             float 350.0
--   tracingStartDistanceFromCamera                      float 0.0
-- ─────────────────────────────────────────────────────────────────────────────

function test_clouds(e)
    uz.log("--- VolumetricCloudsComponent (tracing/aerial) ---")

    test("clouds_view_sample_count_scale",
        bind(uz.scene.set_clouds_view_sample_count_scale, e),
        bind_get(uz.scene.get_clouds_view_sample_count_scale, e),
        1.0, 0.5)

    test("clouds_reflection_view_sample_count_scale",
        bind(uz.scene.set_clouds_reflection_view_sample_count_scale, e),
        bind_get(uz.scene.get_clouds_reflection_view_sample_count_scale, e),
        0.25, 0.1)

    test("clouds_shadow_view_sample_count_scale",
        bind(uz.scene.set_clouds_shadow_view_sample_count_scale, e),
        bind_get(uz.scene.get_clouds_shadow_view_sample_count_scale, e),
        0.75, 0.5)

    test("clouds_shadow_reflection_view_sample_count_scale",
        bind(uz.scene.set_clouds_shadow_reflection_view_sample_count_scale, e),
        bind_get(uz.scene.get_clouds_shadow_reflection_view_sample_count_scale, e),
        0.2, 0.1)

    test("clouds_shadow_tracing_distance",
        bind(uz.scene.set_clouds_shadow_tracing_distance, e),
        bind_get(uz.scene.get_clouds_shadow_tracing_distance, e),
        20.0, 10.0)

    test("clouds_stop_tracing_transmittance_threshold",
        bind(uz.scene.set_clouds_stop_tracing_transmittance_threshold, e),
        bind_get(uz.scene.get_clouds_stop_tracing_transmittance_threshold, e),
        0.001, 0.01)

    test("clouds_aerial_perspective_rayleigh_scattering_start_distance",
        bind(uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_start_distance, e),
        bind_get(uz.scene.get_clouds_aerial_perspective_rayleigh_scattering_start_distance, e),
        5.0, 0.0)

    test("clouds_aerial_perspective_rayleigh_scattering_fade_distance",
        bind(uz.scene.set_clouds_aerial_perspective_rayleigh_scattering_fade_distance, e),
        bind_get(uz.scene.get_clouds_aerial_perspective_rayleigh_scattering_fade_distance, e),
        20.0, 10.0)

    test("clouds_aerial_perspective_mie_scattering_start_distance",
        bind(uz.scene.set_clouds_aerial_perspective_mie_scattering_start_distance, e),
        bind_get(uz.scene.get_clouds_aerial_perspective_mie_scattering_start_distance, e),
        2.0, 0.0)

    test("clouds_aerial_perspective_mie_scattering_fade_distance",
        bind(uz.scene.set_clouds_aerial_perspective_mie_scattering_fade_distance, e),
        bind_get(uz.scene.get_clouds_aerial_perspective_mie_scattering_fade_distance, e),
        5.0, 10.0)

    test("clouds_sky_light_cloud_bottom_occlusion",
        bind(uz.scene.set_clouds_sky_light_cloud_bottom_occlusion, e),
        bind_get(uz.scene.get_clouds_sky_light_cloud_bottom_occlusion, e),
        0.5, 0.2)

    -- tracingMaxDistanceMode: 0 = DistanceFromCloudLayerEntryPoint, 1 = DistanceFromPointOfView
    test("clouds_tracing_max_distance_mode",
        bind(uz.scene.set_clouds_tracing_max_distance_mode, e),
        bind_get(uz.scene.get_clouds_tracing_max_distance_mode, e),
        1, 0)

    test("clouds_tracing_start_max_distance",
        bind(uz.scene.set_clouds_tracing_start_max_distance, e),
        bind_get(uz.scene.get_clouds_tracing_start_max_distance, e),
        700.0, 350.0)

    test("clouds_tracing_start_distance_from_camera",
        bind(uz.scene.set_clouds_tracing_start_distance_from_camera, e),
        bind_get(uz.scene.get_clouds_tracing_start_distance_from_camera, e),
        15.0, 0.0)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Section 4 — SkyLightComponent (phase2-2.22.0, task 7.4)
--
-- New properties and C++ defaults:
--   cloudAmbientOcclusion                   bool  true
--   cloudAmbientOcclusionStrength           float 0.5
--   cloudAmbientOcclusionExtent             float 64.0
--   cloudAmbientOcclusionMapResolutionScale float 1.0
--   cloudAmbientOcclusionApertureScale      float 0.0
--   skyDistanceThreshold                    float 150000.0
--   occlusionContrast                       float 0.2
--   captureEmissiveOnly                     bool  false
--   sourceCubemapAngle                      float 0.0
-- ─────────────────────────────────────────────────────────────────────────────

function test_sky_light(e)
    uz.log("--- SkyLightComponent (cloud AO / threshold) ---")

    test_bool("sky_light_cloud_ambient_occlusion",
        bind(uz.scene.set_sky_light_cloud_ambient_occlusion, e),
        bind_get(uz.scene.get_sky_light_cloud_ambient_occlusion, e),
        false, true)

    test("sky_light_cloud_ambient_occlusion_strength",
        bind(uz.scene.set_sky_light_cloud_ambient_occlusion_strength, e),
        bind_get(uz.scene.get_sky_light_cloud_ambient_occlusion_strength, e),
        0.8, 0.5)

    test("sky_light_cloud_ambient_occlusion_extent",
        bind(uz.scene.set_sky_light_cloud_ambient_occlusion_extent, e),
        bind_get(uz.scene.get_sky_light_cloud_ambient_occlusion_extent, e),
        128.0, 64.0)

    test("sky_light_cloud_ambient_occlusion_map_resolution_scale",
        bind(uz.scene.set_sky_light_cloud_ambient_occlusion_map_resolution_scale, e),
        bind_get(uz.scene.get_sky_light_cloud_ambient_occlusion_map_resolution_scale, e),
        2.0, 1.0)

    test("sky_light_cloud_ambient_occlusion_aperture_scale",
        bind(uz.scene.set_sky_light_cloud_ambient_occlusion_aperture_scale, e),
        bind_get(uz.scene.get_sky_light_cloud_ambient_occlusion_aperture_scale, e),
        1.0, 0.0)

    test("sky_light_sky_distance_threshold",
        bind(uz.scene.set_sky_light_sky_distance_threshold, e),
        bind_get(uz.scene.get_sky_light_sky_distance_threshold, e),
        300000.0, 150000.0)

    test("sky_light_occlusion_contrast",
        bind(uz.scene.set_sky_light_occlusion_contrast, e),
        bind_get(uz.scene.get_sky_light_occlusion_contrast, e),
        0.8, 0.2)

    test_bool("sky_light_capture_emissive_only",
        bind(uz.scene.set_sky_light_capture_emissive_only, e),
        bind_get(uz.scene.get_sky_light_capture_emissive_only, e),
        true, false)

    test("sky_light_source_cubemap_angle",
        bind(uz.scene.set_sky_light_source_cubemap_angle, e),
        bind_get(uz.scene.get_sky_light_source_cubemap_angle, e),
        90.0, 0.0)
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Section 5 — SkyAtmosphereComponent (phase2-2.22.0, task 7.5)
--
-- New properties and C++ defaults:
--   traceSampleCountScale                   float 1.0
--   aerialPerspectiveStartDepth             float 0.1
--   transmittanceMinLightElevationAngle     float -90.0
-- ─────────────────────────────────────────────────────────────────────────────

function test_atmosphere(e)
    uz.log("--- SkyAtmosphereComponent (quality/art) ---")

    test("atmosphere_trace_sample_count_scale",
        bind(uz.scene.set_atmosphere_trace_sample_count_scale, e),
        bind_get(uz.scene.get_atmosphere_trace_sample_count_scale, e),
        2.0, 1.0)

    test("atmosphere_aerial_perspective_start_depth",
        bind(uz.scene.set_atmosphere_aerial_perspective_start_depth, e),
        bind_get(uz.scene.get_atmosphere_aerial_perspective_start_depth, e),
        1.0, 0.1)

    test("atmosphere_transmittance_min_light_elevation_angle",
        bind(uz.scene.set_atmosphere_transmittance_min_light_elevation_angle, e),
        bind_get(uz.scene.get_atmosphere_transmittance_min_light_elevation_angle, e),
        -45.0, -90.0)
end
