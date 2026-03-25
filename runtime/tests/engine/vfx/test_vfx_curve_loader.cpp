/// Unit tests for VFXCurveLoader -- JSON deserialization of .uzcurve files.
///
/// Covers: valid float and vec4 JSON, malformed JSON, unsorted keys, out-of-range
/// time values, and the minimum-2-keys enforcement. All tests are CPU-only and
/// require no file I/O (LoadFromJson is used exclusively).

#include <gtest/gtest.h>

#include "curves/vfx_curve_loader.h"
#include "curves/vfx_curve.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Load valid float curve from JSON
// ─────────────────────────────────────────────────────────────────────────────

/// Well-formed JSON with 3 float keyframes. Verify the curve is returned,
/// has the correct key count, type, and samples correctly.
TEST(VFXCurveLoader, LoadValidFloatCurve_ThreeKeyframes_LoadsAndSamplesCorrectly) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": 0.0, "value": 0.0 },
            { "time": 0.5, "value": 1.0 },
            { "time": 1.0, "value": 0.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    ASSERT_TRUE(result.has_value())
        << "Valid float curve JSON must parse successfully";

    const VFXCurve& curve = *result;
    EXPECT_EQ(curve.GetType(), CurveType::Float)
        << "Parsed curve type must be Float";
    EXPECT_EQ(curve.GetInterpolation(), InterpolationMode::Linear)
        << "Parsed interpolation must be Linear";
    EXPECT_EQ(curve.GetKeyCount(), 3u)
        << "Parsed curve must have exactly 3 keys";
    EXPECT_FALSE(curve.IsEmpty())
        << "Parsed curve must not be empty";

    // Verify sampling returns values consistent with the keys.
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.0f), 0.0f)
        << "Sample at t=0 must equal first key value 0.0";
    EXPECT_FLOAT_EQ(curve.SampleFloat(0.5f), 1.0f)
        << "Sample at t=0.5 must equal second key value 1.0";
    EXPECT_FLOAT_EQ(curve.SampleFloat(1.0f), 0.0f)
        << "Sample at t=1.0 must equal third key value 0.0";
    EXPECT_NEAR(curve.SampleFloat(0.25f), 0.5f, 1e-5f)
        << "Sample at t=0.25 must be linear midpoint 0.5";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Load valid Vec4 curve from JSON
// ─────────────────────────────────────────────────────────────────────────────

/// Well-formed JSON with vec4 values. Verify curve type is Vec4 and all
/// four channels sample correctly at key boundaries.
TEST(VFXCurveLoader, LoadValidVec4Curve_TwoKeyframes_AllComponentsCorrect) {
    const std::string json = R"({
        "type": "vec4",
        "interpolation": "linear",
        "keys": [
            { "time": 0.0, "value": [0.0, 0.0, 0.0, 0.0] },
            { "time": 1.0, "value": [1.0, 2.0, 3.0, 4.0] }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    ASSERT_TRUE(result.has_value())
        << "Valid vec4 curve JSON must parse successfully";

    const VFXCurve& curve = *result;
    EXPECT_EQ(curve.GetType(), CurveType::Vec4)
        << "Parsed curve type must be Vec4";
    EXPECT_EQ(curve.GetKeyCount(), 2u)
        << "Parsed curve must have exactly 2 keys";

    // At t=0: first key exactly.
    const Vec4 atZero = curve.SampleVec4(0.0f);
    EXPECT_FLOAT_EQ(atZero.x, 0.0f) << "x at t=0 must be 0.0";
    EXPECT_FLOAT_EQ(atZero.y, 0.0f) << "y at t=0 must be 0.0";
    EXPECT_FLOAT_EQ(atZero.z, 0.0f) << "z at t=0 must be 0.0";
    EXPECT_FLOAT_EQ(atZero.w, 0.0f) << "w at t=0 must be 0.0";

    // At t=1.0: last key exactly.
    const Vec4 atOne = curve.SampleVec4(1.0f);
    EXPECT_FLOAT_EQ(atOne.x, 1.0f) << "x at t=1.0 must be 1.0";
    EXPECT_FLOAT_EQ(atOne.y, 2.0f) << "y at t=1.0 must be 2.0";
    EXPECT_FLOAT_EQ(atOne.z, 3.0f) << "z at t=1.0 must be 3.0";
    EXPECT_FLOAT_EQ(atOne.w, 4.0f) << "w at t=1.0 must be 4.0";

    // At t=0.5: linear midpoint for each component.
    const Vec4 atHalf = curve.SampleVec4(0.5f);
    EXPECT_NEAR(atHalf.x, 0.5f, 1e-5f) << "x at t=0.5 must be 0.5";
    EXPECT_NEAR(atHalf.y, 1.0f, 1e-5f) << "y at t=0.5 must be 1.0";
    EXPECT_NEAR(atHalf.z, 1.5f, 1e-5f) << "z at t=0.5 must be 1.5";
    EXPECT_NEAR(atHalf.w, 2.0f, 1e-5f) << "w at t=0.5 must be 2.0";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Malformed JSON returns nullopt
// ─────────────────────────────────────────────────────────────────────────────

/// Completely invalid JSON syntax must yield nullopt without crashing.
TEST(VFXCurveLoader, LoadInvalidJson_MalformedSyntax_ReturnsNullopt) {
    const std::string json = "{ this is not valid json at all !!!";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "Malformed JSON must return nullopt";
}

/// Empty string is also invalid JSON.
TEST(VFXCurveLoader, LoadInvalidJson_EmptyString_ReturnsNullopt) {
    const auto result = VFXCurveLoader::LoadFromJson("");

    EXPECT_FALSE(result.has_value())
        << "Empty string must return nullopt";
}

/// JSON with missing required 'keys' field must fail validation.
TEST(VFXCurveLoader, LoadInvalidJson_MissingKeysField_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear"
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "JSON missing 'keys' array must return nullopt";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Unsorted keys return nullopt
// ─────────────────────────────────────────────────────────────────────────────

/// Keys not sorted by ascending time violate the contract and must return nullopt.
TEST(VFXCurveLoader, LoadUnsortedKeys_NotAscendingTime_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": 1.0, "value": 1.0 },
            { "time": 0.0, "value": 0.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "Unsorted keys (time descending) must return nullopt";
}

/// Duplicate time values also constitute an unsorted / invalid sequence.
TEST(VFXCurveLoader, LoadUnsortedKeys_DuplicateTimes_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": 0.5, "value": 0.5 },
            { "time": 0.5, "value": 1.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "Duplicate time values (not strictly ascending) must return nullopt";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: Out-of-range time keys return nullopt
// ─────────────────────────────────────────────────────────────────────────────

/// Keys with time values outside [0, 1] violate the contract.
TEST(VFXCurveLoader, LoadOutOfRangeKeys_TimeNegative_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": -0.1, "value": 0.0 },
            { "time":  1.0, "value": 1.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "Key with negative time must return nullopt";
}

TEST(VFXCurveLoader, LoadOutOfRangeKeys_TimeAboveOne_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": 0.0, "value": 0.0 },
            { "time": 1.5, "value": 1.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "Key with time > 1.0 must return nullopt";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: Too few keys (< 2) returns nullopt
// ─────────────────────────────────────────────────────────────────────────────

/// JSON with a single key violates the minimum-2-key requirement.
TEST(VFXCurveLoader, LoadTooFewKeys_SingleKey_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": [
            { "time": 0.5, "value": 1.0 }
        ]
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "JSON with only 1 key must return nullopt (minimum is 2)";
}

/// JSON with an empty keys array also fails.
TEST(VFXCurveLoader, LoadTooFewKeys_EmptyArray_ReturnsNullopt) {
    const std::string json = R"({
        "type": "float",
        "interpolation": "linear",
        "keys": []
    })";

    const auto result = VFXCurveLoader::LoadFromJson(json);

    EXPECT_FALSE(result.has_value())
        << "JSON with empty keys array must return nullopt";
}
