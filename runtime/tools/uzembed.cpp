/// UzEngine Shader Embed Tool
///
/// Converts `.hlsl` source files into C++ headers containing `constexpr char[]`
/// literals, plus generates an index header (`engine_shaders.gen.h`) with the
/// full enum, registry, and all includes. This eliminates fragile relative-path
/// shader loading at runtime — engine shaders are compiled into the binary at
/// build time.
///
/// # Usage
/// ```
/// uzembed --input-dir <dir> --output-dir <dir>
/// ```
///
/// # Generated Files
/// For each `.hlsl` input:
/// ```
/// overlay_vs.hlsl → overlay_vs.hlsl.gen.h  (constexpr char kOverlayVS[])
/// ```
///
/// Plus one index file:
/// ```
/// engine_shaders.gen.h  (includes all .gen.h, enum EngineShader, struct EngineShaders)
/// ```

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Name Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a snake_case filename stem to a PascalCase name.
///
/// Examples:
///   "overlay_vs"      → "OverlayVS"
///   "overlay_text_ps" → "OverlayTextPS"
///   "grid_ps"         → "GridPS"
///
/// Shader stage suffixes (_vs, _ps, _cs, _gs, _hs, _ds, _as, _ms) are
/// uppercased entirely. All other segments are Title Cased.
static std::string StemToPascal(const std::string& stem) {
    std::vector<std::string> parts;
    std::istringstream ss(stem);
    std::string part;
    while (std::getline(ss, part, '_')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    auto isShaderStage = [](const std::string& s) -> bool {
        return s == "vs" || s == "ps" || s == "cs" ||
               s == "gs" || s == "hs" || s == "ds" ||
               s == "as" || s == "ms";
    };

    std::string result;
    for (auto& p : parts) {
        if (isShaderStage(p)) {
            for (auto& c : p) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        } else {
            p[0] = static_cast<char>(toupper(static_cast<unsigned char>(p[0])));
        }
        result += p;
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Entry — tracks each processed shader for index generation
// ─────────────────────────────────────────────────────────────────────────────

struct ShaderEntry {
    std::string relIncludePath;  ///< e.g. "editor/overlay_vs.hlsl.gen.h"
    std::string constName;       ///< e.g. "kOverlayVS"
    std::string enumName;        ///< e.g. "OverlayVS"
    std::string fileName;        ///< e.g. "overlay_vs.hlsl"
};

// ─────────────────────────────────────────────────────────────────────────────
// File Processing
// ─────────────────────────────────────────────────────────────────────────────

/// Reads an entire file into a string.
static std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

/// Generates a `.hlsl.gen.h` header from an HLSL source file.
///
/// # Returns
/// `true` on success, `false` on I/O error.
static bool GenerateHeader(const fs::path& inputPath, const fs::path& outputPath,
                           const std::string& constName) {
    std::string source = ReadFile(inputPath);
    if (source.empty()) {
        fprintf(stderr, "uzembed: failed to read '%s'\n", inputPath.string().c_str());
        return false;
    }

    while (!source.empty() && (source.back() == '\n' || source.back() == '\r')) {
        source.pop_back();
    }

    std::string filename = inputPath.filename().string();

    std::error_code ec;
    fs::create_directories(outputPath.parent_path(), ec);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        fprintf(stderr, "uzembed: failed to write '%s'\n", outputPath.string().c_str());
        return false;
    }

    out << "// " << filename << ".gen.h -- AUTO-GENERATED, DO NOT EDIT\n";
    out << "#pragma once\n";
    out << "namespace uz::generated {\n";
    out << "inline constexpr char " << constName << "[] = R\"hlsl(\n";
    out << source << "\n";
    out << ")hlsl\";\n";
    out << "} // namespace uz::generated\n";

    return true;
}

/// Generates the `engine_shaders.gen.h` index header.
///
/// Contains:
/// - `#include` for every individual `.hlsl.gen.h`
/// - `enum class EngineShader` with one entry per shader
/// - `struct EngineShaders` with `GetSource()` and `GetName()` accessors
static bool GenerateIndex(const fs::path& outputDir,
                          const std::vector<ShaderEntry>& entries) {
    fs::path indexPath = outputDir / "engine_shaders.gen.h";

    std::error_code ec;
    fs::create_directories(outputDir, ec);

    std::ofstream out(indexPath, std::ios::binary);
    if (!out) {
        fprintf(stderr, "uzembed: failed to write '%s'\n", indexPath.string().c_str());
        return false;
    }

    out << "// engine_shaders.gen.h -- AUTO-GENERATED, DO NOT EDIT\n";
    out << "//\n";
    out << "// Generated by uzembed. Provides compile-time access to all embedded\n";
    out << "// engine shader sources via the EngineShaders registry.\n";
    out << "#pragma once\n\n";

    // Includes
    for (const auto& e : entries) {
        out << "#include \"" << e.relIncludePath << "\"\n";
    }

    out << "\nnamespace uz {\n\n";

    // Enum
    out << "/// Identifies a built-in engine shader.\n";
    out << "enum class EngineShader {\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "    " << entries[i].enumName;
        if (i + 1 < entries.size()) out << ",";
        out << "\n";
    }
    out << "};\n\n";

    // Registry struct
    out << "/// Static registry for built-in engine shader sources.\n";
    out << "///\n";
    out << "/// All shader sources are embedded as constexpr char[] at compile time.\n";
    out << "/// No file I/O or VFS mount is needed.\n";
    out << "struct EngineShaders {\n";

    // GetSource
    out << "    /// Returns the HLSL source code for the given engine shader.\n";
    out << "    static constexpr const char* GetSource(EngineShader shader) {\n";
    out << "        switch (shader) {\n";
    for (const auto& e : entries) {
        out << "            case EngineShader::" << e.enumName
            << ": return generated::" << e.constName << ";\n";
    }
    out << "        }\n";
    out << "        return \"\";\n";
    out << "    }\n\n";

    // GetName
    out << "    /// Returns the filename for the shader (for error messages and stage inference).\n";
    out << "    static constexpr const char* GetName(EngineShader shader) {\n";
    out << "        switch (shader) {\n";
    for (const auto& e : entries) {
        out << "            case EngineShader::" << e.enumName
            << ": return \"" << e.fileName << "\";\n";
    }
    out << "        }\n";
    out << "        return \"unknown.hlsl\";\n";
    out << "    }\n";

    out << "};\n\n";
    out << "} // namespace uz\n";

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI
// ─────────────────────────────────────────────────────────────────────────────

static void PrintUsage() {
    fprintf(stderr,
        "Usage: uzembed --input-dir <dir> --output-dir <dir>\n"
        "\n"
        "Converts all .hlsl files in <input-dir> to .hlsl.gen.h headers in <output-dir>.\n"
        "Also generates engine_shaders.gen.h with enum + registry for all shaders.\n");
}

int main(int argc, char* argv[]) {
    std::string inputDir;
    std::string outputDir;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--input-dir") == 0 && i + 1 < argc) {
            inputDir = argv[++i];
        } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintUsage();
            return 0;
        }
    }

    if (inputDir.empty() || outputDir.empty()) {
        PrintUsage();
        return 1;
    }

    if (!fs::is_directory(inputDir)) {
        fprintf(stderr, "uzembed: input directory '%s' not found\n", inputDir.c_str());
        return 1;
    }

    int processed = 0;
    int failed = 0;
    std::vector<ShaderEntry> entries;

    // Collect and sort entries for deterministic output
    std::vector<fs::directory_entry> hlslFiles;
    for (const auto& entry : fs::recursive_directory_iterator(inputDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".hlsl") continue;
        hlslFiles.push_back(entry);
    }
    std::sort(hlslFiles.begin(), hlslFiles.end(),
              [](const auto& a, const auto& b) { return a.path() < b.path(); });

    for (const auto& entry : hlslFiles) {
        fs::path relPath = fs::relative(entry.path(), inputDir);
        fs::path outPath = fs::path(outputDir) / relPath;
        outPath += ".gen.h";

        std::string stem = entry.path().stem().string();
        std::string pascal = StemToPascal(stem);

        ShaderEntry se;
        se.relIncludePath = (relPath.string() + ".gen.h");
        // Normalize path separators to forward slashes
        std::replace(se.relIncludePath.begin(), se.relIncludePath.end(), '\\', '/');
        se.constName = "k" + pascal;
        se.enumName = pascal;
        se.fileName = entry.path().filename().string();

        if (GenerateHeader(entry.path(), outPath, se.constName)) {
            printf("  %s -> %s\n", relPath.string().c_str(), outPath.filename().string().c_str());
            entries.push_back(std::move(se));
            processed++;
        } else {
            failed++;
        }
    }

    // Generate index header
    if (!entries.empty()) {
        if (GenerateIndex(fs::path(outputDir), entries)) {
            printf("  engine_shaders.gen.h (index, %zu shaders)\n", entries.size());
        } else {
            failed++;
        }
    }

    printf("uzembed: %d files processed, %d failed\n", processed, failed);
    return failed > 0 ? 1 : 0;
}
