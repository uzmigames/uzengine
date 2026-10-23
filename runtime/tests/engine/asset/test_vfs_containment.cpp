/// UzEngine Virtual File System — path containment tests
///
/// Asset and script paths are untrusted content. These tests pin down that
/// `NormalizeVfsPath` and `LooseFileVFS::ResolvePath` keep every such path
/// below the VFS root (lexically and through on-disk links), that pak names
/// from a master index are plain file names, and that legitimate nested paths
/// keep resolving.

#include "vfs.h"
#include "pak_format.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

namespace {

/// Logical paths that must never resolve: absolute, drive, UNC, NUL and
/// parent-directory escapes, plus Win32 forms that alias other files.
std::vector<std::string> EscapingPaths() {
    return {
        "../x",
        "a/../../x",
        "a/b/../../../x",
        "..\\x",
        "a\\..\\..\\x",
        "/etc/x",
        "\\x",
        "C:/x",
        "C:x",
        "c:\\windows\\x",
        "\\\\server\\share\\x",
        "//server/share/x",
        "\\\\?\\C:\\x",
        "\\\\.\\PhysicalDrive0",
        std::string("ok.txt\0/../../x", 15),
        std::string("a\0b", 3),
        "file.txt:stream",
        "",
        ".",
        "a/..",
        "./",
        ".. /x",
        "a/.../x",
        "dir./x",
        "CON",
        "nul.txt",
        "a/com1",
        "LPT9.log",
        "aux .txt",
    };
}

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string ToString(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

/// Creates a directory link `link` -> `target`. Uses a symlink where the
/// process may create one; Windows without that privilege falls back to a
/// directory junction, which any user can create and which path resolution
/// follows exactly like a symlink.
bool CreateDirectoryLink(const fs::path& target, const fs::path& link) {
    std::error_code ec;
    fs::create_directory_symlink(target, link, ec);
    if (!ec) {
        return true;
    }
#ifdef _WIN32
    const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" +
                                target.string() + "\" >nul 2>&1";
    return std::system(command.c_str()) == 0 && fs::exists(link);
#else
    return false;
#endif
}

/// Removes a directory link without touching its target. Must run before a
/// recursive delete: the standard library does not report a junction as a
/// symlink, so `remove_all` would descend into the target and then fail to
/// remove the junction itself, leaving state that breaks the next run.
void RemoveDirectoryLink(const fs::path& link) {
#ifdef _WIN32
    // rmdir removes a junction or directory symlink, never the target's files.
    const std::string command = "cmd /c rmdir \"" + link.string() + "\" >nul 2>&1";
    (void)std::system(command.c_str());
#else
    std::error_code ec;
    fs::remove(link, ec);
#endif
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// NormalizeVfsPath
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfsPathNormalize, AcceptsNestedRelativePaths) {
    EXPECT_EQ(NormalizeVfsPath("shaders/pbr/vertex.hlsl"), std::optional<std::string>("shaders/pbr/vertex.hlsl"));
    EXPECT_EQ(NormalizeVfsPath("file.txt"), std::optional<std::string>("file.txt"));
    EXPECT_EQ(NormalizeVfsPath(".hidden/x.bin"), std::optional<std::string>(".hidden/x.bin"));
    EXPECT_EQ(NormalizeVfsPath("a..b/c"), std::optional<std::string>("a..b/c"));
    EXPECT_EQ(NormalizeVfsPath("console/conf.json"), std::optional<std::string>("console/conf.json"));
}

TEST(VfsPathNormalize, CollapsesSeparatorsDotsAndContainedParents) {
    EXPECT_EQ(NormalizeVfsPath("a/./b//c"), std::optional<std::string>("a/b/c"));
    EXPECT_EQ(NormalizeVfsPath("a\\b\\c.txt"), std::optional<std::string>("a/b/c.txt"));
    EXPECT_EQ(NormalizeVfsPath("a/../b"), std::optional<std::string>("b"));
    EXPECT_EQ(NormalizeVfsPath("a/b/../../c/"), std::optional<std::string>("c"));
    EXPECT_EQ(NormalizeVfsPath("./scenes/x.uzscene"), std::optional<std::string>("scenes/x.uzscene"));
}

TEST(VfsPathNormalize, RejectsEveryEscapingForm) {
    for (const std::string& path : EscapingPaths()) {
        EXPECT_FALSE(NormalizeVfsPath(path).has_value())
            << "accepted: '" << std::string(path.c_str()) << "' (size " << path.size() << ")";
    }
}

TEST(VfsPathNormalize, PlainFileNameAcceptsOnlySingleSegments) {
    EXPECT_TRUE(IsPlainFileName("game_0.uzpak"));
    EXPECT_TRUE(IsPlainFileName("data0.uzpak"));
    EXPECT_FALSE(IsPlainFileName("../evil.uzpak"));
    EXPECT_FALSE(IsPlainFileName("sub/data.uzpak"));
    EXPECT_FALSE(IsPlainFileName("sub\\data.uzpak"));
    EXPECT_FALSE(IsPlainFileName("C:data.uzpak"));
    EXPECT_FALSE(IsPlainFileName("/data.uzpak"));
    EXPECT_FALSE(IsPlainFileName(".."));
    EXPECT_FALSE(IsPlainFileName("."));
    EXPECT_FALSE(IsPlainFileName(""));
    EXPECT_FALSE(IsPlainFileName("./data.uzpak"));
    EXPECT_FALSE(IsPlainFileName(std::string("data\0.uzpak", 11)));
    EXPECT_FALSE(IsPlainFileName("NUL.uzpak"));
}

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS containment
// ─────────────────────────────────────────────────────────────────────────────

class LooseFileVFSContainmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_base = fs::temp_directory_path() / "uz_test_vfs_containment";
        m_root = m_base / "content";
        RemoveTree();
        fs::create_directories(m_root);
        WriteText(m_base / "secret.txt", "top secret");
        WriteText(m_root / "textures" / "brick.txt", "brick");
    }

    void TearDown() override { RemoveTree(); }

    /// Links first (see RemoveDirectoryLink), then the plain directory tree.
    /// Also clears links an interrupted earlier run may have left behind.
    void RemoveTree() const {
        for (const fs::path& link : LinkPaths()) {
            RemoveDirectoryLink(link);
        }
        std::error_code ec;
        fs::remove_all(m_base, ec);
    }

    /// Every directory link the tests below create.
    [[nodiscard]] std::vector<fs::path> LinkPaths() const {
        return {m_root / "escape", m_root / "alias", m_base / "linked_content"};
    }

    fs::path m_base;
    fs::path m_root;
};

TEST_F(LooseFileVFSContainmentTest, ValidNestedPathResolvesInsideRoot) {
    LooseFileVFS vfs(m_root.string());

    const std::optional<std::string> resolved = vfs.ResolvePath("textures/brick.txt");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(fs::weakly_canonical(*resolved), fs::weakly_canonical(m_root / "textures" / "brick.txt"));
    EXPECT_EQ(ToString(vfs.Read("textures/brick.txt")), "brick");
    EXPECT_TRUE(vfs.Exists("textures/brick.txt"));
    EXPECT_EQ(ToString(vfs.Read("textures/../textures/./brick.txt")), "brick");
}

TEST_F(LooseFileVFSContainmentTest, NotYetExistingPathInsideRootResolves) {
    LooseFileVFS vfs(m_root.string());

    const std::optional<std::string> resolved = vfs.ResolvePath("scenes/new/save.uzscene");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(fs::weakly_canonical(*resolved),fs::weakly_canonical(m_root / "scenes" / "new" / "save.uzscene"));
}

TEST_F(LooseFileVFSContainmentTest, EscapingPathsAreRejectedByEveryEntryPoint) {
    LooseFileVFS vfs(m_root.string());

    for (const std::string& path : EscapingPaths()) {
        const std::string printable(path.c_str());
        EXPECT_FALSE(vfs.ResolvePath(path).has_value()) << "resolved: '" << printable << "'";
        EXPECT_TRUE(vfs.Read(path).empty()) << "read: '" << printable << "'";
        EXPECT_FALSE(vfs.Exists(path)) << "exists: '" << printable << "'";
    }
}

TEST_F(LooseFileVFSContainmentTest, ExistingFileOutsideRootIsNotReadable) {
    LooseFileVFS vfs(m_root.string());
    ASSERT_TRUE(fs::exists(m_base / "secret.txt"));

    EXPECT_TRUE(vfs.Read("../secret.txt").empty());
    EXPECT_TRUE(vfs.Read("textures/../../secret.txt").empty());
    EXPECT_FALSE(vfs.Exists("../secret.txt"));

    std::string absolute = (m_base / "secret.txt").string();
    EXPECT_TRUE(vfs.Read(absolute).empty());
    EXPECT_FALSE(vfs.Exists(absolute));
}

TEST_F(LooseFileVFSContainmentTest, SiblingDirectoryWithRootPrefixIsOutside) {
    // "content_evil" shares the textual prefix "content" with the root; a
    // naive string-prefix check would accept "../content_evil/x.txt".
    WriteText(m_base / "content_evil" / "x.txt", "evil");
    LooseFileVFS vfs(m_root.string());

    EXPECT_FALSE(vfs.ResolvePath("../content_evil/x.txt").has_value());
    EXPECT_TRUE(vfs.Read("../content_evil/x.txt").empty());
}

TEST_F(LooseFileVFSContainmentTest, LinkInsideRootPointingOutsideIsRejected) {
    const fs::path outsideDir = m_base / "outside";
    WriteText(outsideDir / "leak.txt", "leaked");
    ASSERT_TRUE(CreateDirectoryLink(outsideDir, m_root / "escape"));
    LooseFileVFS vfs(m_root.string());

    EXPECT_FALSE(vfs.ResolvePath("escape/leak.txt").has_value());
    EXPECT_TRUE(vfs.Read("escape/leak.txt").empty());
    EXPECT_FALSE(vfs.Exists("escape/leak.txt"));
    // A file that does not exist yet behind the link resolves outside as well.
    EXPECT_FALSE(vfs.ResolvePath("escape/new.uzscene").has_value());
}

TEST_F(LooseFileVFSContainmentTest, LinkInsideRootPointingInsideStillResolves) {
    ASSERT_TRUE(CreateDirectoryLink(m_root / "textures", m_root / "alias"));
    LooseFileVFS vfs(m_root.string());

    EXPECT_EQ(ToString(vfs.Read("alias/brick.txt")), "brick");
    EXPECT_TRUE(vfs.Exists("alias/brick.txt"));
}

TEST_F(LooseFileVFSContainmentTest, RootReachedThroughLinkStillContainsPaths) {
    // The root itself may be a link (e.g. a content folder linked into the
    // project): containment is checked against its resolved location.
    const fs::path linkedRoot = m_base / "linked_content";
    ASSERT_TRUE(CreateDirectoryLink(m_root, linkedRoot));
    LooseFileVFS vfs(linkedRoot.string());

    EXPECT_EQ(ToString(vfs.Read("textures/brick.txt")), "brick");
    EXPECT_TRUE(vfs.Read("../secret.txt").empty());
}

TEST_F(LooseFileVFSContainmentTest, LayeredVfsInheritsContainmentOfLooseMount) {
    LayeredVFS layered;
    layered.Mount(std::make_shared<LooseFileVFS>(m_root.string()), 100);

    EXPECT_EQ(ToString(layered.Read("textures/brick.txt")), "brick");
    EXPECT_TRUE(layered.Read("../secret.txt").empty());
    EXPECT_FALSE(layered.Exists("../secret.txt"));
    EXPECT_FALSE(layered.ResolvePath("textures/brick.txt").has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveAssetFsPath
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(LooseFileVFSContainmentTest, ResolveAssetFsPathContainsOnlyWhenVfsBound) {
    LooseFileVFS vfs(m_root.string());

    EXPECT_EQ(ResolveAssetFsPath(&vfs, "textures/brick.txt"), vfs.ResolvePath("textures/brick.txt"));
    EXPECT_FALSE(ResolveAssetFsPath(&vfs, "../secret.txt").has_value());
    EXPECT_FALSE(ResolveAssetFsPath(&vfs, (m_base / "secret.txt").string()).has_value());

    // Without a VFS the path comes from the application itself and is kept.
    EXPECT_EQ(ResolveAssetFsPath(nullptr, "C:/app/mesh.uzmesh"), std::optional<std::string>("C:/app/mesh.uzmesh"));

    PakVFS pak{PakReader{}};
    EXPECT_FALSE(ResolveAssetFsPath(&pak, "textures/brick.txt").has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS master index — pak names must be plain file names
// ─────────────────────────────────────────────────────────────────────────────

class PakVFSIndexNameTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = fs::temp_directory_path() / "uz_test_pakvfs_index_names";
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir);
        m_indexPath = (m_dir / "game.uzpakindex").string();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    [[nodiscard]] bool WriteIndexNaming(const std::string& pakName) const {
        MasterAssetEntry entry{};
        entry.pathHash = 0x1234;
        entry.pakId = 0;
        entry.offset = 0;
        entry.compressedSize = 4;
        entry.rawSize = 4;
        return WriteMasterIndex(m_indexPath, {pakName}, {entry});
    }

    fs::path m_dir;
    std::string m_indexPath;
};

TEST_F(PakVFSIndexNameTest, PlainPakNameIsAccepted) {
    ASSERT_TRUE(WriteIndexNaming("game_0.uzpak"));
    auto vfs = PakVFS::FromIndexFile(m_indexPath);
    ASSERT_NE(vfs, nullptr);
    EXPECT_TRUE(vfs->IsLazyMode());
}

TEST_F(PakVFSIndexNameTest, PakNamesThatLeaveTheIndexDirectoryAreRefused) {
    const std::vector<std::string> unsafeNames = {
        "../evil.uzpak", "sub/game.uzpak", "sub\\game.uzpak", "C:/evil.uzpak",
        "C:evil.uzpak", "/etc/evil", "\\\\server\\share\\evil.uzpak", "..", ".", "CON",
    };
    for (const std::string& name : unsafeNames) {
        ASSERT_TRUE(WriteIndexNaming(name)) << name;
        EXPECT_EQ(PakVFS::FromIndexFile(m_indexPath), nullptr) << "accepted pak name: " << name;
    }
}

} // namespace uz::test
