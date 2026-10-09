/// Unit tests for VFX module defaults and GetName / GetParameters.
///
/// Covers: SpawnModule, UpdateModule, DecayModule, TurbulenceModule,
/// ConstraintModule, SpriteRenderModule. All tests verify default field values,
/// module name identifiers, and parameter list contents. No GPU required.

#include <gtest/gtest.h>

#include "modules/spawn_module.h"
#include "modules/update_module.h"
#include "modules/decay_module.h"
#include "modules/turbulence_module.h"
#include "modules/constraint_module.h"
#include "modules/sprite_render_module.h"

#include <string_view>
#include <vector>

using namespace uz;

namespace {

/// Returns true if a parameter named `paramName` exists in `params`.
bool HasParameter(const std::vector<ParameterDef>& params, const char* paramName) {
    for (const auto& p : params) {
        if (p.name && std::string_view(p.name) == std::string_view(paramName)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

// =============================================================================
// SpawnModule -- defaults, name, and parameters
// =============================================================================

TEST(SpawnModule, Defaults_AllFieldsMatchSpec) {
    SpawnModule mod;
    EXPECT_FLOAT_EQ(mod.rate, 100.0f);
    EXPECT_FLOAT_EQ(mod.lifespanMin, 1.0f);
    EXPECT_FLOAT_EQ(mod.lifespanMax, 5.0f);
    EXPECT_FLOAT_EQ(mod.velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(mod.velocity.y, 10.0f);
    EXPECT_FLOAT_EQ(mod.velocity.z, 0.0f);
    EXPECT_FLOAT_EQ(mod.velocityVariation, 1.0f);
    EXPECT_FLOAT_EQ(mod.spreadAngle, 30.0f);
    EXPECT_EQ(mod.spawnShape, SpawnShape::Point);
    EXPECT_FLOAT_EQ(mod.spawnRadius, 1.0f);
    EXPECT_FLOAT_EQ(mod.spawnBoxExtents.x, 1.0f);
    EXPECT_FLOAT_EQ(mod.spawnBoxExtents.y, 1.0f);
    EXPECT_FLOAT_EQ(mod.spawnBoxExtents.z, 1.0f);
}

TEST(SpawnModule, GetName_ReturnsSpawn) {
    EXPECT_STREQ(SpawnModule{}.GetName(), "spawn");
}

TEST(SpawnModule, GetParameters_ContainsExpectedParameters) {
    const auto params = SpawnModule{}.GetParameters();
    EXPECT_FALSE(params.empty());
    EXPECT_TRUE(HasParameter(params, "rate"));
    EXPECT_TRUE(HasParameter(params, "lifespan_min"));
    EXPECT_TRUE(HasParameter(params, "lifespan_max"));
    EXPECT_TRUE(HasParameter(params, "velocity"));
    EXPECT_TRUE(HasParameter(params, "velocity_variation"));
    EXPECT_TRUE(HasParameter(params, "spread_angle"));
    EXPECT_TRUE(HasParameter(params, "spawn_radius"));
}

// =============================================================================
// UpdateModule -- defaults, name, and parameters
// =============================================================================

TEST(UpdateModule, Defaults_AllFieldsMatchSpec) {
    UpdateModule mod;
    EXPECT_FLOAT_EQ(mod.gravity.x, 0.0f);
    EXPECT_FLOAT_EQ(mod.gravity.y, 0.0f);
    EXPECT_FLOAT_EQ(mod.gravity.z, -981.0f);
    EXPECT_FLOAT_EQ(mod.drag, 0.98f);
    EXPECT_FLOAT_EQ(mod.angularVelocity, 0.0f);
}

TEST(UpdateModule, GetName_ReturnsUpdate) {
    EXPECT_STREQ(UpdateModule{}.GetName(), "update");
}

// =============================================================================
// DecayModule -- name and empty parameters
// =============================================================================

TEST(DecayModule, GetName_ReturnsDecay) {
    EXPECT_STREQ(DecayModule{}.GetName(), "decay");
}

TEST(DecayModule, GetParameters_ReturnsEmptyVector) {
    EXPECT_TRUE(DecayModule{}.GetParameters().empty());
}

// =============================================================================
// TurbulenceModule -- defaults, name, and parameters
// =============================================================================

TEST(TurbulenceModule, Defaults_AllFieldsMatchSpec) {
    TurbulenceModule mod;
    EXPECT_FLOAT_EQ(mod.strength, 1.0f);
    EXPECT_FLOAT_EQ(mod.frequency, 0.5f);
    EXPECT_EQ(mod.octaves, 3);
    EXPECT_EQ(TurbulenceModule::kMinOctaves, 1);
    EXPECT_EQ(TurbulenceModule::kMaxOctaves, 8);
}

TEST(TurbulenceModule, GetName_ReturnsTurbulence) {
    EXPECT_STREQ(TurbulenceModule{}.GetName(), "turbulence");
}

TEST(TurbulenceModule, GetParameters_ThreeParameters_StrengthFrequencyOctaves) {
    const auto params = TurbulenceModule{}.GetParameters();
    EXPECT_EQ(params.size(), 3u);
    EXPECT_TRUE(HasParameter(params, "strength"));
    EXPECT_TRUE(HasParameter(params, "frequency"));
    EXPECT_TRUE(HasParameter(params, "octaves"));
}

// =============================================================================
// ConstraintModule -- defaults and name
// =============================================================================

TEST(ConstraintModule, Defaults_BoundsMatchSpec) {
    ConstraintModule mod;
    EXPECT_FLOAT_EQ(mod.boundsMin.x, -100.0f);
    EXPECT_FLOAT_EQ(mod.boundsMin.y, -100.0f);
    EXPECT_FLOAT_EQ(mod.boundsMin.z, -100.0f);
    EXPECT_FLOAT_EQ(mod.boundsMax.x, 100.0f);
    EXPECT_FLOAT_EQ(mod.boundsMax.y, 100.0f);
    EXPECT_FLOAT_EQ(mod.boundsMax.z, 100.0f);
}

TEST(ConstraintModule, GetName_ReturnsConstraint) {
    EXPECT_STREQ(ConstraintModule{}.GetName(), "constraint");
}

// =============================================================================
// SpriteRenderModule -- defaults, name, parameters, and depth_sort type
// =============================================================================

TEST(SpriteRenderModule, Defaults_AllFieldsMatchSpec) {
    SpriteRenderModule mod;
    EXPECT_FLOAT_EQ(mod.spriteWidth, 1.0f);
    EXPECT_FLOAT_EQ(mod.spriteHeight, 1.0f);
    EXPECT_TRUE(mod.depthSort);
    EXPECT_EQ(mod.sizeOverLifeCurveId, 0u);
    EXPECT_EQ(mod.colorOverLifeCurveId, 0u);

    // Flipbook defaults.
    EXPECT_EQ(mod.flipbookRows, 1u);
    EXPECT_EQ(mod.flipbookCols, 1u);
    EXPECT_EQ(mod.flipbookTotalFrames, 1u);
    EXPECT_FLOAT_EQ(mod.flipbookFPS, 30.0f);
    EXPECT_EQ(mod.flipbookBlendMode, FlipbookBlendMode::None);
}

TEST(SpriteRenderModule, GetName_ReturnsSpriteRender) {
    EXPECT_STREQ(SpriteRenderModule{}.GetName(), "sprite_render");
}

TEST(SpriteRenderModule, GetParameters_HasDepthSortSpriteWidthSpriteHeight) {
    const auto params = SpriteRenderModule{}.GetParameters();
    EXPECT_FALSE(params.empty());
    EXPECT_TRUE(HasParameter(params, "depth_sort"));
    EXPECT_TRUE(HasParameter(params, "sprite_width"));
    EXPECT_TRUE(HasParameter(params, "sprite_height"));
}

TEST(SpriteRenderModule, GetParameters_DepthSort_TypeIsBool) {
    const auto params = SpriteRenderModule{}.GetParameters();
    for (const auto& p : params) {
        if (p.name && std::string_view(p.name) == "depth_sort") {
            EXPECT_EQ(p.type, ParameterType::Bool);
            return;
        }
    }
    ADD_FAILURE() << "depth_sort parameter not found in SpriteRenderModule";
}

// =============================================================================
// SpriteRenderModule -- flipbook / SubUV animation
// =============================================================================

TEST(SpriteRenderModule, HasFlipbook_FalseWithDefaults) {
    const SpriteRenderModule mod;
    EXPECT_FALSE(mod.HasFlipbook());
}

TEST(SpriteRenderModule, HasFlipbook_TrueWhenRowsGreaterThanOne) {
    SpriteRenderModule mod;
    mod.flipbookRows = 4;
    EXPECT_TRUE(mod.HasFlipbook());
}

TEST(SpriteRenderModule, HasFlipbook_TrueWhenColsGreaterThanOne) {
    SpriteRenderModule mod;
    mod.flipbookCols = 4;
    EXPECT_TRUE(mod.HasFlipbook());
}

TEST(SpriteRenderModule, HasFlipbook_TrueWhenBothRowsAndColsGreaterThanOne) {
    SpriteRenderModule mod;
    mod.flipbookRows = 4;
    mod.flipbookCols = 4;
    EXPECT_TRUE(mod.HasFlipbook());
}

TEST(SpriteRenderModule, GetParameters_ContainsFlipbookParams) {
    const auto params = SpriteRenderModule{}.GetParameters();
    EXPECT_EQ(params.size(), 8u);
    EXPECT_TRUE(HasParameter(params, "flipbook_rows"));
    EXPECT_TRUE(HasParameter(params, "flipbook_cols"));
    EXPECT_TRUE(HasParameter(params, "flipbook_total_frames"));
    EXPECT_TRUE(HasParameter(params, "flipbook_fps"));
    EXPECT_TRUE(HasParameter(params, "flipbook_blend_mode"));
}

TEST(SpriteRenderModule, GetParameters_FlipbookRows_TypeIsInt) {
    const auto params = SpriteRenderModule{}.GetParameters();
    for (const auto& p : params) {
        if (p.name && std::string_view(p.name) == "flipbook_rows") {
            EXPECT_EQ(p.type, ParameterType::Int);
            EXPECT_EQ(p.defaultInt, 1);
            return;
        }
    }
    ADD_FAILURE() << "flipbook_rows parameter not found";
}

TEST(SpriteRenderModule, GetParameters_FlipbookFPS_TypeIsFloat) {
    const auto params = SpriteRenderModule{}.GetParameters();
    for (const auto& p : params) {
        if (p.name && std::string_view(p.name) == "flipbook_fps") {
            EXPECT_EQ(p.type, ParameterType::Float);
            EXPECT_FLOAT_EQ(p.defaultFloat, 30.0f);
            return;
        }
    }
    ADD_FAILURE() << "flipbook_fps parameter not found";
}

TEST(SpriteRenderModule, GetParameters_FlipbookBlendMode_DefaultIsNone) {
    const auto params = SpriteRenderModule{}.GetParameters();
    for (const auto& p : params) {
        if (p.name && std::string_view(p.name) == "flipbook_blend_mode") {
            EXPECT_EQ(p.type, ParameterType::Int);
            EXPECT_EQ(p.defaultInt, static_cast<int32_t>(FlipbookBlendMode::None));
            return;
        }
    }
    ADD_FAILURE() << "flipbook_blend_mode parameter not found";
}

TEST(SpriteRenderModule, GetParameters_FlipbookParams_ReflectMutatedValues) {
    SpriteRenderModule mod;
    mod.flipbookRows = 8;
    mod.flipbookCols = 4;
    mod.flipbookTotalFrames = 30;
    mod.flipbookFPS = 24.0f;
    mod.flipbookBlendMode = FlipbookBlendMode::Linear;

    const auto params = mod.GetParameters();
    for (const auto& p : params) {
        if (!p.name) continue;
        const std::string_view name(p.name);
        if (name == "flipbook_rows")         EXPECT_EQ(p.defaultInt, 8);
        if (name == "flipbook_cols")         EXPECT_EQ(p.defaultInt, 4);
        if (name == "flipbook_total_frames") EXPECT_EQ(p.defaultInt, 30);
        if (name == "flipbook_fps")          EXPECT_FLOAT_EQ(p.defaultFloat, 24.0f);
        if (name == "flipbook_blend_mode")   EXPECT_EQ(p.defaultInt, static_cast<int32_t>(FlipbookBlendMode::Linear));
    }
}

TEST(FlipbookBlendMode, EnumValues_MatchExpectedOrder) {
    EXPECT_EQ(static_cast<uint8_t>(FlipbookBlendMode::None), 0);
    EXPECT_EQ(static_cast<uint8_t>(FlipbookBlendMode::Linear), 1);
    EXPECT_EQ(static_cast<uint8_t>(FlipbookBlendMode::Random), 2);
}
