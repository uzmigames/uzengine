/// Unit tests for VFX module HLSL shader code and the Execute() no-op contract.
///
/// Covers: SpawnModule, UpdateModule, DecayModule, TurbulenceModule,
/// ConstraintModule, SpriteRenderModule shader code content verification, and
/// that Execute() is a no-op that does not crash for any module type.

#include <gtest/gtest.h>

#include "modules/spawn_module.h"
#include "modules/update_module.h"
#include "modules/decay_module.h"
#include "modules/turbulence_module.h"
#include "modules/constraint_module.h"
#include "modules/sprite_render_module.h"

#include <string>
#include <string_view>

using namespace uz;

// =============================================================================
// SpawnModule shader
// =============================================================================

TEST(SpawnModule, GetShaderCode_NonEmpty) {
    EXPECT_FALSE(SpawnModule{}.GetShaderCode().empty())
        << "SpawnModule::GetShaderCode() must return a non-empty HLSL snippet";
}

// =============================================================================
// UpdateModule shader
// =============================================================================

/// The update shader integrates gravity and drag; the snippet must reference
/// both "cbGravity" and "cbDrag" to operate on the particle attributes.
TEST(UpdateModule, GetShaderCode_ContainsCbGravityAndCbDrag) {
    const std::string code(UpdateModule{}.GetShaderCode());

    EXPECT_FALSE(code.empty())
        << "UpdateModule::GetShaderCode() must return a non-empty HLSL snippet";
    EXPECT_NE(code.find("cbGravity"), std::string::npos)
        << "UpdateModule shader snippet must reference cbGravity";
    EXPECT_NE(code.find("cbDrag"), std::string::npos)
        << "UpdateModule shader snippet must reference cbDrag";
}

// =============================================================================
// DecayModule shader
// =============================================================================

/// The decay shader recycles expired particles via atomic free-list operations.
/// The snippet must reference "FreeList".
TEST(DecayModule, GetShaderCode_ContainsFreeList) {
    const std::string code(DecayModule{}.GetShaderCode());

    EXPECT_FALSE(code.empty())
        << "DecayModule::GetShaderCode() must return a non-empty HLSL snippet";
    EXPECT_NE(code.find("FreeList"), std::string::npos)
        << "DecayModule shader snippet must reference FreeList for particle recycling";
}

// =============================================================================
// TurbulenceModule shader
// =============================================================================

TEST(TurbulenceModule, GetShaderCode_NonEmpty) {
    EXPECT_FALSE(TurbulenceModule{}.GetShaderCode().empty())
        << "TurbulenceModule::GetShaderCode() must return a non-empty HLSL snippet";
}

// =============================================================================
// ConstraintModule shader
// =============================================================================

/// The constraint shader clamps positions to an AABB. The HLSL `clamp`
/// intrinsic must be present in the snippet.
TEST(ConstraintModule, GetShaderCode_ContainsClamp) {
    const std::string code(ConstraintModule{}.GetShaderCode());

    EXPECT_FALSE(code.empty())
        << "ConstraintModule::GetShaderCode() must return a non-empty HLSL snippet";
    EXPECT_NE(code.find("clamp"), std::string::npos)
        << "ConstraintModule shader snippet must contain HLSL 'clamp' intrinsic";
}

// =============================================================================
// SpriteRenderModule shader
// =============================================================================

TEST(SpriteRenderModule, GetShaderCode_NonEmpty) {
    EXPECT_FALSE(SpriteRenderModule{}.GetShaderCode().empty())
        << "SpriteRenderModule::GetShaderCode() must return a non-empty HLSL snippet";
}

// =============================================================================
// Execute no-op contract
// =============================================================================

/// All modules declare Execute() as a no-op. Verify it does not crash for any
/// module when called with a valid ExecuteContext.
TEST(VFXModuleShaders, Execute_NoOp_DoesNotCrashForAnyModule) {
    ExecuteContext ctx{};
    ctx.numParticles = 100;
    ctx.dt           = 0.016f;
    ctx.frameNumber  = 1;

    SpawnModule      spawn;
    UpdateModule     update;
    DecayModule      decay;
    TurbulenceModule turbulence;
    ConstraintModule constraint;
    SpriteRenderModule sprite;

    spawn.Execute(ctx);
    update.Execute(ctx);
    decay.Execute(ctx);
    turbulence.Execute(ctx);
    constraint.Execute(ctx);
    sprite.Execute(ctx);

    SUCCEED(); // reaching here means no crash
}
