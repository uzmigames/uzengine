/// postprocess_volume_component.cpp — Serialization for PostProcessVolumeComponent.
///
/// Registers a type-erased serializer entry so that the scene loader can
/// dynamically create and serialize PostProcessVolumeComponent by the JSON key
/// "postprocess_volume" without hardcoded dispatch.

#include "postprocess_volume_component.h"
#include "component_serializer.h"
#include "world.h"
#include "../core/json_helpers.h"

#include <json.hpp>
#include <entt/entt.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

UZ_REGISTER_COMPONENT_SERIALIZER(postprocess_volume, uz::PostProcessVolumeComponent,
    // Deserialize
    [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
        const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        auto& c = world.GetRegistry().emplace<uz::PostProcessVolumeComponent>(entt);

        const uz::PostProcessVolumeComponent defaults{};

        // Volume geometry
        c.extents     = uz::json::Vec3FromArray(j, "extents", defaults.extents);
        c.isUnbound   = uz::json::Bool(j, "is_unbound", defaults.isUnbound);

        // Blending control
        c.priority    = uz::json::Float(j, "priority", defaults.priority);
        c.blendRadius = uz::json::Float(j, "blend_radius", defaults.blendRadius);
        c.blendWeight = uz::json::Float(j, "blend_weight", defaults.blendWeight);
        c.enabled     = uz::json::Bool(j, "enabled", defaults.enabled);

        // Bloom
        c.overrideBloomIntensity = uz::json::Bool(j, "override_bloom_intensity", defaults.overrideBloomIntensity);
        c.bloomIntensity         = uz::json::Float(j, "bloom_intensity", defaults.bloomIntensity);
        c.overrideBloomThreshold = uz::json::Bool(j, "override_bloom_threshold", defaults.overrideBloomThreshold);
        c.bloomThreshold         = uz::json::Float(j, "bloom_threshold", defaults.bloomThreshold);

        // Auto-Exposure
        c.overrideAutoExposureMinBrightness = uz::json::Bool(j, "override_auto_exposure_min_brightness", defaults.overrideAutoExposureMinBrightness);
        c.autoExposureMinBrightness         = uz::json::Float(j, "auto_exposure_min_brightness", defaults.autoExposureMinBrightness);
        c.overrideAutoExposureMaxBrightness = uz::json::Bool(j, "override_auto_exposure_max_brightness", defaults.overrideAutoExposureMaxBrightness);
        c.autoExposureMaxBrightness         = uz::json::Float(j, "auto_exposure_max_brightness", defaults.autoExposureMaxBrightness);
        c.overrideAutoExposureBias          = uz::json::Bool(j, "override_auto_exposure_bias", defaults.overrideAutoExposureBias);
        c.autoExposureBias                  = uz::json::Float(j, "auto_exposure_bias", defaults.autoExposureBias);
        c.overrideAutoExposureSpeedUp       = uz::json::Bool(j, "override_auto_exposure_speed_up", defaults.overrideAutoExposureSpeedUp);
        c.autoExposureSpeedUp               = uz::json::Float(j, "auto_exposure_speed_up", defaults.autoExposureSpeedUp);
        c.overrideAutoExposureSpeedDown     = uz::json::Bool(j, "override_auto_exposure_speed_down", defaults.overrideAutoExposureSpeedDown);
        c.autoExposureSpeedDown             = uz::json::Float(j, "auto_exposure_speed_down", defaults.autoExposureSpeedDown);

        // Depth of Field
        c.overrideDofFocalDistance  = uz::json::Bool(j, "override_dof_focal_distance", defaults.overrideDofFocalDistance);
        c.dofFocalDistance          = uz::json::Float(j, "dof_focal_distance", defaults.dofFocalDistance);
        c.overrideDofFocalRegion    = uz::json::Bool(j, "override_dof_focal_region", defaults.overrideDofFocalRegion);
        c.dofFocalRegion            = uz::json::Float(j, "dof_focal_region", defaults.dofFocalRegion);
        c.overrideDofNearTransition = uz::json::Bool(j, "override_dof_near_transition", defaults.overrideDofNearTransition);
        c.dofNearTransition         = uz::json::Float(j, "dof_near_transition", defaults.dofNearTransition);
        c.overrideDofFarTransition  = uz::json::Bool(j, "override_dof_far_transition", defaults.overrideDofFarTransition);
        c.dofFarTransition          = uz::json::Float(j, "dof_far_transition", defaults.dofFarTransition);
        c.overrideDofFstop          = uz::json::Bool(j, "override_dof_fstop", defaults.overrideDofFstop);
        c.dofFstop                  = uz::json::Float(j, "dof_fstop", defaults.dofFstop);

        // Motion Blur
        c.overrideMotionBlurAmount    = uz::json::Bool(j, "override_motion_blur_amount", defaults.overrideMotionBlurAmount);
        c.motionBlurAmount            = uz::json::Float(j, "motion_blur_amount", defaults.motionBlurAmount);
        c.overrideMotionBlurMax       = uz::json::Bool(j, "override_motion_blur_max", defaults.overrideMotionBlurMax);
        c.motionBlurMax               = uz::json::Float(j, "motion_blur_max", defaults.motionBlurMax);
        c.overrideMotionBlurTargetFPS = uz::json::Bool(j, "override_motion_blur_target_fps", defaults.overrideMotionBlurTargetFPS);
        c.motionBlurTargetFPS         = uz::json::Int(j, "motion_blur_target_fps", defaults.motionBlurTargetFPS);

        // Color Grading / White Balance
        c.overrideWhiteTemp = uz::json::Bool(j, "override_white_temp", defaults.overrideWhiteTemp);
        c.whiteTemp         = uz::json::Float(j, "white_temp", defaults.whiteTemp);
        c.overrideWhiteTint = uz::json::Bool(j, "override_white_tint", defaults.overrideWhiteTint);
        c.whiteTint         = uz::json::Float(j, "white_tint", defaults.whiteTint);

        // ACES Tonemap
        c.overrideFilmSlope     = uz::json::Bool(j, "override_film_slope", defaults.overrideFilmSlope);
        c.filmSlope             = uz::json::Float(j, "film_slope", defaults.filmSlope);
        c.overrideFilmToe       = uz::json::Bool(j, "override_film_toe", defaults.overrideFilmToe);
        c.filmToe               = uz::json::Float(j, "film_toe", defaults.filmToe);
        c.overrideFilmShoulder  = uz::json::Bool(j, "override_film_shoulder", defaults.overrideFilmShoulder);
        c.filmShoulder          = uz::json::Float(j, "film_shoulder", defaults.filmShoulder);
        c.overrideFilmBlackClip = uz::json::Bool(j, "override_film_black_clip", defaults.overrideFilmBlackClip);
        c.filmBlackClip         = uz::json::Float(j, "film_black_clip", defaults.filmBlackClip);
        c.overrideFilmWhiteClip = uz::json::Bool(j, "override_film_white_clip", defaults.overrideFilmWhiteClip);
        c.filmWhiteClip         = uz::json::Float(j, "film_white_clip", defaults.filmWhiteClip);

        // Vignette
        c.overrideVignetteIntensity = uz::json::Bool(j, "override_vignette_intensity", defaults.overrideVignetteIntensity);
        c.vignetteIntensity         = uz::json::Float(j, "vignette_intensity", defaults.vignetteIntensity);

        // SSAO
        c.overrideAmbientOcclusionIntensity = uz::json::Bool(j, "override_ambient_occlusion_intensity", defaults.overrideAmbientOcclusionIntensity);
        c.ambientOcclusionIntensity         = uz::json::Float(j, "ambient_occlusion_intensity", defaults.ambientOcclusionIntensity);
        c.overrideAmbientOcclusionRadius    = uz::json::Bool(j, "override_ambient_occlusion_radius", defaults.overrideAmbientOcclusionRadius);
        c.ambientOcclusionRadius            = uz::json::Float(j, "ambient_occlusion_radius", defaults.ambientOcclusionRadius);
        c.overrideAmbientOcclusionPower     = uz::json::Bool(j, "override_ambient_occlusion_power", defaults.overrideAmbientOcclusionPower);
        c.ambientOcclusionPower             = uz::json::Float(j, "ambient_occlusion_power", defaults.ambientOcclusionPower);

        // SSR
        c.overrideScreenSpaceReflectionIntensity = uz::json::Bool(j, "override_ssr_intensity", defaults.overrideScreenSpaceReflectionIntensity);
        c.screenSpaceReflectionIntensity         = uz::json::Float(j, "ssr_intensity", defaults.screenSpaceReflectionIntensity);
        c.overrideScreenSpaceReflectionQuality   = uz::json::Bool(j, "override_ssr_quality", defaults.overrideScreenSpaceReflectionQuality);
        c.screenSpaceReflectionQuality           = uz::json::Float(j, "ssr_quality", defaults.screenSpaceReflectionQuality);
    },
    // Serialize
    [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
        const auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
        const auto* c = world.GetRegistry().try_get<uz::PostProcessVolumeComponent>(entt);
        if (!c) return false;
        auto& j = *static_cast<nlohmann::json*>(jsonPtr);

        // Volume geometry
        uz::json::WriteVec3(j, "extents", c->extents);
        j["is_unbound"]   = c->isUnbound;

        // Blending control
        j["priority"]     = c->priority;
        j["blend_radius"] = c->blendRadius;
        j["blend_weight"] = c->blendWeight;
        j["enabled"]      = c->enabled;

        // Bloom
        j["override_bloom_intensity"] = c->overrideBloomIntensity;
        j["bloom_intensity"]          = c->bloomIntensity;
        j["override_bloom_threshold"] = c->overrideBloomThreshold;
        j["bloom_threshold"]          = c->bloomThreshold;

        // Auto-Exposure
        j["override_auto_exposure_min_brightness"] = c->overrideAutoExposureMinBrightness;
        j["auto_exposure_min_brightness"]          = c->autoExposureMinBrightness;
        j["override_auto_exposure_max_brightness"] = c->overrideAutoExposureMaxBrightness;
        j["auto_exposure_max_brightness"]          = c->autoExposureMaxBrightness;
        j["override_auto_exposure_bias"]           = c->overrideAutoExposureBias;
        j["auto_exposure_bias"]                    = c->autoExposureBias;
        j["override_auto_exposure_speed_up"]       = c->overrideAutoExposureSpeedUp;
        j["auto_exposure_speed_up"]                = c->autoExposureSpeedUp;
        j["override_auto_exposure_speed_down"]     = c->overrideAutoExposureSpeedDown;
        j["auto_exposure_speed_down"]              = c->autoExposureSpeedDown;

        // Depth of Field
        j["override_dof_focal_distance"]  = c->overrideDofFocalDistance;
        j["dof_focal_distance"]           = c->dofFocalDistance;
        j["override_dof_focal_region"]    = c->overrideDofFocalRegion;
        j["dof_focal_region"]             = c->dofFocalRegion;
        j["override_dof_near_transition"] = c->overrideDofNearTransition;
        j["dof_near_transition"]          = c->dofNearTransition;
        j["override_dof_far_transition"]  = c->overrideDofFarTransition;
        j["dof_far_transition"]           = c->dofFarTransition;
        j["override_dof_fstop"]           = c->overrideDofFstop;
        j["dof_fstop"]                    = c->dofFstop;

        // Motion Blur
        j["override_motion_blur_amount"]     = c->overrideMotionBlurAmount;
        j["motion_blur_amount"]              = c->motionBlurAmount;
        j["override_motion_blur_max"]        = c->overrideMotionBlurMax;
        j["motion_blur_max"]                 = c->motionBlurMax;
        j["override_motion_blur_target_fps"] = c->overrideMotionBlurTargetFPS;
        j["motion_blur_target_fps"]          = c->motionBlurTargetFPS;

        // Color Grading / White Balance
        j["override_white_temp"] = c->overrideWhiteTemp;
        j["white_temp"]          = c->whiteTemp;
        j["override_white_tint"] = c->overrideWhiteTint;
        j["white_tint"]          = c->whiteTint;

        // ACES Tonemap
        j["override_film_slope"]      = c->overrideFilmSlope;
        j["film_slope"]               = c->filmSlope;
        j["override_film_toe"]        = c->overrideFilmToe;
        j["film_toe"]                 = c->filmToe;
        j["override_film_shoulder"]   = c->overrideFilmShoulder;
        j["film_shoulder"]            = c->filmShoulder;
        j["override_film_black_clip"] = c->overrideFilmBlackClip;
        j["film_black_clip"]          = c->filmBlackClip;
        j["override_film_white_clip"] = c->overrideFilmWhiteClip;
        j["film_white_clip"]          = c->filmWhiteClip;

        // Vignette
        j["override_vignette_intensity"] = c->overrideVignetteIntensity;
        j["vignette_intensity"]          = c->vignetteIntensity;

        // SSAO
        j["override_ambient_occlusion_intensity"] = c->overrideAmbientOcclusionIntensity;
        j["ambient_occlusion_intensity"]          = c->ambientOcclusionIntensity;
        j["override_ambient_occlusion_radius"]    = c->overrideAmbientOcclusionRadius;
        j["ambient_occlusion_radius"]             = c->ambientOcclusionRadius;
        j["override_ambient_occlusion_power"]     = c->overrideAmbientOcclusionPower;
        j["ambient_occlusion_power"]              = c->ambientOcclusionPower;

        // SSR
        j["override_ssr_intensity"] = c->overrideScreenSpaceReflectionIntensity;
        j["ssr_intensity"]          = c->screenSpaceReflectionIntensity;
        j["override_ssr_quality"]   = c->overrideScreenSpaceReflectionQuality;
        j["ssr_quality"]            = c->screenSpaceReflectionQuality;

        return true;
    }
);
