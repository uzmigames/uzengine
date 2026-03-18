/// UzEngine — Aggregate include for renderer subsystem headers.
#pragma once

// Shadow subsystems
#include "shadows/shadow_atlas.h"
#include "shadows/shadow_cache.h"
#include "shadows/shadow_cascade.h"
#include "shadows/shadow_renderer.h"
#include "shadows/sdf_atlas_manager.h"
#include "shadows/sdf_types.h"
#include "shadows/distance_field_shadow_config.h"
#include "shadows/local_light_shadow_atlas.h"

// Lighting subsystems
#include "lighting/light_renderer.h"
#include "lighting/brdf_math.h"

// Atmosphere subsystems
#include "atmosphere/atmosphere_renderer.h"

// Fog subsystems
#include "fog/fog_renderer.h"
