/// VFX Decay Module implementation -- age tracking and free-list recycling.

#include "decay_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for age update and particle death
// ---------------------------------------------------------------------------

static constexpr const char* kDecayShaderCode = R"hlsl(
// --- DecayModule ---
float age = Ages[particleIdx] + cbDt;
Ages[particleIdx] = age;
if (age >= Lifespans[particleIdx]) {
    uint freeSlot = 0;
    InterlockedAdd(FreeListHead[0], 1, freeSlot);
    FreeList[freeSlot] = particleIdx;
    Ages[particleIdx] = -1.0f; // Mark dead
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* DecayModule::GetName() const noexcept {
    return "decay";
}

/// Returns the HLSL snippet for age advancement and free-list recycling.
/// Uses atomic InterlockedAdd to safely return expired particle slots.
/// Expects cbDt (float) in the per-dispatch constant buffer.
std::string_view DecayModule::GetShaderCode() const {
    return kDecayShaderCode;
}

/// Decay has no configurable parameters. Lifespan values are set by
/// SpawnModule and stored in the Lifespans buffer.
std::vector<ParameterDef> DecayModule::GetParameters() const {
    return {};
}

/// No-op on CPU side. The decay compute shader handles age tracking
/// and free-list return entirely on the GPU.
void DecayModule::Execute(const ExecuteContext& /*ctx*/) {
    // GPU-only module -- nothing to do on the CPU side.
}

} // namespace uz
