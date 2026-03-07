// importer.cpp -- Asset importer implementation for the editor pipeline.
//
// Mesh import:    copies .glb/.gltf to .import/meshes/, writes JSON sidecar.
// Texture import: parses PNG IHDR, copies to .import/textures/, writes JSON sidecar.
//
// Sidecar files are written atomically via a .tmp → rename pattern so a crash
// during writing never leaves a half-written sidecar that looks valid.

#include "engine/editor/importer.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace uz {

// ===========================================================================
// Construction
// ===========================================================================

Importer::Importer(const std::string& project_root)
    : project_root_(project_root)
{
    // Normalize: always end with a forward slash.
    if (!project_root_.empty() &&
        project_root_.back() != '/' &&
        project_root_.back() != '\\') {
        project_root_ += '/';
    }

    import_dir_ = project_root_ + ".import/";
}

// ===========================================================================
// Public API
// ===========================================================================

Importer::ImportResult Importer::import_asset(const std::string& source_path,
                                               AssetType type)
{
    switch (type) {
        case AssetType::Mesh:
            return import_mesh(source_path);
        case AssetType::Texture:
            return import_texture(source_path);
        default:
            break;
    }

    ImportResult result;
    result.success   = false;
    result.error_msg = "unsupported asset type: " +
                       std::string(asset_type_to_string(type));
    return result;
}

Importer::ImportResult Importer::import_mesh(const std::string& source_path)
{
    ImportResult result;

    // 1. Verify source exists.
    if (!platform::path_exists(source_path.c_str())) {
        result.success   = false;
        result.error_msg = "source file not found: " + source_path;
        return result;
    }

    // 2. Determine output path and ensure parent directory exists.
    const std::string output_path = make_import_path(source_path, AssetType::Mesh);
    const std::string output_dir  = fs::path(output_path).parent_path().string();

    if (!platform::mkdir_recursive(output_dir.c_str())) {
        result.success   = false;
        result.error_msg = "failed to create output directory: " + output_dir;
        return result;
    }

    // 3. Copy source to .import/meshes/.
    if (!copy_file(source_path, output_path)) {
        result.success   = false;
        result.error_msg = "failed to copy mesh to: " + output_path;
        return result;
    }

    // 4. Write sidecar with import settings.
    nlohmann::json settings = {
        {"source",            source_path},
        {"type",              "Mesh"},
        {"settings", {
            {"scale",              1.0},
            {"generate_normals",   true},
            {"generate_tangents",  true}
        }}
    };

    if (!write_sidecar(output_path, settings)) {
        result.success   = false;
        result.error_msg = "failed to write sidecar for: " + output_path;
        return result;
    }

    result.success     = true;
    result.output_path = output_path;
    return result;
}

Importer::ImportResult Importer::import_texture(const std::string& source_path)
{
    ImportResult result;

    // 1. Verify source exists.
    if (!platform::path_exists(source_path.c_str())) {
        result.success   = false;
        result.error_msg = "source file not found: " + source_path;
        return result;
    }

    // 2. Read PNG header for dimensions.
    TextureInfo info;
    if (!read_png_header(source_path, info)) {
        result.success   = false;
        result.error_msg = "failed to read PNG header: " + source_path;
        return result;
    }

    // 3. Determine output path and ensure parent directory exists.
    const std::string output_path = make_import_path(source_path, AssetType::Texture);
    const std::string output_dir  = fs::path(output_path).parent_path().string();

    if (!platform::mkdir_recursive(output_dir.c_str())) {
        result.success   = false;
        result.error_msg = "failed to create output directory: " + output_dir;
        return result;
    }

    // 4. Copy source to .import/textures/.
    if (!copy_file(source_path, output_path)) {
        result.success   = false;
        result.error_msg = "failed to copy texture to: " + output_path;
        return result;
    }

    // 5. Write sidecar with texture metadata.
    nlohmann::json settings = {
        {"source", source_path},
        {"type",   "Texture"},
        {"settings", {
            {"width",             info.width},
            {"height",            info.height},
            {"channels",          info.channels},
            {"srgb",              true},
            {"generate_mipmaps",  true}
        }}
    };

    if (!write_sidecar(output_path, settings)) {
        result.success   = false;
        result.error_msg = "failed to write sidecar for: " + output_path;
        return result;
    }

    result.success     = true;
    result.output_path = output_path;
    return result;
}

// ===========================================================================
// PNG header parsing
// ===========================================================================

// PNG file structure (first 33 bytes):
//   [0-7]   PNG signature:  89 50 4E 47 0D 0A 1A 0A
//   [8-11]  IHDR chunk length (4 bytes, big-endian, must be 13)
//   [12-15] IHDR chunk type: 'I' 'H' 'D' 'R'
//   [16-19] width  (4 bytes, big-endian)
//   [20-23] height (4 bytes, big-endian)
//   [24]    bit depth
//   [25]    color type: 0=grey, 2=RGB, 3=indexed, 4=greyA, 6=RGBA
//   [26]    compression method
//   [27]    filter method
//   [28]    interlace method
//   [29-32] CRC

// PNG color type → channel count mapping.
static uint32_t png_color_type_to_channels(uint8_t color_type) noexcept {
    switch (color_type) {
        case 0: return 1;  // Greyscale
        case 2: return 3;  // RGB
        case 3: return 3;  // Indexed (palette-based, 3 channel output)
        case 4: return 2;  // Greyscale + Alpha
        case 6: return 4;  // RGBA
        default: return 0; // Invalid
    }
}

bool Importer::read_png_header(const std::string& path, TextureInfo& out_info)
{
    // Read at least the first 33 bytes (signature + full IHDR chunk).
    size_t file_size = 0;
    char* raw = platform::file_read_all(path.c_str(), &file_size);
    if (!raw) return false;

    // Must have enough bytes for signature + IHDR.
    if (file_size < 33) {
        std::free(raw);
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(raw);

    // Validate PNG signature.
    static constexpr uint8_t k_png_sig[8] = {0x89, 0x50, 0x4E, 0x47,
                                               0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data, k_png_sig, 8) != 0) {
        std::free(raw);
        return false;
    }

    // Validate IHDR chunk type at bytes 12-15.
    static constexpr uint8_t k_ihdr[4] = {'I', 'H', 'D', 'R'};
    if (std::memcmp(data + 12, k_ihdr, 4) != 0) {
        std::free(raw);
        return false;
    }

    // Extract width and height (big-endian uint32 at offsets 16 and 20).
    const uint32_t width  = (static_cast<uint32_t>(data[16]) << 24) |
                            (static_cast<uint32_t>(data[17]) << 16) |
                            (static_cast<uint32_t>(data[18]) <<  8) |
                             static_cast<uint32_t>(data[19]);

    const uint32_t height = (static_cast<uint32_t>(data[20]) << 24) |
                            (static_cast<uint32_t>(data[21]) << 16) |
                            (static_cast<uint32_t>(data[22]) <<  8) |
                             static_cast<uint32_t>(data[23]);

    // Color type is at offset 25.
    const uint8_t  color_type = data[25];
    const uint32_t channels   = png_color_type_to_channels(color_type);

    std::free(raw);

    if (width == 0 || height == 0 || channels == 0) return false;

    out_info.width    = width;
    out_info.height   = height;
    out_info.channels = channels;
    return true;
}

// ===========================================================================
// Accessors
// ===========================================================================

const std::string& Importer::import_dir() const noexcept
{
    return import_dir_;
}

// ===========================================================================
// Private helpers
// ===========================================================================

std::string Importer::make_import_path(const std::string& source_path,
                                        AssetType type) const
{
    // Extract the bare filename from the source path.
    const std::string filename = fs::path(source_path).filename().string();

    switch (type) {
        case AssetType::Mesh:
            return import_dir_ + "meshes/" + filename;
        case AssetType::Texture:
            return import_dir_ + "textures/" + filename;
        default:
            return import_dir_ + filename;
    }
}

bool Importer::write_sidecar(const std::string& output_path,
                              const nlohmann::json& settings) const
{
    const std::string sidecar_path = output_path + ".import.json";
    const std::string tmp_path     = sidecar_path + ".tmp";

    const std::string json_str = settings.dump(2);

    // Write to .tmp first (atomic write pattern).
    if (!platform::file_write_all(tmp_path.c_str(),
                                  json_str.data(),
                                  json_str.size())) {
        return false;
    }

    // Rename .tmp → final path.
    std::error_code ec;
    fs::rename(tmp_path, sidecar_path, ec);
    if (ec) {
        // Attempt to clean up orphaned .tmp on failure.
        fs::remove(tmp_path, ec);
        return false;
    }

    return true;
}

bool Importer::copy_file(const std::string& src, const std::string& dst)
{
    size_t size = 0;
    char* data  = platform::file_read_all(src.c_str(), &size);
    if (!data) return false;

    const bool ok = platform::file_write_all(dst.c_str(), data, size);
    std::free(data);
    return ok;
}

} // namespace uz
