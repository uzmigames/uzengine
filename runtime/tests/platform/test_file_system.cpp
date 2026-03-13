/// Unit tests for UzEngine Platform File I/O (4.6.0).
///
/// Covers: FileSystem read/write/query/directory operations, MemoryMappedFile
/// lifecycle and content validation, and DirectoryWatcher start/stop lifecycle.
///
/// All tests use a per-fixture temporary directory created under the system
/// temp path. TearDown removes the directory tree so no artifacts remain on
/// disk after the suite runs.

#include "file_system.h"
#include "memory_mapped_file.h"
#include "file_watcher.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns an absolute UTF-8 path string for a child of `base`.
static std::string Join(const fs::path& base, const char* child) {
    return (base / child).string();
}

// ─────────────────────────────────────────────────────────────────────────────
// FileSystemTest fixture — creates a unique temp directory, removes it after
// ─────────────────────────────────────────────────────────────────────────────

class FileSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Silence engine log output during tests (errors are still visible via
        // gtest assertion messages).
        fs::path logPath = fs::temp_directory_path() / "uzengine_fs_test.log";
        uz::LogInit(logPath.string().c_str());
        uz::LogSetLevel(uz::LogLevel::Error);

        // Unique directory per test run to avoid cross-test interference.
        m_testDir = fs::temp_directory_path() /
                    ("uzengine_fs_test_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(m_testDir);
    }

    void TearDown() override {
        // Remove every artifact created during the test.
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
        uz::LogShutdown();
    }

    fs::path m_testDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// FileWriteAll + FileReadAll round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, WriteAll_ReadAll_RoundTripMatchesBinary) {
    const std::string path = Join(m_testDir, "binary.bin");
    const uint8_t kData[] = { 0x00, 0xFF, 0x42, 0x13, 0x37, 0xDE, 0xAD, 0xBE, 0xEF };
    const size_t kSize = sizeof(kData);

    const uz::FileResult writeResult = uz::FileWriteAll(path.c_str(), kData, kSize);
    ASSERT_EQ(writeResult, uz::FileResult::Success)
        << "FileWriteAll should succeed for a new file in a writable directory";

    const std::vector<uint8_t> readBack = uz::FileReadAll(path.c_str());
    ASSERT_EQ(readBack.size(), kSize)
        << "FileReadAll should return exactly " << kSize << " bytes";

    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(readBack[i], kData[i])
            << "Byte mismatch at index " << i;
    }
}

TEST_F(FileSystemTest, WriteAll_EmptyBuffer_CreatesZeroByteFile) {
    const std::string path = Join(m_testDir, "empty.bin");

    const uz::FileResult result = uz::FileWriteAll(path.c_str(), nullptr, 0);
    ASSERT_EQ(result, uz::FileResult::Success);

    EXPECT_TRUE(uz::FileExists(path.c_str()));
    EXPECT_EQ(uz::FileGetSize(path.c_str()), 0);

    const std::vector<uint8_t> readBack = uz::FileReadAll(path.c_str());
    EXPECT_TRUE(readBack.empty());
}

TEST_F(FileSystemTest, WriteAll_OverwritesExistingFile_ContentIsReplaced) {
    const std::string path = Join(m_testDir, "overwrite.bin");

    const uint8_t kOriginal[] = { 1, 2, 3, 4, 5 };
    uz::FileWriteAll(path.c_str(), kOriginal, sizeof(kOriginal));

    const uint8_t kReplacement[] = { 9, 8 };
    const uz::FileResult result = uz::FileWriteAll(path.c_str(), kReplacement, sizeof(kReplacement));
    ASSERT_EQ(result, uz::FileResult::Success);

    const std::vector<uint8_t> readBack = uz::FileReadAll(path.c_str());
    ASSERT_EQ(readBack.size(), 2u);
    EXPECT_EQ(readBack[0], 9u);
    EXPECT_EQ(readBack[1], 8u);
}

// ─────────────────────────────────────────────────────────────────────────────
// FileWriteAllText + FileReadAllText round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, WriteAllText_ReadAllText_RoundTripMatchesString) {
    const std::string path = Join(m_testDir, "hello.txt");
    const char* kText = "Hello, UzEngine!\nLine 2\n";

    const uz::FileResult result = uz::FileWriteAllText(path.c_str(), kText);
    ASSERT_EQ(result, uz::FileResult::Success);

    const std::string readBack = uz::FileReadAllText(path.c_str());
    EXPECT_EQ(readBack, std::string(kText));
}

TEST_F(FileSystemTest, WriteAllText_EmptyString_CreatesZeroByteFile) {
    const std::string path = Join(m_testDir, "empty.txt");

    const uz::FileResult result = uz::FileWriteAllText(path.c_str(), "");
    ASSERT_EQ(result, uz::FileResult::Success);

    EXPECT_EQ(uz::FileGetSize(path.c_str()), 0);
    EXPECT_TRUE(uz::FileReadAllText(path.c_str()).empty());
}

TEST_F(FileSystemTest, WriteAllText_DoesNotWriteNullTerminator) {
    const std::string path = Join(m_testDir, "no_null.txt");
    const char* kText = "abc";

    uz::FileWriteAllText(path.c_str(), kText);

    EXPECT_EQ(uz::FileGetSize(path.c_str()), 3)
        << "FileWriteAllText must write strlen bytes only — no null terminator";
}

TEST_F(FileSystemTest, ReadAllText_NonExistentFile_ReturnsEmptyString) {
    const std::string path = Join(m_testDir, "does_not_exist.txt");
    const std::string result = uz::FileReadAllText(path.c_str());
    EXPECT_TRUE(result.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// FileExists
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, FileExists_ExistingFile_ReturnsTrue) {
    const std::string path = Join(m_testDir, "exists.txt");
    uz::FileWriteAllText(path.c_str(), "data");

    EXPECT_TRUE(uz::FileExists(path.c_str()));
}

TEST_F(FileSystemTest, FileExists_NonExistentPath_ReturnsFalse) {
    const std::string path = Join(m_testDir, "ghost.txt");
    EXPECT_FALSE(uz::FileExists(path.c_str()));
}

TEST_F(FileSystemTest, FileExists_DirectoryPath_ReturnsTrue) {
    // FileExists must return true for directories as well as regular files.
    EXPECT_TRUE(uz::FileExists(m_testDir.string().c_str()));
}

// ─────────────────────────────────────────────────────────────────────────────
// FileGetSize
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, FileGetSize_AfterWrite_ReturnsCorrectByteCount) {
    const std::string path = Join(m_testDir, "sized.bin");
    const uint8_t kData[17] = {};  // 17 zero bytes

    uz::FileWriteAll(path.c_str(), kData, sizeof(kData));

    EXPECT_EQ(uz::FileGetSize(path.c_str()), 17);
}

TEST_F(FileSystemTest, FileGetSize_EmptyFile_ReturnsZero) {
    const std::string path = Join(m_testDir, "zero.bin");
    uz::FileWriteAll(path.c_str(), nullptr, 0);

    EXPECT_EQ(uz::FileGetSize(path.c_str()), 0);
}

TEST_F(FileSystemTest, FileGetSize_NonExistentFile_ReturnsNegativeOne) {
    const std::string path = Join(m_testDir, "no_such_file.bin");
    EXPECT_EQ(uz::FileGetSize(path.c_str()), -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// FileDelete
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, FileDelete_ExistingFile_RemovesFile) {
    const std::string path = Join(m_testDir, "todelete.txt");
    uz::FileWriteAllText(path.c_str(), "bye");
    ASSERT_TRUE(uz::FileExists(path.c_str()));

    const uz::FileResult result = uz::FileDelete(path.c_str());
    ASSERT_EQ(result, uz::FileResult::Success);

    EXPECT_FALSE(uz::FileExists(path.c_str()))
        << "FileExists must return false after FileDelete succeeds";
}

TEST_F(FileSystemTest, FileDelete_NonExistentFile_ReturnsNotFound) {
    const std::string path = Join(m_testDir, "phantom.txt");
    ASSERT_FALSE(uz::FileExists(path.c_str()));

    const uz::FileResult result = uz::FileDelete(path.c_str());
    EXPECT_EQ(result, uz::FileResult::NotFound);
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryCreate
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, DirectoryCreate_NewNestedPath_CreatesAllSegments) {
    const std::string path = Join(m_testDir, "a/b/c/deep");

    const uz::FileResult result = uz::DirectoryCreate(path.c_str());
    ASSERT_EQ(result, uz::FileResult::Success);

    EXPECT_TRUE(uz::IsDirectory(path.c_str()));
}

TEST_F(FileSystemTest, DirectoryCreate_AlreadyExists_ReturnsSuccess) {
    // Idempotent: calling twice must still return Success.
    const std::string path = Join(m_testDir, "idempotent_dir");
    ASSERT_EQ(uz::DirectoryCreate(path.c_str()), uz::FileResult::Success);

    const uz::FileResult second = uz::DirectoryCreate(path.c_str());
    EXPECT_EQ(second, uz::FileResult::Success);
}

TEST_F(FileSystemTest, WriteAll_AutoCreatesParentDirectories) {
    // FileWriteAll documentation: "Parent directories are created recursively."
    const std::string path = Join(m_testDir, "auto/parent/creation/file.bin");
    const uint8_t kByte = 0xAB;

    const uz::FileResult result = uz::FileWriteAll(path.c_str(), &kByte, 1);
    ASSERT_EQ(result, uz::FileResult::Success);

    EXPECT_TRUE(uz::FileExists(path.c_str()));
}

// ─────────────────────────────────────────────────────────────────────────────
// IsDirectory
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, IsDirectory_OnDirectory_ReturnsTrue) {
    const std::string path = Join(m_testDir, "isdir_test");
    uz::DirectoryCreate(path.c_str());

    EXPECT_TRUE(uz::IsDirectory(path.c_str()));
}

TEST_F(FileSystemTest, IsDirectory_OnFile_ReturnsFalse) {
    const std::string path = Join(m_testDir, "regular_file.txt");
    uz::FileWriteAllText(path.c_str(), "content");

    EXPECT_FALSE(uz::IsDirectory(path.c_str()));
}

TEST_F(FileSystemTest, IsDirectory_NonExistentPath_ReturnsFalse) {
    const std::string path = Join(m_testDir, "no_such_dir");
    EXPECT_FALSE(uz::IsDirectory(path.c_str()));
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryEnumerate — pattern filtering
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, DirectoryEnumerate_TxtPattern_ReturnsOnlyTxtFiles) {
    // Create a mix of .txt and .bin files in a subdirectory.
    const std::string subdir = Join(m_testDir, "enum_test");
    uz::DirectoryCreate(subdir.c_str());

    const std::string txt1 = subdir + "/file1.txt";
    const std::string txt2 = subdir + "/file2.txt";
    const std::string bin1 = subdir + "/data.bin";
    const std::string xml1 = subdir + "/config.xml";

    uz::FileWriteAllText(txt1.c_str(), "a");
    uz::FileWriteAllText(txt2.c_str(), "b");
    uz::FileWriteAllText(bin1.c_str(), "c");
    uz::FileWriteAllText(xml1.c_str(), "d");

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(subdir.c_str(), "*.txt");

    ASSERT_EQ(entries.size(), 2u)
        << "DirectoryEnumerate(\"*.txt\") must return exactly 2 entries";

    for (const auto& e : entries) {
        const std::string entryPath(e.path);
        EXPECT_NE(entryPath.find(".txt"), std::string::npos)
            << "Non-.txt entry returned: " << e.path;
    }
}

TEST_F(FileSystemTest, DirectoryEnumerate_StarPattern_ReturnsAllEntries) {
    const std::string subdir = Join(m_testDir, "enum_all");
    uz::DirectoryCreate(subdir.c_str());

    uz::FileWriteAllText((subdir + "/a.txt").c_str(), "");
    uz::FileWriteAllText((subdir + "/b.bin").c_str(), "");
    uz::FileWriteAllText((subdir + "/c.xml").c_str(), "");

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(subdir.c_str(), "*");

    EXPECT_EQ(entries.size(), 3u);
}

TEST_F(FileSystemTest, DirectoryEnumerate_EmptyDirectory_ReturnsEmptyVector) {
    const std::string subdir = Join(m_testDir, "empty_enum");
    uz::DirectoryCreate(subdir.c_str());

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(subdir.c_str(), "*");

    EXPECT_TRUE(entries.empty());
}

TEST_F(FileSystemTest, DirectoryEnumerate_NonExistentDirectory_ReturnsEmptyVector) {
    const std::string path = Join(m_testDir, "no_such_dir");
    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(path.c_str(), "*");

    EXPECT_TRUE(entries.empty());
}

TEST_F(FileSystemTest, DirectoryEnumerate_IsNonRecursive_DoesNotDescendSubdirs) {
    // Put a file inside a subdirectory — DirectoryEnumerate must NOT find it.
    const std::string parent = Join(m_testDir, "shallow_parent");
    const std::string nested = parent + "/subdir";
    uz::DirectoryCreate(nested.c_str());
    uz::FileWriteAllText((nested + "/deep.txt").c_str(), "deep");

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(parent.c_str(), "*.txt");

    EXPECT_TRUE(entries.empty())
        << "DirectoryEnumerate must not recurse into subdirectories";
}

TEST_F(FileSystemTest, DirectoryEnumerate_FileEntry_SizeFieldIsCorrect) {
    const std::string subdir = Join(m_testDir, "entry_size");
    uz::DirectoryCreate(subdir.c_str());
    const char* kText = "hello";  // 5 bytes
    uz::FileWriteAllText((subdir + "/test.txt").c_str(), kText);

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(subdir.c_str(), "*.txt");

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].size, 5)
        << "FileEntry::size must equal the number of bytes in the file";
    EXPECT_FALSE(entries[0].isDirectory);
}

TEST_F(FileSystemTest, DirectoryEnumerate_DirectoryEntry_IsDirectoryFlagSet) {
    const std::string parent = Join(m_testDir, "dir_entry_flag");
    const std::string child  = parent + "/subdir";
    uz::DirectoryCreate(parent.c_str());
    uz::DirectoryCreate(child.c_str());

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerate(parent.c_str(), "*");

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].isDirectory)
        << "FileEntry::isDirectory must be true for a directory entry";
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryEnumerateRecursive
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, DirectoryEnumerateRecursive_FindsFilesInSubdirectories) {
    // Structure:
    //   recurse_root/
    //     root_file.txt
    //     sub/
    //       nested_file.txt
    //     sub/sub2/
    //       deeply_nested.txt
    const std::string root = Join(m_testDir, "recurse_root");
    uz::DirectoryCreate((root + "/sub/sub2").c_str());

    uz::FileWriteAllText((root + "/root_file.txt").c_str(), "r");
    uz::FileWriteAllText((root + "/sub/nested_file.txt").c_str(), "n");
    uz::FileWriteAllText((root + "/sub/sub2/deeply_nested.txt").c_str(), "d");

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerateRecursive(root.c_str(), "*.txt");

    EXPECT_EQ(entries.size(), 3u)
        << "DirectoryEnumerateRecursive must find all .txt files across all depths";
}

TEST_F(FileSystemTest, DirectoryEnumerateRecursive_PatternFilters_OnlyMatchingFilesReturned) {
    const std::string root = Join(m_testDir, "recurse_filter");
    uz::DirectoryCreate((root + "/sub").c_str());

    uz::FileWriteAllText((root + "/keep.txt").c_str(), "");
    uz::FileWriteAllText((root + "/skip.bin").c_str(), "");
    uz::FileWriteAllText((root + "/sub/keep2.txt").c_str(), "");
    uz::FileWriteAllText((root + "/sub/skip2.bin").c_str(), "");

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerateRecursive(root.c_str(), "*.txt");

    ASSERT_EQ(entries.size(), 2u);
    for (const auto& e : entries) {
        EXPECT_NE(std::string(e.path).find(".txt"), std::string::npos)
            << "Non-.txt file returned by recursive enumeration: " << e.path;
    }
}

TEST_F(FileSystemTest, DirectoryEnumerateRecursive_EmptyDirectory_ReturnsEmptyVector) {
    const std::string root = Join(m_testDir, "recurse_empty");
    uz::DirectoryCreate(root.c_str());

    const std::vector<uz::FileEntry> entries =
        uz::DirectoryEnumerateRecursive(root.c_str(), "*");

    EXPECT_TRUE(entries.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform path queries
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, GetUserDataDir_ReturnsNonEmptyString) {
    const std::string dir = uz::GetUserDataDir();
    EXPECT_FALSE(dir.empty())
        << "GetUserDataDir must return a non-empty path on Windows";
}

TEST_F(FileSystemTest, GetUserDataDir_ReturnsExistingDirectory) {
    const std::string dir = uz::GetUserDataDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_TRUE(uz::IsDirectory(dir.c_str()))
        << "GetUserDataDir must create the directory if absent: " << dir;
}

TEST_F(FileSystemTest, GetAppDataDir_ReturnsNonEmptyString) {
    const std::string dir = uz::GetAppDataDir();
    EXPECT_FALSE(dir.empty())
        << "GetAppDataDir must return a non-empty path on Windows";
}

TEST_F(FileSystemTest, GetAppDataDir_ReturnsExistingDirectory) {
    const std::string dir = uz::GetAppDataDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_TRUE(uz::IsDirectory(dir.c_str()))
        << "GetAppDataDir must create the directory if absent: " << dir;
}

TEST_F(FileSystemTest, GetTempDir_ReturnsNonEmptyString) {
    const std::string dir = uz::GetTempDir();
    EXPECT_FALSE(dir.empty())
        << "GetTempDir must return a non-empty path on Windows";
}

TEST_F(FileSystemTest, GetTempDir_ReturnsExistingDirectory) {
    const std::string dir = uz::GetTempDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_TRUE(uz::IsDirectory(dir.c_str()))
        << "GetTempDir must create the directory if absent: " << dir;
}

TEST_F(FileSystemTest, GetUserDataDir_ContainsUzEngine) {
    const std::string dir = uz::GetUserDataDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_NE(dir.find("UzEngine"), std::string::npos)
        << "GetUserDataDir must resolve to a path containing 'UzEngine': " << dir;
}

TEST_F(FileSystemTest, GetAppDataDir_ContainsUzEngine) {
    const std::string dir = uz::GetAppDataDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_NE(dir.find("UzEngine"), std::string::npos)
        << "GetAppDataDir must resolve to a path containing 'UzEngine': " << dir;
}

TEST_F(FileSystemTest, GetTempDir_ContainsUzEngine) {
    const std::string dir = uz::GetTempDir();
    ASSERT_FALSE(dir.empty());
    EXPECT_NE(dir.find("UzEngine"), std::string::npos)
        << "GetTempDir must resolve to a path containing 'UzEngine': " << dir;
}

// ─────────────────────────────────────────────────────────────────────────────
// FileResultToString
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, FileResultToString_Success_ReturnsNonNullString) {
    const char* s = uz::FileResultToString(uz::FileResult::Success);
    ASSERT_NE(s, nullptr);
    EXPECT_GT(std::strlen(s), 0u);
}

TEST_F(FileSystemTest, FileResultToString_AllValues_ReturnNonNullNonEmptyStrings) {
    const uz::FileResult values[] = {
        uz::FileResult::Success,
        uz::FileResult::NotFound,
        uz::FileResult::AccessDenied,
        uz::FileResult::AlreadyExists,
        uz::FileResult::IoError,
    };
    for (auto v : values) {
        const char* s = uz::FileResultToString(v);
        ASSERT_NE(s, nullptr) << "FileResultToString returned nullptr for value " << (int)v;
        EXPECT_GT(std::strlen(s), 0u)
            << "FileResultToString returned empty string for value " << (int)v;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FileMode operator helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FileSystemTest, FileModeOperator_OrCombinesFlags) {
    const uz::FileMode combined =
        uz::FileMode::Write | uz::FileMode::Create | uz::FileMode::Truncate;

    EXPECT_TRUE(uz::HasFlag(combined, uz::FileMode::Write));
    EXPECT_TRUE(uz::HasFlag(combined, uz::FileMode::Create));
    EXPECT_TRUE(uz::HasFlag(combined, uz::FileMode::Truncate));
    EXPECT_FALSE(uz::HasFlag(combined, uz::FileMode::Read));
    EXPECT_FALSE(uz::HasFlag(combined, uz::FileMode::Append));
}

TEST_F(FileSystemTest, FileModeOperator_AndMasksFlags) {
    const uz::FileMode combined = uz::FileMode::Read | uz::FileMode::Write;
    const uz::FileMode masked   = combined & uz::FileMode::Read;

    EXPECT_TRUE(uz::HasFlag(masked, uz::FileMode::Read));
    EXPECT_FALSE(uz::HasFlag(masked, uz::FileMode::Write));
}

TEST_F(FileSystemTest, HasFlag_WithMatchingFlag_ReturnsTrue) {
    // A single-flag value must satisfy HasFlag for that exact flag.
    EXPECT_TRUE(uz::HasFlag(uz::FileMode::Read,     uz::FileMode::Read));
    EXPECT_TRUE(uz::HasFlag(uz::FileMode::Write,    uz::FileMode::Write));
    EXPECT_TRUE(uz::HasFlag(uz::FileMode::Append,   uz::FileMode::Append));
    EXPECT_TRUE(uz::HasFlag(uz::FileMode::Create,   uz::FileMode::Create));
    EXPECT_TRUE(uz::HasFlag(uz::FileMode::Truncate, uz::FileMode::Truncate));
}

TEST_F(FileSystemTest, HasFlag_WithoutMatchingFlag_ReturnsFalse) {
    // A single-flag value must NOT satisfy HasFlag for an unrelated flag.
    EXPECT_FALSE(uz::HasFlag(uz::FileMode::Read,  uz::FileMode::Write));
    EXPECT_FALSE(uz::HasFlag(uz::FileMode::Write, uz::FileMode::Read));
    EXPECT_FALSE(uz::HasFlag(uz::FileMode::Read,  uz::FileMode::Append));
    EXPECT_FALSE(uz::HasFlag(uz::FileMode::Read,  uz::FileMode::Create));
    EXPECT_FALSE(uz::HasFlag(uz::FileMode::Read,  uz::FileMode::Truncate));
}

TEST_F(FileSystemTest, HasFlag_CombinedFlags_ReturnsTrueForEach) {
    // All five flags combined must individually satisfy HasFlag.
    const uz::FileMode all = uz::FileMode::Read
                           | uz::FileMode::Write
                           | uz::FileMode::Append
                           | uz::FileMode::Create
                           | uz::FileMode::Truncate;

    EXPECT_TRUE(uz::HasFlag(all, uz::FileMode::Read));
    EXPECT_TRUE(uz::HasFlag(all, uz::FileMode::Write));
    EXPECT_TRUE(uz::HasFlag(all, uz::FileMode::Append));
    EXPECT_TRUE(uz::HasFlag(all, uz::FileMode::Create));
    EXPECT_TRUE(uz::HasFlag(all, uz::FileMode::Truncate));
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryMappedFile fixture — extends FileSystemTest, nothing extra needed
// ─────────────────────────────────────────────────────────────────────────────

class MemoryMappedFileTest : public FileSystemTest {};

// ─────────────────────────────────────────────────────────────────────────────
// MemoryMappedFile — Open / IsOpen / Data / Size
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MemoryMappedFileTest, Open_ExistingFile_Succeeds_DataIsNonNull) {
    const std::string path = Join(m_testDir, "mmap_open.bin");
    const uint8_t kBytes[] = { 0xCA, 0xFE, 0xBA, 0xBE };
    uz::FileWriteAll(path.c_str(), kBytes, sizeof(kBytes));

    uz::MemoryMappedFile mmap;
    const bool ok = mmap.Open(path.c_str());

    ASSERT_TRUE(ok) << "Open must return true for an existing file";
    EXPECT_TRUE(mmap.IsOpen());
    EXPECT_NE(mmap.Data(), nullptr)
        << "Data() must be non-null after a successful Open";
}

TEST_F(MemoryMappedFileTest, Open_ExistingFile_SizeMatchesFileGetSize) {
    const std::string path = Join(m_testDir, "mmap_size.bin");
    const uint8_t kBytes[64] = {};
    uz::FileWriteAll(path.c_str(), kBytes, sizeof(kBytes));

    uz::MemoryMappedFile mmap;
    ASSERT_TRUE(mmap.Open(path.c_str()));

    EXPECT_EQ(mmap.Size(), static_cast<size_t>(uz::FileGetSize(path.c_str())))
        << "MemoryMappedFile::Size() must equal FileGetSize() for the same path";
}

TEST_F(MemoryMappedFileTest, Open_ContentMatchesFileReadAll) {
    const std::string path = Join(m_testDir, "mmap_content.bin");
    const uint8_t kPayload[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
    uz::FileWriteAll(path.c_str(), kPayload, sizeof(kPayload));

    uz::MemoryMappedFile mmap;
    ASSERT_TRUE(mmap.Open(path.c_str()));
    ASSERT_EQ(mmap.Size(), sizeof(kPayload));

    const auto* mapped = static_cast<const uint8_t*>(mmap.Data());
    for (size_t i = 0; i < sizeof(kPayload); ++i) {
        EXPECT_EQ(mapped[i], kPayload[i])
            << "Mapped byte mismatch at index " << i;
    }
}

TEST_F(MemoryMappedFileTest, Open_NonExistentFile_ReturnsFalse) {
    const std::string path = Join(m_testDir, "no_such_mmap.bin");

    uz::MemoryMappedFile mmap;
    const bool ok = mmap.Open(path.c_str());

    EXPECT_FALSE(ok) << "Open must return false for a non-existent file";
    EXPECT_FALSE(mmap.IsOpen());
    EXPECT_EQ(mmap.Data(), nullptr);
    EXPECT_EQ(mmap.Size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryMappedFile — Close
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MemoryMappedFileTest, Close_AfterOpen_IsOpenReturnsFalse) {
    const std::string path = Join(m_testDir, "mmap_close.bin");
    const uint8_t kByte = 0x01;
    uz::FileWriteAll(path.c_str(), &kByte, 1);

    uz::MemoryMappedFile mmap;
    ASSERT_TRUE(mmap.Open(path.c_str()));
    ASSERT_TRUE(mmap.IsOpen());

    mmap.Close();

    EXPECT_FALSE(mmap.IsOpen());
    EXPECT_EQ(mmap.Data(), nullptr)
        << "Data() must return nullptr after Close()";
    EXPECT_EQ(mmap.Size(), 0u)
        << "Size() must return 0 after Close()";
}

TEST_F(MemoryMappedFileTest, Close_WhenNotOpen_IsNoOp) {
    uz::MemoryMappedFile mmap;
    ASSERT_FALSE(mmap.IsOpen());

    // Must not crash or assert.
    mmap.Close();
    mmap.Close();  // second call also safe

    EXPECT_FALSE(mmap.IsOpen());
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryMappedFile — move semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MemoryMappedFileTest, MoveConstructor_TransfersOwnership) {
    const std::string path = Join(m_testDir, "mmap_move.bin");
    const uint8_t kBytes[] = { 0xAA, 0xBB, 0xCC };
    uz::FileWriteAll(path.c_str(), kBytes, sizeof(kBytes));

    uz::MemoryMappedFile original;
    ASSERT_TRUE(original.Open(path.c_str()));
    const void* originalData = original.Data();
    const size_t originalSize = original.Size();

    uz::MemoryMappedFile moved(std::move(original));

    // Moved-into object must own the mapping.
    EXPECT_TRUE(moved.IsOpen());
    EXPECT_EQ(moved.Data(), originalData)
        << "Move constructor must transfer the data pointer, not copy data";
    EXPECT_EQ(moved.Size(), originalSize);

    // Source object must be empty after move.
    EXPECT_FALSE(original.IsOpen());
    EXPECT_EQ(original.Data(), nullptr);
    EXPECT_EQ(original.Size(), 0u);
}

TEST_F(MemoryMappedFileTest, MoveAssignment_TransfersOwnership_ClosesTarget) {
    const std::string pathA = Join(m_testDir, "mmap_move_a.bin");
    const std::string pathB = Join(m_testDir, "mmap_move_b.bin");

    const uint8_t kA[] = { 1 };
    const uint8_t kB[] = { 2, 3 };
    uz::FileWriteAll(pathA.c_str(), kA, sizeof(kA));
    uz::FileWriteAll(pathB.c_str(), kB, sizeof(kB));

    uz::MemoryMappedFile mmapA;
    uz::MemoryMappedFile mmapB;
    ASSERT_TRUE(mmapA.Open(pathA.c_str()));
    ASSERT_TRUE(mmapB.Open(pathB.c_str()));

    // Assign B into A — A's previous mapping must be released.
    mmapA = std::move(mmapB);

    EXPECT_TRUE(mmapA.IsOpen());
    EXPECT_EQ(mmapA.Size(), sizeof(kB));

    EXPECT_FALSE(mmapB.IsOpen());
    EXPECT_EQ(mmapB.Data(), nullptr);
}

TEST_F(MemoryMappedFileTest, Destructor_WhenOpen_ReleasesWithoutCrash) {
    const std::string path = Join(m_testDir, "mmap_dtor.bin");
    const uint8_t kByte = 0xFF;
    uz::FileWriteAll(path.c_str(), &kByte, 1);

    {
        uz::MemoryMappedFile mmap;
        ASSERT_TRUE(mmap.Open(path.c_str()));
        // Destructor fires here — must not crash.
    }

    // We are still alive; test passes.
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryMappedFile — reopen behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MemoryMappedFileTest, Open_WhileAlreadyOpen_ClosesPreviousThenOpens) {
    const std::string pathA = Join(m_testDir, "mmap_reopen_a.bin");
    const std::string pathB = Join(m_testDir, "mmap_reopen_b.bin");
    const uint8_t kA[] = { 0x11 };
    const uint8_t kB[] = { 0x22, 0x33 };
    uz::FileWriteAll(pathA.c_str(), kA, sizeof(kA));
    uz::FileWriteAll(pathB.c_str(), kB, sizeof(kB));

    uz::MemoryMappedFile mmap;
    ASSERT_TRUE(mmap.Open(pathA.c_str()));
    EXPECT_EQ(mmap.Size(), 1u);

    // Opening a second file must implicitly close the first.
    const bool ok = mmap.Open(pathB.c_str());
    ASSERT_TRUE(ok);
    EXPECT_EQ(mmap.Size(), 2u)
        << "After reopening a different file the size must reflect the new file";
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryWatcher fixture
// ─────────────────────────────────────────────────────────────────────────────

class DirectoryWatcherTest : public FileSystemTest {};

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryWatcher — lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DirectoryWatcherTest, DefaultConstructed_IsWatchingReturnsFalse) {
    uz::DirectoryWatcher watcher;
    EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(DirectoryWatcherTest, Start_ValidDirectory_ReturnsTrueAndIsWatching) {
    uz::DirectoryWatcher watcher;

    const bool ok = watcher.Start(
        m_testDir.string().c_str(),
        /*recursive=*/false,
        [](const uz::FileWatchNotification&, void*) {},
        nullptr
    );

    ASSERT_TRUE(ok) << "Start must return true for a valid, existing directory";
    EXPECT_TRUE(watcher.IsWatching());

    watcher.Stop();
}

TEST_F(DirectoryWatcherTest, Start_NonExistentDirectory_ReturnsFalse) {
    const std::string path = Join(m_testDir, "does_not_exist_for_watcher");

    uz::DirectoryWatcher watcher;
    const bool ok = watcher.Start(
        path.c_str(),
        /*recursive=*/false,
        [](const uz::FileWatchNotification&, void*) {},
        nullptr
    );

    EXPECT_FALSE(ok) << "Start must return false for a non-existent directory";
    EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(DirectoryWatcherTest, Stop_AfterStart_IsWatchingReturnsFalse) {
    uz::DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        m_testDir.string().c_str(), false,
        [](const uz::FileWatchNotification&, void*) {}, nullptr));

    watcher.Stop();

    EXPECT_FALSE(watcher.IsWatching())
        << "IsWatching must return false after Stop()";
}

TEST_F(DirectoryWatcherTest, Stop_WhenNotWatching_IsNoOp) {
    uz::DirectoryWatcher watcher;
    ASSERT_FALSE(watcher.IsWatching());

    // Must not crash.
    watcher.Stop();
    watcher.Stop();

    EXPECT_FALSE(watcher.IsWatching());
    SUCCEED();
}

TEST_F(DirectoryWatcherTest, StartStop_MultipleRounds_AreStable) {
    uz::DirectoryWatcher watcher;
    const std::string dir = m_testDir.string();

    for (int i = 0; i < 3; ++i) {
        const bool ok = watcher.Start(
            dir.c_str(), false,
            [](const uz::FileWatchNotification&, void*) {}, nullptr);
        ASSERT_TRUE(ok) << "Start failed on iteration " << i;
        EXPECT_TRUE(watcher.IsWatching());
        watcher.Stop();
        EXPECT_FALSE(watcher.IsWatching());
    }
}

TEST_F(DirectoryWatcherTest, Destructor_WhileWatching_JoinsThreadWithoutCrash) {
    // The destructor must stop the background thread and not crash.
    {
        uz::DirectoryWatcher watcher;
        ASSERT_TRUE(watcher.Start(
            m_testDir.string().c_str(), false,
            [](const uz::FileWatchNotification&, void*) {}, nullptr));
        // Destructor fires here.
    }
    SUCCEED();
}

TEST_F(DirectoryWatcherTest, Start_Recursive_ReturnsTrueAndIsWatching) {
    uz::DirectoryWatcher watcher;

    const bool ok = watcher.Start(
        m_testDir.string().c_str(),
        /*recursive=*/true,
        [](const uz::FileWatchNotification&, void*) {},
        nullptr
    );

    ASSERT_TRUE(ok) << "Start with recursive=true must succeed for a valid directory";
    EXPECT_TRUE(watcher.IsWatching());

    watcher.Stop();
}

TEST_F(DirectoryWatcherTest, Start_ReplacesExistingWatch_WithoutCrash) {
    // Calling Start again while already watching must stop the first watch
    // and start a new one (documented: "stops the previous watch first").
    uz::DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        m_testDir.string().c_str(), false,
        [](const uz::FileWatchNotification&, void*) {}, nullptr));

    // Start again — should not crash or deadlock.
    const bool ok = watcher.Start(
        m_testDir.string().c_str(), false,
        [](const uz::FileWatchNotification&, void*) {}, nullptr);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(watcher.IsWatching());

    watcher.Stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectoryWatcher — notification delivery (timing-sensitive)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(DirectoryWatcherTest, Callback_FileCreated_NotificationDelivered) {
    std::atomic<bool> received{false};
    uz::FileWatchEvent receivedEvent{};
    char receivedPath[512] = {};

    uz::DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        m_testDir.string().c_str(),
        /*recursive=*/false,
        [](const uz::FileWatchNotification& n, void* userData) {
            auto* state = static_cast<std::pair<std::atomic<bool>*, uz::FileWatchEvent*>*>(userData);
            *state->second = n.event;
            *state->first  = true;
        },
        nullptr
    ));

    // We use a simpler callback form with a struct to avoid capturing.
    watcher.Stop();

    // Re-start with a capturing-friendly approach via a struct.
    struct CallbackState {
        std::atomic<bool> fired{false};
        uz::FileWatchEvent event{};
        char path[512]{};
    } state;

    ASSERT_TRUE(watcher.Start(
        m_testDir.string().c_str(),
        /*recursive=*/false,
        [](const uz::FileWatchNotification& n, void* ud) {
            auto* s = static_cast<CallbackState*>(ud);
            s->event = n.event;
            std::strncpy(s->path, n.path, sizeof(s->path) - 1);
            s->fired.store(true, std::memory_order_release);
        },
        &state
    ));

    // Trigger a file creation event.
    const std::string newFile = Join(m_testDir, "watch_trigger.txt");
    uz::FileWriteAllText(newFile.c_str(), "trigger");

    // Give the background thread up to 2 seconds to deliver the notification.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!state.fired.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    watcher.Stop();

    EXPECT_TRUE(state.fired.load())
        << "DirectoryWatcher did not deliver a notification within 2 seconds";
}

TEST_F(DirectoryWatcherTest, Callback_FileModified_NotificationDelivered) {
    // Create the file before starting the watcher so that the subsequent
    // write triggers a Modified event rather than a Created event.
    const std::string targetFile = Join(m_testDir, "watch_modified.txt");
    uz::FileWriteAllText(targetFile.c_str(), "initial");

    struct CallbackState {
        std::atomic<bool> fired{false};
        uz::FileWatchEvent event{};
        char path[512]{};
    } state;

    uz::DirectoryWatcher watcher;
    ASSERT_TRUE(watcher.Start(
        m_testDir.string().c_str(),
        /*recursive=*/false,
        [](const uz::FileWatchNotification& n, void* ud) {
            auto* s = static_cast<CallbackState*>(ud);
            // Only capture the first Modified event; ignore any stray Created
            // notifications that may arrive for the same file.
            if (n.event == uz::FileWatchEvent::Modified && !s->fired.load(std::memory_order_relaxed)) {
                s->event = n.event;
                std::strncpy(s->path, n.path, sizeof(s->path) - 1);
                s->fired.store(true, std::memory_order_release);
            }
        },
        &state
    ));

    // Overwrite the existing file to trigger a Modified event.
    uz::FileWriteAllText(targetFile.c_str(), "modified content");

    // Give the background thread up to 2 seconds to deliver the notification.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!state.fired.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    watcher.Stop();

    ASSERT_TRUE(state.fired.load())
        << "DirectoryWatcher did not deliver a Modified notification within 2 seconds";
    EXPECT_EQ(state.event, uz::FileWatchEvent::Modified)
        << "Expected Modified event but got a different event type";
    EXPECT_GT(std::strlen(state.path), 0u)
        << "Notification path must be non-empty for a Modified event";
}
