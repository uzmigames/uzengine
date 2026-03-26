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

    UZ_LOG(VFX, Info, "Registered {} built-in VFX modules",
           registry.GetRegisteredNames().size());
}

} // namespace uz
