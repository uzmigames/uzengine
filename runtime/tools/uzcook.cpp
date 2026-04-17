/// UzEngine Asset Cook Tool
///
/// Batch-imports source assets (HLSL shaders) into engine-native formats
/// (.uzshader), stores them in the content-addressed cache (.cache/),
/// writes import sidecars (.uzimport), and optionally packs everything
/// into a .uzpak archive with a .uzmanifest for runtime loading.
///
/// # Usage
/// ```
/// uzcook --source <dir> --output <dir> [--pak <name>] [--no-compress]
///
/// Options:
///   --source <dir>    Directory containing source assets (HLSL, etc.)
///   --output <dir>    Output directory for .cache/, .uzimport, .uzpak
///   --pak <name>      Optional: pack all cooked assets into <name>.uzpak
///   --verbose         Enable debug logging
///   --no-compress     Skip BC texture compression (keep RGBA8 uncompressed)
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
#include "fbx_importer.h"
#include "mesh_importer.h"
#include "shader_importer.h"
#include "texture_importer.h"
#include "asset_cache.h"
#include "import_sidecar.h"
#include "uzshader_format.h"
#include "pak_format.h"
#include "asset_manifest.h"
#include "shader_compiler.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
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
    bool noCompress = false;   ///< Skip BC compression for textures (keep RGBA8)
    bool texturesOnly = false; ///< Only register TextureImporter (skip FBX/mesh/shader)
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
        } else if (arg == "--no-compress") {
            args.noCompress = true;
        } else if (arg == "--textures-only") {
            args.texturesOnly = true;
        }
    }

    if (args.sourceDir.empty() || args.outputDir.empty()) {
        std::cerr << "Usage: uzcook --source <dir> --output <dir> [--pak <name>] [--verbose] [--no-compress]\n";
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
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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
// Texture format selection
// ─────────────────────────────────────────────────────────────────────────────

/// Determines the optimal GPU compression format and sRGB flag for a texture
/// based on its filename. Uses naming conventions to identify the texture's
/// role (albedo, normal, roughness, etc.) and selects the best BC format.
///
/// Convention (case-insensitive suffix matching):
///   Albedo/BaseColor/COL/Diffuse  -> BC7 sRGB  (highest quality color)
///   Normal/NRM/NORM               -> BC5 linear (2-channel direction data)
///   Roughness/ROUGH               -> BC4 linear (single channel)
///   Metallic/METAL/METALNESS      -> BC4 linear (single channel)
///   AO/AmbientOcclusion/Occlusion -> BC4 linear (single channel)
///   Emissive/Emission             -> BC7 sRGB  (HDR color data)
///   ORM (packed AO+Rough+Metal)   -> BC7 linear (multi-channel scalar)
///   Opacity/Alpha/Mask            -> BC4 linear (single channel)
///   Unknown/other                 -> BC7 sRGB  (safe default)
static void SelectTextureFormat(const std::string& filename,
                                uz::TextureFormat& outFormat,
                                bool& outSRGB)
{
    // Convert filename to lowercase for case-insensitive matching
    std::string lower = filename;
    for (auto& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Strip extension for matching — avoids false positives from extensions
    // like ".metallic" or path components
    const auto dot = lower.rfind('.');
    if (dot != std::string::npos) {
        lower = lower.substr(0, dot);
    }

    // Priority order: most specific slot identifiers first. Albedo/color
    // keywords checked BEFORE metallic/roughness because filenames like
    // "MetalCorrodedHeavy001_COL_2K_METALNESS.jpg" contain both _COL_ and
    // METALNESS — the _COL_ indicates this IS the color map (the METALNESS
    // suffix is the texture set name, not the slot type).

    // Albedo/BaseColor/Diffuse/COL — BC7 sRGB (highest quality color).
    if (lower.find("basecolor") != std::string::npos ||
        lower.find("albedo") != std::string::npos ||
        lower.find("diffuse") != std::string::npos ||
        lower.find("_col_") != std::string::npos ||
        lower.find("_col.") != std::string::npos) {
        outFormat = uz::TextureFormat::BC7;
        outSRGB = true;
        return;
    }

    // ORM packed texture — BC7 linear (3 independent scalar channels).
    if (lower.find("_orm") != std::string::npos ||
        lower.find("orm_") != std::string::npos) {
        outFormat = uz::TextureFormat::BC7;
        outSRGB = false;
        return;
    }

    // Normal maps — BC5 (2 channels: X,Y; Z reconstructed in shader).
    if (lower.find("normal") != std::string::npos ||
        lower.find("_nrm") != std::string::npos ||
        lower.find("_norm") != std::string::npos) {
        outFormat = uz::TextureFormat::BC5;
        outSRGB = false;
        return;
    }

    // Emissive — BC7 sRGB (color data).
    if (lower.find("emissive") != std::string::npos ||
        lower.find("emission") != std::string::npos) {
        outFormat = uz::TextureFormat::BC7;
        outSRGB = true;
        return;
    }

    // Roughness — BC4 (single channel, linear).
    if (lower.find("roughness") != std::string::npos ||
        lower.find("_rough") != std::string::npos) {
        outFormat = uz::TextureFormat::BC4;
        outSRGB = false;
        return;
    }

    // Metallic — BC4 (single channel, linear).
    if (lower.find("metallic") != std::string::npos ||
        lower.find("metalness") != std::string::npos ||
        lower.find("_metal") != std::string::npos) {
        outFormat = uz::TextureFormat::BC4;
        outSRGB = false;
        return;
    }

    // Ambient occlusion — BC4 (single channel, linear).
    if (lower.find("ambientocclusion") != std::string::npos ||
        lower.find("occlusion") != std::string::npos ||
        lower.find("_ao") != std::string::npos) {
        outFormat = uz::TextureFormat::BC4;
        outSRGB = false;
        return;
    }

    // Opacity/alpha/mask — BC4 (single channel, linear).
    if (lower.find("opacity") != std::string::npos ||
        lower.find("alpha") != std::string::npos ||
        lower.find("_mask") != std::string::npos) {
        outFormat = uz::TextureFormat::BC4;
        outSRGB = false;
        return;
    }

    // Default: BC7 sRGB — safe fallback providing best quality for unknown
    // texture types. sRGB is the common case for authored textures.
    outFormat = uz::TextureFormat::BC7;
    outSRGB = true;
}

/// Returns a human-readable name for a TextureFormat enum value.
/// Used for logging during the cook pipeline.
[[nodiscard]] static const char* TextureFormatName(uz::TextureFormat fmt) {
    switch (fmt) {
        case uz::TextureFormat::BC1:                return "BC1";
        case uz::TextureFormat::BC3:                return "BC3";
        case uz::TextureFormat::BC4:                return "BC4";
        case uz::TextureFormat::BC5:                return "BC5";
        case uz::TextureFormat::BC7:                return "BC7";
        case uz::TextureFormat::Uncompressed_RGBA8: return "RGBA8";
        default:                                    return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main cook pipeline
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    uz::LogInit();

    CookArgs args;
    if (!ParseArgs(argc, argv, args)) {
        return 1;
    }

    UZ_LOG(Cook, Info, "Cook starting: source='{}', output='{}'",
           args.sourceDir, args.outputDir);

    // Get uzcook binary modification time for stale-cache detection.
    // If uzcook was recompiled (importer code changed), sidecars older
    // than the binary are stale even if source hash and version match.
    fs::file_time_type cookBinaryTime{};
    {
        std::error_code ec;
        cookBinaryTime = fs::last_write_time(fs::path(argv[0]), ec);
        if (ec) {
            UZ_LOG(Cook, Warning, "Cannot stat uzcook binary — staleness check disabled");
        }
    }

    // Check if source dir contains any shaders before initializing DXC.
    // DXC LoadLibrary can hang if dxcompiler.dll isn't in the search path,
    // so we skip it entirely when there are no .hlsl files to compile.
    bool hasHlslFiles = false;
    {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(args.sourceDir, ec)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".hlsl" || ext == ".glsl") {
                    hasHlslFiles = true;
                    break;
                }
            }
        }
    }

    uz::ShaderCompiler compiler;
    bool hasShaderCompiler = false;
    if (hasHlslFiles) {
        hasShaderCompiler = compiler.Init();
        if (!hasShaderCompiler) {
            UZ_LOG(Cook, Warning, "DXC shader compiler unavailable — shader import disabled");
        }
    }

    // Register importers
    uz::AssetImporterRegistry registry;
    if (hasShaderCompiler) {
        registry.Register(std::make_unique<uz::ShaderImporter>(compiler));
    }
    if (!args.texturesOnly) {
        registry.Register(std::make_unique<uz::MeshImporter>());
        registry.Register(std::make_unique<uz::FbxImporter>());
    }
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

    std::atomic<uint32_t> imported{0};
    std::atomic<uint32_t> skipped{0};
    std::atomic<uint32_t> failed{0};
    std::mutex cookedMutex; // protects cookedAssets + cache + sidecar writes

    // ── Collect import tasks ─────────────────────────────────────────────
    // Each task captures everything needed to import one file independently.
    struct ImportTask {
        std::string sourcePath;
        std::string logicalPath;
        std::string ext;
        uz::IAssetImporter* importer;
        std::vector<uint8_t> sourceBytes;
        uint64_t sourceHash;
        std::string sourceHashHex;
        uz::AssetImportOptions options;
    };
    std::vector<ImportTask> tasks;

    for (const auto& sourcePath : sourceFiles) {
        fs::path relPath = fs::relative(fs::path(sourcePath), fs::path(args.sourceDir));
        std::string logicalPath = relPath.string();
        for (auto& c : logicalPath) {
            if (c == '\\') c = '/';
        }

        std::string ext = fs::path(sourcePath).extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto* importer = registry.FindByExtension(ext);
        if (!importer) continue;

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
        bool canSkip = sidecarResult.success &&
            !uz::NeedsReimport(sidecarResult.sidecar, sourceHashHex, importer->Version());

        // Binary staleness: if uzcook was recompiled after the sidecar was
        // written, importer code may have changed — force reimport.
        if (canSkip) {
            std::error_code ec;
            auto sidecarTime = fs::last_write_time(sidecarPath, ec);
            if (!ec && cookBinaryTime > sidecarTime) {
                UZ_LOG(Cook, Info, "Reimport: uzcook binary newer than sidecar for '{}'",
                       logicalPath);
                canSkip = false;
            }
        }

        // Companion file check: if the output .uzmesh/.uztexture was deleted
        // but the sidecar still exists, the cache is stale.
        if (canSkip) {
            std::string importerName = importer->Name();
            std::string companionExt;
            if (importerName == "mesh" || importerName == "fbx") companionExt = ".uzmesh";
            else if (importerName == "texture") companionExt = ".uztexture";

            if (!companionExt.empty()) {
                auto dot = sourcePath.rfind('.');
                std::string companionPath = (dot != std::string::npos)
                    ? sourcePath.substr(0, dot) + companionExt
                    : sourcePath + companionExt;
                if (!fs::exists(companionPath)) {
                    UZ_LOG(Cook, Info, "Reimport: companion file missing '{}'", companionPath);
                    canSkip = false;
                }
            }
        }

        if (canSkip) {
            ++skipped;
            std::lock_guard<std::mutex> lock(cookedMutex);
            for (const auto& [variant, outputPath] : sidecarResult.sidecar.outputs) {
                auto outputBytes = ReadFileBytes(outputPath);
                if (!outputBytes.empty()) {
                    std::string uzExt = "." + variant + ".uzshader";
                    cookedAssets.push_back({
                        logicalPath + uzExt, outputPath, outputBytes.size(),
                        uz::AssetType::Shader, sidecarResult.sidecar.dependencies,
                    });
                }
            }
            continue;
        }

        uz::AssetImportOptions options;

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
                ++failed;
                continue;
            }
        }

        if (std::string(importer->Name()) == "texture" && !args.noCompress) {
            SelectTextureFormat(logicalPath, options.textureCompression, options.textureSRGB);
        }

        tasks.push_back({sourcePath, logicalPath, ext, importer,
                          std::move(sourceBytes), sourceHash, sourceHashHex, options});
    }

    // ── Run imports in parallel ──────────────────────────────────────────
    const unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
    UZ_LOG(Cook, Info, "Cooking {} assets on {} threads", tasks.size(), numThreads);

    std::vector<std::future<void>> futures;
    futures.reserve(tasks.size());

    for (auto& task : tasks) {
        futures.push_back(std::async(std::launch::async, [&, t = &task]() {
            auto result = t->importer->Import(t->sourceBytes, t->logicalPath, t->options);
            if (!result.success) {
                ++failed;
                return;
            }

            // ── Post-import: cache, sidecar, companion file (under lock) ──
            std::string optionsStr = t->options.stage + t->options.model +
                                     t->options.entryPoint + t->options.format +
                                     std::to_string(static_cast<uint32_t>(t->options.textureCompression)) +
                                     (t->options.textureSRGB ? "srgb" : "linear");
            uint64_t optionsHash = uz::AssetCache::HashString(optionsStr);
            uint64_t cacheKey = uz::AssetCache::ComputeKey(
                t->importer->Name(), t->importer->Version(), t->sourceHash, optionsHash);

            std::string importerName = t->importer->Name();
            std::string outputExt = ".bin";
            uz::AssetType assetType = uz::AssetType::Generic;
            if (importerName == "shader") { outputExt = ".uzshader"; assetType = uz::AssetType::Shader; }
            else if (importerName == "mesh" || importerName == "fbx") { outputExt = ".uzmesh"; assetType = uz::AssetType::Mesh; }
            else if (importerName == "texture") { outputExt = ".uztexture"; assetType = uz::AssetType::Texture; }

            // Importer may override the primary output extension based on the
            // input content (e.g., FBX with animation but no mesh → .uzanim).
            if (!result.primaryExtensionOverride.empty()) {
                outputExt = result.primaryExtensionOverride;
                if (outputExt == ".uzanim" || outputExt == ".uzskel") {
                    assetType = uz::AssetType::Generic;
                } else if (outputExt == ".uzmesh") {
                    assetType = uz::AssetType::Mesh;
                } else if (outputExt == ".uztexture") {
                    assetType = uz::AssetType::Texture;
                } else if (outputExt == ".uzshader") {
                    assetType = uz::AssetType::Shader;
                }
            }

            // Write companion file (filesystem, no lock needed — unique per file).
            // Mesh/texture imports always emit a companion next to the source.
            // FBX animation-only imports also emit a companion (.uzanim) so the
            // dev-mode LooseFileVFS can find them at their logical VFS path.
            const bool writeCompanion =
                (importerName == "texture" || importerName == "mesh" ||
                 importerName == "fbx");
            if (writeCompanion) {
                std::string companionPath = t->sourcePath;
                auto compDot = companionPath.rfind('.');
                if (compDot != std::string::npos) {
                    companionPath = companionPath.substr(0, compDot) + outputExt;
                }
                std::ofstream out(companionPath, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(result.data.data()),
                              static_cast<std::streamsize>(result.data.size()));
                    UZ_LOG(Cook, Info, "Wrote companion: '{}' ({} bytes)",
                           companionPath, result.data.size());
                } else {
                    UZ_LOG(Cook, Error, "Failed to write companion: '{}'", companionPath);
                }
            }

            // Shared state updates under lock
            {
                std::lock_guard<std::mutex> lock(cookedMutex);

                cache.Store(cacheKey, result.data);

                uz::ImportSidecar sidecar;
                sidecar.importer = importerName;
                sidecar.version = t->importer->Version();
                sidecar.sourceHash = t->sourceHashHex;
                sidecar.options = {
                    {"stage", t->options.stage}, {"model", t->options.model},
                    {"entry_point", t->options.entryPoint},
                    {"texture_format", TextureFormatName(t->options.textureCompression)},
                    {"texture_srgb", t->options.textureSRGB ? "true" : "false"},
                };
                std::string cachePath = cache.CacheDirectory() + "/imported/" +
                                        uz::HashToHex(cacheKey) + ".bin";
                sidecar.outputs = {{t->options.model, cachePath}};
                uz::WriteSidecarFile(t->sourcePath + ".uzimport", sidecar);

                std::string outputLogical = t->logicalPath;
                auto dotPos = outputLogical.rfind('.');
                if (dotPos != std::string::npos) {
                    outputLogical = outputLogical.substr(0, dotPos) + outputExt;
                }
                cookedAssets.push_back({outputLogical, cachePath, result.data.size(), assetType, {}});

                for (const auto& sc : result.sidecars) {
                    std::string sidecarLogical = t->logicalPath;
                    auto scDot = sidecarLogical.rfind('.');
                    if (scDot != std::string::npos) {
                        sidecarLogical = sidecarLogical.substr(0, scDot) + sc.extension;
                    }
                    uint64_t scCacheKey = uz::AssetCache::ComputeKey(
                        (importerName + "_sidecar").c_str(),
                        t->importer->Version(), t->sourceHash, optionsHash);
                    cache.Store(scCacheKey, sc.data);
                    std::string scCachePath = cache.CacheDirectory() + "/imported/" +
                                              uz::HashToHex(scCacheKey) + ".bin";
                    cookedAssets.push_back({sidecarLogical, scCachePath, sc.data.size(),
                                            uz::AssetType::Generic, {}});

                    // Also emit sidecars as companion files next to the source
                    // so dev-mode LooseFileVFS can resolve them. FBX may produce
                    // .uzskel / .uzanim alongside the primary .uzmesh / .uzanim.
                    if (importerName == "fbx") {
                        std::string scCompanionPath = t->sourcePath;
                        auto scCompDot = scCompanionPath.rfind('.');
                        if (scCompDot != std::string::npos) {
                            scCompanionPath = scCompanionPath.substr(0, scCompDot) + sc.extension;
                        }
                        std::ofstream scOut(scCompanionPath, std::ios::binary);
                        if (scOut.is_open()) {
                            scOut.write(reinterpret_cast<const char*>(sc.data.data()),
                                        static_cast<std::streamsize>(sc.data.size()));
                            UZ_LOG(Cook, Info, "Wrote sidecar companion: '{}' ({} bytes)",
                                   scCompanionPath, sc.data.size());
                        }
                    }
                }
            }
            ++imported;
        }));
    }

    // Wait for all imports to complete
    for (auto& f : futures) {
        f.get();
    }

    UZ_LOG(Cook, Info, "Cook complete: imported={}, skipped={}, failed={}",
           imported.load(), skipped.load(), failed.load());

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

    return failed.load() > 0 ? 1 : 0;
}

#else
// uzcook requires DXC which is Windows-only
#include <cstdio>
int main() {
    std::fprintf(stderr, "uzcook is only available on Windows (requires DXC)\n");
    return 1;
}
#endif
