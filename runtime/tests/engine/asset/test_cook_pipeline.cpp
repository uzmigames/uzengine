/// UzEngine Cook Pipeline — End-to-End Tests
///
/// Validates the full asset pipeline path: source files → ShaderImporter →
/// AssetCache → ImportSidecar → PakWriter → PakVFS → AssetManifest.
/// Tests the complete lifecycle from raw HLSL to runtime-loadable pak assets.

#ifdef _WIN32

#include "asset_cache.h"
#include "asset_manifest.h"
#include "import_sidecar.h"
#include "pak_format.h"
#include "shader_importer.h"
#include "shader_compiler.h"
#include "uzshader_format.h"
#include "vfs.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — sets up compiler, importer, temp directories
// ─────────────────────────────────────────────────────────────────────────────

class CookPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_cook_pipeline").string();
        fs::create_directories(m_dir);

        m_sourceDir = m_dir + "/source";
        m_outputDir = m_dir + "/output";
        m_cacheDir = m_outputDir + "/.cache";
        fs::create_directories(m_sourceDir);
        fs::create_directories(m_outputDir);

        m_compilerReady = m_compiler.Init();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    /// Writes a minimal valid HLSL vertex shader to the source directory.
    std::string WriteMinimalVS(const std::string& filename = "test_vs.hlsl") {
        std::string path = m_sourceDir + "/" + filename;
        std::ofstream f(path);
        f << "struct VSOutput { float4 pos : SV_Position; };\n"
          << "VSOutput main(uint id : SV_VertexID) {\n"
          << "    VSOutput o; o.pos = float4(0, 0, 0, 1); return o;\n"
          << "}\n";
        return path;
    }

    /// Writes a minimal valid HLSL pixel shader to the source directory.
    std::string WriteMinimalPS(const std::string& filename = "test_ps.hlsl") {
        std::string path = m_sourceDir + "/" + filename;
        std::ofstream f(path);
        f << "float4 main() : SV_Target { return float4(1, 0, 0, 1); }\n";
        return path;
    }

    /// Reads a file into a byte vector.
    static std::vector<uint8_t> ReadFileBytes(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return {};
        auto size = file.tellg();
        if (size <= 0) return {};
        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return file.good() ? data : std::vector<uint8_t>{};
    }

    std::string m_dir;
    std::string m_sourceDir;
    std::string m_outputDir;
    std::string m_cacheDir;
    ShaderCompiler m_compiler;
    bool m_compilerReady = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: import → cache → sidecar
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CookPipelineTest, ImportAndCacheProducesSidecar) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    auto sourcePath = WriteMinimalVS();
    auto sourceBytes = ReadFileBytes(sourcePath);
    ASSERT_FALSE(sourceBytes.empty());

    ShaderImporter importer(m_compiler);
    AssetImportOptions options;
    options.stage = "vertex";
    options.model = "sm6_6";
    options.entryPoint = "main";

    auto result = importer.Import(sourceBytes, "test_vs.hlsl", options);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_FALSE(result.data.empty());

    // Store in cache
    AssetCache cache(m_cacheDir);
    uint64_t sourceHash = AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());
    uint64_t optionsHash = AssetCache::HashString("vertex" + std::string("sm6_6") + "main");
    uint64_t cacheKey = AssetCache::ComputeKey(
        importer.Name(), importer.Version(), sourceHash, optionsHash);
    cache.Store(cacheKey, result.data);

    // Write sidecar
    ImportSidecar sidecar;
    sidecar.importer = importer.Name();
    sidecar.version = importer.Version();
    sidecar.sourceHash = HashToHex(sourceHash);
    sidecar.options = {{"stage", "vertex"}, {"model", "sm6_6"}, {"entry_point", "main"}};

    std::string cachePath = m_cacheDir + "/imported/" + HashToHex(cacheKey) + ".bin";
    sidecar.outputs = {{"sm6_6", cachePath}};

    std::string sidecarPath = sourcePath + ".uzimport";
    ASSERT_TRUE(WriteSidecarFile(sidecarPath, sidecar));

    // Verify sidecar reads back correctly
    auto readResult = ReadSidecarFile(sidecarPath);
    ASSERT_TRUE(readResult.success) << readResult.error;
    EXPECT_EQ(readResult.sidecar.importer, "shader");
    EXPECT_EQ(readResult.sidecar.version, 1u);
    EXPECT_EQ(readResult.sidecar.sourceHash, HashToHex(sourceHash));

    // Verify NeedsReimport returns false (nothing changed)
    EXPECT_FALSE(NeedsReimport(readResult.sidecar, HashToHex(sourceHash), importer.Version()));
}

TEST_F(CookPipelineTest, NeedsReimportOnSourceChange) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    auto sourcePath = WriteMinimalVS();
    auto sourceBytes = ReadFileBytes(sourcePath);
    uint64_t sourceHash = AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());

    ImportSidecar sidecar;
    sidecar.importer = "shader";
    sidecar.version = 1;
    sidecar.sourceHash = HashToHex(sourceHash);

    // Same hash → no reimport
    EXPECT_FALSE(NeedsReimport(sidecar, HashToHex(sourceHash), 1));

    // Different hash → reimport
    EXPECT_TRUE(NeedsReimport(sidecar, "aaaaaaaaaaaaaaaa", 1));

    // Higher version → reimport
    EXPECT_TRUE(NeedsReimport(sidecar, HashToHex(sourceHash), 2));
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: import → pak → VFS read
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CookPipelineTest, ImportPackAndReadViaPakVFS) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    auto sourcePath = WriteMinimalVS();
    auto sourceBytes = ReadFileBytes(sourcePath);
    ASSERT_FALSE(sourceBytes.empty());

    ShaderImporter importer(m_compiler);
    AssetImportOptions options;
    options.stage = "vertex";
    options.model = "sm6_6";
    options.entryPoint = "main";

    auto result = importer.Import(sourceBytes, "test_vs.hlsl", options);
    ASSERT_TRUE(result.success) << result.error;

    // Pack into .uzpak
    std::vector<PakWriteEntry> entries;
    entries.push_back({"shaders/test_vs.uzshader", result.data, 0});

    std::string pakPath = m_outputDir + "/test.uzpak";
    ASSERT_TRUE(WritePakFile(pakPath, entries));

    // Read back via PakVFS
    auto pakVfs = PakVFS::FromFile(pakPath);
    ASSERT_NE(pakVfs, nullptr);

    EXPECT_TRUE(pakVfs->Exists("shaders/test_vs.uzshader"));
    EXPECT_FALSE(pakVfs->Exists("shaders/nonexistent.uzshader"));

    auto readData = pakVfs->Read("shaders/test_vs.uzshader");
    ASSERT_FALSE(readData.empty());

    // Validate the .uzshader header
    ASSERT_GE(readData.size(), sizeof(UzShaderHeader));
    UzShaderHeader header;
    std::memcpy(&header, readData.data(), sizeof(header));
    EXPECT_EQ(header.magic, kUzShaderMagic);
    EXPECT_EQ(header.stage, static_cast<uint32_t>(UzShaderStage::Vertex));
}

TEST_F(CookPipelineTest, ImportPackManifestAndQuery) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    // Import both VS and PS
    auto vsPath = WriteMinimalVS();
    auto psPath = WriteMinimalPS();
    auto vsBytes = ReadFileBytes(vsPath);
    auto psBytes = ReadFileBytes(psPath);

    ShaderImporter importer(m_compiler);

    AssetImportOptions vsOpts;
    vsOpts.stage = "vertex";
    vsOpts.model = "sm6_6";
    vsOpts.entryPoint = "main";
    auto vsResult = importer.Import(vsBytes, "test_vs.hlsl", vsOpts);
    ASSERT_TRUE(vsResult.success) << vsResult.error;

    AssetImportOptions psOpts;
    psOpts.stage = "pixel";
    psOpts.model = "sm6_6";
    psOpts.entryPoint = "main";
    auto psResult = importer.Import(psBytes, "test_ps.hlsl", psOpts);
    ASSERT_TRUE(psResult.success) << psResult.error;

    // Pack
    std::vector<PakWriteEntry> pakEntries;
    pakEntries.push_back({"shaders/test_vs.uzshader", vsResult.data, 0});
    pakEntries.push_back({"shaders/test_ps.uzshader", psResult.data, 0});

    std::string pakPath = m_outputDir + "/test.uzpak";
    ASSERT_TRUE(WritePakFile(pakPath, pakEntries));

    // Write manifest
    AssetManifestWriter manifestWriter;
    manifestWriter.AddEntry({"shaders/test_vs.uzshader",
                             vsResult.data.size(), AssetType::Shader, {}});
    manifestWriter.AddEntry({"shaders/test_ps.uzshader",
                             psResult.data.size(), AssetType::Shader, {}});

    std::string manifestPath = m_outputDir + "/test.uzmanifest";
    ASSERT_TRUE(WriteManifestFile(manifestPath, manifestWriter));

    // Load manifest and query
    AssetManifest manifest;
    ASSERT_TRUE(manifest.LoadFile(manifestPath));
    EXPECT_EQ(manifest.EntryCount(), 2u);

    auto* vsEntry = manifest.Find("shaders/test_vs.uzshader");
    ASSERT_NE(vsEntry, nullptr);
    EXPECT_EQ(vsEntry->type, AssetType::Shader);
    EXPECT_EQ(vsEntry->size, vsResult.data.size());

    auto* psEntry = manifest.Find("shaders/test_ps.uzshader");
    ASSERT_NE(psEntry, nullptr);
    EXPECT_EQ(psEntry->type, AssetType::Shader);

    EXPECT_EQ(manifest.Find("shaders/nonexistent.uzshader"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: layered VFS (loose overrides pak)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CookPipelineTest, LayeredVFSLooseOverridesPak) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    // Create a pak with a VS
    auto vsPath = WriteMinimalVS();
    auto vsBytes = ReadFileBytes(vsPath);

    ShaderImporter importer(m_compiler);
    AssetImportOptions opts;
    opts.stage = "vertex";
    opts.model = "sm6_6";
    opts.entryPoint = "main";
    auto result = importer.Import(vsBytes, "test_vs.hlsl", opts);
    ASSERT_TRUE(result.success);

    std::vector<PakWriteEntry> entries;
    entries.push_back({"shaders/test_vs.uzshader", result.data, 0});
    std::string pakPath = m_outputDir + "/base.uzpak";
    ASSERT_TRUE(WritePakFile(pakPath, entries));

    // Create a loose override directory with a different file
    std::string looseDir = m_dir + "/loose";
    fs::create_directories(looseDir + "/shaders");
    {
        std::ofstream f(looseDir + "/shaders/test_vs.uzshader", std::ios::binary);
        std::string override_content = "OVERRIDE_DATA";
        f.write(override_content.data(), override_content.size());
    }

    // Build layered VFS: loose (priority 100) > pak (priority 0)
    auto pakVfs = PakVFS::FromFile(pakPath);
    ASSERT_NE(pakVfs, nullptr);
    auto looseVfs = std::make_unique<LooseFileVFS>(looseDir);

    LayeredVFS layered;
    layered.Mount(std::move(pakVfs), 0);
    layered.Mount(std::move(looseVfs), 100);

    EXPECT_TRUE(layered.Exists("shaders/test_vs.uzshader"));

    // Should read from loose (higher priority), not pak
    auto data = layered.Read("shaders/test_vs.uzshader");
    std::string content(data.begin(), data.end());
    EXPECT_EQ(content, "OVERRIDE_DATA");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache effectiveness — second import is a cache hit
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CookPipelineTest, SecondImportHitsCache) {
    if (!m_compilerReady) GTEST_SKIP() << "DXC not available";

    auto sourcePath = WriteMinimalVS();
    auto sourceBytes = ReadFileBytes(sourcePath);

    ShaderImporter importer(m_compiler);
    AssetImportOptions options;
    options.stage = "vertex";
    options.model = "sm6_6";
    options.entryPoint = "main";

    auto result1 = importer.Import(sourceBytes, "test_vs.hlsl", options);
    ASSERT_TRUE(result1.success);

    // Store in cache
    AssetCache cache(m_cacheDir);
    uint64_t sourceHash = AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());
    uint64_t optionsHash = AssetCache::HashString("vertexsm6_6main");
    uint64_t cacheKey = AssetCache::ComputeKey(
        importer.Name(), importer.Version(), sourceHash, optionsHash);
    cache.Store(cacheKey, result1.data);

    // Write sidecar
    ImportSidecar sidecar;
    sidecar.importer = importer.Name();
    sidecar.version = importer.Version();
    sidecar.sourceHash = HashToHex(sourceHash);
    std::string cachePath = m_cacheDir + "/imported/" + HashToHex(cacheKey) + ".bin";
    sidecar.outputs = {{"sm6_6", cachePath}};

    // Check: sidecar says no reimport needed
    EXPECT_FALSE(NeedsReimport(sidecar, HashToHex(sourceHash), importer.Version()));

    // Cache lookup should succeed
    auto cached = cache.Lookup(cacheKey);
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->size(), result1.data.size());
}

} // namespace uz::test

#else

#include <gtest/gtest.h>

TEST(CookPipelineTest, SkippedOnNonWindows) {
    GTEST_SKIP() << "Cook pipeline tests require DXC (Windows only)";
}

#endif // _WIN32
