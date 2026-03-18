/// UzEngine — Aggregate include for renderer core infrastructure headers.
#pragma once

// GBuffer & draw list
#include "gbuffer.h"
#include "gbuffer_per_object_params.h"
#include "draw_list.h"
#include "render_path.h"
#include "debug_view_mode.h"

// Constant buffer definitions
#include "constant_buffers.h"
#include "per_frame_params.h"
#include "shadow_data_params.h"
#include "shadow_pass_params.h"
#include "lighting_params.h"

// Pipeline & shader infrastructure
#include "pipeline_library.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "frame_pacer.h"

// Shader parameter system
#include "shader_parameters.h"
#include "shader_parameter_binder.h"
#include "shader_parameter_hlsl.h"
#include "shader_parameter_macros.h"

// Rendering helpers
#include "root_slots.h"
#include "instance_batcher.h"
#include "render_world.h"
#include "scene_lighting.h"
#include "multi_light.h"

// LOD
#include "lod_group.h"
#include "lod_selector.h"
