/// UzEngine — Aggregate include for all ECS component headers.
#pragma once

// Core
#include "core/component.h"
#include "core/entity.h"

// Scene components
#include "scene/camera_component.h"
#include "scene/scene_component.h"
#include "scene/primitive_component.h"

// Mesh components
#include "mesh/static_mesh_component.h"

// Lighting components
#include "lighting/light_component.h"

// Environment components
#include "environment/decal_component.h"
#include "environment/sky_atmosphere_component.h"
#include "environment/sky_light_component.h"
#include "environment/height_fog_component.h"
#include "environment/volumetric_clouds_component.h"
#include "environment/vfx_emitter_component.h"

// Metadata components
#include "metadata/name_component.h"

// Update components
#include "update/tickable_component.h"
