/// Unit tests for DecalAsset.
///
/// Covers: LoadFromString with a fully-specified valid JSON (all fields parsed
/// correctly), optional fields with default values, missing required fields
/// (material, format, version), lifetime <= 0, invalid blend_mode string,
/// halfExtents with a zero component, malformed JSON, normalIntensity clamping,
/// fadeDuration clamping to [0, lifetime], roughnessOverride -1 (no-override
/// sentinel), Load with nonexistent file path, and all four blend mode values.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/decals/decal_asset.h"

#include <string>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Valid JSON fixtures
// ─────────────────────────────────────────────────────────────────────────────

/// A fully-specified, valid .uzdecal JSON.
constexpr const char* kFullySpecifiedJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/bullet_hole.uzmat",
    "blend_mode": "Additive",
    "lifetime": 15.0,
    "fade_duration": 3.0,
    "normal_intensity": 0.6,
    "roughness_override": 0.3,
    "sort_order": 5,
    "half_extents": [0.4, 0.3, 0.15]
})";

/// A minimal valid JSON with only the required fields (format, version, material).
constexpr const char* kMinimalJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/splash.uzmat"
})";

} // anonymous namespace

// =============================================================================
// LoadFromString — fully-specified valid JSON
// =============================================================================

TEST(DecalAsset, LoadFromString_FullySpecified_ReturnsNonNull) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr)
        << "LoadFromString must succeed for a fully-specified valid JSON";
}

TEST(DecalAsset, LoadFromString_FullySpecified_MaterialPathCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->materialPath, "materials/bullet_hole.uzmat");
}

TEST(DecalAsset, LoadFromString_FullySpecified_BlendModeAdditive) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->blendMode, DecalBlendMode::Additive);
}

TEST(DecalAsset, LoadFromString_FullySpecified_LifetimeCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->lifetime, 15.0f);
}

TEST(DecalAsset, LoadFromString_FullySpecified_FadeDurationCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->fadeDuration, 3.0f);
}

TEST(DecalAsset, LoadFromString_FullySpecified_NormalIntensityCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->normalIntensity, 0.6f);
}

TEST(DecalAsset, LoadFromString_FullySpecified_RoughnessOverrideCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->roughnessOverride, 0.3f);
}

TEST(DecalAsset, LoadFromString_FullySpecified_SortOrderCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 5);
}

TEST(DecalAsset, LoadFromString_FullySpecified_HalfExtentsCorrect) {
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->halfExtents.x, 0.4f);
    EXPECT_FLOAT_EQ(asset->halfExtents.y, 0.3f);
    EXPECT_FLOAT_EQ(asset->halfExtents.z, 0.15f);
}

TEST(DecalAsset, LoadFromString_FullySpecified_SourcePathIsEmpty) {
    // LoadFromString does not set sourcePath -- that's only for Load().
    const auto asset = DecalAsset::LoadFromString(kFullySpecifiedJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->sourcePath.empty());
}

// =============================================================================
// LoadFromString — all blend mode values
// =============================================================================

TEST(DecalAsset, LoadFromString_BlendModeOpaque_Parsed) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": "Opaque"
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->blendMode, DecalBlendMode::Opaque);
}

TEST(DecalAsset, LoadFromString_BlendModeMultiply_Parsed) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": "Multiply"
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->blendMode, DecalBlendMode::Multiply);
}

TEST(DecalAsset, LoadFromString_BlendModeScreen_Parsed) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": "Screen"
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->blendMode, DecalBlendMode::Screen);
}

// =============================================================================
// LoadFromString — default values when optional fields are absent
// =============================================================================

TEST(DecalAsset, LoadFromString_Minimal_BlendModeDefaultsToOpaque) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->blendMode, DecalBlendMode::Opaque);
}

TEST(DecalAsset, LoadFromString_Minimal_LifetimeDefaultsTo10) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->lifetime, 10.0f);
}

TEST(DecalAsset, LoadFromString_Minimal_FadeDurationDefaultsTo2) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->fadeDuration, 2.0f);
}

TEST(DecalAsset, LoadFromString_Minimal_NormalIntensityDefaultsTo0p8) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->normalIntensity, 0.8f);
}

TEST(DecalAsset, LoadFromString_Minimal_RoughnessOverrideDefaultsToNegativeOne) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->roughnessOverride, -1.0f);
}

TEST(DecalAsset, LoadFromString_Minimal_SortOrderDefaultsToZero) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 0);
}

TEST(DecalAsset, LoadFromString_Minimal_HalfExtentsDefaultTo_0p5_0p5_0p2) {
    const auto asset = DecalAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->halfExtents.x, 0.5f);
    EXPECT_FLOAT_EQ(asset->halfExtents.y, 0.5f);
    EXPECT_FLOAT_EQ(asset->halfExtents.z, 0.2f);
}

// =============================================================================
// LoadFromString — invalid format field
// =============================================================================

TEST(DecalAsset, LoadFromString_WrongFormat_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzparticle", "version": 1, "material": "m.uzmat"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Wrong 'format' field must cause load failure";
}

TEST(DecalAsset, LoadFromString_MissingFormat_ReturnsNullptr) {
    const std::string json = R"({
        "version": 1, "material": "m.uzmat"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Missing 'format' field must cause load failure";
}

TEST(DecalAsset, LoadFromString_FormatIsNumber_ReturnsNullptr) {
    const std::string json = R"({
        "format": 42, "version": 1, "material": "m.uzmat"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — missing or invalid version field
// =============================================================================

TEST(DecalAsset, LoadFromString_MissingVersion_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "material": "m.uzmat"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Missing 'version' must cause load failure";
}

TEST(DecalAsset, LoadFromString_VersionIsString_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": "1", "material": "m.uzmat"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "String version must cause load failure (must be integer)";
}

// =============================================================================
// LoadFromString — missing or invalid material field
// =============================================================================

TEST(DecalAsset, LoadFromString_MissingMaterial_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Missing 'material' must cause load failure";
}

TEST(DecalAsset, LoadFromString_EmptyMaterial_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": ""
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Empty 'material' path must cause load failure";
}

TEST(DecalAsset, LoadFromString_MaterialIsNumber_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": 123
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — invalid lifetime
// =============================================================================

TEST(DecalAsset, LoadFromString_LifetimeIsZero_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat", "lifetime": 0.0
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "lifetime = 0 must cause load failure";
}

TEST(DecalAsset, LoadFromString_LifetimeIsNegative_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat", "lifetime": -5.0
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Negative lifetime must cause load failure";
}

TEST(DecalAsset, LoadFromString_LifetimeIsString_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat", "lifetime": "ten"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "String lifetime must cause load failure";
}

// =============================================================================
// LoadFromString — invalid blend_mode
// =============================================================================

TEST(DecalAsset, LoadFromString_InvalidBlendModeString_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": "Transparent"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Unrecognized blend_mode string must cause load failure";
}

TEST(DecalAsset, LoadFromString_LowercaseBlendMode_ReturnsNullptr) {
    // Blend mode parsing is case-sensitive.
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": "opaque"
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Lowercase blend_mode must fail (parsing is case-sensitive)";
}

TEST(DecalAsset, LoadFromString_BlendModeIsNumber_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1,
        "material": "m.uzmat", "blend_mode": 0
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Numeric blend_mode must cause load failure";
}

// =============================================================================
// LoadFromString — invalid half_extents
// =============================================================================

TEST(DecalAsset, LoadFromString_HalfExtentsXIsZero_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [0.0, 0.5, 0.2]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "half_extents.x = 0 must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsYIsZero_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [0.5, 0.0, 0.2]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "half_extents.y = 0 must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsZIsZero_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [0.5, 0.5, 0.0]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "half_extents.z = 0 must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsXIsNegative_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [-0.5, 0.5, 0.2]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Negative half_extents.x must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsWrongArraySize_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [0.5, 0.5]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "half_extents array with 2 elements must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsContainsString_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": [0.5, "big", 0.2]
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Non-numeric half_extents element must cause load failure";
}

TEST(DecalAsset, LoadFromString_HalfExtentsIsNotArray_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "half_extents": 0.5
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Scalar half_extents must cause load failure";
}

// =============================================================================
// LoadFromString — normalIntensity clamping
// =============================================================================

TEST(DecalAsset, LoadFromString_NormalIntensityAboveOne_ClampedTo1) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "normal_intensity": 5.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr)
        << "normal_intensity > 1 should be clamped (not rejected)";
    EXPECT_FLOAT_EQ(asset->normalIntensity, 1.0f)
        << "normal_intensity must be clamped to 1.0";
}

TEST(DecalAsset, LoadFromString_NormalIntensityBelowZero_ClampedTo0) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "normal_intensity": -2.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr)
        << "normal_intensity < 0 should be clamped (not rejected)";
    EXPECT_FLOAT_EQ(asset->normalIntensity, 0.0f)
        << "normal_intensity must be clamped to 0.0";
}

// =============================================================================
// LoadFromString — fade_duration clamping
// =============================================================================

TEST(DecalAsset, LoadFromString_FadeDurationExceedsLifetime_ClampedToLifetime) {
    // fade_duration clamped to [0, lifetime].
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "lifetime": 5.0, "fade_duration": 100.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->fadeDuration, 5.0f)
        << "fade_duration exceeding lifetime must be clamped to lifetime";
}

TEST(DecalAsset, LoadFromString_NegativeFadeDuration_ClampedToZero) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "lifetime": 10.0, "fade_duration": -3.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->fadeDuration, 0.0f)
        << "Negative fade_duration must be clamped to 0.0";
}

// =============================================================================
// LoadFromString — malformed JSON
// =============================================================================

TEST(DecalAsset, LoadFromString_MalformedJson_ReturnsNullptr) {
    EXPECT_EQ(DecalAsset::LoadFromString("{not valid json}"), nullptr)
        << "Malformed JSON must cause load failure";
}

TEST(DecalAsset, LoadFromString_EmptyString_ReturnsNullptr) {
    EXPECT_EQ(DecalAsset::LoadFromString(""), nullptr)
        << "Empty string must cause load failure";
}

TEST(DecalAsset, LoadFromString_PlainString_ReturnsNullptr) {
    EXPECT_EQ(DecalAsset::LoadFromString("hello"), nullptr);
}

TEST(DecalAsset, LoadFromString_EmptyJsonObject_ReturnsNullptr) {
    // {} has neither format nor material.
    EXPECT_EQ(DecalAsset::LoadFromString("{}"), nullptr);
}

TEST(DecalAsset, LoadFromString_JsonArray_ReturnsNullptr) {
    EXPECT_EQ(DecalAsset::LoadFromString("[1,2,3]"), nullptr);
}

// =============================================================================
// LoadFromString — sort_order (integer, optional)
// =============================================================================

TEST(DecalAsset, LoadFromString_SortOrderIsFloat_ReturnsNullptr) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "sort_order": 1.5
    })";
    EXPECT_EQ(DecalAsset::LoadFromString(json), nullptr)
        << "Non-integer sort_order must cause load failure";
}

TEST(DecalAsset, LoadFromString_NegativeSortOrder_IsValid) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "sort_order": -10
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, -10);
}

// =============================================================================
// LoadFromString — roughnessOverride semantics
// =============================================================================

TEST(DecalAsset, LoadFromString_RoughnessOverrideNegativeOne_IsNoOverride) {
    // -1.0 is the documented sentinel meaning "no roughness override".
    // The loader must accept it without clamping or rejecting it.
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "roughness_override": -1.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr)
        << "roughness_override = -1.0 (no-override sentinel) must be accepted";
    EXPECT_FLOAT_EQ(asset->roughnessOverride, -1.0f)
        << "roughness_override must be stored verbatim as -1.0";
}

TEST(DecalAsset, LoadFromString_RoughnessOverrideZeroToOne_IsAccepted) {
    // Values in [0, 1] override the surface roughness directly.
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "roughness_override": 0.75
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->roughnessOverride, 0.75f);
}

TEST(DecalAsset, LoadFromString_RoughnessOverrideZero_IsAccepted) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "roughness_override": 0.0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->roughnessOverride, 0.0f);
}

// =============================================================================
// Load (file path) — nonexistent file
// =============================================================================

TEST(DecalAsset, Load_NonexistentFilePath_ReturnsNullptr) {
    // The file does not exist -- Load must return nullptr gracefully.
    const auto asset = DecalAsset::Load(
        "__nonexistent_decal_asset_test_file_xyz123__.uzdecal");
    EXPECT_EQ(asset, nullptr)
        << "Load must return nullptr for a path that does not exist";
}

TEST(DecalAsset, Load_EmptyFilePath_ReturnsNullptr) {
    // An empty path cannot refer to a valid file.
    const auto asset = DecalAsset::Load("");
    EXPECT_EQ(asset, nullptr)
        << "Load must return nullptr for an empty file path";
}

// =============================================================================
// LoadFromString — sort_order boundary values
// =============================================================================

TEST(DecalAsset, LoadFromString_SortOrderZero_IsValid) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "sort_order": 0
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 0);
}

TEST(DecalAsset, LoadFromString_LargePositiveSortOrder_IsValid) {
    const std::string json = R"({
        "format": "uzdecal", "version": 1, "material": "m.uzmat",
        "sort_order": 1000
    })";
    const auto asset = DecalAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 1000);
}
