/// Unit tests for DecalAtlas.
///
/// Covers: construction (atlasSize), AddEntry (valid UV coordinates in [0,1],
/// uvMin < uvMax), non-overlapping entries, atlas overflow (nullopt), HasSpace,
/// GetEntryCount/GetUsedArea/GetOccupancy accuracy, Reset, small atlas with many
/// entries, and entries larger than the atlas.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/decals/decal_atlas.h"

#include <cstdint>
#include <optional>
#include <vector>

using namespace uz;

namespace {

/// Checks whether two UV rectangles [aMin, aMax) and [bMin, bMax) overlap.
bool RectanglesOverlap(const Vec2& aMin, const Vec2& aMax,
                       const Vec2& bMin, const Vec2& bMax) {
    // Axis-aligned rectangle intersection test.
    if (aMax.x <= bMin.x || bMax.x <= aMin.x) return false;
    if (aMax.y <= bMin.y || bMax.y <= aMin.y) return false;
    return true;
}

} // anonymous namespace

// =============================================================================
// Construction
// =============================================================================

TEST(DecalAtlas, Constructor_AtlasSize2048_GetAtlasSizeMatches) {
    DecalAtlas atlas(2048);
    EXPECT_EQ(atlas.GetAtlasSize(), 2048u);
}

TEST(DecalAtlas, Constructor_AtlasSize512_GetAtlasSizeMatches) {
    DecalAtlas atlas(512);
    EXPECT_EQ(atlas.GetAtlasSize(), 512u);
}

TEST(DecalAtlas, Constructor_InitialCounts_AreZero) {
    DecalAtlas atlas(2048);
    EXPECT_EQ(atlas.GetEntryCount(), 0u);
    EXPECT_EQ(atlas.GetUsedArea(), 0u);
    EXPECT_FLOAT_EQ(atlas.GetOccupancy(), 0.0f);
}

// =============================================================================
// AddEntry — valid UV range [0, 1]
// =============================================================================

TEST(DecalAtlas, AddEntry_256x256_ReturnsValidEntry) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 256);
    ASSERT_TRUE(entry.has_value())
        << "AddEntry must succeed for 256x256 in a 2048-atlas";
}

TEST(DecalAtlas, AddEntry_UvMin_IsInZeroOneRange) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 256);
    ASSERT_TRUE(entry.has_value());
    EXPECT_GE(entry->uvMin.x, 0.0f);
    EXPECT_GE(entry->uvMin.y, 0.0f);
    EXPECT_LT(entry->uvMin.x, 1.0f);
    EXPECT_LT(entry->uvMin.y, 1.0f);
}

TEST(DecalAtlas, AddEntry_UvMax_IsInZeroOneRange) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 256);
    ASSERT_TRUE(entry.has_value());
    EXPECT_GT(entry->uvMax.x, 0.0f);
    EXPECT_GT(entry->uvMax.y, 0.0f);
    EXPECT_LE(entry->uvMax.x, 1.0f);
    EXPECT_LE(entry->uvMax.y, 1.0f);
}

TEST(DecalAtlas, AddEntry_UvMin_IsLessThanUvMax) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 256);
    ASSERT_TRUE(entry.has_value());
    EXPECT_LT(entry->uvMin.x, entry->uvMax.x)
        << "uvMin.x must be strictly less than uvMax.x";
    EXPECT_LT(entry->uvMin.y, entry->uvMax.y)
        << "uvMin.y must be strictly less than uvMax.y";
}

TEST(DecalAtlas, AddEntry_PreservesPixelDimensions) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(128, 64);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->width, 128u);
    EXPECT_EQ(entry->height, 64u);
}

TEST(DecalAtlas, AddEntry_SmallEntry_UvSizeMatchesDimensions) {
    // For a 2048-atlas, a 256x256 entry should have UV size = 256/2048 = 0.125
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 256);
    ASSERT_TRUE(entry.has_value());
    const float expectedSize = 256.0f / 2048.0f;
    EXPECT_NEAR(entry->uvMax.x - entry->uvMin.x, expectedSize, 1e-5f);
    EXPECT_NEAR(entry->uvMax.y - entry->uvMin.y, expectedSize, 1e-5f);
}

// =============================================================================
// AddEntry — non-overlapping entries
// =============================================================================

TEST(DecalAtlas, AddEntry_TwoEntries_DoNotOverlap) {
    DecalAtlas atlas(2048);
    const auto e1 = atlas.AddEntry(256, 256);
    const auto e2 = atlas.AddEntry(256, 256);
    ASSERT_TRUE(e1.has_value());
    ASSERT_TRUE(e2.has_value());
    EXPECT_FALSE(RectanglesOverlap(e1->uvMin, e1->uvMax, e2->uvMin, e2->uvMax))
        << "Two separately-allocated entries must not overlap in UV space";
}

TEST(DecalAtlas, AddEntry_TenEntries_NoPairOverlaps) {
    DecalAtlas atlas(2048);
    std::vector<AtlasEntry> entries;
    for (int i = 0; i < 10; ++i) {
        const auto e = atlas.AddEntry(128, 128);
        ASSERT_TRUE(e.has_value()) << "Entry " << i << " must succeed";
        entries.push_back(*e);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            EXPECT_FALSE(RectanglesOverlap(entries[i].uvMin, entries[i].uvMax,
                                           entries[j].uvMin, entries[j].uvMax))
                << "Entries " << i << " and " << j << " must not overlap";
        }
    }
}

TEST(DecalAtlas, AddEntry_MixedSizes_NoOverlaps) {
    DecalAtlas atlas(1024);
    const uint32_t sizes[][2] = {
        {64, 64}, {128, 32}, {32, 256}, {256, 64}, {64, 128}
    };
    std::vector<AtlasEntry> entries;
    for (const auto& s : sizes) {
        const auto e = atlas.AddEntry(s[0], s[1]);
        ASSERT_TRUE(e.has_value());
        entries.push_back(*e);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            EXPECT_FALSE(RectanglesOverlap(entries[i].uvMin, entries[i].uvMax,
                                           entries[j].uvMin, entries[j].uvMax))
                << "Mixed-size entries " << i << " and " << j << " must not overlap";
        }
    }
}

// =============================================================================
// AddEntry — atlas overflow
// =============================================================================

TEST(DecalAtlas, AddEntry_EntryLargerThanAtlas_ReturnsNullopt) {
    DecalAtlas atlas(256);
    const auto entry = atlas.AddEntry(512, 512);
    EXPECT_FALSE(entry.has_value())
        << "Entry larger than atlas must return nullopt";
}

TEST(DecalAtlas, AddEntry_WidthLargerThanAtlas_ReturnsNullopt) {
    DecalAtlas atlas(256);
    const auto entry = atlas.AddEntry(512, 64);
    EXPECT_FALSE(entry.has_value());
}

TEST(DecalAtlas, AddEntry_HeightLargerThanAtlas_ReturnsNullopt) {
    DecalAtlas atlas(256);
    const auto entry = atlas.AddEntry(64, 512);
    EXPECT_FALSE(entry.has_value());
}

TEST(DecalAtlas, AddEntry_ZeroWidth_ReturnsNullopt) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(0, 256);
    EXPECT_FALSE(entry.has_value())
        << "Zero-width entry must return nullopt";
}

TEST(DecalAtlas, AddEntry_ZeroHeight_ReturnsNullopt) {
    DecalAtlas atlas(2048);
    const auto entry = atlas.AddEntry(256, 0);
    EXPECT_FALSE(entry.has_value())
        << "Zero-height entry must return nullopt";
}

TEST(DecalAtlas, AddEntry_FullAtlas_ReturnsNullopt) {
    // Fill a small atlas completely with one maximum-sized entry.
    DecalAtlas atlas(256);
    const auto first = atlas.AddEntry(256, 256);
    ASSERT_TRUE(first.has_value()) << "First full-size entry must succeed";

    // Any further entry must fail.
    const auto second = atlas.AddEntry(1, 1);
    EXPECT_FALSE(second.has_value())
        << "Any further entry after full atlas must return nullopt";
}

TEST(DecalAtlas, AddEntry_ProgressivelyFillAtlas_FinalEntryFails) {
    // Fill a 128-atlas entirely with 64x64 tiles (4 tiles = 128*128 pixels).
    // Then adding one more must fail.
    DecalAtlas atlas(128);

    // 4 tiles of 64x64: first row (2 tiles), second row (2 tiles).
    for (int i = 0; i < 4; ++i) {
        const auto e = atlas.AddEntry(64, 64);
        ASSERT_TRUE(e.has_value()) << "Tile " << i << " must succeed";
    }

    const auto overflow = atlas.AddEntry(64, 64);
    EXPECT_FALSE(overflow.has_value())
        << "Fifth 64x64 tile must not fit in a 128x128 atlas";
}

// =============================================================================
// HasSpace
// =============================================================================

TEST(DecalAtlas, HasSpace_EmptyAtlas_ReturnsTrueForSmallEntry) {
    DecalAtlas atlas(2048);
    EXPECT_TRUE(atlas.HasSpace(256, 256));
}

TEST(DecalAtlas, HasSpace_EmptyAtlas_ReturnsFalseForOversize) {
    DecalAtlas atlas(256);
    EXPECT_FALSE(atlas.HasSpace(512, 512));
}

TEST(DecalAtlas, HasSpace_FullAtlas_ReturnsFalseForAnySize) {
    DecalAtlas atlas(64);
    ASSERT_TRUE(atlas.AddEntry(64, 64).has_value());
    EXPECT_FALSE(atlas.HasSpace(1, 1))
        << "HasSpace must return false when atlas is full";
}

TEST(DecalAtlas, HasSpace_DoesNotModifyEntryCount) {
    DecalAtlas atlas(2048);
    const uint32_t countBefore = atlas.GetEntryCount();
    atlas.HasSpace(256, 256);
    EXPECT_EQ(atlas.GetEntryCount(), countBefore)
        << "HasSpace must not modify the atlas state";
}

TEST(DecalAtlas, HasSpace_ZeroWidth_ReturnsFalse) {
    DecalAtlas atlas(2048);
    EXPECT_FALSE(atlas.HasSpace(0, 256));
}

TEST(DecalAtlas, HasSpace_ZeroHeight_ReturnsFalse) {
    DecalAtlas atlas(2048);
    EXPECT_FALSE(atlas.HasSpace(256, 0));
}

// =============================================================================
// GetEntryCount / GetUsedArea / GetOccupancy
// =============================================================================

TEST(DecalAtlas, GetEntryCount_IncrementsOnEachSuccessfulAdd) {
    DecalAtlas atlas(2048);
    EXPECT_EQ(atlas.GetEntryCount(), 0u);
    atlas.AddEntry(128, 128);
    EXPECT_EQ(atlas.GetEntryCount(), 1u);
    atlas.AddEntry(64, 64);
    EXPECT_EQ(atlas.GetEntryCount(), 2u);
}

TEST(DecalAtlas, GetEntryCount_DoesNotIncrementOnFailedAdd) {
    DecalAtlas atlas(64);
    ASSERT_TRUE(atlas.AddEntry(64, 64).has_value());
    EXPECT_EQ(atlas.GetEntryCount(), 1u);
    atlas.AddEntry(64, 64); // fails
    EXPECT_EQ(atlas.GetEntryCount(), 1u);
}

TEST(DecalAtlas, GetUsedArea_MatchesSumOfEntryPixels) {
    DecalAtlas atlas(2048);
    atlas.AddEntry(128, 64);  // 8192 pixels
    atlas.AddEntry(256, 32);  // 8192 pixels
    EXPECT_EQ(atlas.GetUsedArea(), 8192u + 8192u);
}

TEST(DecalAtlas, GetOccupancy_SingleEntry_MatchesRatio) {
    // 256x256 in a 1024x1024 atlas: occupancy = (256*256) / (1024*1024)
    DecalAtlas atlas(1024);
    atlas.AddEntry(256, 256);
    const float expected = (256.0f * 256.0f) / (1024.0f * 1024.0f);
    EXPECT_NEAR(atlas.GetOccupancy(), expected, 1e-6f);
}

TEST(DecalAtlas, GetOccupancy_EmptyAtlas_IsZero) {
    DecalAtlas atlas(2048);
    EXPECT_FLOAT_EQ(atlas.GetOccupancy(), 0.0f);
}

TEST(DecalAtlas, GetOccupancy_FullAtlas_IsOne) {
    DecalAtlas atlas(64);
    ASSERT_TRUE(atlas.AddEntry(64, 64).has_value());
    EXPECT_FLOAT_EQ(atlas.GetOccupancy(), 1.0f);
}

// =============================================================================
// Reset
// =============================================================================

TEST(DecalAtlas, Reset_ClearsEntryCount) {
    DecalAtlas atlas(2048);
    atlas.AddEntry(128, 128);
    atlas.AddEntry(256, 64);
    atlas.Reset();
    EXPECT_EQ(atlas.GetEntryCount(), 0u);
}

TEST(DecalAtlas, Reset_ClearsUsedArea) {
    DecalAtlas atlas(2048);
    atlas.AddEntry(128, 128);
    atlas.Reset();
    EXPECT_EQ(atlas.GetUsedArea(), 0u);
}

TEST(DecalAtlas, Reset_OccupancyBecomesZero) {
    DecalAtlas atlas(2048);
    atlas.AddEntry(256, 256);
    atlas.Reset();
    EXPECT_FLOAT_EQ(atlas.GetOccupancy(), 0.0f);
}

TEST(DecalAtlas, Reset_DoesNotChangeAtlasSize) {
    DecalAtlas atlas(2048);
    atlas.AddEntry(256, 256);
    atlas.Reset();
    EXPECT_EQ(atlas.GetAtlasSize(), 2048u);
}

TEST(DecalAtlas, Reset_AllowsFullReuseAfterReset) {
    DecalAtlas atlas(256);
    ASSERT_TRUE(atlas.AddEntry(256, 256).has_value());
    atlas.Reset();
    // After reset, the full-size entry should succeed again.
    const auto entry = atlas.AddEntry(256, 256);
    EXPECT_TRUE(entry.has_value())
        << "Full-size entry must succeed after Reset";
}

// =============================================================================
// Small atlas + many small entries
// =============================================================================

TEST(DecalAtlas, SmallAtlas_ManySmallEntries_FillsCorrectly) {
    // 256x256 atlas with 32x32 tiles: capacity = 8*8 = 64 tiles.
    DecalAtlas atlas(256);
    int successCount = 0;
    for (int i = 0; i < 64; ++i) {
        if (atlas.AddEntry(32, 32).has_value()) {
            ++successCount;
        }
    }
    EXPECT_EQ(successCount, 64)
        << "A 256-atlas should fit exactly 64 tiles of 32x32";
}

TEST(DecalAtlas, SmallAtlas_ManySmallEntries_AllNonOverlapping) {
    DecalAtlas atlas(128);
    std::vector<AtlasEntry> entries;
    for (int i = 0; i < 16; ++i) {
        const auto e = atlas.AddEntry(32, 32);
        if (e.has_value()) {
            entries.push_back(*e);
        }
    }
    ASSERT_EQ(static_cast<int>(entries.size()), 16)
        << "128-atlas should fit exactly 16 tiles of 32x32";

    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            EXPECT_FALSE(RectanglesOverlap(entries[i].uvMin, entries[i].uvMax,
                                           entries[j].uvMin, entries[j].uvMax))
                << "Small-atlas entries " << i << " and " << j << " must not overlap";
        }
    }
}

// =============================================================================
// Move semantics
// =============================================================================

TEST(DecalAtlas, MoveConstructor_TransfersState) {
    DecalAtlas src(512);
    src.AddEntry(64, 64);
    src.AddEntry(32, 32);
    const uint32_t entryCount = src.GetEntryCount();
    const uint32_t usedArea = src.GetUsedArea();

    DecalAtlas dst(std::move(src));
    EXPECT_EQ(dst.GetAtlasSize(), 512u);
    EXPECT_EQ(dst.GetEntryCount(), entryCount);
    EXPECT_EQ(dst.GetUsedArea(), usedArea);
}

TEST(DecalAtlas, MoveAssignment_TransfersState) {
    DecalAtlas src(1024);
    src.AddEntry(128, 128);
    const uint32_t entryCount = src.GetEntryCount();

    DecalAtlas dst(256);
    dst = std::move(src);
    EXPECT_EQ(dst.GetAtlasSize(), 1024u);
    EXPECT_EQ(dst.GetEntryCount(), entryCount);
}

// =============================================================================
// Shelf-packing — new shelf opened when current row is full
// =============================================================================

TEST(DecalAtlas, AddEntry_TallEntryThatFillsFirstShelf_ThenShorterEntryOpensNewShelf) {
    // In a 128-wide atlas:
    //   First entry: 128x64  — fills the entire first row.
    //   Second entry: 64x32  — must open a new shelf (y=64) because first shelf is full.
    //   Both must succeed and must not overlap.
    DecalAtlas atlas(128);

    const auto e1 = atlas.AddEntry(128, 64);
    ASSERT_TRUE(e1.has_value())
        << "First entry (full-width) must succeed";

    const auto e2 = atlas.AddEntry(64, 32);
    ASSERT_TRUE(e2.has_value())
        << "Second entry must succeed by opening a new shelf";

    // The second entry's uvMin.y must be at or after the first shelf's bottom edge.
    const float firstShelfBottom = 64.0f / 128.0f;
    EXPECT_GE(e2->uvMin.y, firstShelfBottom - 1e-5f)
        << "Second entry must start on a new shelf, not inside the first shelf's rows";

    EXPECT_FALSE(RectanglesOverlap(e1->uvMin, e1->uvMax, e2->uvMin, e2->uvMax))
        << "Full-width first row and next-shelf entry must not overlap";
}

TEST(DecalAtlas, AddEntry_ExactlyFillsAtlasHeight_ThenFailsOnNextEntry) {
    // Fill the entire vertical extent with two 64x64 entries in a 64x128 atlas.
    // (Width=64 so each entry fills the full width, height=64 each row.)
    DecalAtlas atlas(64);
    const auto e1 = atlas.AddEntry(64, 32);
    ASSERT_TRUE(e1.has_value());
    const auto e2 = atlas.AddEntry(64, 32);
    ASSERT_TRUE(e2.has_value());

    // Atlas is now 64 pixels tall and fully occupied.
    const auto overflow = atlas.AddEntry(64, 1);
    EXPECT_FALSE(overflow.has_value())
        << "Entry must fail when no vertical space remains after two full-width shelves";
}

TEST(DecalAtlas, AddEntry_WidthFitsOnExistingShelf_DoesNotOpenNewShelf) {
    // Place a 64x64 entry first (opens shelf 0 at y=0, height=64, xCursor=64).
    // Then add a 32x64 entry — it must fit on the same shelf (xCursor 64+32=96 <= 128),
    // NOT open a new shelf.  Verify by checking both entries share the same uvMin.y.
    DecalAtlas atlas(128);
    const auto e1 = atlas.AddEntry(64, 64);
    ASSERT_TRUE(e1.has_value());

    const auto e2 = atlas.AddEntry(32, 64);
    ASSERT_TRUE(e2.has_value());

    // Both must be on the same shelf (same y).
    EXPECT_NEAR(e1->uvMin.y, e2->uvMin.y, 1e-5f)
        << "Second entry must reuse the existing shelf, not open a new one";

    EXPECT_FALSE(RectanglesOverlap(e1->uvMin, e1->uvMax, e2->uvMin, e2->uvMax));
}
