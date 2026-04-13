/// Builtin module registration -- populates ModuleRegistry with core VFX modules.

#include "register_builtin_modules.h"

#include "engine/vfx/modules/module_registry.h"

// Core simulation modules
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/decay_module.h"
#include "engine/vfx/modules/sprite_render_module.h"
#include "engine/vfx/modules/turbulence_module.h"
#include "engine/vfx/modules/constraint_module.h"

// Advanced VFX modules
#include "engine/vfx/modules/collision_module.h"
#include "engine/vfx/modules/light_render_module.h"
#include "engine/vfx/modules/mesh_render_module.h"

// Simulation force modules (Phase 3.68.0)
#include "engine/vfx/modules/attractor_module.h"
#include "engine/vfx/modules/vector_field_module.h"
#include "engine/vfx/modules/sub_emitter_module.h"
#include "engine/vfx/modules/orbit_module.h"
#include "engine/vfx/modules/pin_module.h"
#include "engine/vfx/modules/distance_constraint_module.h"

// Dynamic material parameter modules (Phase 4.14.0)
#include "engine/vfx/modules/dynamic_params_module.h"

// Data interface modules (Phase 4.16.0+)
#include "engine/vfx/modules/skeletal_spawn_module.h"
#include "engine/vfx/modules/audio_spectrum_module.h"

// Spatial interaction and feedback modules (Phase 4.18.0+)
#include "engine/vfx/modules/neighbor_interaction_module.h"
#include "engine/vfx/modules/render_target_di_module.h"
#include "engine/vfx/modules/async_gpu_trace_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

void RegisterBuiltinModules(ModuleRegistry& registry)
{
    // Core simulation modules
    registry.Register<SpawnModule>("spawn");
    registry.Register<UpdateModule>("update");
    registry.Register<DecayModule>("decay");
    registry.Register<SpriteRenderModule>("sprite_render");
    registry.Register<TurbulenceModule>("turbulence");
    registry.Register<ConstraintModule>("constraint");

    // Advanced VFX modules (collision, lighting, mesh instancing)
    registry.Register<CollisionModule>("collision");
    registry.Register<LightRenderModule>("light_render");
    registry.Register<MeshRenderModule>("mesh_render");

    // Simulation force modules (Phase 3.68.0)
    registry.Register<AttractorModule>("attractor");
    registry.Register<VectorFieldModule>("vector_field");
    registry.Register<SubEmitterModule>("sub_emitter");
    registry.Register<OrbitModule>("orbit");
    registry.Register<PinModule>("pin");
    registry.Register<DistanceConstraintModule>("distance_constraint");

    // Dynamic material parameter modules (Phase 4.14.0)
    registry.Register<DynamicParamsModule>("dynamic_params");

    // Data interface modules (Phase 4.16.0+)
    registry.Register<SkeletalSpawnModule>("skeletal_spawn");
    registry.Register<AudioSpectrumModule>("audio_spectrum");

    // Spatial interaction and feedback modules (Phase 4.18.0+)
    registry.Register<NeighborInteractionModule>("neighbor_interaction");
    registry.Register<RenderTargetDIModule>("render_target_di");
    registry.Register<AsyncGPUTraceModule>("async_gpu_trace");

    UZ_LOG(VFX, Info, "Registered {} built-in VFX modules",
           registry.GetRegisteredNames().size());
}

} // namespace uz
