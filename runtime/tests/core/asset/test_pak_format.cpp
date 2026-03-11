/// UzEngine .uzpak Archive Format — Tests
///
/// Tests write/read round-trip, alignment, lookup by path and hash,
/// multi-entry archives, file I/O, and error handling.

#include "pak_format.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Struct sizes
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, HeaderSizeIs16Bytes) {
    EXPECT_EQ(sizeof(PakHeader), 16u);
}

TEST(PakFormat, IndexEntrySizeIs40Bytes) {
    EXPECT_EQ(sizeof(PakIndexEntry), 40u);
}

TEST(PakFormat, TrailerSizeIs16Bytes) {
    EXPECT_EQ(sizeof(PakTrailer), 16u);
}

TEST(PakFormat, AlignmentIs64Bytes) {
    EXPECT_EQ(kPakAlignment, 64u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Write + Read round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, EmptyArchiveRoundTrips) {
    auto data = WritePak({});

    PakReader reader;
    ASSERT_TRUE(reader.Load(data));
    EXPECT_EQ(reader.EntryCount(), 0u);
    EXPECT_TRUE(reader.IsLoaded());
}

TEST(PakFormat, SingleEntryRoundTrip) {
    std::vector<PakWriteEntry> entries = {
        {"shaders/simple_vs.uzshader", {0xDE, 0xAD, 0xBE, 0xEF}, 1},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    EXPECT_EQ(reader.EntryCount(), 1u);
    EXPECT_TRUE(reader.Contains("shaders/simple_vs.uzshader"));

    auto result = reader.Lookup("shaders/simple_vs.uzshader");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(PakFormat, MultipleEntriesRoundTrip) {
    std::vector<PakWriteEntry> entries = {
        {"shaders/a.uzshader", {0x01, 0x02}, 1},
        {"shaders/b.uzshader", {0x03, 0x04, 0x05}, 1},
        {"textures/floor.uztexture", {0x10, 0x20, 0x30, 0x40}, 2},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    EXPECT_EQ(reader.EntryCount(), 3u);

    auto a = reader.Lookup("shaders/a.uzshader");
    EXPECT_EQ(a, (std::vector<uint8_t>{0x01, 0x02}));

    auto b = reader.Lookup("shaders/b.uzshader");
    EXPECT_EQ(b, (std::vector<uint8_t>{0x03, 0x04, 0x05}));

    auto t = reader.Lookup("textures/floor.uztexture");
    EXPECT_EQ(t, (std::vector<uint8_t>{0x10, 0x20, 0x30, 0x40}));
}

TEST(PakFormat, LookupMissingPathReturnsEmpty) {
    std::vector<PakWriteEntry> entries = {
        {"shaders/a.uzshader", {0x01}, 0},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    auto result = reader.Lookup("nonexistent");
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(reader.Contains("nonexistent"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Alignment
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, DataBlobsAreAligned) {
    std::vector<PakWriteEntry> entries = {
        {"a", std::vector<uint8_t>(10, 0xAA), 0},
        {"b", std::vector<uint8_t>(100, 0xBB), 0},
        {"c", std::vector<uint8_t>(1, 0xCC), 0},
    };

    auto data = WritePak(entries);

    // Read the trailer to find the index
    PakTrailer trailer;
    std::memcpy(&trailer, data.data() + data.size() - sizeof(PakTrailer), sizeof(trailer));

    PakHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    // Check each entry's offset is 64-byte aligned
    for (uint32_t i = 0; i < header.entryCount; ++i) {
        PakIndexEntry entry;
        std::memcpy(&entry,
                    data.data() + trailer.indexOffset + i * sizeof(PakIndexEntry),
                    sizeof(entry));
        EXPECT_EQ(entry.offset % kPakAlignment, 0u)
            << "Entry " << i << " offset " << entry.offset << " not aligned to " << kPakAlignment;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Large data
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, LargeEntryRoundTrips) {
    std::vector<uint8_t> bigData(64 * 1024, 0xFF); // 64 KB

    std::vector<PakWriteEntry> entries = {
        {"big_asset", bigData, 0},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    auto result = reader.Lookup("big_asset");
    EXPECT_EQ(result.size(), bigData.size());
    EXPECT_EQ(result, bigData);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetPathHashes
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, GetPathHashesReturnsAll) {
    std::vector<PakWriteEntry> entries = {
        {"a", {1}, 0},
        {"b", {2}, 0},
        {"c", {3}, 0},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    auto hashes = reader.GetPathHashes();
    EXPECT_EQ(hashes.size(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unload
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, UnloadClearsState) {
    std::vector<PakWriteEntry> entries = {{"a", {1}, 0}};
    auto data = WritePak(entries);

    PakReader reader;
    ASSERT_TRUE(reader.Load(data));
    EXPECT_TRUE(reader.IsLoaded());

    reader.Unload();
    EXPECT_FALSE(reader.IsLoaded());
    EXPECT_EQ(reader.EntryCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, LoadEmptyDataFails) {
    PakReader reader;
    EXPECT_FALSE(reader.Load({}));
}

TEST(PakFormat, LoadTooSmallDataFails) {
    PakReader reader;
    std::vector<uint8_t> small(10, 0);
    EXPECT_FALSE(reader.Load(small));
}

TEST(PakFormat, LoadBadMagicFails) {
    auto data = WritePak({{"a", {1}, 0}});
    data[0] = 0x00; // Corrupt header magic
    PakReader reader;
    EXPECT_FALSE(reader.Load(data));
}

TEST(PakFormat, LoadBadVersionFails) {
    auto data = WritePak({{"a", {1}, 0}});
    data[4] = 0xFF; // Corrupt version
    PakReader reader;
    EXPECT_FALSE(reader.Load(data));
}

TEST(PakFormat, LoadBadTrailerMagicFails) {
    auto data = WritePak({{"a", {1}, 0}});
    data[data.size() - 1] = 0x00; // Corrupt trailer magic
    PakReader reader;
    EXPECT_FALSE(reader.Load(data));
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

class PakFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_pak").string();
        fs::create_directories(m_dir);
        m_path = m_dir + "/test.uzpak";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
    std::string m_path;
};

TEST_F(PakFileTest, WriteAndReadFile) {
    std::vector<PakWriteEntry> entries = {
        {"shaders/vs.uzshader", {0x01, 0x02, 0x03}, 1},
        {"textures/diffuse.uztexture", {0x10, 0x20}, 2},
    };

    ASSERT_TRUE(WritePakFile(m_path, entries));

    PakReader reader;
    ASSERT_TRUE(reader.LoadFile(m_path));
    EXPECT_EQ(reader.EntryCount(), 2u);

    auto vs = reader.Lookup("shaders/vs.uzshader");
    EXPECT_EQ(vs, (std::vector<uint8_t>{0x01, 0x02, 0x03}));

    auto tex = reader.Lookup("textures/diffuse.uztexture");
    EXPECT_EQ(tex, (std::vector<uint8_t>{0x10, 0x20}));
}

TEST_F(PakFileTest, LoadNonexistentFileFails) {
    PakReader reader;
    EXPECT_FALSE(reader.LoadFile(m_dir + "/nonexistent.uzpak"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Empty data entry
// ─────────────────────────────────────────────────────────────────────────────

TEST(PakFormat, EmptyEntryRoundTrips) {
    std::vector<PakWriteEntry> entries = {
        {"empty", {}, 0},
    };

    auto data = WritePak(entries);
    PakReader reader;
    ASSERT_TRUE(reader.Load(data));

    EXPECT_TRUE(reader.Contains("empty"));
    auto result = reader.Lookup("empty");
    EXPECT_TRUE(result.empty());
}

} // namespace uz::test
