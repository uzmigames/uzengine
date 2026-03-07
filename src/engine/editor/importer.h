#pragma once

// importer.h -- Asset importer for the editor pipeline.
//
// The Importer converts source assets into compiled artifacts placed under
// the project's `.import/` directory.  A JSON sidecar is written alongside
// each output file so subsequent runs can detect whether re-import is needed.
//
// Supported operations:
//   Mesh    (.glb/.gltf) -- copies to .import/meshes/  + records import settings
//   Texture (.png)       -- copies to .import/textures/ + records width/height/channels
//
// The class is intentionally simple: it delegates real compression (BC7 etc.)
// to the UzBuild offline tool.  The editor importer only needs fast enough
// results to preview assets during development.

#include "engine/editor/asset_registry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace uz {

// ---------------------------------------------------------------------------
// Importer
// ---------------------------------------------------------------------------
class Importer {
public:
    // -----------------------------------------------------------------------
    // Result type returned by every import operation.
    // -----------------------------------------------------------------------
    struct ImportResult {
        bool        success    = false;
        std::string output_path;   // Absolute path of the compiled artifact in .import/
        std::string error_msg;     // Non-empty on failure
    };

    // -----------------------------------------------------------------------
    // Metadata extracted from a PNG file header (IHDR chunk).
    // -----------------------------------------------------------------------
    struct TextureInfo {
        uint32_t width    = 0;
        uint32_t height   = 0;
        uint32_t channels = 0;  // 1=Grey, 2=GreyA, 3=RGB, 4=RGBA
    };

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Construct an Importer rooted at the given project directory.
    /// The `.import/` artifact cache lives at `project_root + ".import/"`.
    explicit Importer(const std::string& project_root);

    // Non-copyable (owns the project_root string, no shared state needed).
    Importer(const Importer&) = delete;
    Importer& operator=(const Importer&) = delete;

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /// Dispatch-import: routes to import_mesh or import_texture based on type.
    /// Returns an error ImportResult for unsupported types.
    ImportResult import_asset(const std::string& source_path, AssetType type);

    /// Import a mesh source file (.glb / .gltf).
    /// Copies the source into `.import/meshes/<filename>` and writes a JSON
    /// sidecar recording import settings.
    ImportResult import_mesh(const std::string& source_path);

    /// Import a texture source file (.png).
    /// Reads the PNG header for dimensions, copies the source into
    /// `.import/textures/<filename>`, and writes a JSON sidecar.
    ImportResult import_texture(const std::string& source_path);

    // -----------------------------------------------------------------------
    // Utility (public for unit-test access)
    // -----------------------------------------------------------------------

    /// Parse the PNG IHDR chunk from the first 33 bytes of the file.
    /// Returns false when the file is missing, truncated, or not a valid PNG.
    /// On success, `out_info` is populated with width, height, and channels.
    static bool read_png_header(const std::string& path, TextureInfo& out_info);

    /// Absolute path of the `.import/` directory for this project root.
    [[nodiscard]] const std::string& import_dir() const noexcept;

private:
    std::string project_root_;  // Absolute project root, always ends with '/'
    std::string import_dir_;    // project_root_ + ".import/"

    // Build the output path inside .import/ for a given source file.
    // Mesh   → .import/meshes/<filename>
    // Texture → .import/textures/<filename>
    [[nodiscard]] std::string make_import_path(const std::string& source_path,
                                                AssetType type) const;

    // Write a JSON sidecar file next to `output_path` as `<output_path>.import.json`.
    // Written atomically: write to a `.tmp` file then rename.
    bool write_sidecar(const std::string& output_path,
                       const nlohmann::json& settings) const;

    // Copy a file from src to dst using the platform file I/O API.
    static bool copy_file(const std::string& src, const std::string& dst);
};

} // namespace uz
