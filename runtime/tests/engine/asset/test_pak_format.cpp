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

// ═════════════════════════════════════════════════════════════════════════════
// Phase 1: MasterIndex + WriteMasterIndex
// ═════════════════════════════════════════════════════════════════════════════

// ─── Struct size checks ──────────────────────────────────────────────────────

TEST(MasterIndex, HeaderSizeIs16Bytes) {
    EXPECT_EQ(sizeof(MasterIndexHeader), 16u);
}

TEST(MasterIndex, PakTableEntrySizeIs264Bytes) {
    EXPECT_EQ(sizeof(MasterPakTableEntry), 264u);
}

TEST(MasterIndex, AssetEntrySizeIs48Bytes) {
    EXPECT_EQ(sizeof(MasterAssetEntry), 48u);
}

TEST(MasterIndex, TrailerSizeIs20Bytes) {
    EXPECT_EQ(sizeof(MasterIndexTrailer), 20u);
}

// ─── MasterIndex file-based fixture ─────────────────────────────────────────

class MasterIndexFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_master_idx").string();
        fs::create_directories(m_dir);
        m_indexPath = m_dir + "/test.uzpakindex";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
    std::string m_indexPath;
};

TEST_F(MasterIndexFileTest, WriteThenLoad_RoundTrips) {
    std::vector<std::string> pakFiles = {"data0.uzpak", "data1.uzpak"};

    std::vector<MasterAssetEntry> entries;
    {
        MasterAssetEntry e;
        e.pathHash = 0xAAAAAAAA00000001ULL;
        e.pakId = 0;
        e.typeId = 10;
        e.offset = 64;
        e.compressedSize = 100;
        e.rawSize = 100;
        e.contentHash = 0xBBBB0001;
        entries.push_back(e);
    }
    {
        MasterAssetEntry e;
        e.pathHash = 0xAAAAAAAA00000002ULL;
        e.pakId = 1;
        e.typeId = 20;
        e.offset = 128;
        e.compressedSize = 200;
        e.rawSize = 200;
        e.contentHash = 0xBBBB0002;
        entries.push_back(e);
    }
    {
        MasterAssetEntry e;
        e.pathHash = 0xAAAAAAAA00000003ULL;
        e.pakId = 0;
        e.typeId = 30;
        e.offset = 256;
        e.compressedSize = 50;
        e.rawSize = 50;
        e.contentHash = 0xBBBB0003;
        entries.push_back(e);
    }

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, entries));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));
    EXPECT_TRUE(idx.IsLoaded());
    EXPECT_EQ(idx.EntryCount(), 3u);
    EXPECT_EQ(idx.PakCount(), 2u);

    // Verify pak filenames
    const auto& fnames = idx.PakFilenames();
    ASSERT_EQ(fnames.size(), 2u);
    EXPECT_EQ(fnames[0], "data0.uzpak");
    EXPECT_EQ(fnames[1], "data1.uzpak");

    // Verify lookup for first entry
    auto loc1 = idx.Lookup(0xAAAAAAAA00000001ULL);
    ASSERT_TRUE(loc1.has_value());
    EXPECT_EQ(loc1->pakFilename, "data0.uzpak");
    EXPECT_EQ(loc1->offset, 64u);
    EXPECT_EQ(loc1->compressedSize, 100u);
    EXPECT_EQ(loc1->rawSize, 100u);
    EXPECT_EQ(loc1->contentHash, 0xBBBB0001u);
    EXPECT_EQ(loc1->typeId, 10u);

    // Verify lookup for second entry (different pak)
    auto loc2 = idx.Lookup(0xAAAAAAAA00000002ULL);
    ASSERT_TRUE(loc2.has_value());
    EXPECT_EQ(loc2->pakFilename, "data1.uzpak");
    EXPECT_EQ(loc2->offset, 128u);
    EXPECT_EQ(loc2->compressedSize, 200u);
    EXPECT_EQ(loc2->rawSize, 200u);
    EXPECT_EQ(loc2->typeId, 20u);

    // Verify lookup for third entry
    auto loc3 = idx.Lookup(0xAAAAAAAA00000003ULL);
    ASSERT_TRUE(loc3.has_value());
    EXPECT_EQ(loc3->pakFilename, "data0.uzpak");
    EXPECT_EQ(loc3->offset, 256u);
    EXPECT_EQ(loc3->compressedSize, 50u);
    EXPECT_EQ(loc3->rawSize, 50u);
    EXPECT_EQ(loc3->typeId, 30u);
}

TEST_F(MasterIndexFileTest, Lookup_MissingHash_ReturnsNullopt) {
    std::vector<std::string> pakFiles = {"data.uzpak"};
    MasterAssetEntry e;
    e.pathHash = 0x1234;
    e.pakId = 0;
    e.offset = 64;
    e.compressedSize = 10;
    e.rawSize = 10;
    e.contentHash = 0xABCD;
    e.typeId = 1;

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, {e}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    auto result = idx.Lookup(0x9999);
    EXPECT_FALSE(result.has_value());
}

TEST_F(MasterIndexFileTest, Contains_TrueForExisting_FalseForMissing) {
    std::vector<std::string> pakFiles = {"pack.uzpak"};
    MasterAssetEntry e;
    e.pathHash = 0xABCDEF01;
    e.pakId = 0;
    e.offset = 64;
    e.compressedSize = 8;
    e.rawSize = 8;
    e.contentHash = 0x11;
    e.typeId = 0;

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, {e}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    EXPECT_TRUE(idx.Contains(0xABCDEF01));
    EXPECT_FALSE(idx.Contains(0xDEADBEEF));
    EXPECT_FALSE(idx.Contains(0x0));
}

TEST_F(MasterIndexFileTest, EntryCountAndPakCount_MatchWrittenData) {
    std::vector<std::string> pakFiles = {"a.uzpak", "b.uzpak", "c.uzpak"};

    std::vector<MasterAssetEntry> entries;
    for (uint32_t i = 0; i < 5; ++i) {
        MasterAssetEntry e;
        e.pathHash = 0x100 + i;
        e.pakId = i % 3;
        e.offset = 64 * (i + 1);
        e.compressedSize = 16;
        e.rawSize = 16;
        e.contentHash = 0xFF00 + i;
        e.typeId = i;
        entries.push_back(e);
    }

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, entries));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    EXPECT_EQ(idx.EntryCount(), 5u);
    EXPECT_EQ(idx.PakCount(), 3u);
}

TEST_F(MasterIndexFileTest, PakFilenames_ReturnsCorrectList) {
    std::vector<std::string> pakFiles = {
        "content/shaders.uzpak",
        "content/textures.uzpak",
        "content/meshes.uzpak",
    };

    // Need at least one entry per pak to be valid, but entries are optional
    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, {}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    const auto& fnames = idx.PakFilenames();
    ASSERT_EQ(fnames.size(), 3u);
    EXPECT_EQ(fnames[0], "content/shaders.uzpak");
    EXPECT_EQ(fnames[1], "content/textures.uzpak");
    EXPECT_EQ(fnames[2], "content/meshes.uzpak");
}

TEST_F(MasterIndexFileTest, WriteThenLoad_EmptyEntries_ProducesValidIndex) {
    std::vector<std::string> pakFiles = {"empty.uzpak"};

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, {}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    EXPECT_TRUE(idx.IsLoaded());
    EXPECT_EQ(idx.EntryCount(), 0u);
    EXPECT_EQ(idx.PakCount(), 1u);
    EXPECT_FALSE(idx.Contains(0x1));
    EXPECT_FALSE(idx.Lookup(0x1).has_value());
}

TEST_F(MasterIndexFileTest, WriteThenLoad_EmptyPaksAndEntries_ProducesValidIndex) {
    ASSERT_TRUE(WriteMasterIndex(m_indexPath, {}, {}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));

    EXPECT_TRUE(idx.IsLoaded());
    EXPECT_EQ(idx.EntryCount(), 0u);
    EXPECT_EQ(idx.PakCount(), 0u);
}

TEST_F(MasterIndexFileTest, LoadFile_NonexistentPath_ReturnsFalse) {
    MasterIndex idx;
    EXPECT_FALSE(idx.LoadFile(m_dir + "/does_not_exist.uzpakindex"));
    EXPECT_FALSE(idx.IsLoaded());
}

TEST_F(MasterIndexFileTest, Unload_ClearsState) {
    std::vector<std::string> pakFiles = {"data.uzpak"};
    MasterAssetEntry e;
    e.pathHash = 0x42;
    e.pakId = 0;
    e.offset = 64;
    e.compressedSize = 16;
    e.rawSize = 16;
    e.contentHash = 0xCC;
    e.typeId = 1;

    ASSERT_TRUE(WriteMasterIndex(m_indexPath, pakFiles, {e}));

    MasterIndex idx;
    ASSERT_TRUE(idx.LoadFile(m_indexPath));
    EXPECT_TRUE(idx.IsLoaded());
    EXPECT_EQ(idx.EntryCount(), 1u);
    EXPECT_TRUE(idx.Contains(0x42));

    idx.Unload();

    EXPECT_FALSE(idx.IsLoaded());
    EXPECT_EQ(idx.EntryCount(), 0u);
    EXPECT_EQ(idx.PakCount(), 0u);
    EXPECT_FALSE(idx.Contains(0x42));
    EXPECT_FALSE(idx.Lookup(0x42).has_value());
    EXPECT_TRUE(idx.PakFilenames().empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 2: XOR Scramble (PakCompressBlob / PakDecompressBlob)
// ═════════════════════════════════════════════════════════════════════════════

TEST(PakScramble, CompressDecompress_RoundTrips) {
    const std::vector<uint8_t> original = {
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F,
        0x72, 0x6C, 0x64, 0x21, 0x00, 0xFF, 0x80, 0x7F,
    };
    const uint64_t pathHash = 0xDEADBEEF12345678ULL;
    const uint64_t secret = 0xCAFEBABE00000001ULL;

    auto scrambled = PakCompressBlob(original, pathHash, secret);
    ASSERT_EQ(scrambled.size(), original.size());

    auto restored = PakDecompressBlob(
        scrambled.data(), scrambled.size(), original.size(), pathHash, secret);

    ASSERT_EQ(restored.size(), original.size());
    EXPECT_EQ(restored, original);
}

TEST(PakScramble, ScrambledOutput_DiffersFromInput) {
    const std::vector<uint8_t> original = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C,
    };
    const uint64_t pathHash = 0x1111111111111111ULL;
    const uint64_t secret = 0x2222222222222222ULL;

    auto scrambled = PakCompressBlob(original, pathHash, secret);
    ASSERT_EQ(scrambled.size(), original.size());
    EXPECT_NE(scrambled, original)
        << "Scrambled output must differ from input (XOR key is non-zero)";
}

TEST(PakScramble, DifferentPathHash_ProducesDifferentOutput) {
    const std::vector<uint8_t> original = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    const uint64_t secret = 0x1234567890ABCDEFULL;

    auto scrambled_a = PakCompressBlob(original, 0x1000, secret);
    auto scrambled_b = PakCompressBlob(original, 0x2000, secret);

    ASSERT_EQ(scrambled_a.size(), original.size());
    ASSERT_EQ(scrambled_b.size(), original.size());
    EXPECT_NE(scrambled_a, scrambled_b)
        << "Different pathHash values must produce different scrambled output";
}

TEST(PakScramble, DifferentSecret_ProducesDifferentOutput) {
    const std::vector<uint8_t> original = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
    const uint64_t pathHash = 0xABCDABCDABCDABCDULL;

    auto scrambled_a = PakCompressBlob(original, pathHash, 0x0001);
    auto scrambled_b = PakCompressBlob(original, pathHash, 0x0002);

    ASSERT_EQ(scrambled_a.size(), original.size());
    ASSERT_EQ(scrambled_b.size(), original.size());
    EXPECT_NE(scrambled_a, scrambled_b)
        << "Different secret values must produce different scrambled output";
}

TEST(PakScramble, EmptyBlob_RoundTrips) {
    const std::vector<uint8_t> empty;
    auto scrambled = PakCompressBlob(empty, 0x1234, 0x5678);
    EXPECT_TRUE(scrambled.empty());

    auto restored = PakDecompressBlob(nullptr, 0, 0, 0x1234, 0x5678);
    EXPECT_TRUE(restored.empty());
}

TEST(PakScramble, LargeBlob_64KB_RoundTrips) {
    std::vector<uint8_t> original(64 * 1024);
    // Fill with a recognizable pattern
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    const uint64_t pathHash = 0xFEDCBA9876543210ULL;
    const uint64_t secret = 0x0123456789ABCDEFULL;

    auto scrambled = PakCompressBlob(original, pathHash, secret);
    ASSERT_EQ(scrambled.size(), original.size());
    EXPECT_NE(scrambled, original);

    auto restored = PakDecompressBlob(
        scrambled.data(), scrambled.size(), original.size(), pathHash, secret);

    ASSERT_EQ(restored.size(), original.size());
    EXPECT_EQ(restored, original);
}

TEST(PakScramble, Decompress_NullData_ReturnsEmpty) {
    auto result = PakDecompressBlob(nullptr, 100, 100, 0x1, 0x2);
    EXPECT_TRUE(result.empty());
}

TEST(PakScramble, Decompress_SizeMismatch_ReturnsEmpty) {
    // When no LZ4, compressedSize must equal rawSize
    const std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = PakDecompressBlob(data.data(), 3, 10, 0x1, 0x2);
    EXPECT_TRUE(result.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 3: PakStreamReader
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(PakFileTest, StreamReader_Open_ValidFile_IsOpenTrue) {
    std::vector<PakWriteEntry> entries = {
        {"shaders/test.hlsl", {0xAA, 0xBB, 0xCC, 0xDD}, 1},
    };
    ASSERT_TRUE(WritePakFile(m_path, entries));

    PakStreamReader reader;
    EXPECT_FALSE(reader.IsOpen());

    ASSERT_TRUE(reader.Open(m_path));
    EXPECT_TRUE(reader.IsOpen());
    EXPECT_EQ(reader.Path(), m_path);
}

TEST_F(PakFileTest, StreamReader_ReadBlob_ReturnsCorrectData) {
    // Write a pak, then use PakReader to find the offset, then use PakStreamReader
    // to read the blob at that offset
    const std::vector<uint8_t> blobData = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    std::vector<PakWriteEntry> entries = {
        {"assets/mesh.bin", blobData, 5},
    };

    // Write to file
    ASSERT_TRUE(WritePakFile(m_path, entries));

    // Load with PakReader to discover the blob offset and size
    PakReader pakReader;
    ASSERT_TRUE(pakReader.LoadFile(m_path));
    auto fullBlob = pakReader.Lookup("assets/mesh.bin");
    ASSERT_EQ(fullBlob.size(), blobData.size());

    // Parse the pak to extract the index entry offset for this blob
    auto pakData = WritePak(entries);
    PakTrailer trailer;
    std::memcpy(&trailer, pakData.data() + pakData.size() - sizeof(PakTrailer), sizeof(trailer));
    PakIndexEntry idxEntry;
    std::memcpy(&idxEntry, pakData.data() + trailer.indexOffset, sizeof(idxEntry));

    // Now use PakStreamReader to read the blob via seek+read
    PakStreamReader streamReader;
    ASSERT_TRUE(streamReader.Open(m_path));

    auto streamBlob = streamReader.ReadBlob(idxEntry.offset, idxEntry.size);
    ASSERT_EQ(streamBlob.size(), blobData.size());
    EXPECT_EQ(streamBlob, blobData);
}

TEST_F(PakFileTest, StreamReader_Open_NonexistentFile_ReturnsFalse) {
    PakStreamReader reader;
    EXPECT_FALSE(reader.Open(m_dir + "/nonexistent.uzpak"));
    EXPECT_FALSE(reader.IsOpen());
}

TEST(PakStreamReader_NoFixture, ReadBlob_OnClosedReader_ReturnsEmpty) {
    PakStreamReader reader;
    EXPECT_FALSE(reader.IsOpen());

    auto result = reader.ReadBlob(0, 100);
    EXPECT_TRUE(result.empty());
}

TEST_F(PakFileTest, StreamReader_CloseAndReopen) {
    const std::vector<uint8_t> blobData = {0x01, 0x02, 0x03};
    std::vector<PakWriteEntry> entries = {
        {"file.bin", blobData, 0},
    };
    ASSERT_TRUE(WritePakFile(m_path, entries));

    PakStreamReader reader;
    ASSERT_TRUE(reader.Open(m_path));
    EXPECT_TRUE(reader.IsOpen());

    reader.Close();
    EXPECT_FALSE(reader.IsOpen());
    EXPECT_TRUE(reader.Path().empty());

    // Reopen
    ASSERT_TRUE(reader.Open(m_path));
    EXPECT_TRUE(reader.IsOpen());
    EXPECT_EQ(reader.Path(), m_path);
}

TEST_F(PakFileTest, StreamReader_MoveConstructor_TransfersOwnership) {
    const std::vector<uint8_t> blobData = {0xAA, 0xBB};
    std::vector<PakWriteEntry> entries = {
        {"data.bin", blobData, 0},
    };
    ASSERT_TRUE(WritePakFile(m_path, entries));

    PakStreamReader original;
    ASSERT_TRUE(original.Open(m_path));
    EXPECT_TRUE(original.IsOpen());

    // Move construct
    PakStreamReader moved(std::move(original));
    EXPECT_TRUE(moved.IsOpen());
    EXPECT_EQ(moved.Path(), m_path);

    // Original should no longer be open (moved-from state)
    EXPECT_FALSE(original.IsOpen());

    // Read blob from moved reader — discover offset from in-memory pak
    auto pakData = WritePak(entries);
    PakTrailer trailer;
    std::memcpy(&trailer, pakData.data() + pakData.size() - sizeof(PakTrailer), sizeof(trailer));
    PakIndexEntry idxEntry;
    std::memcpy(&idxEntry, pakData.data() + trailer.indexOffset, sizeof(idxEntry));

    auto blob = moved.ReadBlob(idxEntry.offset, idxEntry.size);
    ASSERT_EQ(blob.size(), blobData.size());
    EXPECT_EQ(blob, blobData);
}

TEST_F(PakFileTest, StreamReader_ReadBlob_ZeroSize_ReturnsEmpty) {
    const std::vector<uint8_t> blobData = {0x01};
    std::vector<PakWriteEntry> entries = {{"x", blobData, 0}};
    ASSERT_TRUE(WritePakFile(m_path, entries));

    PakStreamReader reader;
    ASSERT_TRUE(reader.Open(m_path));

    auto result = reader.ReadBlob(0, 0);
    EXPECT_TRUE(result.empty());
}

} // namespace uz::test
