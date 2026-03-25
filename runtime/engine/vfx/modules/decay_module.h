#pragma once

/// VFX Decay Module -- particle age tracking and death recycling.
///
/// Increments particle age each frame and returns expired particles to the
/// free list for reuse. Dead particles are marked with age = -1.0f so other
/// modules can skip them. This module has no configurable parameters.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.

#include "vfx_module.h"

namespace uz {

// ---------------------------------------------------------------------------
// DecayModule -- age increment and free-list recycling
// ---------------------------------------------------------------------------

/// Advances particle age and recycles expired particles.
///
/// Each frame the HLSL snippet increments the age of every living particle
/// by `cbDt`. When age exceeds the particle's lifespan, the slot is returned
/// to the free list via atomic increment and the age is set to -1.0f (dead
/// sentinel). No configurable parameters -- lifespan is set by SpawnModule.
class DecayModule final : public IVFXModule {
public:
    DecayModule() = default;
    ~DecayModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "decay".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns HLSL snippet for age update and free-list return.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns an empty vector (no configurable parameters).
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op. Compute shader handles all decay logic on the GPU.
    void Execute(const ExecuteContext& ctx) override;
};

} // namespace uz
