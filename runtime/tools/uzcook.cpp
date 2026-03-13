/// UzEngine Asset Cook Tool
///
/// Batch-imports source assets (HLSL shaders) into engine-native formats
/// (.uzshader), stores them in the content-addressed cache (.cache/),
/// writes import sidecars (.uzimport), and optionally packs everything
/// into a .uzpak archive with a .uzmanifest for runtime loading.
///
/// # Usage
/// ```
/// uzcook --source <dir> --output <dir> [--pak <name>]
///
/// Options:
///   --source <dir>    Directory containing source assets (HLSL, etc.)
///   --output <dir>    Output directory for .cache/, .uzimport, .uzpak
///   --pak <name>      Optional: pack all cooked assets into <name>.uzpak
///   --verbose         Enable debug logging
/// ```
///
/// # Cook Pipeline
/// 1. Scan `--source` directory for importable files (by extension)
/// 2. For each file: check .uzimport sidecar — skip if up-to-date
/// 3. Import via registered importer (ShaderImporter for .hlsl)
/// 4. Store compiled output in .cache/imported/
/// 5. Write/update .uzimport sidecar
/// 6. If `--pak` specified: pack all outputs into .uzpak + .uzmanifest

#ifdef _WIN32

#include "log.h"
#include "asset_importer.h"
#include "asset_registry.h"
#include "mesh_importer.h"
#include "shader_importer.h"
#include "texture_importer.h"
#include "asset_cache.h"
#include "import_sidecar.h"
#include "uzshader_format.h"
#include "pak_format.h"
#include "asset_manifest.h"
#include "shader_compiler.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

UZ_DECLARE_LOG_CATEGORY(Cook);
UZ_DEFINE_LOG_CATEGORY(Cook, Info);

// ─────────────────────────────────────────────────────────────────────────────
// CLI argument parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Parsed command-line arguments for the cook tool.
struct CookArgs {
    std::string sourceDir;     ///< Source asset directory to scan
    std::string outputDir;     ///< Output directory for cache, sidecars, pak
    std::string pakName;       ///< Optional .uzpak archive name (empty = no pak)
    bool verbose = false;      ///< Enable debug-level logging
};

/// Parses CLI arguments into a CookArgs struct.
///
/// # Returns
/// `true` if required arguments (--source, --output) were provided.
/// Prints usage to stderr and returns `false` on missing args.
static bool ParseArgs(int argc, char** argv, CookArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--source" && i + 1 < argc) {
            args.sourceDir = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.outputDir = argv[++i];
        } else if (arg == "--pak" && i + 1 < argc) {
            args.pakName = argv[++i];
        } else if (arg == "--verbose") {
            args.verbose = true;
        }
    }

    if (args.sourceDir.empty() || args.outputDir.empty()) {
        std::cerr << "Usage: uzcook --source <dir> --output <dir> [--pak <name>] [--verbose]\n";
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// File scanning
// ─────────────────────────────────────────────────────────────────────────────

/// Scans a directory recursively for files matching registered importer extensions.
///
/// # Parameters
/// - `dir`: Root directory to scan.
/// - `registry`: Importer registry to check extensions against.
///
/// # Returns
/// List of absolute paths to importable source files.
static std::vector<std::string> ScanSourceFiles(
    const std::string& dir,
    const uz::AssetImporterRegistry& registry) {

    std::vector<std::string> files;
    std::error_code ec;

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (registry.FindByExtension(ext) != nullptr) {
            files.push_back(entry.path().string());
        }
    }

    if (ec) {
        UZ_LOG(Cook, Error, "Directory scan failed: dir='{}', error='{}'", dir, ec.message());
    }

    return files;
}

/// Reads a file's entire content into a byte vector.
///
/// # Returns
/// File bytes on success, empty vector on failure.
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

// ─────────────────────────────────────────────────────────────────────────────
// Main cook pipeline
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // Logging auto-initializes via static constructors

    CookArgs args;
    if (!ParseArgs(argc, argv, args)) {
        return 1;
    }

    UZ_LOG(Cook, Info, "Cook starting: source='{}', output='{}'",
           args.sourceDir, args.outputDir);

    // Initialize shader compiler
    uz::ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Cook, Error, "Failed to initialize DXC shader compiler");
        return 1;
    }

    // Register importers
    uz::AssetImporterRegistry registry;
    registry.Register(std::make_unique<uz::ShaderImporter>(compiler));
    registry.Register(std::make_unique<uz::MeshImporter>());
    registry.Register(std::make_unique<uz::TextureImporter>());

    // Initialize cache
    std::string cacheDir = args.outputDir + "/.cache";
    uz::AssetCache cache(cacheDir);

    // Scan for source files
    auto sourceFiles = ScanSourceFiles(args.sourceDir, registry);
    UZ_LOG(Cook, Info, "Found {} importable source files", sourceFiles.size());

    // Track cooked outputs for pak generation
    struct CookedAsset {
        std::string logicalPath;       ///< Relative path from source dir
        std::string cachePath;         ///< Absolute path to cached output
        uint64_t size;                 ///< Output size in bytes
        uz::AssetType type;            ///< Asset type for manifest
        std::vector<std::string> deps; ///< Dependencies
    };
    std::vector<CookedAsset> cookedAssets;

    uint32_t imported = 0;
    uint32_t skipped = 0;
    uint32_t failed = 0;

    for (const auto& sourcePath : sourceFiles) {
        // Compute relative path for logical naming
        fs::path relPath = fs::relative(fs::path(sourcePath), fs::path(args.sourceDir));
        std::string logicalPath = relPath.string();
        // Normalize to forward slashes
        for (auto& c : logicalPath) {
            if (c == '\\') c = '/';
        }

        // Find importer
        std::string ext = fs::path(sourcePath).extension().string();
        auto* importer = registry.FindByExtension(ext);
        if (!importer) continue;

        // Read source bytes and compute hash
        auto sourceBytes = ReadFileBytes(sourcePath);
        if (sourceBytes.empty()) {
            UZ_LOG(Cook, Error, "Cannot read source: path='{}'", sourcePath);
            ++failed;
            continue;
        }

        uint64_t sourceHash = uz::AssetCache::HashBytes(sourceBytes.data(), sourceBytes.size());
        std::string sourceHashHex = uz::HashToHex(sourceHash);

        // Check sidecar for skip
        std::string sidecarPath = sourcePath + ".uzimport";
        auto sidecarResult = uz::ReadSidecarFile(sidecarPath);
        if (sidecarResult.success &&
            !uz::NeedsReimport(sidecarResult.sidecar, sourceHashHex, importer->Version())) {
            UZ_LOG(Cook, Debug, "Up-to-date, skipping: '{}'", logicalPath);
            ++skipped;

            // Still track for pak generation
            for (const auto& [variant, outputPath] : sidecarResult.sidecar.outputs) {
                auto outputBytes = ReadFileBytes(outputPath);
                if (!outputBytes.empty()) {
                    std::string uzExt = "." + variant + ".uzshader";
                    cookedAssets.push_back({
                        logicalPath + uzExt,
                        outputPath,
                        outputBytes.size(),
                        uz::AssetType::Shader,
                        sidecarResult.sidecar.dependencies,
                    });
                }
            }
            continue;
        }

        // Import
        uz::AssetImportOptions options;

        // Shader-specific options: determine stage from filename convention
        if (std::string(importer->Name()) == "shader") {
            options.entryPoint = "main";
            options.format = "dxil";
            options.model = "sm6_6";

            if (logicalPath.find("_vs") != std::string::npos ||
                logicalPath.find("_vert") != std::string::npos) {
                options.stage = "vertex";
            } else if (logicalPath.find("_ps") != std::string::npos ||
                       logicalPath.find("_frag") != std::string::npos ||
                       logicalPath.find("_pixel") != std::string::npos) {
                options.stage = "pixel";
            } else if (logicalPath.find("_cs") != std::string::npos ||
                       logicalPath.find("_comp") != std::string::npos) {
                options.stage = "compute";
            } else {
                UZ_LOG(Cook, Error, "Cannot determine shader stage from filename: '{}'", logicalPath);
                ++failed;
                continue;
            }
        }

        UZ_LOG(Cook, Info, "Importing: '{}' (stage={}, model={})",
               logicalPath, options.stage, options.model);

        auto result = importer->Import(sourceBytes, logicalPath, options);
        if (!result.success) {
            UZ_LOG(Cook, Error, "Import failed: '{}': {}", logicalPath, result.error);
            ++failed;
            continue;
        }

        // Compute cache key and store
        uint64_t optionsHash = uz::AssetCache::HashString(
            options.stage + options.model + options.entryPoint + options.format);
        uint64_t cacheKey = uz::AssetCache::ComputeKey(
            importer->Name(), importer->Version(), sourceHash, optionsHash);

        cache.Store(cacheKey, result.data);

        // Write sidecar
        uz::ImportSidecar sidecar;
        sidecar.importer = importer->Name();
        sidecar.version = importer->Version();
        sidecar.sourceHash = sourceHashHex;
        sidecar.options = {
            {"stage", options.stage},
            {"model", options.model},
            {"entry_point", options.entryPoint},
        };

        std::string cachePath = cache.CacheDirectory() + "/imported/" +
                                uz::HashToHex(cacheKey) + ".bin";
        sidecar.outputs = {{options.model, cachePath}};

        uz::WriteSidecarFile(sidecarPath, sidecar);

        // Determine output extension and asset type from importer
        std::string outputExt = ".bin";
        uz::AssetType assetType = uz::AssetType::Generic;
        std::string importerName = importer->Name();
        if (importerName == "shader") {
            outputExt = ".uzshader";
            assetType = uz::AssetType::Shader;
        } else if (importerName == "mesh") {
            outputExt = ".uzmesh";
            assetType = uz::AssetType::Mesh;
        } else if (importerName == "texture") {
            outputExt = ".uztexture";
            assetType = uz::AssetType::Texture;
        }

        // Track for pak
        std::string outputLogical = logicalPath;
        auto dotPos = outputLogical.rfind('.');
        if (dotPos != std::string::npos) {
            outputLogical = outputLogical.substr(0, dotPos) + outputExt;
        }

        cookedAssets.push_back({
            outputLogical,
            cachePath,
            result.data.size(),
            assetType,
            {},
        });

        ++imported;
    }

    UZ_LOG(Cook, Info, "Cook complete: imported={}, skipped={}, failed={}",
           imported, skipped, failed);

    // Pack into .uzpak if requested
    if (!args.pakName.empty() && !cookedAssets.empty()) {
        std::vector<uz::PakWriteEntry> pakEntries;
        uz::AssetManifestWriter manifestWriter;

        for (const auto& asset : cookedAssets) {
            auto data = ReadFileBytes(asset.cachePath);
            if (data.empty()) {
                UZ_LOG(Cook, Error, "Cannot read cached asset for pak: '{}'", asset.cachePath);
                continue;
            }

            pakEntries.push_back({asset.logicalPath, std::move(data), 0});
            manifestWriter.AddEntry({
                asset.logicalPath,
                asset.size,
                asset.type,
                asset.deps,
            });
        }

        std::string pakPath = args.outputDir + "/" + args.pakName + ".uzpak";
        std::string manifestPath = args.outputDir + "/" + args.pakName + ".uzmanifest";

        if (uz::WritePakFile(pakPath, pakEntries)) {
            UZ_LOG(Cook, Info, "Wrote pak: '{}' ({} entries)", pakPath, pakEntries.size());
        } else {
            UZ_LOG(Cook, Error, "Failed to write pak: '{}'", pakPath);
        }

        if (uz::WriteManifestFile(manifestPath, manifestWriter)) {
            UZ_LOG(Cook, Info, "Wrote manifest: '{}' ({} entries)",
                   manifestPath, manifestWriter.EntryCount());
        } else {
            UZ_LOG(Cook, Error, "Failed to write manifest: '{}'", manifestPath);
        }
    }

    return failed > 0 ? 1 : 0;
}

#else
// uzcook requires DXC which is Windows-only
#include <cstdio>
int main() {
    std::fprintf(stderr, "uzcook is only available on Windows (requires DXC)\n");
    return 1;
}
#endif
