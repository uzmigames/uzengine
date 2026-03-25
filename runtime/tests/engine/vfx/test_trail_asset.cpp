/// Unit tests for TrailAsset.
///
/// Covers: LoadFromString with a fully-specified valid JSON (all fields parsed
/// correctly), LoadFromString with missing/wrong "format", missing/empty material,
/// zero/negative widthStart, negative widthEnd, zero/negative maxPoints,
/// zero/negative pointLifetime, zero/negative minPointDistance, color clamping,
/// optional-field defaults, negative uvScrollRate (accepted), sortOrder parsing,
/// malformed JSON, Load with nonexistent file, and sourcePath population via Load.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_asset.h"

#include <string>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// JSON fixtures
// ─────────────────────────────────────────────────────────────────────────────

/// A fully-specified, valid .uztrail JSON with all optional and required fields.
constexpr const char* kFullJson = R"({
    "format": "uztrail",
    "version": 1,
    "material": "materials/fire_trail.uzmat",
    "width_start": 0.5,
    "width_end": 0.05,
    "color_start": [1.0, 0.8, 0.2, 1.0],
    "color_end": [1.0, 0.2, 0.0, 0.0],
    "uv_scroll_rate": 1.5,
    "max_points": 100,
    "point_lifetime": 2.0,
    "min_point_distance": 0.1,
    "sort_order": 3
})";

/// Minimal valid JSON — only required fields (format, version, material).
constexpr const char* kMinimalJson = R"({
    "format": "uztrail",
    "version": 1,
    "material": "materials/basic.uzmat"
})";

} // anonymous namespace

// =============================================================================
// LoadFromString — fully specified valid JSON
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_FullJson_ReturnsNonNull) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_MaterialPathCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->materialPath, "materials/fire_trail.uzmat");
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_WidthStartCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->widthStart, 0.5f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_WidthEndCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->widthEnd, 0.05f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_ColorStartCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->colorStart.x, 1.0f, 1e-5f);
    EXPECT_NEAR(asset->colorStart.y, 0.8f, 1e-5f);
    EXPECT_NEAR(asset->colorStart.z, 0.2f, 1e-5f);
    EXPECT_NEAR(asset->colorStart.w, 1.0f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_ColorEndCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->colorEnd.x, 1.0f, 1e-5f);
    EXPECT_NEAR(asset->colorEnd.y, 0.2f, 1e-5f);
    EXPECT_NEAR(asset->colorEnd.z, 0.0f, 1e-5f);
    EXPECT_NEAR(asset->colorEnd.w, 0.0f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_UvScrollRateCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->uvScrollRate, 1.5f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_MaxPointsCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->maxPoints, 100u);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_PointLifetimeCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->pointLifetime, 2.0f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_MinPointDistanceCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->minPointDistance, 0.1f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_SortOrderCorrect) {
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 3);
}

TEST(UZ_TrailAsset, LoadFromString_FullJson_SourcePathIsEmpty) {
    // LoadFromString has no file path — sourcePath must be empty.
    auto asset = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->sourcePath.empty())
        << "sourcePath must be empty when loaded from a string, not a file";
}

// =============================================================================
// LoadFromString — missing or wrong "format"
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MissingFormat_ReturnsNullptr) {
    constexpr const char* json = R"({
        "version": 1,
        "material": "materials/x.uzmat"
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_WrongFormatValue_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uzdecal",
        "version": 1,
        "material": "materials/x.uzmat"
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_FormatIsInteger_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": 42,
        "version": 1,
        "material": "materials/x.uzmat"
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — missing "version"
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MissingVersion_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "material": "materials/x.uzmat"
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — missing or empty "material"
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MissingMaterial_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_EmptyMaterial_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": ""
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_MaterialIsNumber_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": 12345
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — widthStart validation
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_WidthStartZero_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "width_start": 0.0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_WidthStartNegative_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "width_start": -1.0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — widthEnd validation (>= 0 required)
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_WidthEndZero_IsAccepted) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "width_start": 0.5,
        "width_end": 0.0
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr) << "widthEnd == 0 must be accepted (trail tapers to a point)";
    EXPECT_NEAR(asset->widthEnd, 0.0f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_WidthEndNegative_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "width_start": 0.5,
        "width_end": -0.1
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — maxPoints validation
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MaxPointsZero_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "max_points": 0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_MaxPointsNegative_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "max_points": -50
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — pointLifetime validation
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_PointLifetimeZero_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "point_lifetime": 0.0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_PointLifetimeNegative_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "point_lifetime": -1.0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — minPointDistance validation
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MinPointDistanceZero_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "min_point_distance": 0.0
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_MinPointDistanceNegative_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "min_point_distance": -0.5
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — color component clamping to [0, 1]
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_ColorStartAboveOne_ClampedToOne) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "color_start": [2.0, -0.5, 1.0, 1.5]
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->colorStart.x, 1.0f, 1e-5f) << "2.0 must clamp to 1.0";
    EXPECT_NEAR(asset->colorStart.y, 0.0f, 1e-5f) << "-0.5 must clamp to 0.0";
    EXPECT_NEAR(asset->colorStart.z, 1.0f, 1e-5f);
    EXPECT_NEAR(asset->colorStart.w, 1.0f, 1e-5f) << "1.5 must clamp to 1.0";
}

TEST(UZ_TrailAsset, LoadFromString_ColorEndBelowZero_ClampedToZero) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "color_end": [-1.0, 0.5, 0.5, -0.1]
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->colorEnd.x, 0.0f, 1e-5f) << "-1.0 must clamp to 0.0";
    EXPECT_NEAR(asset->colorEnd.y, 0.5f, 1e-5f);
    EXPECT_NEAR(asset->colorEnd.w, 0.0f, 1e-5f) << "-0.1 must clamp to 0.0";
}

TEST(UZ_TrailAsset, LoadFromString_ColorStartWrongElementCount_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "color_start": [1.0, 0.0, 1.0]
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr)
        << "color_start with 3 elements (not 4) must fail validation";
}

TEST(UZ_TrailAsset, LoadFromString_ColorEndNotArray_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "color_end": "red"
    })";
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — optional field defaults
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MinimalJson_UsesDefaults) {
    auto asset = TrailAsset::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);

    EXPECT_NEAR(asset->widthStart,       0.5f,  1e-5f);
    EXPECT_NEAR(asset->widthEnd,         0.05f, 1e-5f);
    EXPECT_NEAR(asset->uvScrollRate,     0.0f,  1e-5f);
    EXPECT_EQ  (asset->maxPoints,        100u);
    EXPECT_NEAR(asset->pointLifetime,    2.0f,  1e-5f);
    EXPECT_NEAR(asset->minPointDistance, 0.1f,  1e-5f);
    EXPECT_EQ  (asset->sortOrder,        0);

    // Default color_start = [1,1,1,1]
    EXPECT_NEAR(asset->colorStart.x, 1.0f, 1e-5f);
    EXPECT_NEAR(asset->colorStart.w, 1.0f, 1e-5f);

    // Default color_end = [1,1,1,0]
    EXPECT_NEAR(asset->colorEnd.x, 1.0f, 1e-5f);
    EXPECT_NEAR(asset->colorEnd.w, 0.0f, 1e-5f);
}

// =============================================================================
// LoadFromString — uvScrollRate accepts negative values
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_NegativeUvScrollRate_IsAccepted) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "uv_scroll_rate": -2.5
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr)
        << "Negative uvScrollRate must be accepted (reverse scroll direction)";
    EXPECT_NEAR(asset->uvScrollRate, -2.5f, 1e-5f);
}

TEST(UZ_TrailAsset, LoadFromString_ZeroUvScrollRate_IsAccepted) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "uv_scroll_rate": 0.0
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->uvScrollRate, 0.0f, 1e-5f);
}

// =============================================================================
// LoadFromString — sortOrder parsing (positive, negative, zero)
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_SortOrderPositive_ParsedCorrectly) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "sort_order": 10
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, 10);
}

TEST(UZ_TrailAsset, LoadFromString_SortOrderNegative_ParsedCorrectly) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "sort_order": -5
    })";
    auto asset = TrailAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sortOrder, -5);
}

TEST(UZ_TrailAsset, LoadFromString_SortOrderIsFloat_ReturnsNullptr) {
    constexpr const char* json = R"({
        "format": "uztrail",
        "version": 1,
        "material": "m.uzmat",
        "sort_order": 1.5
    })";
    // sort_order must be an integer; a float value must be rejected.
    EXPECT_EQ(TrailAsset::LoadFromString(json), nullptr);
}

// =============================================================================
// LoadFromString — malformed JSON
// =============================================================================

TEST(UZ_TrailAsset, LoadFromString_MalformedJson_ReturnsNullptr) {
    EXPECT_EQ(TrailAsset::LoadFromString("{bad json"), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_EmptyString_ReturnsNullptr) {
    EXPECT_EQ(TrailAsset::LoadFromString(""), nullptr);
}

TEST(UZ_TrailAsset, LoadFromString_NotAnObject_ReturnsNullptr) {
    EXPECT_EQ(TrailAsset::LoadFromString("[1, 2, 3]"), nullptr);
}

// =============================================================================
// Load — nonexistent file returns nullptr
// =============================================================================

TEST(UZ_TrailAsset, Load_NonexistentFile_ReturnsNullptr) {
    auto asset = TrailAsset::Load("__nonexistent_trail_file_xyz123__.uztrail");
    EXPECT_EQ(asset, nullptr);
}

// =============================================================================
// Move semantics — TrailAsset is movable
// =============================================================================

TEST(UZ_TrailAsset, MoveConstruct_TransfersData) {
    auto src = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(src, nullptr);
    const std::string matPath = src->materialPath;

    TrailAsset moved(std::move(*src));
    EXPECT_EQ(moved.materialPath, matPath);
}

TEST(UZ_TrailAsset, MoveAssign_TransfersData) {
    auto src = TrailAsset::LoadFromString(kFullJson);
    ASSERT_NE(src, nullptr);
    const float ws = src->widthStart;

    TrailAsset dst;
    dst = std::move(*src);
    EXPECT_NEAR(dst.widthStart, ws, 1e-5f);
}
