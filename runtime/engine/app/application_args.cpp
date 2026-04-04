/// uz::Application — CLI argument parsing.
///
/// Handles --backend, --sm, --screenshot, --frames, --cvar, --camera-*,
/// --vis, --pass-*, and other command-line flags.
/// See application.h for the full Application interface.

#include "application.h"
#include "uzengine.h"
#include "cvar.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Anonymous helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

bool ParseBackendArg(const char* str, RHIBackend& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "dx12")   { out = RHIBackend::DX12;   return true; }
    if (s == "dx11")   { out = RHIBackend::DX11;   return true; }
    if (s == "vulkan") { out = RHIBackend::Vulkan;  return true; }
    return false;
}

bool ParseShaderModelArg(const char* str, ShaderModel& out) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.rfind("sm", 0) == 0) s = s.substr(2);
    std::replace(s.begin(), s.end(), '_', '.');
    if (s == "5" || s == "5.0")  { out = ShaderModel::SM5_0; return true; }
    if (s == "5.1")              { out = ShaderModel::SM5_1; return true; }
    if (s == "6" || s == "6.0")  { out = ShaderModel::SM6_0; return true; }
    if (s == "6.5")              { out = ShaderModel::SM6_5; return true; }
    if (s == "6.6")              { out = ShaderModel::SM6_6; return true; }
    return false;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ParseArgs — Parses common CLI flags into m_config
// ─────────────────────────────────────────────────────────────────────────────

void Application::ParseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            RHIBackend backend{};
            if (ParseBackendArg(argv[++i], backend)) m_config.backend = backend;
        } else if (std::strcmp(argv[i], "--sm") == 0 && i + 1 < argc) {
            ShaderModel sm{};
            if (ParseShaderModelArg(argv[++i], sm)) m_config.shaderModel = sm;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            m_config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            // Deprecated — screenshot now auto-triggers after scene fully loads.
            // Argument consumed but ignored.
            ++i;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            m_config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            m_config.debugViewMode = DebugViewModeFromString(argv[++i]);
        } else if (std::strcmp(argv[i], "--all-modes") == 0 && i + 1 < argc) {
            m_config.allModesOutputPath = argv[++i];
        } else if (std::strcmp(argv[i], "--shadow-debug") == 0 && i + 1 < argc) {
            m_shadowDebugMode = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-x") == 0 && i + 1 < argc) {
            m_config.cameraPositionX = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-y") == 0 && i + 1 < argc) {
            m_config.cameraPositionY = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-z") == 0 && i + 1 < argc) {
            m_config.cameraPositionZ = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-yaw") == 0 && i + 1 < argc) {
            m_config.cameraYawDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--camera-pitch") == 0 && i + 1 < argc) {
            m_config.cameraPitchDeg = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--screenshot-stdout") == 0) {
            m_screenshotBase64 = true;
        } else if (std::strcmp(argv[i], "--headless") == 0) {
            m_headless = true;
        } else if (std::strcmp(argv[i], "--vis") == 0 && i + 1 < argc) {
            m_visTarget = argv[++i];
        } else if (std::strcmp(argv[i], "--pass-validate") == 0) {
            m_passValidate = true;
        } else if (std::strcmp(argv[i], "--pass-dump") == 0) {
            m_passDumpRequested = true;
        } else if (std::strcmp(argv[i], "--pass-toggle") == 0 && i + 1 < argc) {
            m_passToggle = argv[++i];
        } else if (std::strcmp(argv[i], "--pass-compare") == 0 && i + 1 < argc) {
            m_passCompare = argv[++i];
        } else if (std::strcmp(argv[i], "--pixel-debug") == 0 && i + 2 < argc) {
            m_pixelDebugX = static_cast<uint32_t>(std::atoi(argv[++i]));
            m_pixelDebugY = static_cast<uint32_t>(std::atoi(argv[++i]));
            m_pixelDebugEnabled = true;
        } else if (std::strcmp(argv[i], "--diag") == 0) {
            m_diagMode = true;
        } else if (std::strcmp(argv[i], "--gpu-profile") == 0) {
            m_config.gpuProfile = true;
        } else if (std::strcmp(argv[i], "--force-loose") == 0) {
            m_config.forceLooseVFS = true;
        } else if (std::strcmp(argv[i], "--pak-cache-mb") == 0 && i + 1 < argc) {
            m_config.pakCacheMB = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--flip") == 0) {
            m_flipMode = true;
        } else if (std::strcmp(argv[i], "--flip-ref") == 0 && i + 1 < argc) {
            m_flipRefPath = argv[++i];
        } else if (std::strcmp(argv[i], "--flip-test") == 0 && i + 1 < argc) {
            m_flipTestPath = argv[++i];
        } else if (std::strcmp(argv[i], "--flip-output") == 0 && i + 1 < argc) {
            m_flipOutputPath = argv[++i];
        } else if (std::strcmp(argv[i], "--flip-threshold") == 0 && i + 1 < argc) {
            m_flipThreshold = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--upscaler") == 0 && i + 1 < argc) {
            const char* mode = argv[++i];
            int upscalerVal = 0;
            if (std::strcmp(mode, "none") == 0)      upscalerVal = 0;
            else if (std::strcmp(mode, "taa") == 0)   upscalerVal = 1;
            else if (std::strcmp(mode, "tsr") == 0)   upscalerVal = 2;
            else if (std::strcmp(mode, "fsr2") == 0)  upscalerVal = 3;
            else if (std::strcmp(mode, "dlss") == 0)  upscalerVal = 4;
            else if (std::strcmp(mode, "xess") == 0)  upscalerVal = 5;
            else { UZ_LOG(App, Warning, "Unknown upscaler '{}', using TAA", mode); upscalerVal = 1; }
            m_cvarOverrides.emplace_back("r.AntiAliasingMethod", static_cast<float>(upscalerVal));
        } else if (std::strcmp(argv[i], "--dlss-quality") == 0 && i + 1 < argc) {
            const char* q = argv[++i];
            int qVal = 3; // Quality default
            if (std::strcmp(q, "ultra") == 0 || std::strcmp(q, "ultraperformance") == 0) qVal = 0;
            else if (std::strcmp(q, "performance") == 0) qVal = 1;
            else if (std::strcmp(q, "balanced") == 0)    qVal = 2;
            else if (std::strcmp(q, "quality") == 0)     qVal = 3;
            else if (std::strcmp(q, "dlaa") == 0)        qVal = 4;
            m_cvarOverrides.emplace_back("r.DLSS.Quality", static_cast<float>(qVal));
        } else if (std::strcmp(argv[i], "--cvar") == 0 && i + 1 < argc) {
            std::string arg = argv[++i];
            const auto eqPos = arg.find('=');
            if (eqPos != std::string::npos) {
                m_cvarOverrides.emplace_back(arg.substr(0, eqPos),
                    static_cast<float>(std::atof(arg.substr(eqPos + 1).c_str())));
            } else {
                UZ_LOG(App, Warning, "--cvar expects name=value format, got '{}'", arg);
            }
        }
    }

    // Screenshot: auto-triggers after scene fully loads. Set enough frames to allow loading.
    if (!m_config.screenshotPath.empty() && m_config.maxFrames == 0) {
        m_config.maxFrames = 500; // Enough for async texture loading + convergence
    }
    if (m_screenshotBase64 && m_config.maxFrames == 0) {
        m_config.maxFrames = 500;
    }

    // --screenshot-stdout implies --headless (LLM doesn't need a visible window)
    if (m_screenshotBase64) {
        m_headless = true;
    }

    // Suppress console logging when stdout is reserved for base64 output.
    // Logs still go to the file sink (uzengine.log).
    if (m_screenshotBase64) {
        LogSuppressConsole();
    }

    m_screenshotPending = !m_config.screenshotPath.empty() || m_screenshotBase64;

    // --vis with --screenshot-stdout: auto-set frame limits for headless vis capture
    if (!m_visTarget.empty() && m_screenshotBase64 && m_config.maxFrames == 0) {
        m_config.screenshotFrame = std::max(m_config.screenshotFrame, 5u);
        m_config.maxFrames = m_config.screenshotFrame + 2;
    }

    // --pass-validate: default to 120 frames then exit
    if (m_passValidate && m_config.maxFrames == 0) {
        m_config.maxFrames = 120;
    }

    // --diag: run diagnostics and output JSON to stdout, then exit.
    // Implies headless mode and suppresses console logging so stdout
    // contains only the DIAG_JSON: prefixed output.
    if (m_diagMode) {
        m_headless = true;
        if (m_config.maxFrames == 0) {
            m_config.maxFrames = 120;
        }
        LogSuppressConsole();
    }

    // --pixel-debug: implies headless + limited frames + suppressed console.
    // Output goes to stdout as PIXEL_DEBUG_JSON: prefix.
    if (m_pixelDebugEnabled) {
        m_headless = true;
        if (m_config.maxFrames == 0) {
            m_config.maxFrames = 120;
        }
        LogSuppressConsole();
    }

    // --flip: headless FLIP comparison mode.
    // Loads two images, runs FlipPass compute shader, outputs FLIP_JSON to stdout.
    // Does NOT require a scene or rendering pipeline — uses device + compute only.
    if (m_flipMode) {
        m_headless = true;
        m_config.maxFrames = 1; // Exit after one frame
        LogSuppressConsole();

        if (m_flipRefPath.empty() || m_flipTestPath.empty()) {
            UZ_LOG(App, Error, "--flip requires --flip-ref <path> and --flip-test <path>");
            m_flipMode = false;
        }
    }
}

} // namespace uz
