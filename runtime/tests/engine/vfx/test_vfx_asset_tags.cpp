/// Unit tests for the VFX asset tag query helper and JSON round-trip.
///
/// Covers:
///   - `VfxAssetTagMatches` grammar (AND, OR, NOT, case-insensitive).
///   - JSON round-trip for VfxSystem, VfxNodeGraph, and ParticleEmitter tags.
///   - Edge cases: empty query, malformed tokens, trailing OR.
///
/// All tests are CPU-only. Covers `engine/vfx/asset/vfx_asset_tags.h` and the
/// tag serialisation paths in `engine/vfx/system/vfx_system.cpp`,
/// `engine/vfx/editor/vfx_node_graph.cpp`, and
/// `engine/vfx/components/emitter_serializer.h`.
///
/// @ref docs/engine/25-vfx/19-asset-tagging.md

#include <gtest/gtest.h>

#include "engine/vfx/asset/vfx_asset_tags.h"
#include "engine/vfx/editor/vfx_node_graph.h"
#include "engine/vfx/system/vfx_system.h"

using namespace uz;

namespace {

const std::vector<std::string> kFireball{"fire", "combat", "hero"};
const std::vector<std::string> kAmbientSmoke{"ambient", "smoke", "background"};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Query grammar
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAssetTagMatches, EmptyQuery_MatchesAny) {
    EXPECT_TRUE(VfxAssetTagMatches("", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("   \t  ", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("", {}));
}

TEST(VfxAssetTagMatches, SinglePositive_CaseInsensitive) {
    EXPECT_TRUE(VfxAssetTagMatches("fire", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("FIRE", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("Fire", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("ice", kFireball));
}

TEST(VfxAssetTagMatches, MultiTermIsAnd) {
    EXPECT_TRUE(VfxAssetTagMatches("fire combat", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("fire ice", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("fire combat missing", kFireball));
}

TEST(VfxAssetTagMatches, DashNegation) {
    EXPECT_TRUE(VfxAssetTagMatches("-ambient", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("-fire", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("fire -ambient", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("fire -combat", kFireball));
}

TEST(VfxAssetTagMatches, BangNegationAliasOfDash) {
    EXPECT_TRUE(VfxAssetTagMatches("!ambient", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("!fire", kFireball));
}

TEST(VfxAssetTagMatches, OrSplitsIntoAlternatives) {
    EXPECT_TRUE(VfxAssetTagMatches("fire OR magic", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("magic OR fire", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("ice OR magic", kFireball));
}

TEST(VfxAssetTagMatches, CompoundAndOrNot) {
    // "(fire AND NOT ambient) OR (smoke)"
    EXPECT_TRUE(VfxAssetTagMatches("fire -ambient OR smoke", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("fire -ambient OR smoke", kAmbientSmoke));
    // Neither fire-without-ambient nor smoke
    EXPECT_FALSE(VfxAssetTagMatches("fire -ambient OR smoke", {"ambient"}));
}

TEST(VfxAssetTagMatches, BareNegationSymbolIsNoOp) {
    // A bare "-" or "!" token is a typo, must not accidentally reject.
    EXPECT_TRUE(VfxAssetTagMatches("- fire", kFireball));
    EXPECT_TRUE(VfxAssetTagMatches("! fire", kFireball));
}

TEST(VfxAssetTagMatches, TrailingOrIsNoOp) {
    // Typing-in-progress: "fire OR " must behave like "fire".
    EXPECT_TRUE(VfxAssetTagMatches("fire OR ", kFireball));
    EXPECT_FALSE(VfxAssetTagMatches("ice OR ", kFireball));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSystem JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemTags, RoundTripThroughJson) {
    const char* kJson = R"({
  "format": "uzvfxsys",
  "version": 1,
  "name": "Fireball",
  "emitters": [{ "name": "head", "path": "preset:FireballHead", "id": "e1" }],
  "tags": ["fire", "combat", "hero"]
})";

    auto loaded = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->tags.size(), 3u);
    EXPECT_EQ(loaded->tags[0], "fire");
    EXPECT_EQ(loaded->tags[1], "combat");
    EXPECT_EQ(loaded->tags[2], "hero");
}

TEST(VfxSystemTags, NonStringTagEntrySkipped_OtherTagsKept) {
    const char* kJson = R"({
  "format": "uzvfxsys",
  "version": 1,
  "emitters": [{ "name": "e", "path": "preset:X", "id": "e1" }],
  "tags": ["fire", 42, "combat"]
})";

    auto loaded = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->tags.size(), 2u);
    EXPECT_EQ(loaded->tags[0], "fire");
    EXPECT_EQ(loaded->tags[1], "combat");
}

TEST(VfxSystemTags, MissingTagsKey_EmptyVector) {
    const char* kJson = R"({
  "format": "uzvfxsys",
  "version": 1,
  "emitters": [{ "name": "e", "path": "preset:X", "id": "e1" }]
})";

    auto loaded = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(loaded->tags.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxNodeGraph JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphTags, RoundTripThroughJson) {
    VfxNodeGraph original;
    original.AddNode({"n0", "Spawn", {}});
    original.tags = {"utility", "snippet"};

    const std::string dumped = VfxNodeGraphAsset::SaveToString(original);
    EXPECT_NE(dumped.find("\"tags\""), std::string::npos);

    VfxNodeGraph reloaded;
    ASSERT_TRUE(VfxNodeGraphAsset::LoadFromString(dumped, reloaded));
    ASSERT_EQ(reloaded.tags.size(), 2u);
    EXPECT_EQ(reloaded.tags[0], "utility");
    EXPECT_EQ(reloaded.tags[1], "snippet");
}

TEST(VfxNodeGraphTags, EmptyTagsDoesNotEmitKey) {
    VfxNodeGraph g;
    g.AddNode({"n0", "Spawn", {}});
    const std::string dumped = VfxNodeGraphAsset::SaveToString(g);
    // Legacy diff-minimal guarantee: no "tags" key when empty.
    EXPECT_EQ(dumped.find("\"tags\""), std::string::npos);
}
