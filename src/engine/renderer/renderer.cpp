// ---------------------------------------------------------------------------
// renderer.cpp -- Renderer implementation using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  Zero backend-specific includes.
//
// RULE RHI-1: No backend types stored above rhi.h
// RULE RHI-2: No DirectXMath above rhi.h — uses uz::Mat4
// RULE CS-7:  No exceptions — returns bool for fallible operations
// ---------------------------------------------------------------------------

#include "renderer.h"

// Only need the backend vtable symbol for rhi_set_backend() in init().
extern "C" {
extern const rhi_backend_t rhi_dx12_backend;
}

#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/safe_string.h"

#include "engine/core/diag.h"

namespace {

// Resolve shader directory: <exe_dir>/../../shaders/builtin/
std::string getShaderDir() {
    char exePathBuf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH);
    std::string exeDir(exePathBuf);
    auto lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos)
        exeDir = exeDir.substr(0, lastSlash);
    return exeDir + "/../../shaders/builtin/";
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}

} // anonymous namespace

namespace uz {

// ===========================================================================
// Public API
// ===========================================================================

bool Renderer::init(void* window_handle, uint32_t width, uint32_t height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    // Register the DX12 backend before using rhi().
    rhi_set_backend(&rhi_dx12_backend);
    DIAG("=== renderer.init START ===");

    // Initialize the RHI backend (device, swapchain, heaps, cmd queue).
    DIAG("rhi()->init()...");
    if (!rhi()->init(window_handle, width, height)) {
        DIAG("FATAL: rhi()->init() returned false");
        return false;
    }
    DIAG("rhi()->init() OK");

    // Create renderer-specific resources via RHI.
    DIAG("createPipelineState()...");
    if (!createPipelineState()) {
        DIAG("FATAL: createPipelineState() failed");
        return false;
    }
    DIAG("createPipelineState() OK");


    DIAG("createDepthBuffer()...");
    if (!createDepthBuffer()) {
        DIAG("FATAL: createDepthBuffer() failed");
        return false;
    }

    DIAG("createReadbackBuffers()...");
    if (!createReadbackBuffers()) {
        DIAG("FATAL: createReadbackBuffers() failed");
        return false;
    }

    // EditorComponent — now fully RHI, no bridge needed.
    DIAG("editor_.init()...");
    editor_.init();

    // Engine-math identity matrix.
    viewProj_ = Mat4::identity();

    // Record the stream start time.
    streamTimer_.reset();

    DIAG("=== renderer.init COMPLETE ===");
    initialized_ = true;
    return true;
}

bool Renderer::resize(int width, int height) {
    if (width < 64 || width > 7680 || height < 64 || height > 4320)
        return false;
    if (!initialized_)
        return false;

    const uint32_t newW = static_cast<uint32_t>(width);
    const uint32_t newH = static_cast<uint32_t>(height);

    if (newW == width_ && newH == height_)
        return true;

    // Destroy old depth and readback resources.
    if (rhiDepthBuf_ != RHI_NULL) {
        rhi()->texture_destroy(rhiDepthBuf_);
        rhiDepthBuf_ = RHI_NULL;
    }
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (rhiReadbackBufs_[i] != RHI_NULL) {
            rhi()->buffer_destroy(rhiReadbackBufs_[i]);
            rhiReadbackBufs_[i] = RHI_NULL;
        }
    }

    rhi()->resize(newW, newH);

    width_  = newW;
    height_ = newH;

    createDepthBuffer();
    createReadbackBuffers();

    return true;
}

void Renderer::update(double /*totalTime*/, double deltaTime) {
    fpsOverlay_.update(deltaTime);

    // Check for pending viewport resize.
    if (sceneParams_) {
        auto [rw, rh] = sceneParams_->consumeResize();
        if (rw > 0 && rh > 0 &&
            (static_cast<uint32_t>(rw) != width_ ||
             static_cast<uint32_t>(rh) != height_)) {
            resize(rw, rh);
        }
    }

    if (sceneParams_) {
        std::lock_guard<std::mutex> lk(sceneParams_->bgMutex);
        clearR_ = sceneParams_->bgR;
        clearG_ = sceneParams_->bgG;
        clearB_ = sceneParams_->bgB;
    }

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);

    // Editor fly-cam uses engine math types (camera_component.h).
    const float dt = std::min(static_cast<float>(deltaTime), 0.1f);
    CameraState camState = camera_update_fly(sceneParams_, aspect, dt);
    viewProj_ = camState.view_proj;

    // Collect primary directional light from the scene.
    cachedLight_ = light_collect_primary(scene_);
}

void Renderer::render(bool readback) {
    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) return;

    // -----------------------------------------------------------------------
    // Build the frame-local render graph (§RENDER-PIPELINE)
    // -----------------------------------------------------------------------
    renderGraph_.reset();

    // Import external (persistent) resources into the graph.
    RhiTexture backbuffer = rhi()->swapchain_backbuffer();
    auto rgBackbuffer = renderGraph_.import_texture("backbuffer", backbuffer);
    auto rgDepth      = renderGraph_.import_texture("depth",      rhiDepthBuf_);

    // --- Pass 1: Scene Draw ------------------------------------------------
    // Clears and draws the scene into the backbuffer + depth.
    renderGraph_.add_pass("scene_draw", RgPassType::Graphics,
        [&](RgPassBuilder& builder) {
            builder.write(rgBackbuffer);
            builder.write(rgDepth);
            builder.force_run();
        },
        [this](RhiCmdList cmd) {
            rhi()->cmd_set_pipeline(cmd, rhiPipeline_);

            rhi()->cmd_set_viewport(cmd, 0.0f, 0.0f,
                                   static_cast<float>(width_),
                                   static_cast<float>(height_),
                                   0.0f, 1.0f);
            rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

            RhiTexture bb = rhi()->swapchain_backbuffer();

            rhi()->cmd_barrier_texture(cmd, bb,
                                       RHI_STATE_PRESENT, RHI_STATE_RENDER_TARGET);

            rhi()->cmd_clear_render_target(cmd, bb,
                                           clearR_, clearG_, clearB_, 1.0f);
            rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);

            rhi()->cmd_set_render_targets(cmd, &bb, 1, rhiDepthBuf_);

            // Light data via root constants.
            struct {
                float dir[3]; float intensity;
                float color[3]; float _pad;
            } lightCB;
            lightCB.dir[0]    = cachedLight_.direction.x;
            lightCB.dir[1]    = cachedLight_.direction.y;
            lightCB.dir[2]    = cachedLight_.direction.z;
            lightCB.intensity = cachedLight_.intensity;
            lightCB.color[0]  = cachedLight_.color.x;
            lightCB.color[1]  = cachedLight_.color.y;
            lightCB.color[2]  = cachedLight_.color.z;
            lightCB._pad      = 0.0f;
            rhi()->cmd_set_root_constants(cmd, 2, &lightCB, 8, 0);

            {
                uint32_t selNode = sceneParams_
                    ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;
                int gizMode = sceneParams_
                    ? sceneParams_->gizmoMode.load(std::memory_order_relaxed) : 0;
                int gizAxis = sceneParams_
                    ? sceneParams_->gizmoActiveAxis.load(std::memory_order_relaxed) : -1;
                if (gizAxis < 0 && sceneParams_)
                    gizAxis = sceneParams_->gizmoHoverAxis.load(std::memory_order_relaxed);

                Float3 camPos{0.0f, 0.0f, 0.0f};
                if (sceneParams_) {
                    camPos = {sceneParams_->camPosX, sceneParams_->camPosY, sceneParams_->camPosZ};
                }

                bool usePivot = false;
                Float3 pivotPos{0.0f, 0.0f, 0.0f};
                if (sceneParams_) {
                    usePivot = sceneParams_->useGizmoPivot.load(std::memory_order_relaxed);
                    if (usePivot) {
                        std::lock_guard<std::mutex> lk(sceneParams_->gizmoPivotMutex);
                        pivotPos = {sceneParams_->gizmoPivotX, sceneParams_->gizmoPivotY, sceneParams_->gizmoPivotZ};
                    }
                }

                editor_.draw(cmd, viewProj_, camPos, selNode, gizMode, gizAxis, usePivot, pivotPos);
            }
        });

    // --- Pass 2: Readback --------------------------------------------------
    // Copies the rendered backbuffer to a CPU-readable staging buffer.
    if (readback) {
        renderGraph_.add_pass("readback", RgPassType::Copy,
            [&](RgPassBuilder& builder) {
                builder.read(rgBackbuffer);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                RhiTexture bb = rhi()->swapchain_backbuffer();

                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);

                const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
                RhiTextureFootprint footprint{};
                footprint.width     = width_;
                footprint.height    = height_;
                footprint.depth     = 1;
                footprint.row_pitch = alignedPitch;
                footprint.offset    = 0;
                footprint.format    = RHI_FORMAT_RGBA8_UNORM;

                rhi()->cmd_copy_texture_to_buffer(cmd, bb,
                                                  rhiReadbackBufs_[frameIndex_],
                                                  &footprint);

                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_COPY_SRC, RHI_STATE_PRESENT);
            });
    } else {
        // No readback — still need the present barrier.
        renderGraph_.add_pass("present_barrier", RgPassType::Copy,
            [&](RgPassBuilder& builder) {
                builder.read(rgBackbuffer);
                builder.force_run();
            },
            [](RhiCmdList cmd) {
                RhiTexture bb = rhi()->swapchain_backbuffer();
                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT);
            });
    }

    // -----------------------------------------------------------------------
    // Compile and execute the render graph
    // -----------------------------------------------------------------------
    if (!renderGraph_.compile()) {
        // Graph has a cycle or is invalid — skip execution, still present.
        rhi()->cmd_end(cmd);
        rhi()->cmd_submit(cmd);
        rhi()->swapchain_present(0);
        return;
    }
    renderGraph_.execute(cmd);

    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);
    rhi()->swapchain_present(0);

    if (readback) {
        performReadback();
    }
}

FrameData Renderer::getLatestFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestFrame_;
}

FrameData Renderer::getLastFrameCopy() const {
    return getLatestFrame();
}

void Renderer::setSceneParams(SceneParams* params) {
    sceneParams_ = params;
}

void Renderer::shutdown() {
    if (!initialized_) return;

    editor_.shutdown();

    if (rhiReadbackFence_ != RHI_NULL) {
        rhi()->fence_destroy(rhiReadbackFence_);
        rhiReadbackFence_ = RHI_NULL;
    }
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (rhiReadbackBufs_[i] != RHI_NULL) {
            rhi()->buffer_destroy(rhiReadbackBufs_[i]);
            rhiReadbackBufs_[i] = RHI_NULL;
        }
    }
    if (rhiDepthBuf_ != RHI_NULL) {
        rhi()->texture_destroy(rhiDepthBuf_);
        rhiDepthBuf_ = RHI_NULL;
    }

    if (rhiPipeline_ != RHI_NULL) {
        rhi()->pipeline_destroy(rhiPipeline_);
        rhiPipeline_ = RHI_NULL;
    }

    rhi()->shutdown();
    initialized_ = false;
}

// ===========================================================================
// Private -- pipeline state via RHI
// ===========================================================================

bool Renderer::createPipelineState() {
    const std::string shaderDir = getShaderDir();
    DIAG_FMT("Shader dir: %s", shaderDir.c_str());

    // Read HLSL source files as raw text.
    // The RHI's pipeline_create compiles them internally (Phase 1 convention:
    // spirv_data holds HLSL source, not SPIR-V bytecode).
    const std::string vsPath = shaderDir + "basic.vs.hlsl";
    const std::string psPath = shaderDir + "basic.ps.hlsl";

    auto readFile = [](const std::string& path) -> std::string {
        FILE* f = uz::file_open(path.c_str(), "rb");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string data(sz, '\0');
        fread(data.data(), 1, sz, f);
        fclose(f);
        return data;
    };

    std::string vsSource = readFile(vsPath);
    std::string psSource = readFile(psPath);

    DIAG_FMT("VS source: %zu bytes from %s", vsSource.size(), vsPath.c_str());
    DIAG_FMT("PS source: %zu bytes from %s", psSource.size(), psPath.c_str());

    if (vsSource.empty()) {
        DIAG_FMT("FATAL: Cannot read vertex shader: %s", vsPath.c_str());
        return false;
    }
    if (psSource.empty()) {
        DIAG_FMT("FATAL: Cannot read pixel shader: %s", psPath.c_str());
        return false;
    }

    RhiPipelineDesc desc{};
    desc.vertex_shader.spirv_data  = vsSource.data();
    desc.vertex_shader.spirv_size  = vsSource.size();
    desc.vertex_shader.entry_point = "VSMain";
    desc.pixel_shader.spirv_data   = psSource.data();
    desc.pixel_shader.spirv_size   = psSource.size();
    desc.pixel_shader.entry_point  = "PSMain";

    // Vertex layout: POSITION (float3) + COLOR (float3).
    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0] = {
        "POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3, 0, 0
    };
    desc.vertex_layout.attribs[1] = {
        "COLOR", 0, RHI_VERTEX_FORMAT_FLOAT3,
        3 * sizeof(float), 0   // 12 bytes offset after POSITION
    };

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D32_FLOAT;
    desc.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;

    desc.rasterizer.fill_mode = RHI_FILL_SOLID;
    desc.rasterizer.cull_mode = RHI_CULL_BACK;

    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = true;
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS;

    desc.blend.render_targets[0].write_mask = 0xF;
    desc.debug_name = "MainTrianglePSO";

    DIAG("pipeline_create()...");
    rhiPipeline_ = rhi()->pipeline_create(&desc);
    DIAG_FMT("pipeline_create => handle=%llu", (unsigned long long)rhiPipeline_);
    return rhiPipeline_ != RHI_NULL;
}


// ===========================================================================
// Private -- depth buffer via RHI
// ===========================================================================

bool Renderer::createDepthBuffer() {
    RhiTextureDesc desc{};
    desc.width      = width_;
    desc.height     = height_;
    desc.depth      = 1;
    desc.mip_levels = 1;
    desc.array_size = 1;
    desc.format     = RHI_FORMAT_D32_FLOAT;
    desc.dimension  = RHI_TEXTURE_DIM_2D;
    desc.usage      = RHI_TEXTURE_USAGE_DEPTH_STENCIL;
    desc.debug_name = "DepthBuffer";

    rhiDepthBuf_ = rhi()->texture_create(&desc);
    return rhiDepthBuf_ != RHI_NULL;
}

// ===========================================================================
// Private -- readback buffers via RHI
// ===========================================================================

bool Renderer::createReadbackBuffers() {
    const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
    const uint64_t bufSize = static_cast<uint64_t>(alignedPitch) * height_;

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        RhiBufferDesc desc{};
        desc.size       = bufSize;
        desc.usage      = RHI_BUFFER_USAGE_READBACK;
        desc.debug_name = "ReadbackBuf";

        rhiReadbackBufs_[i] = rhi()->buffer_create(&desc);
        if (rhiReadbackBufs_[i] == RHI_NULL) return false;
    }

    rhiReadbackFence_ = rhi()->fence_create();
    if (rhiReadbackFence_ == RHI_NULL) return false;

    readbackFenceValue_ = 0;
    return true;
}

// ===========================================================================
// Private -- GPU -> CPU readback
// ===========================================================================

void Renderer::performReadback() {
    ++readbackFenceValue_;
    rhi()->fence_signal(rhiReadbackFence_, readbackFenceValue_);
    rhi()->fence_wait(rhiReadbackFence_, readbackFenceValue_, UINT64_MAX);

    const uint32_t actualRowBytes = width_ * 4;
    const uint32_t alignedPitch   = (actualRowBytes + 255u) & ~255u;

    void* mappedData = rhi()->buffer_map(rhiReadbackBufs_[frameIndex_]);
    if (!mappedData) return;

    const uint64_t timestamp = streamTimer_.elapsedMicros();

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_.width       = width_;
        latestFrame_.height      = height_;
        latestFrame_.frameNumber = ++frameNumber_;
        latestFrame_.timestamp   = timestamp;

        latestFrame_.pixels.resize(
            static_cast<size_t>(actualRowBytes) * height_);

        const auto* src = static_cast<const uint8_t*>(mappedData);
        uint8_t*    dst = latestFrame_.pixels.data();

        for (uint32_t row = 0; row < height_; ++row) {
            std::memcpy(dst, src, actualRowBytes);
            src += alignedPitch;
            dst += actualRowBytes;
        }

        fpsOverlay_.renderInto(latestFrame_.pixels.data(),
                               width_, height_, actualRowBytes);
    }

    rhi()->buffer_unmap(rhiReadbackBufs_[frameIndex_]);
}

// ===========================================================================
// Public -- editor scene loading
// ===========================================================================

size_t Renderer::loadEditorScene(std::vector<EditorNode> nodes) {
    const size_t count = editor_.loadNodes(std::move(nodes));
    return count;
}

void Renderer::setScene(Scene* scene) {
    scene_ = scene;
}

} // namespace uz
