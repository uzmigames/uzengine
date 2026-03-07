// test_importer.cpp -- Comprehensive tests for uz::Importer.
//
// Covers:
//   - Importer construction and import_dir() accessor
//   - import_mesh(): happy path, source not found, directory creation
//   - import_texture(): happy path, PNG header parsing, source not found
//   - import_asset(): dispatch to mesh / texture, unsupported types
//   - read_png_header(): valid PNG (1x1 grey, 2x3 RGB, 4x5 RGBA),
//                        non-existent file, truncated file, invalid signature,
//                        invalid IHDR tag, zero dimensions
//   - make_import_path() (via output_path inspection)
//   - write_sidecar(): atomic tmp→rename, content correctness
//   - copy_file(): content fidelity
//   - Sidecar JSON key/value correctness for both mesh and texture

#include <gtest/gtest.h>

#include "engine/editor/importer.h"
#include "engine/editor/asset_registry.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace uz;

// ============================================================================
// Minimal PNG builder (generates valid PNG binary from raw parameters)
// ============================================================================
// PNG spec reference offsets:
//   [0-7]   signature
//   [8-11]  IHDR chunk length = 13
//   [12-15] "IHDR"
//   [16-19] width  (big-endian uint32)
//   [20-23] height (big-endian uint32)
//   [24]    bit depth
//   [25]    color type
//   [26-28] compression, filter, interlace
//   [29-32] CRC (we write zeros — Importer does not validate CRC)

static std::vector<uint8_t> make_png_header_bytes(uint32_t width,
                                                   uint32_t height,
                                                   uint8_t  color_type)
{
    std::vector<uint8_t> buf(33, 0);

    // PNG signature.
    buf[0] = 0x89; buf[1] = 0x50; buf[2] = 0x4E; buf[3] = 0x47;
    buf[4] = 0x0D; buf[5] = 0x0A; buf[6] = 0x1A; buf[7] = 0x0A;

    // IHDR chunk length = 13 (big-endian).
    buf[8]  = 0x00; buf[9]  = 0x00; buf[10] = 0x00; buf[11] = 0x0D;

    // IHDR chunk type.
    buf[12] = 'I'; buf[13] = 'H'; buf[14] = 'D'; buf[15] = 'R';

    // Width (big-endian).
    buf[16] = static_cast<uint8_t>((width  >> 24) & 0xFF);
    buf[17] = static_cast<uint8_t>((width  >> 16) & 0xFF);
    buf[18] = static_cast<uint8_t>((width  >>  8) & 0xFF);
    buf[19] = static_cast<uint8_t>( width         & 0xFF);

    // Height (big-endian).
    buf[20] = static_cast<uint8_t>((height >> 24) & 0xFF);
    buf[21] = static_cast<uint8_t>((height >> 16) & 0xFF);
    buf[22] = static_cast<uint8_t>((height >>  8) & 0xFF);
    buf[23] = static_cast<uint8_t>( height        & 0xFF);

    // Bit depth = 8, color type, compression=0, filter=0, interlace=0.
    buf[24] = 8;
    buf[25] = color_type;
    // buf[26..32] remain zero (compression, filter, interlace, CRC).

    return buf;
}

/// Write a minimal valid PNG file to disk at the given absolute path.
static void write_png_file(const std::string& path,
                           uint32_t width,
                           uint32_t height,
                           uint8_t  color_type)
{
    const auto bytes = make_png_header_bytes(width, height, color_type);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// ============================================================================
// Test fixture
// ============================================================================

class ImporterTest : public ::testing::Test {
protected:
    std::string test_root_;

    void SetUp() override {
        // Unique temp directory per test (use line number as disambiguator).
        test_root_ = fs::temp_directory_path().string() + "/uz_importer_test_" +
                     std::to_string(::testing::UnitTest::GetInstance()
                                        ->current_test_info()->line());
        fs::remove_all(test_root_);
        fs::create_directories(test_root_);
    }

    void TearDown() override {
        fs::remove_all(test_root_);
    }

    // Helper: create a text file.
    void create_file(const std::string& rel_path,
                     const std::string& content = "dummy") {
        const std::string abs = test_root_ + "/" + rel_path;
        fs::create_directories(fs::path(abs).parent_path());
        std::ofstream ofs(abs, std::ios::binary);
        ofs << content;
    }

    // Helper: create a minimal .glb (just a GLB magic header for content).
    std::string create_glb(const std::string& rel_path) {
        const std::string abs = test_root_ + "/" + rel_path;
        fs::create_directories(fs::path(abs).parent_path());
        // GLB magic: 0x46546C67 (little-endian "glTF"), version 2
        const uint8_t glb_header[12] = {
            0x67, 0x6C, 0x54, 0x46,  // magic "glTF"
            0x02, 0x00, 0x00, 0x00,  // version 2
            0x0C, 0x00, 0x00, 0x00   // total length = 12
        };
        std::ofstream ofs(abs, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(glb_header), sizeof(glb_header));
        return abs;
    }

    // Helper: read file bytes from disk.
    std::vector<uint8_t> read_bytes(const std::string& abs_path) {
        std::ifstream ifs(abs_path, std::ios::binary);
        return {std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>()};
    }

    // Helper: read file as string.
    std::string read_string(const std::string& abs_path) {
        size_t size = 0;
        char* data  = platform::file_read_all(abs_path.c_str(), &size);
        if (!data) return {};
        std::string s(data, size);
        std::free(data);
        return s;
    }
};

// ============================================================================
// Construction and import_dir()
// ============================================================================

TEST_F(ImporterTest, Constructor_SetsImportDir_WithTrailingSlash) {
    Importer imp(test_root_);
    const std::string& dir = imp.import_dir();
    EXPECT_FALSE(dir.empty());
    // Must contain ".import" segment.
    EXPECT_NE(dir.find(".import"), std::string::npos);
    // Must end with '/'.
    EXPECT_EQ(dir.back(), '/');
}

TEST_F(ImporterTest, Constructor_ProjectRootAlreadyHasSlash) {
    Importer imp(test_root_ + "/");
    // import_dir should not have double slash between root and ".import".
    const std::string& dir = imp.import_dir();
    EXPECT_EQ(dir.find("//"), std::string::npos);
}

// ============================================================================
// import_mesh() — happy path
// ============================================================================

TEST_F(ImporterTest, ImportMesh_HappyPath_CopiesFileAndWritesSidecar) {
    const std::string src = create_glb("meshes/player.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.output_path.empty());
    EXPECT_TRUE(result.error_msg.empty());

    // Output must exist under .import/meshes/.
    EXPECT_TRUE(fs::exists(result.output_path));
    EXPECT_NE(result.output_path.find(".import"), std::string::npos);
    EXPECT_NE(result.output_path.find("meshes"),  std::string::npos);
    EXPECT_EQ(fs::path(result.output_path).filename().string(), "player.glb");
}

TEST_F(ImporterTest, ImportMesh_HappyPath_OutputContentMatchesSource) {
    const std::string src = create_glb("cube.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    const auto src_bytes = read_bytes(src);
    const auto out_bytes = read_bytes(result.output_path);
    EXPECT_EQ(src_bytes, out_bytes);
}

TEST_F(ImporterTest, ImportMesh_HappyPath_SidecarExistsNextToOutput) {
    const std::string src = create_glb("box.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    const std::string sidecar = result.output_path + ".import.json";
    EXPECT_TRUE(fs::exists(sidecar));
}

TEST_F(ImporterTest, ImportMesh_HappyPath_SidecarHasCorrectKeys) {
    const std::string src = create_glb("sphere.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    const std::string sidecar_str = read_string(result.output_path + ".import.json");
    ASSERT_FALSE(sidecar_str.empty());

    const auto j = nlohmann::json::parse(sidecar_str);
    EXPECT_EQ(j["source"].get<std::string>(), src);
    EXPECT_EQ(j["type"].get<std::string>(), "Mesh");
    EXPECT_DOUBLE_EQ(j["settings"]["scale"].get<double>(), 1.0);
    EXPECT_TRUE(j["settings"]["generate_normals"].get<bool>());
    EXPECT_TRUE(j["settings"]["generate_tangents"].get<bool>());
}

// ============================================================================
// import_mesh() — error paths
// ============================================================================

TEST_F(ImporterTest, ImportMesh_SourceNotFound_ReturnsFailure) {
    Importer imp(test_root_);
    const auto result = imp.import_mesh(test_root_ + "/nonexistent.glb");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.output_path.empty());
    EXPECT_FALSE(result.error_msg.empty());
}

// ============================================================================
// import_texture() — happy path
// ============================================================================

TEST_F(ImporterTest, ImportTexture_HappyPath_RGBA_CopiesFileAndWritesSidecar) {
    const std::string src = test_root_ + "/albedo.png";
    write_png_file(src, 256, 256, 6 /*RGBA*/);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.output_path.empty());
    EXPECT_TRUE(result.error_msg.empty());

    EXPECT_TRUE(fs::exists(result.output_path));
    EXPECT_NE(result.output_path.find(".import"),   std::string::npos);
    EXPECT_NE(result.output_path.find("textures"),  std::string::npos);
    EXPECT_EQ(fs::path(result.output_path).filename().string(), "albedo.png");
}

TEST_F(ImporterTest, ImportTexture_HappyPath_SidecarHasCorrectDimensions) {
    const std::string src = test_root_ + "/grass.png";
    write_png_file(src, 128, 64, 2 /*RGB*/);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    const std::string sidecar_str = read_string(result.output_path + ".import.json");
    ASSERT_FALSE(sidecar_str.empty());

    const auto j = nlohmann::json::parse(sidecar_str);
    EXPECT_EQ(j["source"].get<std::string>(), src);
    EXPECT_EQ(j["type"].get<std::string>(), "Texture");
    EXPECT_EQ(j["settings"]["width"].get<uint32_t>(),    128u);
    EXPECT_EQ(j["settings"]["height"].get<uint32_t>(),   64u);
    EXPECT_EQ(j["settings"]["channels"].get<uint32_t>(), 3u);
    EXPECT_TRUE(j["settings"]["srgb"].get<bool>());
    EXPECT_TRUE(j["settings"]["generate_mipmaps"].get<bool>());
}

TEST_F(ImporterTest, ImportTexture_HappyPath_GreyscaleChannels) {
    const std::string src = test_root_ + "/height.png";
    write_png_file(src, 32, 32, 0 /*Grey*/);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    const auto j = nlohmann::json::parse(
        read_string(result.output_path + ".import.json"));
    EXPECT_EQ(j["settings"]["channels"].get<uint32_t>(), 1u);
}

TEST_F(ImporterTest, ImportTexture_HappyPath_GreyAlphaChannels) {
    const std::string src = test_root_ + "/mask.png";
    write_png_file(src, 16, 16, 4 /*GreyA*/);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    const auto j = nlohmann::json::parse(
        read_string(result.output_path + ".import.json"));
    EXPECT_EQ(j["settings"]["channels"].get<uint32_t>(), 2u);
}

TEST_F(ImporterTest, ImportTexture_HappyPath_IndexedChannels) {
    const std::string src = test_root_ + "/indexed.png";
    write_png_file(src, 8, 8, 3 /*Indexed*/);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    const auto j = nlohmann::json::parse(
        read_string(result.output_path + ".import.json"));
    EXPECT_EQ(j["settings"]["channels"].get<uint32_t>(), 3u);
}

TEST_F(ImporterTest, ImportTexture_HappyPath_OutputContentMatchesSource) {
    const std::string src = test_root_ + "/copy_test.png";
    write_png_file(src, 4, 4, 6);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    const auto src_bytes = read_bytes(src);
    const auto out_bytes = read_bytes(result.output_path);
    EXPECT_EQ(src_bytes, out_bytes);
}

// ============================================================================
// import_texture() — error paths
// ============================================================================

TEST_F(ImporterTest, ImportTexture_SourceNotFound_ReturnsFailure) {
    Importer imp(test_root_);
    const auto result = imp.import_texture(test_root_ + "/missing.png");

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.output_path.empty());
    EXPECT_FALSE(result.error_msg.empty());
}

TEST_F(ImporterTest, ImportTexture_InvalidPNG_ReturnsFailure) {
    const std::string src = test_root_ + "/not_a_png.png";
    create_file("not_a_png.png", "this is not a png file at all !!");

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_msg.empty());
}

// ============================================================================
// import_asset() — dispatch
// ============================================================================

TEST_F(ImporterTest, ImportAsset_MeshType_RoutesToImportMesh) {
    const std::string src = create_glb("dispatch_test.glb");
    Importer imp(test_root_);

    const auto result = imp.import_asset(src, AssetType::Mesh);

    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output_path.find("meshes"), std::string::npos);
}

TEST_F(ImporterTest, ImportAsset_TextureType_RoutesToImportTexture) {
    const std::string src = test_root_ + "/dispatch_tex.png";
    write_png_file(src, 4, 4, 6);

    Importer imp(test_root_);
    const auto result = imp.import_asset(src, AssetType::Texture);

    EXPECT_TRUE(result.success);
    EXPECT_NE(result.output_path.find("textures"), std::string::npos);
}

TEST_F(ImporterTest, ImportAsset_AudioType_ReturnsUnsupportedError) {
    Importer imp(test_root_);
    const auto result = imp.import_asset(test_root_ + "/sound.wav", AssetType::Audio);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_msg.find("unsupported"), std::string::npos);
}

TEST_F(ImporterTest, ImportAsset_SceneType_ReturnsUnsupportedError) {
    Importer imp(test_root_);
    const auto result = imp.import_asset(test_root_ + "/world.uzscene", AssetType::Scene);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_msg.empty());
}

TEST_F(ImporterTest, ImportAsset_MaterialType_ReturnsUnsupportedError) {
    Importer imp(test_root_);
    const auto result = imp.import_asset(test_root_ + "/mat.uzmat", AssetType::Material);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_msg.empty());
}

TEST_F(ImporterTest, ImportAsset_ShaderType_ReturnsUnsupportedError) {
    Importer imp(test_root_);
    const auto result = imp.import_asset(test_root_ + "/fx.hlsl", AssetType::Shader);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_msg.empty());
}

TEST_F(ImporterTest, ImportAsset_UnknownType_ReturnsUnsupportedError) {
    Importer imp(test_root_);
    const auto result = imp.import_asset(test_root_ + "/data.bin", AssetType::Unknown);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_msg.empty());
}

// ============================================================================
// read_png_header() — valid cases
// ============================================================================

TEST_F(ImporterTest, ReadPngHeader_1x1_Grey_Returns1Channel) {
    const std::string path = test_root_ + "/grey_1x1.png";
    write_png_file(path, 1, 1, 0 /*Grey*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.width,    1u);
    EXPECT_EQ(info.height,   1u);
    EXPECT_EQ(info.channels, 1u);
}

TEST_F(ImporterTest, ReadPngHeader_2x3_RGB_Returns3Channels) {
    const std::string path = test_root_ + "/rgb_2x3.png";
    write_png_file(path, 2, 3, 2 /*RGB*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.width,    2u);
    EXPECT_EQ(info.height,   3u);
    EXPECT_EQ(info.channels, 3u);
}

TEST_F(ImporterTest, ReadPngHeader_4x5_RGBA_Returns4Channels) {
    const std::string path = test_root_ + "/rgba_4x5.png";
    write_png_file(path, 4, 5, 6 /*RGBA*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.width,    4u);
    EXPECT_EQ(info.height,   5u);
    EXPECT_EQ(info.channels, 4u);
}

TEST_F(ImporterTest, ReadPngHeader_LargeResolution_Parsed) {
    const std::string path = test_root_ + "/large.png";
    write_png_file(path, 4096, 2048, 6 /*RGBA*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.width,  4096u);
    EXPECT_EQ(info.height, 2048u);
    EXPECT_EQ(info.channels, 4u);
}

TEST_F(ImporterTest, ReadPngHeader_GreyAlpha_Returns2Channels) {
    const std::string path = test_root_ + "/greya.png";
    write_png_file(path, 10, 10, 4 /*GreyA*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.channels, 2u);
}

TEST_F(ImporterTest, ReadPngHeader_Indexed_Returns3Channels) {
    const std::string path = test_root_ + "/indexed.png";
    write_png_file(path, 8, 8, 3 /*Indexed*/);

    Importer::TextureInfo info;
    ASSERT_TRUE(Importer::read_png_header(path, info));
    EXPECT_EQ(info.channels, 3u);
}

// ============================================================================
// read_png_header() — error cases
// ============================================================================

TEST_F(ImporterTest, ReadPngHeader_FileNotFound_ReturnsFalse) {
    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(test_root_ + "/no_such.png", info));
}

TEST_F(ImporterTest, ReadPngHeader_TruncatedFile_ReturnsFalse) {
    // Write only 16 bytes — not enough for a full IHDR.
    const std::string path = test_root_ + "/truncated.png";
    const auto bytes = make_png_header_bytes(1, 1, 6);
    std::vector<uint8_t> short_bytes(bytes.begin(), bytes.begin() + 16);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(short_bytes.data()),
              static_cast<std::streamsize>(short_bytes.size()));

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_WrongSignature_ReturnsFalse) {
    const std::string path = test_root_ + "/bad_sig.png";
    auto bytes = make_png_header_bytes(1, 1, 6);
    bytes[0] = 0x00; // Corrupt first signature byte.
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_WrongIHDRTag_ReturnsFalse) {
    const std::string path = test_root_ + "/bad_ihdr.png";
    auto bytes = make_png_header_bytes(1, 1, 6);
    bytes[12] = 'X'; // Corrupt IHDR tag.
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_InvalidColorType_ReturnsFalse) {
    const std::string path = test_root_ + "/bad_color.png";
    // color_type 1 is not a valid PNG color type.
    write_png_file(path, 1, 1, 1 /*invalid*/);

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_ZeroWidth_ReturnsFalse) {
    const std::string path = test_root_ + "/zero_w.png";
    write_png_file(path, 0, 1, 6);

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_ZeroHeight_ReturnsFalse) {
    const std::string path = test_root_ + "/zero_h.png";
    write_png_file(path, 1, 0, 6);

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

TEST_F(ImporterTest, ReadPngHeader_EmptyFile_ReturnsFalse) {
    const std::string path = test_root_ + "/empty.png";
    std::ofstream ofs(path, std::ios::binary); // Creates an empty file.

    Importer::TextureInfo info;
    EXPECT_FALSE(Importer::read_png_header(path, info));
}

// ============================================================================
// make_import_path() — verified through output_path inspection
// ============================================================================

TEST_F(ImporterTest, MakeImportPath_Mesh_PlacedUnderMeshesSubdir) {
    const std::string src = create_glb("models/hero.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    // Output path must contain ".import/meshes/hero.glb".
    EXPECT_NE(result.output_path.find(".import/meshes/hero.glb"), std::string::npos);
}

TEST_F(ImporterTest, MakeImportPath_Texture_PlacedUnderTexturesSubdir) {
    const std::string src = test_root_ + "/ui/icon.png";
    fs::create_directories(test_root_ + "/ui");
    write_png_file(src, 64, 64, 6);

    Importer imp(test_root_);
    const auto result = imp.import_texture(src);
    ASSERT_TRUE(result.success);

    EXPECT_NE(result.output_path.find(".import/textures/icon.png"), std::string::npos);
}

// ============================================================================
// Sidecar atomic write
// ============================================================================

TEST_F(ImporterTest, Sidecar_NoTmpFileLeftBehind_AfterSuccessfulWrite) {
    const std::string src = create_glb("atomic.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    // The .tmp file must not exist after a successful import.
    const std::string tmp = result.output_path + ".import.json.tmp";
    EXPECT_FALSE(fs::exists(tmp));
}

TEST_F(ImporterTest, Sidecar_JsonIsPrettyPrinted) {
    const std::string src = create_glb("pretty.glb");
    Importer imp(test_root_);

    const auto result = imp.import_mesh(src);
    ASSERT_TRUE(result.success);

    const std::string content = read_string(result.output_path + ".import.json");
    // dump(2) indentation means the file contains newlines.
    EXPECT_NE(content.find('\n'), std::string::npos);
}

// ============================================================================
// Repeated imports — idempotent
// ============================================================================

TEST_F(ImporterTest, ImportMesh_CalledTwice_SecondCallSucceeds) {
    const std::string src = create_glb("twin.glb");
    Importer imp(test_root_);

    const auto r1 = imp.import_mesh(src);
    ASSERT_TRUE(r1.success);

    const auto r2 = imp.import_mesh(src);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(r1.output_path, r2.output_path);
}

TEST_F(ImporterTest, ImportTexture_CalledTwice_SecondCallSucceeds) {
    const std::string src = test_root_ + "/twin.png";
    write_png_file(src, 8, 8, 6);
    Importer imp(test_root_);

    const auto r1 = imp.import_texture(src);
    ASSERT_TRUE(r1.success);

    const auto r2 = imp.import_texture(src);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(r1.output_path, r2.output_path);
}
