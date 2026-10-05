/// UzEngine Standalone Runtime Executable
///
/// Entry point for the engine runtime. Parses CLI arguments, resolves RHI
/// configuration, creates a window (unless headless), and runs a render loop.
/// Currently renders an animated clear screen as a foundation for future
/// scene rendering.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "config.h"
#include "rhi_config.h"
#include "rhi_backend.h"
#include "log.h"
#include "flip_pass.h"
#include "file_system.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "shader_manager.h"

#include <spng.h>
#include <jpeglib.h>
#include <stb_image.h>

#include "../engine/app/project_loader.h"
#include "../engine/core/cvar.h"   // run manifest: effective CVar values

// The command line, verbatim, for the run manifest. `cli` and `cvars` could not tell an explicit
// `--cvar X=<default>` from an omitted one (followup-14, 2026-09-16); argv can.
static std::vector<std::string> g_argv;
#include "world.h"
#include "scene_loader.h"
#include "../engine/scripting/lua_vm.h"
#include "lua_scene_api.h"
#include "lua_scene_io_api.h"
#include "lua_renderer_api.h"
#include "../engine/scripting/script_system.h"
#include "../engine/asset/vfs.h"
#include "../engine/renderer/scene/scene_renderer.h"
#include "../engine/animation/skeletal_mesh_subsystem.h"
#include "../engine/scripting/core/lua_input_api.h"
#include "../editor/input/editor_input_handler.h"
#include "../editor/selection/selection_manager.h"
#include "../engine/renderer/camera/fly_camera_controller.h"
#include "../engine/scripting/editor/lua_editor_api.h"
#include "../engine/renderer/passes/debug/pixel_debug.h"
#include "../engine/renderer/passes/debug/inspector_pass.h"
#include "../engine/asset/importers/png_codec.h"
#include "texture_loader.h"

#ifdef _WIN32
#include "platform_window.h"
#endif

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Exit codes
// ─────────────────────────────────────────────────────────────────────────────

/// Process exit code: normal termination.
static constexpr int kExitSuccess = 0;

/// Process exit code: RHI device/swapchain/command list creation failure.
static constexpr int kExitRenderError = 1;

/// Process exit code: shader compilation failure.
static constexpr int kExitShaderError = 2;

/// Process exit code: GPU device lost (TDR, driver crash).
static constexpr int kExitDeviceLost = 3;

/// Number of swapchain back buffers for triple-buffered frame pacing.
static constexpr uint32_t kFramesInFlight = 3;

/// Default window width in pixels when no `--width` CLI override is given.
static constexpr int kDefaultWidth = 1280;

/// Default window height in pixels when no `--height` CLI override is given.
static constexpr int kDefaultHeight = 720;

/// Sentinel value for `--frames` meaning "run until window close".
static constexpr int kInfiniteFrames = -1;

// ─────────────────────────────────────────────────────────────────────────────
// Config helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Loads the project file and applies CLI overrides.
static Config BuildConfig(int argc, char** argv) {
    Config config;
    ConfigSetEngineDefaults(config);

    const char* const* constArgv = argv;
    config.ApplyCLIOverrides(argc, constArgv);

    const std::string projectPath = config.GetString("project");
    if (!projectPath.empty()) {
        UZ_LOG(Engine, Info, "Loading project: {}", projectPath);
        if (!config.LoadFromFile(projectPath)) {
            UZ_LOG(Engine, Warning, "Failed to load project file: {}", projectPath);
        }
        // Re-apply CLI so they take precedence over project file
        config.ApplyCLIOverrides(argc, constArgv);
    }

    return config;
}

/// Logs the resolved RHI configuration for diagnostics.
static void LogResolvedConfig(const RHIConfig& rhiCfg, bool headless, int frames, int width, int height) {
    UZ_LOG(Engine, Info, "RHI backend:       {}", RHIBackendToString(rhiCfg.backend));
    UZ_LOG(Engine, Info, "Shader model:      {}", rhiCfg.shaderModel.has_value() ? ShaderModelToString(*rhiCfg.shaderModel) : "auto");
    UZ_LOG(Engine, Info, "Validation:        {}", rhiCfg.enableValidation ? "on" : "off");
    UZ_LOG(Engine, Info, "GPU validation:    {}", rhiCfg.enableGpuValidation ? "on" : "off");
    UZ_LOG(Engine, Info, "Breadcrumbs:       {}", rhiCfg.enableBreadcrumbs ? "on" : "off");
    UZ_LOG(Engine, Info, "Adapter index:     {}", rhiCfg.adapterIndex);
    UZ_LOG(Engine, Info, "Headless:          {}", headless ? "yes" : "no");
    UZ_LOG(Engine, Info, "Frame limit:       {}", frames == kInfiniteFrames ? "infinite" : std::to_string(frames));
    UZ_LOG(Engine, Info, "Window size:       {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Headless mode
// ─────────────────────────────────────────────────────────────────────────────

/// Runs headless mode: creates device, logs success, exits.
static int RunHeadless(const RHIDeviceDesc& deviceDesc) {
    UZ_LOG(Engine, Info, "Headless mode — creating device only");

    auto device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(Engine, Error, "Failed to create RHI device in headless mode");
        return kExitRenderError;
    }

    UZ_LOG(Engine, Info, "Headless device created successfully");
    return kExitSuccess;
}

// ─────────────────────────────────────────────────────────────────────────────
// Windowed render loop (Windows)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

/// Creates per-frame fences for triple-buffered synchronization.
///
/// Returns false if any fence creation fails. On failure, all previously
/// created fences are cleaned up.
static bool CreateFrameFences(IRHIDevice* device, IRHIFence* fences[], uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* f = device->CreateFence();
        if (!f) {
            UZ_LOG(RHI, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) {
                device->DestroyFence(fences[j]);
            }
            return false;
        }
        fences[i] = f;
    }
    return true;
}

/// Computes an animated sine-wave clear color.
static void ComputeClearColor(float t, float outColor[4]) {
    outColor[0] = 0.5f + 0.5f * std::sin(t * 1.0f);
    outColor[1] = 0.5f + 0.5f * std::sin(t * 1.3f + 2.094f);
    outColor[2] = 0.5f + 0.5f * std::sin(t * 1.7f + 4.189f);
    outColor[3] = 1.0f;
}

/// Records and submits a single clear-screen frame.
static void RenderClearFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain, float t) {
    float color[4];
    ComputeClearColor(t, color);

    cmdList->Begin();

    const RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    const RHIBarrierDesc toRT{
        .texture = backBuffer,
        .stateBefore = RHIResourceState::Present,
        .stateAfter = RHIResourceState::RenderTarget,
    };
    cmdList->ResourceBarrier(&toRT, 1);

    cmdList->ClearRenderTarget(backBuffer, color);

    const RHIBarrierDesc toPresent{
        .texture = backBuffer,
        .stateBefore = RHIResourceState::RenderTarget,
        .stateAfter = RHIResourceState::Present,
    };
    cmdList->ResourceBarrier(&toPresent, 1);

    cmdList->End();
    cmdList->Execute();
}

/// Logs final render statistics after the loop ends.
static void LogRenderStats(uint32_t frameIndex, float totalMs) {
    const float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;
    const float fps = avgMs > 0.0f ? 1000.0f / avgMs : 0.0f;
    UZ_LOG(Engine, Info, "Render loop finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, fps);
}

// ─────────────────────────────────────────────────────────────────────────────
// FLIP Headless Evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Loads a PNG/JPG via VFS + stbi, creates a GPU texture, returns SRV index.
static uint32_t LoadImageToGPU(IRHIDevice* device, const std::string& path,
                                uint32_t& outWidth, uint32_t& outHeight) {
    // Read file bytes via filesystem
    auto data = FileReadAll(path.c_str());
    if (data.empty()) {
        UZ_LOG(Engine, Error, "FLIP: failed to read '{}'", path);
        return UINT32_MAX;
    }

    int w = 0, h = 0, ch = 0;
    uint8_t* pixels = nullptr;

    // PNG: spng (SIMD-accelerated)
    static constexpr uint8_t kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() >= 8 && std::memcmp(data.data(), kPngSig, 8) == 0) {
        spng_ctx* ctx = spng_ctx_new(0);
        if (ctx) {
            spng_set_png_buffer(ctx, data.data(), data.size());
            struct spng_ihdr ihdr;
            if (spng_get_ihdr(ctx, &ihdr) == SPNG_OK) {
                size_t outSize = 0;
                if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &outSize) == SPNG_OK) {
                    auto* px = static_cast<uint8_t*>(std::malloc(outSize));
                    if (px && spng_decode_image(ctx, px, outSize, SPNG_FMT_RGBA8, 0) == SPNG_OK) {
                        w = static_cast<int>(ihdr.width);
                        h = static_cast<int>(ihdr.height);
                        pixels = px;
                    } else {
                        std::free(px);
                    }
                }
            }
            spng_ctx_free(ctx);
        }
    }

    // JPEG: libjpeg-turbo
    if (!pixels && data.size() >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, data.data(), static_cast<unsigned long>(data.size()));
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            cinfo.out_color_space = JCS_RGB;
            jpeg_start_decompress(&cinfo);
            w = static_cast<int>(cinfo.output_width);
            h = static_cast<int>(cinfo.output_height);
            const size_t rgbaSize = static_cast<size_t>(w) * h * 4;
            auto* px = static_cast<uint8_t*>(std::malloc(rgbaSize));
            auto* rgbRow = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(w) * 3));
            if (px && rgbRow) {
                unsigned char* rowPtr = rgbRow;
                while (cinfo.output_scanline < cinfo.output_height) {
                    jpeg_read_scanlines(&cinfo, &rowPtr, 1);
                    uint8_t* dst = px + static_cast<size_t>(cinfo.output_scanline - 1) * w * 4;
                    for (int x = 0; x < w; ++x) {
                        dst[x * 4 + 0] = rgbRow[x * 3 + 0];
                        dst[x * 4 + 1] = rgbRow[x * 3 + 1];
                        dst[x * 4 + 2] = rgbRow[x * 3 + 2];
                        dst[x * 4 + 3] = 255;
                    }
                }
                pixels = px;
            } else {
                std::free(px);
            }
            std::free(rgbRow);
            jpeg_finish_decompress(&cinfo);
        }
        jpeg_destroy_decompress(&cinfo);
    }

    // Fallback: stb_image (TGA, BMP, HDR, etc.)
    if (!pixels) {
        pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                       &w, &h, &ch, 4);
    }

    if (!pixels) {
        UZ_LOG(Engine, Error, "FLIP: failed to decode '{}'", path);
        return UINT32_MAX;
    }
    outWidth  = static_cast<uint32_t>(w);
    outHeight = static_cast<uint32_t>(h);

    // Create RGBA8 texture
    RHITextureDesc texDesc;
    texDesc.width     = outWidth;
    texDesc.height    = outHeight;
    texDesc.format    = RHIFormat::RGBA8Unorm;
    texDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.debugName = path.c_str();
    auto texture = device->CreateTexture(texDesc);

    // Upload via staging buffer
    const uint32_t rowPitch = outWidth * 4;
    const uint32_t alignedRowPitch = (rowPitch + 255) & ~255u;
    const uint32_t uploadSize = alignedRowPitch * outHeight;

    RHIBufferDesc stagingDesc;
    stagingDesc.sizeBytes = uploadSize;
    stagingDesc.usage     = RHIBufferUsage::CopySrc;
    stagingDesc.memory    = RHIMemoryType::Upload;
    stagingDesc.debugName = "FLIP_ImageStaging";
    auto staging = device->CreateBuffer(stagingDesc);

    auto* mapped = static_cast<uint8_t*>(device->MapBuffer(staging));
    for (uint32_t row = 0; row < outHeight; ++row) {
        std::memcpy(mapped + row * alignedRowPitch, pixels + row * rowPitch, rowPitch);
    }
    device->UnmapBuffer(staging);
    std::free(pixels);  // works for spng, jpeglib, and stbi (all use malloc)

    auto* cmdList = device->CreateCommandList();
    cmdList->Begin();

    RHIBarrierDesc toCopyDst;
    toCopyDst.texture     = texture;
    toCopyDst.stateBefore = RHIResourceState::Common;
    toCopyDst.stateAfter  = RHIResourceState::CopyDst;
    cmdList->ResourceBarrier(&toCopyDst, 1);

    cmdList->CopyBufferToTexture(staging, 0, alignedRowPitch, texture, 0);

    RHIBarrierDesc toSrv;
    toSrv.texture     = texture;
    toSrv.stateBefore = RHIResourceState::CopyDst;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    device->DestroyBuffer(staging);

    return device->GetTextureSRVIndex(texture);
}

/// Runs FLIP comparison between two images. Prints JSON to stdout, returns exit code.
static int RunFlip(const RHIDeviceDesc& deviceDesc,
                    const std::string& refPath, const std::string& testPath,
                    const std::string& outputPath, float threshold) {
    UZ_LOG(Engine, Info, "FLIP: comparing '{}' vs '{}'", refPath, testPath);

    // Create headless device
    auto device = CreateRHIDevice(deviceDesc);
    auto* devicePtr = device.get();
    if (!device) {
        UZ_LOG(Engine, Error, "FLIP: failed to create RHI device");
        return kExitRenderError;
    }

    // Load images
    uint32_t refW = 0, refH = 0, testW = 0, testH = 0;
    uint32_t refSrv  = LoadImageToGPU(devicePtr, refPath, refW, refH);
    uint32_t testSrv = LoadImageToGPU(devicePtr, testPath, testW, testH);

    if (refSrv == UINT32_MAX || testSrv == UINT32_MAX) {
        UZ_LOG(Engine, Error, "FLIP: failed to load images");
        return kExitRenderError;
    }

    if (refW != testW || refH != testH) {
        UZ_LOG(Engine, Error, "FLIP: image dimensions mismatch ({}x{} vs {}x{})",
               refW, refH, testW, testH);
        return kExitRenderError;
    }

    // Shader manager for FLIP compute pipeline
    auto shaderMgr = std::make_unique<ShaderManager>();
    if (!shaderMgr->Init(devicePtr)) {
        UZ_LOG(Engine, Error, "FLIP: failed to init shader manager");
        return kExitRenderError;
    }

    // Run FLIP evaluation
    FlipPass flipPass;
    if (!flipPass.Init(devicePtr, shaderMgr.get())) {
        UZ_LOG(Engine, Error, "FLIP: failed to init FlipPass");
        return kExitRenderError;
    }

    auto* cmdList = devicePtr->CreateCommandList();
    cmdList->Begin();
    auto result = flipPass.Evaluate(cmdList, refSrv, testSrv, refW, refH, threshold);

    // Save heatmap PNG if output path specified
    if (!outputPath.empty() && result.heatmap.IsValid()) {
        const uint32_t rowPitch = refW * 8; // RGBA16F = 4 channels * 2 bytes
        const uint32_t bufSize  = rowPitch * refH;

        RHIBufferDesc rbDesc;
        rbDesc.sizeBytes = bufSize;
        rbDesc.usage     = RHIBufferUsage::CopyDst;
        rbDesc.memory    = RHIMemoryType::Readback;
        rbDesc.debugName = "FLIP_HeatmapReadback";
        auto readbackBuf = devicePtr->CreateBuffer(rbDesc);

        if (readbackBuf.IsValid()) {
            // Reuse existing cmdList — it's idle after Evaluate's WaitIdle
            cmdList->Begin();

            RHIBarrierDesc toCopy;
            toCopy.texture     = result.heatmap;
            toCopy.stateBefore = RHIResourceState::ShaderResource;
            toCopy.stateAfter  = RHIResourceState::CopySrc;
            cmdList->ResourceBarrier(&toCopy, 1);
            cmdList->CopyTextureToBuffer(result.heatmap, 0, readbackBuf, 0, rowPitch);
            cmdList->End();
            cmdList->Execute();
            devicePtr->WaitIdle();

            // Convert RGBA16F → RGBA8 and save
            const auto* src = static_cast<const uint16_t*>(devicePtr->MapBuffer(readbackBuf));
            std::vector<uint8_t> rgba8(refW * refH * 4);
            for (uint32_t i = 0; i < refW * refH * 4; ++i) {
                // float16 → float32 → uint8
                uint16_t fp16 = src[i];
                uint32_t sign = (fp16 >> 15) & 0x1;
                uint32_t exp  = (fp16 >> 10) & 0x1F;
                uint32_t man  = fp16 & 0x3FF;
                float f;
                if (exp == 0) {
                    f = (man == 0) ? 0.0f : std::ldexp(static_cast<float>(man), -24);
                } else if (exp == 31) {
                    f = 1.0f; // clamp inf/nan
                } else {
                    f = std::ldexp(static_cast<float>(man + 1024), static_cast<int>(exp) - 25);
                }
                if (sign) f = -f;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                rgba8[i] = static_cast<uint8_t>(f * 255.0f + 0.5f);
            }
            devicePtr->UnmapBuffer(readbackBuf);
            devicePtr->DestroyBuffer(readbackBuf);

            if (PngCodec::EncodeFile(outputPath, rgba8.data(), refW, refH, 4)) {
                UZ_LOG(Engine, Info, "FLIP: heatmap saved to '{}'", outputPath);
            } else {
                UZ_LOG(Engine, Error, "FLIP: failed to save heatmap to '{}'", outputPath);
            }
        } else {
            UZ_LOG(Engine, Error, "FLIP: failed to create readback buffer for heatmap");
        }
    }

    // Print JSON to stdout
    std::printf("FLIP_JSON:{\"mean_flip\":%.6f,\"max_flip\":%.6f,\"threshold\":%.4f,"
                "\"pass\":%s,\"ref\":\"%s\",\"test\":\"%s\",\"width\":%u,\"height\":%u}\n",
                result.meanFlip, result.maxFlip, threshold,
                result.pass ? "true" : "false",
                refPath.c_str(), testPath.c_str(), refW, refH);
    std::fflush(stdout);

    flipPass.Shutdown();
    devicePtr->DestroyCommandList(cmdList);

    return result.pass ? kExitSuccess : 1;
}

/// Runs the windowed render loop with animated clear screen, optional scene + Lua scripting.

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics: region-dump rectangles and the run manifest
// ─────────────────────────────────────────────────────────────────────────────

/// Parses `--regions "name:x,y,w,h;name2:x,y,w,h"` (name optional) into rectangles.
/// Rectangles are screen pixels in the output (viewport) resolution. At most 8 are kept:
/// every extra rectangle costs one more per-pixel record block in the dump buffer.
static std::vector<uz::SceneRenderer::RegionRect> ParseRegionSpecs(const std::string& spec) {
    std::vector<uz::SceneRenderer::RegionRect> out;
    size_t pos = 0;
    while (pos < spec.size() && out.size() < 8) {
        const size_t end = spec.find(';', pos);
        std::string item = spec.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = (end == std::string::npos) ? spec.size() : end + 1;
        if (item.empty()) continue;
        uz::SceneRenderer::RegionRect rect;
        const size_t colon = item.find(':');
        if (colon != std::string::npos) {
            rect.name = item.substr(0, colon);
            item = item.substr(colon + 1);
        } else {
            rect.name = "roi" + std::to_string(out.size());
        }
        unsigned x = 0, y = 0, w = 0, h = 0;
        if (std::sscanf(item.c_str(), "%u,%u,%u,%u", &x, &y, &w, &h) != 4 || w == 0 || h == 0) {
            UZ_LOG(Engine, Warning, "--regions: '{}' not understood (expected [name:]x,y,w,h)", item);
            continue;
        }
        rect.x = x; rect.y = y; rect.w = w; rect.h = h;
        if (rect.name.size() > 15) {
            // The file format stores a 16-byte nul-terminated name; truncating silently would make
            // an analysis look up a rectangle that does not exist under the name it asked for.
            UZ_LOG(Engine, Warning, "--regions: name '{}' truncated to 15 characters", rect.name);
            rect.name.resize(15);
        }
        out.push_back(rect);
    }
    return out;
}

/// Maps `--region-fields` to the bitmask of shaders/runtime/common/region_dump.hlsli.
/// `output` (1 record/pixel) is the cheap preset for long sequences; `taa` adds the
/// resolve's intermediates and the lighting records; `gather` adds the final gather's internal
/// stages; `full` is currently `taa+extra` plus the lighting components.
static uint32_t ParseRegionFields(const std::string& name) {
    if (name == "output") return 4u;
    if (name == "lighting") return 1u;
    if (name == "taa") return 1u | 2u;
    if (name == "taa+extra") return 1u | 2u | 8u;
    if (name == "extra") return 8u;
    if (name == "components") return 1u | 2u | 16u;
    // `motion` is the cheap preset for long trajectories: the TAA output plus the normal and
    // view distance needed to track a world point across frames (48 bytes per pixel).
    if (name == "motion") return 1u | 4u;
    // `gather` is the chain the final gather's own stages belong to: the accumulator and the
    // reconstruction (32), what the lighting made of them (16), and what the TAA did with the
    // result (2). 21 records per pixel.
    if (name == "gather") return 1u | 2u | 16u | 32u;
    if (name == "gather+extra") return 1u | 2u | 8u | 16u | 32u;
    if (name == "full" || name.empty()) return 1u | 2u | 8u | 16u;
    UZ_LOG(Engine, Warning,
           "--region-fields '{}' not understood "
           "(output|lighting|taa|taa+extra|extra|components|motion|gather|gather+extra|full)", name);
    return 1u | 2u;
}

/// Statistics a run reports in its manifest so a capture can be reproduced and compared.
struct RunStats {
    uint32_t framesRendered = 0;
    uint32_t measuredFirst = 0;      ///< First frame included in the timing statistics.
    std::vector<float> frameMs;      ///< CPU wall time per frame from the measured window on.
    float    exposure = 0.f;
    float    cameraPos[3] = {};
    float    cameraTarget[3] = {};
    uint32_t regionFiles = 0;
    uint32_t regionInvalid = 0;
    uint32_t renderWidth = 0, renderHeight = 0;
    /// Per-pass GPU time over the measured window: sum, sample count, min and max ms.
    /// G5 asks about GPU cost; CPU wall time is a different quantity and cannot stand in for it.
    struct GpuAcc { double sum = 0.0; uint32_t count = 0; float minMs = 0.f; float maxMs = 0.f; };
    std::map<std::string, GpuAcc> gpuMs;
    /// GPU frame span (first pass begin to last pass end) of each measured frame, ms.
    std::vector<float> gpuFrameMs;
};

static std::string JsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) continue;
        else out += c;
    }
    return out;
}

/// FNV-1a 64 of a file's bytes — identifies the exact binary/asset a capture used.
static std::string HashFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    uint64_t h = 1469598103934665603ull;
    unsigned char buf[64 * 1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        for (size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 1099511628211ull; }
    std::fclose(f);
    char out[32];
    std::snprintf(out, sizeof(out), "fnv1a64:%016llx", static_cast<unsigned long long>(h));
    return out;
}

/// Writes the run manifest (`--run-manifest`): executable identity, effective CVars, scene,
/// resolution, jitter, exposure, camera pose, frame timing and region-dump integrity counters.
/// Everything a later comparison needs in order to claim two captures are comparable.
static void WriteRunManifest(const std::string& path, const uz::Config& config,
                             const std::string& projectPath, const std::string& scenePath,
                             int width, int height, const RunStats& stats) {
    std::string exePath;
#ifdef _WIN32
    char exeBuf[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exeBuf, MAX_PATH) > 0) exePath = exeBuf;
#endif
    std::vector<float> ms = stats.frameMs;
    std::sort(ms.begin(), ms.end());
    const auto pick = [&ms](double q) {
        return ms.empty() ? 0.f : ms[std::min(ms.size() - 1, static_cast<size_t>(q * ms.size()))];
    };
    double sum = 0.0;
    for (float v : ms) sum += v;

    std::ostringstream o;
    o << "{\n";
    o << "  \"schema\": \"uzengine.run-manifest/1\",\n";
    o << "  \"executable\": {\"path\": \"" << JsonEscape(exePath) << "\", \"hash\": \"" << HashFile(exePath) << "\"},\n";
    o << "  \"project\": \"" << JsonEscape(projectPath) << "\",\n";
    o << "  \"scene\": \"" << JsonEscape(scenePath) << "\",\n";
    o << "  \"resolution\": {\"output\": [" << width << ", " << height << "], \"render\": ["
      << stats.renderWidth << ", " << stats.renderHeight << "]},\n";
    if (!stats.gpuMs.empty()) {
        o << "  \"gpu_ms\": {";
        bool first = true;
        double total = 0.0;
        for (const auto& [name, acc] : stats.gpuMs) {
            if (acc.count == 0) continue;
            const double mean = acc.sum / acc.count;
            total += mean;
            o << (first ? "\n" : ",\n") << "    \"" << JsonEscape(name) << "\": " << mean;
            first = false;
        }
        double frameSum = 0.0;
        for (float v : stats.gpuFrameMs) frameSum += v;
        // _total = sum of the per-pass means; _frame = mean GPU span of a frame (gaps included).
        o << (first ? "" : ",\n") << "    \"_total\": " << total
          << ",\n    \"_frame\": " << (stats.gpuFrameMs.empty() ? 0.0 : frameSum / static_cast<double>(stats.gpuFrameMs.size()))
          << "\n  },\n";
    }
    o << "  \"output_encoding\": \"screenshots: 8-bit sRGB PNG; region dump: linear HDR float32 RGB\",\n";
    o << "  \"frames\": {\"rendered\": " << stats.framesRendered
      << ", \"timing_from\": " << stats.measuredFirst
      << ", \"timing_samples\": " << ms.size() << "},\n";
    o << "  \"frame_ms\": {\"mean\": " << (ms.empty() ? 0.0 : sum / ms.size())
      << ", \"min\": " << (ms.empty() ? 0.f : ms.front())
      << ", \"p50\": " << pick(0.50) << ", \"p95\": " << pick(0.95)
      << ", \"max\": " << (ms.empty() ? 0.f : ms.back()) << "},\n";
    o << "  \"exposure\": " << stats.exposure << ",\n";
    o << "  \"camera\": {\"position\": [" << stats.cameraPos[0] << ", " << stats.cameraPos[1] << ", " << stats.cameraPos[2]
      << "], \"target\": [" << stats.cameraTarget[0] << ", " << stats.cameraTarget[1] << ", " << stats.cameraTarget[2] << "]},\n";
    o << "  \"region_dump\": {\"files\": " << stats.regionFiles << ", \"invalid\": " << stats.regionInvalid << "},\n";

    o << "  \"cli\": {";
    bool firstCli = true;
    for (const char* key : {"regions", "region-fields", "region-first", "region-last", "screenshot",
                            "screenshot-frames", "frames", "view-mode", "run-manifest"}) {
        const std::string v = config.GetString(key, "");
        if (v.empty()) continue;
        if (!firstCli) o << ", ";
        firstCli = false;
        o << "\"" << key << "\": \"" << JsonEscape(v) << "\"";
    }
    o << "},\n";

    // Effective CVars: every registered CVar with its current value, plus the subset that
    // differs from the built-in default (what a project/scene/CLI actually changed).
    const auto& all = uz::CVarRegistry::Get().GetAll();
    std::vector<std::string> names;
    names.reserve(all.size());
    for (const auto& kv : all) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    // The complete command line and every override as applied, in order (project file first,
    // then the command line), so the condition a capture ran under is in its own record.
    o << "  \"argv\": [";
    for (size_t i = 0; i < g_argv.size(); ++i) {
        if (i) o << ", ";
        o << "\"" << JsonEscape(g_argv[i]) << "\"";
    }
    o << "],\n";
    o << "  \"cvar_overrides\": [";
    {
        bool firstOv = true;
        for (const auto& [name, value] : config.GetPendingCvarOverrides()) {
            if (!firstOv) o << ", ";
            firstOv = false;
            o << "{\"name\": \"" << JsonEscape(name) << "\", \"value\": " << value << "}";
        }
    }
    o << "],\n";
    o << "  \"cvars_non_default\": {";
    bool firstCv = true;
    for (const auto& n : names) {
        const auto& cv = all.at(n);
        if (cv.value == cv.defaultValue) continue;
        if (!firstCv) o << ", ";
        firstCv = false;
        o << "\"" << JsonEscape(n) << "\": " << cv.value;
    }
    o << "},\n";
    o << "  \"cvars\": {";
    firstCv = true;
    for (const auto& n : names) {
        if (!firstCv) o << ", ";
        firstCv = false;
        o << "\"" << JsonEscape(n) << "\": " << all.at(n).value;
    }
    o << "}\n}\n";

    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        const std::string text = o.str();
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        UZ_LOG(Engine, Info, "Run manifest written: {}", path);
    } else {
        UZ_LOG(Engine, Error, "Run manifest: cannot open '{}'", path);
    }
}

static int RunWindowed(const RHIDeviceDesc& deviceDesc, int width, int height, int maxFrames,
                       const std::string& windowTitle,
                       const std::string& projectDir,
                       const std::string& contentRoot,
                       const std::string& defaultScene,
                       const std::string& screenshotPath = {},
                       int screenshotFrame = 5,
                       int shadowDebugMode = 0,
                       bool editorMode = false,
                       bool diagMode = false,
                       bool pixelDebugEnabled = false,
                       int pixelDebugX = 960,
                       int pixelDebugY = 540,
                       bool passDump = false,
                       Config* configPtr = nullptr,
                       const std::string& screenshotFrames = {}) {
    // --screenshot-frames A-B[:step]: capture EVERY frame of the range from this one
    // process (numbered <screenshot base>_<frame>.png). Consecutive frames then share
    // history, loading state and timing — what a static-vs-motion flicker analysis
    // needs; separate `--screenshot-frame K` runs only approximate that.
    uint32_t seqFirst = 0, seqLast = 0, seqStep = 1; bool seqActive = false;
    if (!screenshotFrames.empty() && !screenshotPath.empty()) {
        unsigned a = 0, b = 0, st = 1;
        const int n = std::sscanf(screenshotFrames.c_str(), "%u-%u:%u", &a, &b, &st);
        if (n >= 2 && b >= a) { seqFirst = a; seqLast = b; seqStep = std::max(st, 1u); seqActive = true; }
        else UZ_LOG(Engine, Warning, "--screenshot-frames '{}' not understood (expected A-B or A-B:step)", screenshotFrames);
    }
    std::string seqBase = screenshotPath, seqExt = ".png";
    if (seqActive) {
        const size_t dot = screenshotPath.find_last_of('.');
        if (dot != std::string::npos && dot > screenshotPath.find_last_of("/\\") + 0) { seqBase = screenshotPath.substr(0, dot); seqExt = screenshotPath.substr(dot); }
    }
    // --diag / --pixel-debug / --pass-dump: enforce a frame limit
    int effectiveMaxFrames = maxFrames;
    if ((diagMode || pixelDebugEnabled || passDump) && maxFrames == kInfiniteFrames) {
        effectiveMaxFrames = 120;
        UZ_LOG(Engine, Info, "Diagnostic mode: auto-limiting to {} frames (use --frames to override)", effectiveMaxFrames);
    }
    // Safety net: --screenshot without --frames also gets a limit
    if (!screenshotPath.empty() && maxFrames == kInfiniteFrames) {
        effectiveMaxFrames = 500;
    }
    bool screenshotRequested = false;

    // ── Window ──────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = windowTitle.empty() ? "UzEngine Runtime" : windowTitle.c_str();
    winDesc.width = width;
    winDesc.height = height;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(Engine, Error, "Failed to create window");
        return kExitRenderError;
    }
    window.Show();

    // ── Device ──────────────────────────────────────────────────────────
    auto rhiDevice = CreateRHIDevice(deviceDesc);
    if (!rhiDevice) {
        UZ_LOG(Engine, Error, "Failed to create RHI device");
        return kExitRenderError;
    }
    auto* device = rhiDevice.get();

    // ── Swapchain ───────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetNativeHandle();
    scDesc.displayHandle = window.GetNativeDisplayHandle();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    auto* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(Engine, Error, "Failed to create swapchain");
        return kExitRenderError;
    }

    // ── Command List ────────────────────────────────────────────────────
    auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Engine, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        return kExitRenderError;
    }

    // ── Per-frame fences ────────────────────────────────────────────────
    RunStats runStats{};
    if (configPtr) runStats.measuredFirst = static_cast<uint32_t>(configPtr->GetInt("timing-first", 200));

    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};

    if (!CreateFrameFences(device, frameFences, kFramesInFlight)) {
        device->DestroyCommandList(cmdList);
        device->DestroySwapChain(swapChain);
        return kExitRenderError;
    }

    // ── World + VFS + Lua ─────────────────────────────────────────────
    World world;
    const std::string resolvedContentRoot = projectDir.empty()
        ? std::filesystem::current_path().string()
        : projectDir + "/" + contentRoot;
    LooseFileVFS vfs(resolvedContentRoot);

    // Load scene if specified
    if (!defaultScene.empty()) {
        auto sceneBytes = vfs.Read(defaultScene);
        if (!sceneBytes.empty()) {
            const size_t count = LoadScene(world, sceneBytes, defaultScene, &vfs);
            UZ_LOG(Engine, Info, "Loaded {} entities from '{}'", count, defaultScene);
        } else {
            UZ_LOG(Engine, Warning, "Failed to read scene: '{}'", defaultScene);
        }
    }

    // Initialize Lua scripting
    bool luaReady = false;
    if (LuaVM::Init()) {
        LuaSetWorld(LuaVM::GetState(), &world);
        LuaSetVFS(LuaVM::GetState(), &vfs);
        ScriptSystem::Initialize(&world, &vfs);
        luaReady = true;
        UZ_LOG(Engine, Info, "Lua scripting initialized");
    } else {
        UZ_LOG(Engine, Warning, "LuaVM failed to initialize — scripting disabled");
    }

    // Flush pending --cvar overrides BEFORE SceneRenderer::Init so any
    // file-scope CVar declared via UZ_CVAR_BOOL in a translation unit
    // included from the renderer (registered during static init) sees the
    // CLI override during the renderer's own Init() — not after. Without
    // this ordering, init-time gating CVars (`r.BrixelizerGI.RealInit`,
    // `r.BrixelizerGI.AllocSdf`, etc.) would always observe the default
    // value because the override was still pending in the Config map.
    if (configPtr) configPtr->FlushCvarOverrides();

    // ── Scene Renderer ─────────────────────────────────────────────────
    SceneRenderer sceneRenderer;
    // Forward the project/CLI-supplied content root to the renderer so
    // features like VFX hot-reload can attach to the correct directory.
    // When the project did not set one, the renderer skips hot-reload
    // silently — no path is ever hardcoded inside the runtime.
    if (configPtr) {
        sceneRenderer.SetContentRoot(configPtr->GetString("content_root", ""));
    }
    bool sceneRendererReady = sceneRenderer.Init(device, &window,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    if (!sceneRendererReady) {
        UZ_LOG(Engine, Warning, "SceneRenderer init failed — falling back to clear screen");
    }

    // Re-flush in case SceneRenderer::Init registered any new CVars
    // referenced by the same CLI args. Idempotent — applies any names
    // that didn't exist on the first pass.
    if (configPtr) configPtr->FlushCvarOverrides();

    // Apply shadow debug mode from CLI (--shadow-debug <0-8>)
    if (shadowDebugMode > 0 && sceneRendererReady) {
        sceneRenderer.SetShadowDebugMode(static_cast<uint32_t>(shadowDebugMode));
        UZ_LOG(Engine, Info, "Shadow debug mode set to {}", shadowDebugMode);
    }

    // Region dump (diagnostics): --region-dump <dir> with either
    //   --regions "name:x,y,w,h;name2:x,y,w,h"   (up to 8 rectangles), or
    //   --region-x X --region-y Y --region-w W --region-h H   (single rectangle),
    // plus --region-fields output|lighting|taa|full and --region-first/--region-last.
    // Per-pass GPU timestamps are off by default (they cost a pair of queries per pass), so a
    // manifest would otherwise report CPU wall time only — which is a different quantity from the
    // GPU cost an acceptance gate asks about. Enabled when a manifest is requested, for --diag
    // (its report carries the per-pass statistics) and for --gpu-profile.
    if (sceneRendererReady && configPtr
        && (diagMode || configPtr->GetBool("gpu-profile", false)
            || !configPtr->GetString("run-manifest", "").empty())) {
        sceneRenderer.SetGpuProfileEnabled(true);
    }

    // --component-replay <file>: replay recorded AO and specular per frame and per pixel
    // (followup-12 H-28 isolation). Built by scripts/make_component_replay.py.
    if (sceneRendererReady && configPtr) {
        const std::string replay = configPtr->GetString("component-replay", "");
        if (!replay.empty()) sceneRenderer.SetComponentReplay(replay);
    }

    if (sceneRendererReady && configPtr) {
        const std::string regionDir = configPtr->GetString("region-dump", "");
        if (!regionDir.empty()) {
            std::vector<uz::SceneRenderer::RegionRect> rects =
                ParseRegionSpecs(configPtr->GetString("regions", ""));
            if (rects.empty()) {
                uz::SceneRenderer::RegionRect one;
                one.name = "roi";
                one.x = static_cast<uint32_t>(configPtr->GetInt("region-x", 0));
                one.y = static_cast<uint32_t>(configPtr->GetInt("region-y", 0));
                one.w = static_cast<uint32_t>(configPtr->GetInt("region-w", 0));
                one.h = static_cast<uint32_t>(configPtr->GetInt("region-h", 0));
                if (one.w != 0 && one.h != 0) rects.push_back(one);
            }
            if (rects.empty()) {
                UZ_LOG(Engine, Warning, "--region-dump given without any rectangle (--regions or --region-w/h)");
            } else {
                sceneRenderer.SetRegionDump(regionDir, std::move(rects),
                    static_cast<uint32_t>(configPtr->GetInt("region-first", 0)),
                    static_cast<uint32_t>(configPtr->GetInt("region-last", 1000000)),
                    ParseRegionFields(configPtr->GetString("region-fields", "")));
            }
        }
    }

    // Apply editor mode from CLI (--editor)
    if (editorMode && sceneRendererReady) {
        sceneRenderer.SetEditorMode(true);
    }

    // Apply debug view mode from CLI (--view-mode <name>). The uzengine
    // runtime has its own Config-based CLI parser (distinct from
    // Application::ParseArgs), so `--view-mode` must be routed here manually
    // or the renderer defaults to Lit regardless of the flag.
    if (sceneRendererReady && configPtr) {
        const std::string vmArg = configPtr->GetString("view-mode", "");
        if (!vmArg.empty()) {
            const DebugViewMode vm = DebugViewModeFromString(std::string_view{vmArg});
            sceneRenderer.SetDebugViewMode(vm);
            UZ_LOG(Engine, Info, "Debug view mode: '{}' (enum={})",
                   DebugViewModeName(vm), static_cast<uint32_t>(vm));
        }
    }

    // ── Editor Input & Selection (only when editor mode is active) ────
    SelectionManager selectionManager;
    EditorInputHandler editorInput;
    if (editorMode && sceneRendererReady) {
        editorInput.Init(sceneRenderer.GetInput(), &selectionManager,
                         &sceneRenderer.GetCamera(), &world);
        editorInput.SetSceneRenderer(&sceneRenderer);
        sceneRenderer.SetEditorInputHandler(&editorInput);
        // Editor mode: WASD moves camera only while RMB held.
        sceneRenderer.GetCameraController().SetEditorMode(true);
        UZ_LOG(Engine, Info, "Editor input handler initialized");
    }

    // Bind SceneRenderer into the Lua VM so uz.renderer / uz.mesh / uz.texture work.
    if (sceneRendererReady && luaReady) {
        LuaSetSceneRenderer(LuaVM::GetState(), &sceneRenderer);
        LuaSetInputSystem(LuaVM::GetState(), sceneRenderer.GetInput());
        UZ_LOG(Engine, Info, "Lua renderer + input API connected to SceneRenderer");
    }

    // Wire editor state into Lua so uz.editor.* can access selection and gizmo.
    if (editorMode && luaReady) {
        LuaSetEditorState(LuaVM::GetState(), &selectionManager, &editorInput);
    }

    // ── Pixel Debug (GPU printf) ────────────────────────────────────────
    std::unique_ptr<PixelDebugSystem> pixelDebug;
    if (pixelDebugEnabled && sceneRendererReady) {
        pixelDebug = std::make_unique<PixelDebugSystem>();
        PixelDebugConfig pdConfig;
        pdConfig.pixelX     = static_cast<uint32_t>(pixelDebugX);
        pdConfig.pixelY     = static_cast<uint32_t>(pixelDebugY);
        pdConfig.maxRecords = 256;
        pdConfig.enabled    = true;
        if (pixelDebug->Init(device, pdConfig)) {
            sceneRenderer.SetPixelDebugSystem(pixelDebug.get());
            UZ_LOG(Engine, Info, "PixelDebug enabled: pixel=({},{})", pixelDebugX, pixelDebugY);
        } else {
            UZ_LOG(Engine, Error, "PixelDebug initialization failed");
            pixelDebug.reset();
        }
    }

    // ── Pre-load: drain async texture decodes before the render loop ──────
    // Render one frame to trigger lazy material texture loads (SyncWorldToDrawList
    // → LoadTexture → async decoder submit), then drain all pending decodes via
    // the lightweight DrainPendingTextureLoads (no full RenderFrame overhead).
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;

    // Finalize skeletal-mesh components after SceneRenderer is up: loads the
    // .uzmesh / .uzskel / .uzanim for every animated entity, wires per-entity
    // procedural output meshes, and registers the animation subsystem state.
    if (sceneRendererReady) {
        SkeletalMeshSubsystem::Get().Finalize(world, sceneRenderer, vfs);
    }

    if (sceneRendererReady && !defaultScene.empty()) {
        UZ_LOG(Engine, Info, "Pre-loading scene geometry, distance fields and textures...");
#ifdef _WIN32
        const std::string loadingTitle = std::string(winDesc.title) + " - Loading scene...";
        window.SetTitle(loadingTitle.c_str());
#endif
        // The old preload presented one frame and drained only the textures the
        // first meshes had requested; the 4-meshes-per-frame budget kept
        // streaming geometry for seconds afterwards while lighting computed
        // through missing occluders was already on screen. Pump batches and
        // window messages until readiness, presenting nothing.
        sceneRenderer.SetPreloading(true);   // mesh SDF bakes unthrottled while nothing is presented (block B.3)
        while (window.PumpMessages() && window.IsOpen()) {
            sceneRenderer.RenderFrame(cmdList, swapChain, &world, &vfs, 0.016f, frameIndex);
            device->WaitIdle();
            ++frameIndex;

            cmdList->Begin();
            sceneRenderer.DrainPendingTextureLoads(cmdList, &vfs);
            cmdList->End();
            cmdList->Execute();
            device->WaitIdle();
            if (sceneRenderer.IsSceneFullyLoaded()) {
                // Exposure reads the previous HDR frame, then its result returns
                // through a one-frame GPU readback. Seed both with the ready scene.
                sceneRenderer.RenderFrame(cmdList, swapChain, &world, &vfs, 0.0f, frameIndex++);
                device->WaitIdle();
                cmdList->Begin();
                sceneRenderer.ResetExposureHistory(cmdList);
                cmdList->End();
                cmdList->Execute();
                device->WaitIdle();
                sceneRenderer.RenderFrame(cmdList, swapChain, &world, &vfs, 0.0f, frameIndex++);
                device->WaitIdle();
                UZ_LOG(Engine, Info, "Pre-load complete: assets ready and exposure seeded after {} frames; no partial scene presented", frameIndex);
                break;
            }
        }
        sceneRenderer.SetPreloading(false);
#ifdef _WIN32
        window.SetTitle(winDesc.title);
#endif
    }

    // ── Render Loop ─────────────────────────────────────────────────────
    auto startTime = std::chrono::high_resolution_clock::now();
    auto prevTime = startTime;

    // frameLimit accounts for pre-load frames so --frames N means N frames after pre-load.
    const uint32_t frameLimit = effectiveMaxFrames == kInfiniteFrames
        ? 0 : (frameIndex + static_cast<uint32_t>(effectiveMaxFrames));
    // --diag measures GPU timings inside its own frame budget: unless --timing-first was given,
    // the window opens after the first quarter of the budget (the default of 200 lies beyond
    // the 120-frame diagnostic run and collected nothing).
    if (diagMode && configPtr && frameLimit > 0 && configPtr->GetString("timing-first", "").empty()) {
        runStats.measuredFirst = frameIndex + static_cast<uint32_t>(effectiveMaxFrames) / 4;
    }
    UZ_LOG(Engine, Info, "Starting render loop (frames={})", effectiveMaxFrames);

    while (window.PumpMessages() && window.IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (frameLimit > 0 && frameIndex >= frameLimit) break;

        if (window.WasResized()) {
            device->WaitIdle();
            const uint32_t newW = static_cast<uint32_t>(window.GetWidth());
            const uint32_t newH = static_cast<uint32_t>(window.GetHeight());
            swapChain->Resize(newW, newH);
            if (sceneRendererReady) {
                sceneRenderer.Resize(newW, newH);
            }
            window.ClearResizedFlag();
        }

        // Wait for frame N-2 before reusing its allocator
        const uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // Update timing
        auto now = std::chrono::high_resolution_clock::now();
        const float t = std::chrono::duration<float>(now - startTime).count();
        const float dt = std::chrono::duration<float>(now - prevTime).count();
        prevTime = now;

        // Run manifest (diagnostics): CPU wall time per frame once loading is over, so the
        // cost of instrumentation can be compared between runs instead of assumed.
        if (frameIndex >= runStats.measuredFirst) {
            runStats.frameMs.push_back(dt * 1000.f);
            if (sceneRendererReady && sceneRenderer.GetDevice()) {
                const auto& results = sceneRenderer.GetDevice()->GpuProfilerGetResults();
                for (const auto& r : results) {
                    if (!r.name) continue;
                    auto& acc = runStats.gpuMs[r.name];
                    if (acc.count == 0 || r.ms < acc.minMs) acc.minMs = r.ms;
                    if (acc.count == 0 || r.ms > acc.maxMs) acc.maxMs = r.ms;
                    acc.sum += static_cast<double>(r.ms);
                    acc.count += 1u;
                }
                if (!results.empty()) {
                    runStats.gpuFrameMs.push_back(sceneRenderer.GetDevice()->GpuProfilerGetFrameTimeMs());
                }
            }
        }

        // Update editor input (before scripts, so editor hotkeys take priority)
        if (editorMode && sceneRendererReady) {
            editorInput.Update(dt);
            // Sync selection + gizmo state to renderer for outline/gizmo passes
            sceneRenderer.SetSelectedEntity(selectionManager.GetSelected());
            sceneRenderer.SetGizmoMode(static_cast<uint8_t>(editorInput.GetGizmoMode()));
            sceneRenderer.SetGizmoSpace(static_cast<uint8_t>(editorInput.GetGizmoSpace()));
            sceneRenderer.SetGizmoHoveredAxis(editorInput.GetGizmoHoveredAxis());
            sceneRenderer.SetGizmoActiveAxis(editorInput.GetGizmoActiveAxis());
        }

        // Update Lua scripts
        if (luaReady) {
            LuaVM::UpdateFrameGlobals(dt, t, static_cast<uint64_t>(frameIndex));
            ScriptSystem::Update(dt);
        }

        // Advance all skeletal animation controllers in parallel and CPU-skin
        // each entity's vertex buffer. Must run before RenderFrame so the GPU
        // upload (queued inside SceneRenderer::RenderFrame → FlushPendingLoads)
        // sees the latest skinned bytes.
        SkeletalMeshSubsystem::Get().TickAll(dt);

        // Render
        if (sceneRendererReady) {
            sceneRenderer.RenderFrame(cmdList, swapChain, &world, &vfs, dt, frameIndex);
        } else {
            RenderClearFrame(cmdList, swapChain, t);
        }

        // Screenshot capture — waits for all assets (meshes, textures) to finish loading
        // before capturing, so async-loaded scenes appear fully rendered.
        if (!seqActive && sceneRendererReady && !screenshotPath.empty() && !screenshotRequested
            && frameIndex >= static_cast<uint32_t>(screenshotFrame)) {
            // Log asset loading state once per frame until ready
            if (frameIndex % 30 == 0) (void)sceneRenderer.IsSceneFullyLoadedDebug();
        }
        if (seqActive) {
            // RenderFrame(frameIndex) has already run at this point, so a request issued now is
            // captured from the NEXT frame's render: name the file by the frame it will contain
            // (renderIdx) so `<base>_<N>.png` and the region dump's `region_<N>.bin` refer to the
            // same render. Captures made before 2026-09-09 carry the old naming (file N = render N+1).
            const uint32_t renderIdx = frameIndex + 1;
            if (sceneRendererReady && renderIdx >= seqFirst && renderIdx <= seqLast
                && (renderIdx - seqFirst) % seqStep == 0 && sceneRenderer.IsSceneFullyLoaded()) {
                sceneRenderer.RequestScreenshot(seqBase + "_" + std::to_string(renderIdx) + seqExt);
            }
        } else if (sceneRendererReady && !screenshotPath.empty() && !screenshotRequested
            && frameIndex >= static_cast<uint32_t>(screenshotFrame)
            // No 120-frame timeout bypass: readiness is now exact (budget-deferred
            // meshes, uploads, SDF bakes and async slots all count), so waiting for
            // it cannot hang, and capturing a partial scene is never acceptable.
            && sceneRenderer.IsSceneFullyLoaded()) {
            sceneRenderer.RequestScreenshot(screenshotPath);
            screenshotRequested = true;
        }

        // Present + signal fence
        swapChain->Present(false);
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);

        frameIndex++;
    }

    // ── Run manifest (diagnostics) ──────────────────────────────────────
    if (configPtr) {
        const std::string manifestPath = configPtr->GetString("run-manifest", "");
        if (!manifestPath.empty()) {
            runStats.framesRendered = frameIndex;
            // The *internal* render resolution, which is the window size scaled by
            // r.ScreenPercentage — not the window size. Writing `width`/`height` here made every
            // manifest claim a full-resolution render even when the renderer had resized (a
            // 50 % capture logged "screen percentage set to 50.0%" while its manifest still said
            // 1920x1080). Same formula as SceneRenderer::Resize.
            runStats.renderWidth = static_cast<uint32_t>(width);
            runStats.renderHeight = static_cast<uint32_t>(height);
            if (sceneRendererReady) {
                const float pct = sceneRenderer.GetScreenPercentage();
                runStats.renderWidth =
                    std::max(1u, static_cast<uint32_t>(static_cast<float>(width) * pct / 100.f));
                runStats.renderHeight =
                    std::max(1u, static_cast<uint32_t>(static_cast<float>(height) * pct / 100.f));
                runStats.exposure = sceneRenderer.GetLastFrameExposure();
                runStats.regionFiles = sceneRenderer.GetRegionDumpFileCount();
                runStats.regionInvalid = sceneRenderer.GetRegionDumpMismatchCount();
                const auto& cam = sceneRenderer.GetCamera();
                const Vec3 p = cam.GetPosition();
                const Vec3 tgt = cam.GetPosition() + cam.GetForward();
                runStats.cameraPos[0] = p.x; runStats.cameraPos[1] = p.y; runStats.cameraPos[2] = p.z;
                runStats.cameraTarget[0] = tgt.x; runStats.cameraTarget[1] = tgt.y; runStats.cameraTarget[2] = tgt.z;
            }
            WriteRunManifest(manifestPath, *configPtr, configPtr->GetString("project", projectDir), defaultScene, width, height, runStats);
        }
    }

    // ── Cleanup (reverse order of creation) ─────────────────────────────
    if (luaReady) {
        ScriptSystem::Shutdown();
        LuaVM::Shutdown();
    }

    device->WaitIdle();

    // ── Pass dump — all targets as PNGs + INDEX.html ──────────────────
    if (passDump && sceneRendererReady) {
        auto& inspector = sceneRenderer.GetPassInspector();
        inspector.ExecuteDump(device, cmdList, frameIndex, ".screenshots");
    }

    // ── Diagnostic output (JSON to stdout) ──────────────────────────────
    if (diagMode && sceneRendererReady) {
        auto& inspector = sceneRenderer.GetPassInspector();
        // Per-pass GPU statistics of the measured window (mean / min / max / samples) plus the
        // GPU frame span, from the same accumulation the run manifest reports.
        std::vector<PassTimingEntry> timing;
        for (const auto& [name, acc] : runStats.gpuMs) {
            if (acc.count == 0) continue;
            PassTimingEntry e{};
            e.name      = name;
            e.gpuTimeMs = static_cast<float>(acc.sum / acc.count);
            e.minMs     = acc.minMs;
            e.maxMs     = acc.maxMs;
            e.samples   = acc.count;
            timing.push_back(std::move(e));
        }
        if (!runStats.gpuFrameMs.empty()) {
            PassTimingEntry e{};
            e.name  = "_frame";
            e.minMs = runStats.gpuFrameMs.front();
            e.maxMs = e.minMs;
            double sum = 0.0;
            for (float v : runStats.gpuFrameMs) {
                sum += v;
                e.minMs = std::min(e.minMs, v);
                e.maxMs = std::max(e.maxMs, v);
            }
            e.gpuTimeMs = static_cast<float>(sum / static_cast<double>(runStats.gpuFrameMs.size()));
            e.samples   = static_cast<uint32_t>(runStats.gpuFrameMs.size());
            timing.push_back(std::move(e));
        }
        inspector.SetTimingData(timing);
        DiagnosticReport report = inspector.GenerateReport(device, cmdList, frameIndex);
        std::printf("DIAG_JSON:%s\n", report.ToJSON().c_str());
        std::fflush(stdout);
    }

    if (pixelDebug && pixelDebug->IsEnabled()) {
        auto records = pixelDebug->ReadResults(device);
        const std::string json = PixelDebugSystem::ToJSON(
            records, static_cast<uint32_t>(pixelDebugX),
            static_cast<uint32_t>(pixelDebugY), frameIndex);
        std::printf("PIXEL_DEBUG_JSON:%s\n", json.c_str());
        std::fflush(stdout);
    }

    if (sceneRendererReady) {
        sceneRenderer.Shutdown();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    const float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    LogRenderStats(frameIndex, totalMs);

    // ── Pixel Debug cleanup ────────────────────────────────────────────
    if (pixelDebug) {
        pixelDebug->Shutdown(device);
        pixelDebug.reset();
    }

    for (uint32_t i = kFramesInFlight; i > 0; --i) {
        device->DestroyFence(frameFences[i - 1]);
    }
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);

    return kExitSuccess;
}

#endif // _WIN32

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    uz::LogInit();

    // Config: project file + CLI overrides
    g_argv.assign(argv, argv + argc);
    uz::Config config = uz::BuildConfig(argc, argv);

    // --rt-test: enable RT and the UV test pass via CVar overrides
    if (config.GetBool("rt-test", false)) {
        config.AddPendingCvarOverride("r.RayTracing", 1.0f);
        config.AddPendingCvarOverride("r.RayTracing.TestPass", 1.0f);
    }

    // Log level: default is Error (set in log.cpp g_runtimeLevel).
    // --verbose sets Info. --log-level overrides both.
    {
        const std::string ll = config.GetString("log-level",
            config.GetString("log_level", ""));
        if (!ll.empty()) {
            if (ll == "trace")        uz::LogSetLevel(uz::LogLevel::Trace);
            else if (ll == "debug")   uz::LogSetLevel(uz::LogLevel::Debug);
            else if (ll == "info")    uz::LogSetLevel(uz::LogLevel::Info);
            else if (ll == "warning") uz::LogSetLevel(uz::LogLevel::Warning);
            else if (ll == "error")   uz::LogSetLevel(uz::LogLevel::Error);
            else UZ_LOG(Engine, Warning, "Unknown log level '{}', using default", ll);
        } else if (config.GetBool("verbose", false)) {
            uz::LogSetLevel(uz::LogLevel::Info);
        }
    }

    UZ_LOG(Engine, Info, "UzEngine Runtime starting");

    // Resolve RHI settings from config
    const uz::RHIConfig rhiCfg = uz::ResolveRHIConfig(config);
    const uz::RHIDeviceDesc deviceDesc = uz::ToDeviceDesc(rhiCfg);

    const bool headless = config.GetBool("headless", false);
    const std::string projectPath = config.GetString("project");

    // Load .uzproject and extract project-level settings
    std::string windowTitle;
    std::string projectDir;
    std::string contentRoot;
    std::string defaultScene;
    int width = config.GetInt("width", uz::kDefaultWidth);
    int height = config.GetInt("height", uz::kDefaultHeight);
    const int maxFrames = config.GetInt("frames", uz::kInfiniteFrames);

    if (!projectPath.empty()) {
        auto projectCfg = uz::LoadProject(projectPath);
        if (projectCfg) {
            projectDir = std::filesystem::path(projectPath).parent_path().string();
            contentRoot = projectCfg->contentRoot;
            defaultScene = projectCfg->defaultScene;
            windowTitle = projectCfg->windowTitle;

            // Project values serve as defaults; CLI overrides take precedence
            if (!config.Has("width"))  width = projectCfg->windowWidth;
            if (!config.Has("height")) height = projectCfg->windowHeight;

            // Push the project's "cvars" block onto the pending override
            // queue as *defaults*. They are flushed in the same place as the
            // `--cvar` CLI overrides (just before SceneRenderer::Init), so
            // init-time gating CVars see them — but the CLI entries were queued
            // earlier, during argument parsing, so appending here would put the
            // project AFTER the command line and let it win (that was the
            // `0, 0, 1` flush order). Front-insertion keeps explicit CLI values
            // last, and therefore authoritative, on duplicate names.
            for (const auto& [name, value] : projectCfg->cvarOverrides) {
                config.AddPendingCvarOverrideDefault(name, value);
            }
            if (!projectCfg->cvarOverrides.empty()) {
                UZ_LOG(Engine, Info,
                       "Project: queued {} CVar override(s) from .uzproject",
                       projectCfg->cvarOverrides.size());
            }
        }
    }

    // Fallback: --scene without --project
    if (defaultScene.empty()) {
        defaultScene = config.GetString("scene");
    }

    uz::LogResolvedConfig(rhiCfg, headless, maxFrames, width, height);

    int exitCode = uz::kExitSuccess;

    // ── FLIP comparison mode ────────────────────────────────────────────
    const std::string flipRef  = config.GetString("flip-ref");
    const std::string flipTest = config.GetString("flip-test");
    if (!flipRef.empty() && !flipTest.empty()) {
        const std::string flipOutput = config.GetString("flip-output");
        const float flipThreshold = config.GetFloat("flip-threshold", 0.05f);
        exitCode = uz::RunFlip(deviceDesc, flipRef, flipTest, flipOutput, flipThreshold);
        UZ_LOG(Engine, Info, "UzEngine Runtime exiting with code {}", exitCode);
        uz::LogShutdown();
        return exitCode;
    }

    if (headless) {
        exitCode = uz::RunHeadless(deviceDesc);
    } else {
#ifdef _WIN32
        const std::string ssPath = config.GetString("screenshot");
        const int ssFrame = config.GetInt("screenshot-frame", 5);
        const int shadowDebug = config.GetInt("shadow-debug", 0);
        const bool editorMode = config.GetBool("editor", false);
        const bool diagMode = config.GetBool("diag", false);
        const bool pixelDebugEnabled = config.GetBool("pixel-debug", false);
        const int pixelDebugX = config.GetInt("pixel-debug-x", 960);
        const int pixelDebugY = config.GetInt("pixel-debug-y", 540);
        const bool passDump = config.GetBool("pass-dump", false);
        exitCode = uz::RunWindowed(deviceDesc, width, height, maxFrames,
                                    windowTitle, projectDir, contentRoot, defaultScene,
                                    ssPath, ssFrame, shadowDebug, editorMode,
                                    diagMode, pixelDebugEnabled, pixelDebugX, pixelDebugY,
                                    passDump, &config, config.GetString("screenshot-frames"));
#else
        UZ_LOG(Engine, Error, "Windowed mode not supported on this platform yet");
        exitCode = uz::kExitRenderError;
#endif
    }

    UZ_LOG(Engine, Info, "UzEngine Runtime exiting with code {}", exitCode);
    uz::LogShutdown();
    return exitCode;
}
