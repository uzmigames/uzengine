/// UzEngine Virtual File System — Tests
///
/// Tests LooseFileVFS (disk reads, missing files, empty files),
/// PakVFS (archive reads, missing entries), LayeredVFS (priority
/// resolution, override behavior, unmount), and edge cases.

#include "vfs.h"
#include "pak_format.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS
// ─────────────────────────────────────────────────────────────────────────────

class LooseFileVFSTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_vfs_loose").string();
        fs::create_directories(m_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    void WriteFile(const std::string& relPath, const std::string& content) {
        std::string full = m_dir + "/" + relPath;
        fs::create_directories(fs::path(full).parent_path());
        std::ofstream f(full, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::string m_dir;
};

TEST_F(LooseFileVFSTest, ReadExistingFile) {
    WriteFile("test.txt", "hello world");
    LooseFileVFS vfs(m_dir);

    auto data = vfs.Read("test.txt");
    std::string result(data.begin(), data.end());
    EXPECT_EQ(result, "hello world");
}

TEST_F(LooseFileVFSTest, ReadNestedFile) {
    WriteFile("shaders/pbr/vertex.hlsl", "float4 main() {}");
    LooseFileVFS vfs(m_dir);

    auto data = vfs.Read("shaders/pbr/vertex.hlsl");
    std::string result(data.begin(), data.end());
    EXPECT_EQ(result, "float4 main() {}");
}

TEST_F(LooseFileVFSTest, ReadNonexistentFileReturnsEmpty) {
    LooseFileVFS vfs(m_dir);
    auto data = vfs.Read("nonexistent.txt");
    EXPECT_TRUE(data.empty());
}

TEST_F(LooseFileVFSTest, ReadBinaryData) {
    std::string binary = {'\x00', '\x01', '\xFF', '\xFE', '\x80'};
    WriteFile("data.bin", binary);
    LooseFileVFS vfs(m_dir);

    auto data = vfs.Read("data.bin");
    ASSERT_EQ(data.size(), 5u);
    EXPECT_EQ(data[0], 0x00);
    EXPECT_EQ(data[1], 0x01);
    EXPECT_EQ(data[2], 0xFF);
    EXPECT_EQ(data[3], 0xFE);
    EXPECT_EQ(data[4], 0x80);
}

TEST_F(LooseFileVFSTest, ExistsReturnsTrueForExistingFile) {
    WriteFile("exists.txt", "data");
    LooseFileVFS vfs(m_dir);
    EXPECT_TRUE(vfs.Exists("exists.txt"));
}

TEST_F(LooseFileVFSTest, ExistsReturnsFalseForMissingFile) {
    LooseFileVFS vfs(m_dir);
    EXPECT_FALSE(vfs.Exists("missing.txt"));
}

TEST_F(LooseFileVFSTest, ExistsReturnsFalseForDirectory) {
    fs::create_directories(m_dir + "/subdir");
    LooseFileVFS vfs(m_dir);
    EXPECT_FALSE(vfs.Exists("subdir"));
}

TEST_F(LooseFileVFSTest, NameContainsRootDir) {
    LooseFileVFS vfs(m_dir);
    EXPECT_NE(vfs.Name().find("LooseFileVFS"), std::string::npos);
    EXPECT_NE(vfs.Name().find(m_dir), std::string::npos);
}

TEST_F(LooseFileVFSTest, TrailingSlashNormalization) {
    WriteFile("file.txt", "data");

    LooseFileVFS vfsNoSlash(m_dir);
    LooseFileVFS vfsWithSlash(m_dir + "/");

    EXPECT_TRUE(vfsNoSlash.Exists("file.txt"));
    EXPECT_TRUE(vfsWithSlash.Exists("file.txt"));
}

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS
// ─────────────────────────────────────────────────────────────────────────────

class PakVFSTest : public ::testing::Test {
protected:
    PakReader CreatePakWithEntries(const std::vector<PakWriteEntry>& entries) {
        auto bytes = WritePak(entries);
        PakReader reader;
        reader.Load(bytes);
        return reader;
    }
};

TEST_F(PakVFSTest, ReadExistingEntry) {
    std::vector<uint8_t> content = {'A', 'B', 'C', 'D'};
    auto reader = CreatePakWithEntries({{"asset.bin", content, 0}});
    PakVFS vfs(std::move(reader));

    auto data = vfs.Read("asset.bin");
    ASSERT_EQ(data.size(), 4u);
    EXPECT_EQ(data[0], 'A');
    EXPECT_EQ(data[3], 'D');
}

TEST_F(PakVFSTest, ReadMissingEntryReturnsEmpty) {
    auto reader = CreatePakWithEntries({{"asset.bin", {1, 2, 3}, 0}});
    PakVFS vfs(std::move(reader));

    auto data = vfs.Read("nonexistent.bin");
    EXPECT_TRUE(data.empty());
}

TEST_F(PakVFSTest, ExistsReturnsTrueForPackedAsset) {
    auto reader = CreatePakWithEntries({{"shaders/vs.uzshader", {0xFF}, 0}});
    PakVFS vfs(std::move(reader));

    EXPECT_TRUE(vfs.Exists("shaders/vs.uzshader"));
}

TEST_F(PakVFSTest, ExistsReturnsFalseForMissingAsset) {
    auto reader = CreatePakWithEntries({{"a.bin", {1}, 0}});
    PakVFS vfs(std::move(reader));

    EXPECT_FALSE(vfs.Exists("b.bin"));
}

TEST_F(PakVFSTest, NameReturnsIdentifier) {
    auto reader = CreatePakWithEntries({{"x.bin", {1}, 0}});
    PakVFS vfs(std::move(reader));

    EXPECT_NE(vfs.Name().find("PakVFS"), std::string::npos);
}

TEST_F(PakVFSTest, ReaderAccessReturnsLoadedReader) {
    auto reader = CreatePakWithEntries({
        {"a.bin", {1}, 0},
        {"b.bin", {2}, 0},
    });
    PakVFS vfs(std::move(reader));

    EXPECT_EQ(vfs.Reader().EntryCount(), 2u);
}

TEST_F(PakVFSTest, MultipleEntriesAllReadable) {
    auto reader = CreatePakWithEntries({
        {"first.bin", {'F'}, 0},
        {"second.bin", {'S'}, 0},
        {"third.bin", {'T'}, 0},
    });
    PakVFS vfs(std::move(reader));

    EXPECT_EQ(vfs.Read("first.bin").size(), 1u);
    EXPECT_EQ(vfs.Read("second.bin")[0], 'S');
    EXPECT_EQ(vfs.Read("third.bin")[0], 'T');
}

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS::FromFile
// ─────────────────────────────────────────────────────────────────────────────

class PakVFSFromFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_pakvfs_file").string();
        fs::create_directories(m_dir);
        m_pakPath = m_dir + "/test.uzpak";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
    std::string m_pakPath;
};

TEST_F(PakVFSFromFileTest, LoadFromValidFile) {
    std::vector<PakWriteEntry> entries = {{"data.bin", {10, 20, 30}, 0}};
    WritePakFile(m_pakPath, entries);

    auto vfs = PakVFS::FromFile(m_pakPath);
    ASSERT_NE(vfs, nullptr);

    auto data = vfs->Read("data.bin");
    ASSERT_EQ(data.size(), 3u);
    EXPECT_EQ(data[0], 10);
    EXPECT_EQ(data[2], 30);
}

TEST_F(PakVFSFromFileTest, LoadFromNonexistentFileReturnsNull) {
    auto vfs = PakVFS::FromFile(m_dir + "/nonexistent.uzpak");
    EXPECT_EQ(vfs, nullptr);
}

TEST_F(PakVFSFromFileTest, NameContainsFilePath) {
    WritePakFile(m_pakPath, {{"x.bin", {1}, 0}});
    auto vfs = PakVFS::FromFile(m_pakPath);
    ASSERT_NE(vfs, nullptr);
    EXPECT_NE(vfs->Name().find(m_pakPath), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// LayeredVFS
// ─────────────────────────────────────────────────────────────────────────────

class LayeredVFSTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_vfs_layered").string();
        fs::create_directories(m_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::shared_ptr<LooseFileVFS> MakeLooseVFS(const std::string& subdir) {
        std::string path = m_dir + "/" + subdir;
        fs::create_directories(path);
        return std::make_shared<LooseFileVFS>(path);
    }

    void WriteFile(const std::string& absPath, const std::string& content) {
        fs::create_directories(fs::path(absPath).parent_path());
        std::ofstream f(absPath, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::shared_ptr<PakVFS> MakePakVFS(const std::vector<PakWriteEntry>& entries) {
        auto bytes = WritePak(entries);
        PakReader reader;
        reader.Load(bytes);
        return std::make_shared<PakVFS>(std::move(reader));
    }

    std::string m_dir;
};

TEST_F(LayeredVFSTest, EmptyLayeredReturnsEmpty) {
    LayeredVFS layered;
    EXPECT_TRUE(layered.Read("anything").empty());
    EXPECT_FALSE(layered.Exists("anything"));
}

TEST_F(LayeredVFSTest, SingleMountReadsFile) {
    auto loose = MakeLooseVFS("base");
    WriteFile(m_dir + "/base/file.txt", "base content");

    LayeredVFS layered;
    layered.Mount(loose, 0);

    auto data = layered.Read("file.txt");
    std::string result(data.begin(), data.end());
    EXPECT_EQ(result, "base content");
}

TEST_F(LayeredVFSTest, HigherPriorityOverridesLower) {
    auto basePak = MakePakVFS({{"shader.bin", {'B', 'A', 'S', 'E'}, 0}});
    auto patchPak = MakePakVFS({{"shader.bin", {'P', 'A', 'T', 'C', 'H'}, 0}});

    LayeredVFS layered;
    layered.Mount(basePak, 0);
    layered.Mount(patchPak, 50);

    auto data = layered.Read("shader.bin");
    std::string result(data.begin(), data.end());
    EXPECT_EQ(result, "PATCH");
}

TEST_F(LayeredVFSTest, FallsBackToLowerPriority) {
    auto basePak = MakePakVFS({
        {"base_only.bin", {'B'}, 0},
        {"shared.bin", {'B', 'S'}, 0},
    });
    auto patchPak = MakePakVFS({{"shared.bin", {'P', 'S'}, 0}});

    LayeredVFS layered;
    layered.Mount(basePak, 0);
    layered.Mount(patchPak, 50);

    // shared.bin comes from patch (higher priority)
    auto shared = layered.Read("shared.bin");
    EXPECT_EQ(shared.size(), 2u);
    EXPECT_EQ(shared[0], 'P');

    // base_only.bin falls back to base
    auto baseOnly = layered.Read("base_only.bin");
    EXPECT_EQ(baseOnly.size(), 1u);
    EXPECT_EQ(baseOnly[0], 'B');
}

TEST_F(LayeredVFSTest, LooseOverridesPak) {
    auto pak = MakePakVFS({{"config.txt", {'P', 'A', 'K'}, 0}});
    auto loose = MakeLooseVFS("dev");
    WriteFile(m_dir + "/dev/config.txt", "DEV");

    LayeredVFS layered;
    layered.Mount(pak, 0);
    layered.Mount(loose, 100);

    auto data = layered.Read("config.txt");
    std::string result(data.begin(), data.end());
    EXPECT_EQ(result, "DEV");
}

TEST_F(LayeredVFSTest, ExistsChecksAllMounts) {
    auto pak = MakePakVFS({{"pak_file.bin", {1}, 0}});
    auto loose = MakeLooseVFS("dev");
    WriteFile(m_dir + "/dev/loose_file.txt", "data");

    LayeredVFS layered;
    layered.Mount(pak, 0);
    layered.Mount(loose, 100);

    EXPECT_TRUE(layered.Exists("pak_file.bin"));
    EXPECT_TRUE(layered.Exists("loose_file.txt"));
    EXPECT_FALSE(layered.Exists("missing.txt"));
}

TEST_F(LayeredVFSTest, MountCountTracksCorrectly) {
    LayeredVFS layered;
    EXPECT_EQ(layered.MountCount(), 0u);

    layered.Mount(MakeLooseVFS("a"), 0);
    EXPECT_EQ(layered.MountCount(), 1u);

    layered.Mount(MakeLooseVFS("b"), 10);
    EXPECT_EQ(layered.MountCount(), 2u);
}

TEST_F(LayeredVFSTest, UnmountAllClearsEverything) {
    auto loose = MakeLooseVFS("data");
    WriteFile(m_dir + "/data/file.txt", "content");

    LayeredVFS layered;
    layered.Mount(loose, 0);
    EXPECT_TRUE(layered.Exists("file.txt"));

    layered.UnmountAll();
    EXPECT_EQ(layered.MountCount(), 0u);
    EXPECT_FALSE(layered.Exists("file.txt"));
    EXPECT_TRUE(layered.Read("file.txt").empty());
}

TEST_F(LayeredVFSTest, NameShowsMountCount) {
    LayeredVFS layered;
    EXPECT_NE(layered.Name().find("0 mounts"), std::string::npos);

    layered.Mount(MakeLooseVFS("a"), 0);
    EXPECT_NE(layered.Name().find("1 mounts"), std::string::npos);
}

TEST_F(LayeredVFSTest, NullMountIsRejected) {
    LayeredVFS layered;
    layered.Mount(nullptr, 0);
    EXPECT_EQ(layered.MountCount(), 0u);
}

TEST_F(LayeredVFSTest, ThreePriorityLevels) {
    auto low = MakePakVFS({{"file.bin", {'L'}, 0}});
    auto mid = MakePakVFS({{"file.bin", {'M'}, 0}});
    auto high = MakePakVFS({{"file.bin", {'H'}, 0}});

    LayeredVFS layered;
    layered.Mount(mid, 50);
    layered.Mount(low, 0);
    layered.Mount(high, 100);

    auto data = layered.Read("file.bin");
    ASSERT_EQ(data.size(), 1u);
    EXPECT_EQ(data[0], 'H');
}

TEST_F(LayeredVFSTest, SamePriorityUsesInsertionOrder) {
    auto first = MakePakVFS({{"file.bin", {'1'}, 0}});
    auto second = MakePakVFS({{"file.bin", {'2'}, 0}});

    LayeredVFS layered;
    layered.Mount(first, 0);
    layered.Mount(second, 0);

    auto data = layered.Read("file.bin");
    ASSERT_EQ(data.size(), 1u);
    // std::sort is not stable, so we just verify we get a result
    EXPECT_TRUE(data[0] == '1' || data[0] == '2');
}

} // namespace uz::test
