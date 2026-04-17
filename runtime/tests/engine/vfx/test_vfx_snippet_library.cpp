/// Unit tests for phase 8.8.0 — VFX snippet library index + filter.
///
/// Uses `AddEntry` to populate the index without a real filesystem so
/// tests run on any machine without a content directory.
///
/// @ref engine/vfx/editor/vfx_snippet_library.h
/// @ref docs/engine/25-vfx/25-snippet-library.md

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_snippet_library.h"

using namespace uz;

namespace {

VfxSnippetEntry MakeEntry(std::string path, std::string name,
                          std::vector<std::string> tags,
                          std::string fmt = "uzvfxsys") {
    VfxSnippetEntry e;
    e.relativePath  = std::move(path);
    e.displayName   = std::move(name);
    e.tags          = std::move(tags);
    e.format        = std::move(fmt);
    e.thumbnailPath = VfxThumbnailPathForAsset(e.relativePath);
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Empty index
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, DefaultEmpty) {
    VfxSnippetIndex idx;
    EXPECT_EQ(idx.Count(), 0u);
    EXPECT_TRUE(idx.Entries().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// AddEntry + deterministic sort
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, AddEntry_SortedByRelativePath) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("z_spark.uzvfxsys", "Spark",    {"combat"}));
    idx.AddEntry(MakeEntry("a_fire.uzvfxsys",  "Fireball", {"fire"}));
    idx.AddEntry(MakeEntry("m_smoke.uzfx",      "Smoke",    {"ambient"}));

    ASSERT_EQ(idx.Count(), 3u);
    EXPECT_EQ(idx.Entries()[0].relativePath, "a_fire.uzvfxsys");
    EXPECT_EQ(idx.Entries()[1].relativePath, "m_smoke.uzfx");
    EXPECT_EQ(idx.Entries()[2].relativePath, "z_spark.uzvfxsys");
}

// ─────────────────────────────────────────────────────────────────────────────
// Thumbnail path derivation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, ThumbnailPathDerived) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("vfx/fire.uzvfxsys", "Fire", {}));
    EXPECT_EQ(idx.Entries()[0].thumbnailPath, "vfx/fire.thumb.png");
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter — delegates to VfxAssetTagMatches (8.2.0)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, Filter_EmptyQuery_ReturnsAll) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {"fire"}));
    idx.AddEntry(MakeEntry("b.uzfx",     "B", {"smoke"}));

    auto results = idx.Filter("");
    EXPECT_EQ(results.size(), 2u);
}

TEST(VfxSnippetIndex, Filter_SingleTag_MatchesCorrectEntries) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("fire.uzvfxsys",  "Fire",  {"fire", "combat"}));
    idx.AddEntry(MakeEntry("smoke.uzfx",      "Smoke", {"ambient", "smoke"}));
    idx.AddEntry(MakeEntry("spark.uzvfxsys",  "Spark", {"fire", "hero"}));

    auto results = idx.Filter("fire");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]->displayName, "Fire");
    EXPECT_EQ(results[1]->displayName, "Spark");
}

TEST(VfxSnippetIndex, Filter_AndComposition) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {"fire", "combat"}));
    idx.AddEntry(MakeEntry("b.uzvfxsys", "B", {"fire"}));

    auto results = idx.Filter("fire combat");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->displayName, "A");
}

TEST(VfxSnippetIndex, Filter_Negation) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {"fire", "ambient"}));
    idx.AddEntry(MakeEntry("b.uzvfxsys", "B", {"fire"}));

    auto results = idx.Filter("fire -ambient");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->displayName, "B");
}

TEST(VfxSnippetIndex, Filter_OrComposition) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {"fire"}));
    idx.AddEntry(MakeEntry("b.uzvfxsys", "B", {"ice"}));
    idx.AddEntry(MakeEntry("c.uzvfxsys", "C", {"smoke"}));

    auto results = idx.Filter("fire OR ice");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]->displayName, "A");
    EXPECT_EQ(results[1]->displayName, "B");
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, Clear_DropsAllEntries) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {}));
    EXPECT_EQ(idx.Count(), 1u);
    idx.Clear();
    EXPECT_EQ(idx.Count(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Format field
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnippetIndex, FormatPreserved) {
    VfxSnippetIndex idx;
    idx.AddEntry(MakeEntry("a.uzvfxsys", "A", {}, "uzvfxsys"));
    idx.AddEntry(MakeEntry("b.uzfx",     "B", {}, "uzfx"));
    EXPECT_EQ(idx.Entries()[0].format, "uzvfxsys");
    EXPECT_EQ(idx.Entries()[1].format, "uzfx");
}
