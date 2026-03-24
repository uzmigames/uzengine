/// DemoApplication — CLI argument parsing and static utility commands.
///
/// Contains: GetScreenshotsDir(), ParseCommonArgs(), TryComposeGrid(), TryDiffImages(),
/// and the anonymous-namespace helpers ParseBackendArg() / ParseShaderModelArg().

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "demo_application.h"
#include "uzengine.h"
#include "compare_grid.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GetScreenshotsDir — Platform-aware screenshots output directory
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the user-scoped directory where all engine screenshots are saved.
///
/// - Windows : `%APPDATA%\UzEngine\.screenshots\`
/// - Linux   : `~/.local/share/UzEngine/.screenshots/`
/// - macOS   : `~/Library/Application Support/UzEngine/.screenshots/`
///
/// The directory is created if it does not exist.
/// Falls back to `.screenshots/` relative to CWD on any error.
std::filesystem::path DemoApplication::GetScreenshotsDir() {
    std::filesystem::path base;

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        base = std::filesystem::path(appdata) / "UzEngine" / ".screenshots";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / "Library" / "Application Support"
             / "UzEngine" / ".screenshots";
    }
#else // Linux / other POSIX
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = std::filesystem::path(xdg) / "UzEngine" / ".screenshots";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".local" / "share"
             / "UzEngine" / ".screenshots";
    }
#endif

    if (base.empty()) {
        base = std::filesystem::path(".screenshots");
    }

    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommonArgs — Parses common CLI flags into DemoConfig
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Case-insensitive match for backend name.
/// Returns true if recognized.
bool ParseBackendArg(const char* str, RHIBackend& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "dx12")   { out = RHIBackend::DX12;   return true; }
    if (s == "dx11")   { out = RHIBackend::DX11;   return true; }
    if (s == "vulkan") { out = RHIBackend::Vulkan;  return true; }
    return false;
}

/// Case-insensitive match for shader model.
/// Accepts: "5.0", "5.1", "6.0", "6.5", "6.6", etc.
bool ParseShaderModelArg(const char* str, ShaderModel& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Strip optional "sm" prefix
    if (s.rfind("sm", 0) == 0) s = s.substr(2);
    std::replace(s.begin(), s.end(), '_', '.');

    if (s == "5" || s == "5.0")  { out = ShaderModel::SM5_0; return true; }
    if (s == "5.1")              { out = ShaderModel::SM5_1; return true; }
    if (s == "6" || s == "6.0")  { out = ShaderModel::SM6_0; return true; }
    if (s == "6.1")              { out = ShaderModel::SM6_1; return true; }
    if (s == "6.2")              { out = ShaderModel::SM6_2; return true; }
    if (s == "6.3")              { out = ShaderModel::SM6_3; return true; }
    if (s == "6.4")              { out = ShaderModel::SM6_4; return true; }
    if (s == "6.5")              { out = ShaderModel::SM6_5; return true; }
    if (s == "6.6")              { out = ShaderModel::SM6_6; return true; }
    if (s == "6.7")              { out = ShaderModel::SM6_7; return true; }
    if (s == "6.8")              { out = ShaderModel::SM6_8; return true; }
    if (s == "6.9")              { out = ShaderModel::SM6_9; return true; }
    return false;
}

} // anonymous namespace

void DemoApplication::ParseCommonArgs(int argc, char* argv[], DemoConfig& config) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            RHIBackend backend{};
            if (ParseBackendArg(argv[++i], backend)) {
                config.backend = backend;
            } else {
                UZ_LOG(App, Warning, "Unknown --backend '{}', ignoring", argv[i]);
            }
        } else if (std::strcmp(argv[i], "--sm") == 0 && i + 1 < argc) {
            ShaderModel sm{};
            if (ParseShaderModelArg(argv[++i], sm)) {
                config.shaderModel = sm;
            } else {
                UZ_LOG(App, Warning, "Unknown --sm '{}', ignoring", argv[i]);
            }
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            config.debugViewMode = DebugViewModeFromString(argv[++i]);
        } else if (std::strcmp(argv[i], "--all-modes") == 0 && i + 1 < argc) {
            config.allModesOutputPath = argv[++i];
        } else if (std::strcmp(argv[i], "--camera-x") == 0 && i + 1 < argc) {
            config.cameraPositionX = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-y") == 0 && i + 1 < argc) {
            config.cameraPositionY = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-z") == 0 && i + 1 < argc) {
            config.cameraPositionZ = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-yaw") == 0 && i + 1 < argc) {
            config.cameraYawDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-pitch") == 0 && i + 1 < argc) {
            config.cameraPitchDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--cvar") == 0 && i + 1 < argc) {
            // Parse "name=value" format (e.g., "r.TAA=0", "r.Bloom=1")
            std::string arg = argv[++i];
            const auto eqPos = arg.find('=');
            if (eqPos != std::string::npos) {
                std::string name  = arg.substr(0, eqPos);
                float       value = static_cast<float>(std::atof(arg.substr(eqPos + 1).c_str()));
                config.cvarOverrides.emplace_back(std::move(name), value);
            } else {
                UZ_LOG(App, Warning, "--cvar expects name=value format, got '{}'", arg);
            }
        }
    }

    // Route bare filenames (no directory separator) into the user-scoped
    // screenshots directory so output is never mixed with exe/build files.
    auto routeToScreenshotDir = [](std::string& path) {
        if (path.empty()) return;
        const std::filesystem::path p(path);
        if (!p.has_parent_path() || p.parent_path() == ".") {
            path = (GetScreenshotsDir() / p.filename()).string();
        }
    };
    routeToScreenshotDir(config.screenshotPath);
    routeToScreenshotDir(config.allModesOutputPath);

    // Auto-exit after screenshot so CI pipelines don't block
    if (!config.screenshotPath.empty() && config.maxFrames == 0) {
        config.screenshotFrame = std::max(config.screenshotFrame, 5u);
        config.maxFrames = config.screenshotFrame + 2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TryComposeGrid — Static utility for --compose-grid mode
// ─────────────────────────────────────────────────────────────────────────────

int DemoApplication::TryComposeGrid(int argc, char* argv[]) {
    // Find --compose-grid flag
    int gridIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--compose-grid") == 0) {
            gridIdx = i;
            break;
        }
    }
    if (gridIdx < 0) return -1; // Not in grid mode

    // Next arg is output path
    if (gridIdx + 1 >= argc) {
        UZ_LOG(App, Error, "--compose-grid requires output path");
        return 1;
    }
    const std::string outputPath = argv[gridIdx + 1];
    int nextArg = gridIdx + 2;

    CompareGrid grid;

    // Optional --cell-size <W> (square cells)
    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--cell-size") == 0) {
        const uint32_t sz = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (sz > 0) grid.SetCellSize(sz, sz);
        nextArg += 2;
    }

    // Optional --label-height <H>
    if (nextArg + 1 < argc && std::strcmp(argv[nextArg], "--label-height") == 0) {
        const uint32_t h = static_cast<uint32_t>(std::atoi(argv[nextArg + 1]));
        if (h > 0) grid.SetLabelHeight(h);
        nextArg += 2;
    }

    // Remaining args: triplets of <input.png> <label> <sublabel>
    while (nextArg + 2 < argc) {
        const std::string path = argv[nextArg];
        const std::string label = argv[nextArg + 1];
        const std::string sublabel = argv[nextArg + 2];

        if (!grid.AddCellFromPNG(path, label, sublabel)) {
            UZ_LOG(App, Error, "Failed to load grid cell: path='{}'", path);
            return 1;
        }
        nextArg += 3;
    }

    if (grid.GetCellCount() == 0) {
        UZ_LOG(App, Error, "--compose-grid: no input images specified");
        return 1;
    }

    if (!grid.SaveToPNG(outputPath)) {
        UZ_LOG(App, Error, "Failed to save grid: path='{}'", outputPath);
        return 1;
    }

    UZ_LOG(App, Info, "Compare grid saved: path='{}' ({} cells)", outputPath, grid.GetCellCount());
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// TryDiffImages — Static utility for --diff mode
// ─────────────────────────────────────────────────────────────────────────────

int DemoApplication::TryDiffImages(int argc, char* argv[]) {
    int diffIdx = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--diff") == 0) {
            diffIdx = i;
            break;
        }
    }
    if (diffIdx < 0) return -1;

    // --diff <output> <imgA> <labelA> <imgB> <labelB> [--cell-size N]
    if (diffIdx + 5 >= argc) {
        UZ_LOG(App, Error, "--diff requires: <output.png> <imgA.png> <labelA> <imgB.png> <labelB>");
        return 1;
    }

    const std::string output = argv[diffIdx + 1];
    const std::string pathA = argv[diffIdx + 2];
    const std::string labelA = argv[diffIdx + 3];
    const std::string pathB = argv[diffIdx + 4];
    const std::string labelB = argv[diffIdx + 5];

    uint32_t cellSize = 384;
    // Check for optional --cell-size after the 5 required args
    const int optIdx = diffIdx + 6;
    if (optIdx + 1 < argc && std::strcmp(argv[optIdx], "--cell-size") == 0) {
        const int sz = std::atoi(argv[optIdx + 1]);
        if (sz > 0) cellSize = static_cast<uint32_t>(sz);
    }

    if (!ImageDiff::DiffToPNG(pathA, labelA, pathB, labelB, output, cellSize)) {
        UZ_LOG(App, Error, "Failed to generate diff: output='{}'", output);
        return 1;
    }

    return 0;
}

} // namespace uz
