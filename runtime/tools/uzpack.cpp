/// UzEngine Asset Packing Tool
///
/// Packs a directory of assets into .uzpak archives with a .uzpakindex master
/// index for lazy loading at runtime. Uses LZ4 compression (when available)
/// and XOR scramble to protect shipped assets from casual extraction.
///
/// # Usage
/// ```
/// uzpack --input <dir> --output <dir> --name <basename>
///        [--max-pak-mb 500] [--secret <hex64>] [--verbose]
/// ```
///
/// # Output Files
/// - `<output>/<name>.uzpakindex` — master index (loaded at startup)
/// - `<output>/<name>_001.uzpak`, `<name>_002.uzpak`, ... — data archives
///
/// # Example
/// ```
/// uzpack --input assets/ --output build/pak/ --name game --secret DEADBEEF12345678
/// ```

#include "pak_format.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Usage
// ─────────────────────────────────────────────────────────────────────────────

static void PrintUsage(const char* exe) {
    std::fprintf(stderr,
        "Usage: %s --input <dir> --output <dir> --name <basename>\n"
        "       [--max-pak-mb 500] [--secret <hex64>] [--verbose]\n"
        "\nPacks all files in <dir> into .uzpak archives with a .uzpakindex.\n"
        "\nOptions:\n"
        "  --input <dir>       Source directory to scan recursively\n"
        "  --output <dir>      Output directory for .uzpak and .uzpakindex files\n"
        "  --name <basename>   Base name for output files (e.g., 'game')\n"
        "  --max-pak-mb <N>    Maximum size per .uzpak in MB (default: 500)\n"
        "  --secret <hex64>    64-bit XOR scramble key in hexadecimal\n"
        "  --verbose           Print each file as it is added\n"
        "  --help, -h          Show this help message\n",
        exe);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Parse arguments ─────────────────────────────────────────────────────
    std::string inputDir;
    std::string outputDir;
    std::string baseName;
    size_t maxPakMB = 500;
    uint64_t secret = 0;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            inputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            baseName = argv[++i];
        } else if (std::strcmp(argv[i], "--max-pak-mb") == 0 && i + 1 < argc) {
            maxPakMB = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--secret") == 0 && i + 1 < argc) {
            secret = std::strtoull(argv[++i], nullptr, 16);
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // ── Validate required arguments ─────────────────────────────────────────
    if (inputDir.empty() || outputDir.empty() || baseName.empty()) {
        std::fprintf(stderr, "Error: --input, --output, and --name are required.\n");
        PrintUsage(argv[0]);
        return 1;
    }

    if (!fs::is_directory(inputDir)) {
        std::fprintf(stderr, "Error: input directory does not exist: %s\n", inputDir.c_str());
        return 1;
    }

    fs::create_directories(outputDir);

    // ── Scan input directory recursively ────────────────────────────────────
    std::vector<uz::PakWriteEntry> entries;
    size_t totalBytes = 0;

    for (const auto& dirEntry : fs::recursive_directory_iterator(inputDir)) {
        if (!dirEntry.is_regular_file()) continue;

        // Compute relative path with forward slashes for platform-independent lookup
        auto relPath = fs::relative(dirEntry.path(), inputDir).generic_string();

        // Read file contents
        std::ifstream file(dirEntry.path(), std::ios::binary | std::ios::ate);
        if (!file) {
            std::fprintf(stderr, "Warning: cannot read %s, skipping\n",
                         dirEntry.path().string().c_str());
            continue;
        }

        const auto fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<uint8_t> data(fileSize);
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));

        if (verbose) {
            std::printf("  + %s (%zu bytes)\n", relPath.c_str(), data.size());
        }

        totalBytes += data.size();
        entries.push_back({std::move(relPath), std::move(data), 0});
    }

    std::printf("Scanning: %zu files, %zu bytes total\n", entries.size(), totalBytes);

    if (entries.empty()) {
        std::printf("No files found in %s. Writing empty index.\n", inputDir.c_str());
    }

    // ── Pack into split .uzpak archives ─────────────────────────────────────
    const size_t maxPakBytes = maxPakMB * size_t{1024} * 1024;
    auto result = uz::WritePakSplit(entries, outputDir, baseName, maxPakBytes, secret);

    if (!result.success) {
        std::fprintf(stderr, "Error: packing failed.\n");
        return 1;
    }

    // ── Print summary ───────────────────────────────────────────────────────
    std::printf("\nPacked successfully:\n");
    std::printf("  Assets:      %zu\n", entries.size());
    std::printf("  Pak files:   %u\n", result.pakCount);
    std::printf("  Raw size:    %.2f MB\n",
                static_cast<double>(result.totalRawBytes) / (1024.0 * 1024.0));
    std::printf("  Packed size: %.2f MB\n",
                static_cast<double>(result.totalPackedBytes) / (1024.0 * 1024.0));
    if (result.totalRawBytes > 0) {
        std::printf("  Ratio:       %.1f%%\n",
                    100.0 * static_cast<double>(result.totalPackedBytes)
                          / static_cast<double>(result.totalRawBytes));
    }
    std::printf("  Index:       %s\n", result.indexPath.c_str());

    return 0;
}
