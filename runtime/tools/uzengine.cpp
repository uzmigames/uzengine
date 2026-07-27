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
#include "world.h"
#include "scene_loader.h"
#include "../engine/ecs/scene/camera_component.h"
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
#include <cstdlib>
#include <filesystem>
#include <string>
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
                       Config* configPtr = nullptr) {
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
    // --hidden: keep the HWND invisible (swapchain still works). Automated
    // capture runs (--screenshot/--diag/--pass-dump) shouldn't steal focus.
    const bool hiddenWindow = configPtr && configPtr->GetBool("hidden", false);
    if (!hiddenWindow) {
        window.Show();
    } else {
        UZ_LOG(Engine, Info, "Hidden window mode: HWND created but not shown");
    }

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

    // ── CLI camera pose override (--camera-pos x,y,z / --camera-look x,y,z) ──
    // Applied to the first ACTIVE CameraComponent after scene load, before the
    // first frame, so both play-mode ECS sync and the editor fly-cam seed pick
    // it up. Primary use: repeatable multi-pose captures (path-tracer goldens,
    // PSNR comparisons) without authoring scene variants. In play mode a
    // camera-driving Lua script may still override the pose every frame — use
    // --editor (scripts dormant) for deterministic captures.
    if (configPtr) {
        const std::string camPosArg  = configPtr->GetString("camera-pos", "");
        const std::string camLookArg = configPtr->GetString("camera-look", "");
        auto parseVec3 = [](const std::string& s, Vec3& out) -> bool {
            float x = 0.f, y = 0.f, z = 0.f;
            if (std::sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) != 3) return false;
            out = Vec3{x, y, z};
            return true;
        };
        Vec3 camPos{}, camLook{};
        const bool hasPos  = !camPosArg.empty()  && parseVec3(camPosArg, camPos);
        const bool hasLook = !camLookArg.empty() && parseVec3(camLookArg, camLook);
        if (hasPos || hasLook) {
            bool applied = false;
            auto camView = world.View<CameraComponent>();
            for (auto [entity, cam] : camView.each()) {
                if (!cam.isActive) continue;
                // CameraComponent derives from SceneComponent — position is
                // set directly on the component.
                if (hasPos) cam.SetPosition(camPos);
                if (hasLook) {
                    cam.hasLookTarget = true;
                    cam.lookTarget    = camLook;
                }
                applied = true;
                UZ_LOG(Engine, Info,
                       "Camera override: pos=({}) look=({})",
                       hasPos ? camPosArg : "scene", hasLook ? camLookArg : "scene");
                break;
            }
            if (!applied) {
                UZ_LOG(Engine, Warning,
                       "--camera-pos/--camera-look given but no active CameraComponent found");
            }
        }
    }

    // Initialize Lua scripting
    bool luaReady = false;
    if (LuaVM::Init()) {
        LuaSetWorld(LuaVM::GetState(), &world);
        LuaSetVFS(LuaVM::GetState(), &vfs);
        ScriptSystem::Initialize(&world, &vfs);
        // Standalone runtime: scripts are dormant in editor mode (--editor
        // CLI) and active otherwise. Mirrors the Unreal / Unity / Godot
        // editor convention — BeginPlay / Start / _ready only fire when the
        // game is actually running.
        ScriptSystem::SetPlayMode(!editorMode);
        luaReady = true;
        UZ_LOG(Engine, Info, "Lua scripting initialized (play mode: {})",
               editorMode ? "OFF" : "ON");
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

        // Route --vis <target> to the PassInspector for the same reason —
        // without this the flag is silently ignored by the uzengine tool
        // (only DemoApplication parsed it) and the "visualize any pass
        // target" workflow documented in rendering-diagnostics.md is dead.
        const std::string visArg = configPtr->GetString("vis", "");
        if (!visArg.empty()) {
            sceneRenderer.GetPassInspector().SetVisTarget(visArg.c_str());
            UZ_LOG(Engine, Info, "PassInspector vis target: '{}'", visArg);
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
        UZ_LOG(Engine, Info, "Pre-loading scene textures...");

        // First frame triggers lazy material texture loads via SyncWorldToDrawList.
        sceneRenderer.RenderFrame(cmdList, swapChain, &world, &vfs, 0.016f, 0);
        swapChain->Present(false);
        globalFenceValue++;
        fenceValues[0] = globalFenceValue;
        frameFences[0]->Signal(globalFenceValue);
        frameIndex = 1;

        // Wait for frame 0 to finish before draining textures on the same command list.
        frameFences[0]->WaitCPU(globalFenceValue);

        // Drain all pending async texture decodes + GPU uploads.
        cmdList->Begin();
        sceneRenderer.DrainPendingTextureLoads(cmdList, &vfs);
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();

        UZ_LOG(Engine, Info, "Pre-load complete, all textures loaded");
    }

    // ── Render Loop ─────────────────────────────────────────────────────
    auto startTime = std::chrono::high_resolution_clock::now();
    auto prevTime = startTime;

    // frameLimit accounts for pre-load frames so --frames N means N frames after pre-load.
    const uint32_t frameLimit = effectiveMaxFrames == kInfiniteFrames
        ? 0 : (frameIndex + static_cast<uint32_t>(effectiveMaxFrames));
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
        if (sceneRendererReady && !screenshotPath.empty() && !screenshotRequested
            && frameIndex >= static_cast<uint32_t>(screenshotFrame)) {
            // Log asset loading state once per frame until ready
            if (frameIndex % 30 == 0) (void)sceneRenderer.IsSceneFullyLoadedDebug();
        }
        if (sceneRendererReady && !screenshotPath.empty() && !screenshotRequested
            && frameIndex >= static_cast<uint32_t>(screenshotFrame)
            && (sceneRenderer.IsSceneFullyLoaded() || frameIndex >= static_cast<uint32_t>(screenshotFrame) + 120)) {
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

    // Teardown checkpoints flush the buffered log so an abort inside any
    // teardown stage (e.g. the D3D12MA not-empty-block assert) still leaves
    // the log on disk up to the last completed stage.
    UZ_LOG(Engine, Info, "Teardown: SceneRenderer.Shutdown begin");
    LogFlushAll();
    if (sceneRendererReady) {
        sceneRenderer.Shutdown();
    }
    UZ_LOG(Engine, Info, "Teardown: SceneRenderer.Shutdown done");
    LogFlushAll();

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
    UZ_LOG(Engine, Info, "Teardown: fences/cmdlist/swapchain destroyed");
    LogFlushAll();

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
            // queue. These are flushed in the same place as `--cvar` CLI
            // overrides (just before SceneRenderer::Init), so init-time
            // gating CVars see the project values. CLI overrides added
            // below — e.g., `--cvar` or `--rt-test` — are appended after
            // these and therefore take precedence on duplicate names.
            for (const auto& [name, value] : projectCfg->cvarOverrides) {
                config.AddPendingCvarOverride(name, value);
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
                                    passDump, &config);
#else
        UZ_LOG(Engine, Error, "Windowed mode not supported on this platform yet");
        exitCode = uz::kExitRenderError;
#endif
    }

    UZ_LOG(Engine, Info, "UzEngine Runtime exiting with code {}", exitCode);
    uz::LogShutdown();
    return exitCode;
}
