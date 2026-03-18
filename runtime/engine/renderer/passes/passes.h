/// UzEngine — Aggregate include for all render pass headers.
#pragma once

// Geometry passes
#include "geometry/depth_prepass.h"
#include "geometry/gbuffer_pass.h"
#include "geometry/gpu_cull_pass.h"
#include "geometry/pbr_pass.h"

// Lighting passes
#include "lighting/shadow_pass.h"
#include "lighting/lighting_pass.h"

// Debug passes
#include "debug/gbuffer_debug_pass.h"
#include "debug/debug_view_pass.h"

// Atmosphere passes
#include "atmosphere/sky_atmosphere_pass.h"
#include "atmosphere/transmittance_lut_pass.h"
#include "atmosphere/sky_cubemap_capture_pass.h"
#include "atmosphere/sky_cubemap_capture_ps_pass.h"
#include "atmosphere/height_fog_pass.h"

// Post-process passes
#include "postprocess/auto_exposure.h"
#include "postprocess/bloom_pass.h"
#include "postprocess/gtao_pass.h"
#include "postprocess/hiz_pyramid_pass.h"
#include "postprocess/velocity_pass.h"
#include "postprocess/postprocess_math.h"
#include "postprocess/postprocess_stack.h"
