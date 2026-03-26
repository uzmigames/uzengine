/// Unit tests for VFXComposite — multi-emitter composite effect loader.
///
/// Covers: loading a valid .uzvfx_composite JSON string with three entries,
/// verifying names, graph paths, offsets, and delays; error cases for wrong
/// format field, missing emitters array, empty emitters array, entry missing
/// name field, entry missing graph field, and negative delay.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "asset/vfx_composite.h"

#include <memory>
#include <string>

using namespace uz;

namespace {

/// A minimal valid .uzvfx_composite JSON with one emitter.
constexpr const char* kMinimalValid = R"({
  "format": "uzvfx_composite",
  "version": 1,
  "name": "simple",
  "emitters": [
    { "name": "fire", "graph": "vfx/fire.uzvfx", "offset": [0.0, 0.0, 0.0], "delay": 0.0 }
  ]
})";

/// A three-entry composite used by several tests.
constexpr const char* kThreeEntry = R"({
  "format": "uzvfx_composite",
  "version": 2,
  "name": "explosion",
  "emitters": [
    { "name": "fire",   "graph": "vfx/fire.uzvfx",   "offset": [0.0, 0.0, 0.0], "delay": 0.0 },
    { "name": "smoke",  "graph": "vfx/smoke.uzvfx",  "offset": [0.0, 0.5, 0.0], "delay": 0.2 },
    { "name": "debris", "graph": "vfx/debris.uzvfx", "offset": [1.0, 0.0, -1.0], "delay": 0.05 }
  ]
})";

} // anonymous namespace

// =============================================================================
// Load valid composite — basic smoke test
// =============================================================================

/// A valid .uzvfx_composite JSON loads without error and returns non-null.
TEST(VFXComposite, LoadFromString_ValidJson_ReturnsNonNull) {
    auto composite = VFXComposite::LoadFromString(kMinimalValid);
    ASSERT_NE(composite, nullptr);
}

/// Name field is parsed from the JSON root.
TEST(VFXComposite, LoadFromString_ValidJson_NameParsed) {
    auto composite = VFXComposite::LoadFromString(kMinimalValid);
    ASSERT_NE(composite, nullptr);
    EXPECT_EQ(composite->GetName(), "simple");
}

/// Version field is parsed from the JSON root.
TEST(VFXComposite, LoadFromString_ValidJson_VersionParsed) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);
    EXPECT_EQ(composite->GetVersion(), 2u);
}

// =============================================================================
// Three-entry composite — names, graph paths, offsets, delays
// =============================================================================

/// All three entries are present and emitter count matches.
TEST(VFXComposite, LoadFromString_ThreeEntries_CorrectCount) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);
    EXPECT_EQ(composite->GetEmitterCount(), 3u);
    EXPECT_EQ(composite->GetEntries().size(), 3u);
}

/// Entry names are parsed correctly and in order.
TEST(VFXComposite, LoadFromString_ThreeEntries_NamesInOrder) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);

    const auto& entries = composite->GetEntries();
    EXPECT_EQ(entries[0].name, "fire");
    EXPECT_EQ(entries[1].name, "smoke");
    EXPECT_EQ(entries[2].name, "debris");
}

/// Entry graph paths are parsed correctly and in order.
TEST(VFXComposite, LoadFromString_ThreeEntries_GraphPathsInOrder) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);

    const auto& entries = composite->GetEntries();
    EXPECT_EQ(entries[0].graphPath, "vfx/fire.uzvfx");
    EXPECT_EQ(entries[1].graphPath, "vfx/smoke.uzvfx");
    EXPECT_EQ(entries[2].graphPath, "vfx/debris.uzvfx");
}

/// Entry offsets are parsed with correct component values.
TEST(VFXComposite, LoadFromString_ThreeEntries_OffsetsCorrect) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);

    const auto& entries = composite->GetEntries();

    // Entry 0: offset [0, 0, 0]
    EXPECT_FLOAT_EQ(entries[0].offset.x, 0.0f);
    EXPECT_FLOAT_EQ(entries[0].offset.y, 0.0f);
    EXPECT_FLOAT_EQ(entries[0].offset.z, 0.0f);

    // Entry 1: offset [0, 0.5, 0]
    EXPECT_FLOAT_EQ(entries[1].offset.x, 0.0f);
    EXPECT_FLOAT_EQ(entries[1].offset.y, 0.5f);
    EXPECT_FLOAT_EQ(entries[1].offset.z, 0.0f);

    // Entry 2: offset [1.0, 0, -1.0]
    EXPECT_FLOAT_EQ(entries[2].offset.x,  1.0f);
    EXPECT_FLOAT_EQ(entries[2].offset.y,  0.0f);
    EXPECT_FLOAT_EQ(entries[2].offset.z, -1.0f);
}

/// Entry delays are parsed with correct float values.
TEST(VFXComposite, LoadFromString_ThreeEntries_DelaysCorrect) {
    auto composite = VFXComposite::LoadFromString(kThreeEntry);
    ASSERT_NE(composite, nullptr);

    const auto& entries = composite->GetEntries();

    EXPECT_FLOAT_EQ(entries[0].delay, 0.0f);
    EXPECT_FLOAT_EQ(entries[1].delay, 0.2f);
    EXPECT_FLOAT_EQ(entries[2].delay, 0.05f);
}

/// A zero delay entry has delay exactly 0 (not negative, not epsilon).
TEST(VFXComposite, LoadFromString_ZeroDelay_IsExactlyZero) {
    auto composite = VFXComposite::LoadFromString(kMinimalValid);
    ASSERT_NE(composite, nullptr);
    EXPECT_FLOAT_EQ(composite->GetEntries()[0].delay, 0.0f);
}

// =============================================================================
// Absent offset field — defaults to zero vector
// =============================================================================

/// When an entry has no "offset" field, the offset defaults to {0, 0, 0}.
TEST(VFXComposite, LoadFromString_EntryMissingOffset_DefaultsToZero) {
    constexpr const char* kNoOffset = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx" }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNoOffset);
    ASSERT_NE(composite, nullptr);

    const auto& entry = composite->GetEntries()[0];
    EXPECT_FLOAT_EQ(entry.offset.x, 0.0f);
    EXPECT_FLOAT_EQ(entry.offset.y, 0.0f);
    EXPECT_FLOAT_EQ(entry.offset.z, 0.0f);
}

/// When an entry has no "delay" field, the delay defaults to 0.
TEST(VFXComposite, LoadFromString_EntryMissingDelay_DefaultsToZero) {
    constexpr const char* kNoDelay = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx" }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNoDelay);
    ASSERT_NE(composite, nullptr);
    EXPECT_FLOAT_EQ(composite->GetEntries()[0].delay, 0.0f);
}

// =============================================================================
// Error paths — wrong or missing format field
// =============================================================================

/// format field set to an incorrect value returns nullptr.
TEST(VFXComposite, LoadFromString_WrongFormatField_ReturnsNull) {
    constexpr const char* kWrongFormat = R"({
      "format": "uzvfx",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx" }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kWrongFormat);
    EXPECT_EQ(composite, nullptr)
        << "format 'uzvfx' is wrong for composite — must return nullptr";
}

/// Missing format field returns nullptr.
TEST(VFXComposite, LoadFromString_MissingFormatField_ReturnsNull) {
    constexpr const char* kNoFormat = R"({
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx" }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNoFormat);
    EXPECT_EQ(composite, nullptr);
}

// =============================================================================
// Error paths — malformed JSON
// =============================================================================

/// Malformed JSON returns nullptr.
TEST(VFXComposite, LoadFromString_MalformedJson_ReturnsNull) {
    auto composite = VFXComposite::LoadFromString("{ not valid json!");
    EXPECT_EQ(composite, nullptr);
}

/// Empty string returns nullptr.
TEST(VFXComposite, LoadFromString_EmptyString_ReturnsNull) {
    auto composite = VFXComposite::LoadFromString("");
    EXPECT_EQ(composite, nullptr);
}

// =============================================================================
// Error paths — missing emitters array
// =============================================================================

/// Missing "emitters" key returns nullptr.
TEST(VFXComposite, LoadFromString_MissingEmittersKey_ReturnsNull) {
    constexpr const char* kNoEmitters = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test"
    })";

    auto composite = VFXComposite::LoadFromString(kNoEmitters);
    EXPECT_EQ(composite, nullptr)
        << "Missing 'emitters' array must return nullptr";
}

// =============================================================================
// Error paths — empty emitters array
// =============================================================================

/// An empty "emitters" array returns nullptr.
TEST(VFXComposite, LoadFromString_EmptyEmittersArray_ReturnsNull) {
    constexpr const char* kEmptyEmitters = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": []
    })";

    auto composite = VFXComposite::LoadFromString(kEmptyEmitters);
    EXPECT_EQ(composite, nullptr)
        << "Empty 'emitters' array must return nullptr — at least 1 entry required";
}

// =============================================================================
// Error paths — entry missing name field
// =============================================================================

/// An entry without a "name" field causes the entire load to return nullptr.
TEST(VFXComposite, LoadFromString_EntryMissingName_ReturnsNull) {
    constexpr const char* kNoName = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "graph": "vfx/fx.uzvfx", "offset": [0.0, 0.0, 0.0], "delay": 0.0 }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNoName);
    EXPECT_EQ(composite, nullptr)
        << "Entry missing 'name' field must cause load to return nullptr";
}

// =============================================================================
// Error paths — entry missing graph field
// =============================================================================

/// An entry without a "graph" field causes the entire load to return nullptr.
TEST(VFXComposite, LoadFromString_EntryMissingGraph_ReturnsNull) {
    constexpr const char* kNoGraph = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "offset": [0.0, 0.0, 0.0], "delay": 0.0 }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNoGraph);
    EXPECT_EQ(composite, nullptr)
        << "Entry missing 'graph' field must cause load to return nullptr";
}

// =============================================================================
// Error paths — negative delay
// =============================================================================

/// An entry with a negative delay causes the entire load to return nullptr.
TEST(VFXComposite, LoadFromString_EntryNegativeDelay_ReturnsNull) {
    constexpr const char* kNegDelay = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx", "offset": [0.0, 0.0, 0.0], "delay": -0.5 }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kNegDelay);
    EXPECT_EQ(composite, nullptr)
        << "Negative delay must cause load to return nullptr";
}

/// A very small negative delay (near zero) also returns nullptr.
TEST(VFXComposite, LoadFromString_EntrySmallNegativeDelay_ReturnsNull) {
    constexpr const char* kTinyNeg = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "fx", "graph": "vfx/fx.uzvfx", "delay": -0.001 }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kTinyNeg);
    EXPECT_EQ(composite, nullptr);
}

// =============================================================================
// Error paths — valid first entry, invalid second entry
// =============================================================================

/// If the second of two entries is invalid, the entire load returns nullptr.
TEST(VFXComposite, LoadFromString_SecondEntryInvalid_ReturnsNull) {
    constexpr const char* kSecondBad = R"({
      "format": "uzvfx_composite",
      "version": 1,
      "name": "test",
      "emitters": [
        { "name": "good", "graph": "vfx/good.uzvfx" },
        { "graph": "vfx/bad.uzvfx" }
      ]
    })";

    auto composite = VFXComposite::LoadFromString(kSecondBad);
    EXPECT_EQ(composite, nullptr)
        << "One invalid entry (missing 'name') must cause the whole load to fail";
}
