// ---------------------------------------------------------------------------
// renderer.cpp -- DX12Renderer implementation using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  Direct DX12 usage is limited to:
//   1. Shader compilation via D3DCompileFromFile — will move into RHI in P3
//
// RULE RHI-1: No backend types stored above rhi.h
// RULE RHI-2: No DirectXMath above rhi.h — uses uz::Mat4
// RULE CS-7:  No exceptions — returns bool for fallible operations
// ---------------------------------------------------------------------------

#include "renderer.h"

extern "C" {
#include "engine/renderer/rhi/rhi_dx12.h"
}
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/core/diag.h"



// D3DCompileFromFile for shader compilation (temporary — will move to RHI)
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// Platform timer (Windows-specific for now).
#include <windows.h>

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

// Left-handed look-at matrix matching DX12 convention.
// We build this in row-major layout (Mat4.m[row][col] when uploaded as
// 16 floats via root constants).  This matches what the legacy shaders expect.
uz::Mat4 mat4_look_at_lh(uz::Vec3 eye, uz::Vec3 target, uz::Vec3 up) {
    using namespace uz;
    const Vec3 f = vec3_normalize(target - eye);   // Forward (+Z)
    const Vec3 r = vec3_normalize(vec3_cross(up, f)); // Right
    const Vec3 u = vec3_cross(f, r);               // True up

    Mat4 result = {};
    result.m[0][0] = r.x;  result.m[0][1] = u.x;  result.m[0][2] = f.x;  result.m[0][3] = 0.0f;
    result.m[1][0] = r.y;  result.m[1][1] = u.y;  result.m[1][2] = f.y;  result.m[1][3] = 0.0f;
    result.m[2][0] = r.z;  result.m[2][1] = u.z;  result.m[2][2] = f.z;  result.m[2][3] = 0.0f;
    result.m[3][0] = -vec3_dot(r, eye);
    result.m[3][1] = -vec3_dot(u, eye);
    result.m[3][2] = -vec3_dot(f, eye);
    result.m[3][3] = 1.0f;
    return result;
}

// Left-handed perspective projection, depth [0..1], row-major.
uz::Mat4 mat4_perspective_fov_lh(float fov_y, float aspect, float near_z, float far_z) {
    const float h = 1.0f / std::tan(fov_y * 0.5f);
    const float w = h / aspect;
    const float q = far_z / (far_z - near_z);

    uz::Mat4 result = {};
    result.m[0][0] = w;
    result.m[1][1] = h;
    result.m[2][2] = q;
    result.m[2][3] = 1.0f;
    result.m[3][2] = -near_z * q;
    return result;
}

// Rotation around Y axis (row-major).
uz::Mat4 mat4_rotation_y(float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    uz::Mat4 r = uz::Mat4::identity();
    r.m[0][0] =  c;  r.m[0][2] = s;
    r.m[2][0] = -s;  r.m[2][2] = c;
    return r;
}

// Rotation around X axis (row-major).
uz::Mat4 mat4_rotation_x(float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    uz::Mat4 r = uz::Mat4::identity();
    r.m[1][1] =  c;  r.m[1][2] = s;
    r.m[2][1] = -s;  r.m[2][2] = c;
    return r;
}

} // anonymous namespace

namespace uz {

// ===========================================================================
// Public API
// ===========================================================================

bool DX12Renderer::init(void* window_handle, uint32_t width, uint32_t height) {
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

    DIAG("createVertexBuffer()...");
    if (!createVertexBuffer()) {
        DIAG("FATAL: createVertexBuffer() failed");
        return false;
    }

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

    // Engine-math identity matrices.
    worldViewProj_ = Mat4::identity();
    viewProj_      = Mat4::identity();

    // Record the stream start time.
    {
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        streamStartMicros_ = static_cast<uint64_t>(
            now.QuadPart * 1'000'000 / freq.QuadPart);
    }

    DIAG("=== renderer.init COMPLETE ===");
    initialized_ = true;
    return true;
}

bool DX12Renderer::resize(int width, int height) {
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

void DX12Renderer::update(double /*totalTime*/, double deltaTime) {
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

    // Rotation speeds from IPC-controlled scene params.
    float rotYSpeed = 1.2f;
    float rotXTilt  = 0.3f;
    if (sceneParams_) {
        rotYSpeed = sceneParams_->rotY.load(std::memory_order_relaxed);
        rotXTilt  = sceneParams_->rotX.load(std::memory_order_relaxed);

        std::lock_guard<std::mutex> lk(sceneParams_->bgMutex);
        clearR_ = sceneParams_->bgR;
        clearG_ = sceneParams_->bgG;
        clearB_ = sceneParams_->bgB;
    }

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);

    if (editorMode_) {
        // Editor fly-cam uses engine math types (camera_component.h).
        const float dt = std::min(static_cast<float>(deltaTime), 0.1f);
        CameraState camState = camera_update_fly(sceneParams_, aspect, dt);
        viewProj_      = camState.view_proj;
        worldViewProj_ = camState.view_proj;
    } else {
        // Legacy pyramid spinning camera — uses engine math (uz::Mat4).
        const float dt = static_cast<float>(deltaTime);
        rotAngleY_ += dt * rotYSpeed;
        rotAngleX_ += dt * rotXTilt;

        const Mat4 world = mat4_rotation_y(rotAngleY_)
                         * mat4_rotation_x(0.3f + rotAngleX_);

        const Vec3 eye    = {0.0f, 0.0f, -3.0f, 0.0f};
        const Vec3 target = {0.0f, 0.0f,  0.0f, 0.0f};
        const Vec3 up     = {0.0f, 1.0f,  0.0f, 0.0f};

        const Mat4 view = mat4_look_at_lh(eye, target, up);
        const Mat4 proj = mat4_perspective_fov_lh(kHalfPi * 0.5f, aspect, 0.1f, 100.0f);
        const Mat4 vp   = view * proj;

        viewProj_      = vp;
        worldViewProj_ = world * vp;
    }

    // Collect primary directional light from the scene.
    cachedLight_ = light_collect_primary(scene_);
}

void DX12Renderer::render(bool readback) {
    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) return;

    rhi()->cmd_set_pipeline(cmd, rhiPipeline_);

    rhi()->cmd_set_viewport(cmd, 0.0f, 0.0f,
                           static_cast<float>(width_),
                           static_cast<float>(height_),
                           0.0f, 1.0f);
    rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

    RhiTexture backbuffer = rhi()->swapchain_backbuffer();

    rhi()->cmd_barrier_texture(cmd, backbuffer,
                               RHI_STATE_PRESENT, RHI_STATE_RENDER_TARGET);

    rhi()->cmd_clear_render_target(cmd, backbuffer,
                                   clearR_, clearG_, clearB_, 1.0f);
    rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);

    rhi()->cmd_set_render_targets(cmd, &backbuffer, 1, rhiDepthBuf_);

    // --- Draw scene ---
    const bool paused = sceneParams_ &&
        sceneParams_->paused.load(std::memory_order_relaxed);

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

    if (editorMode_) {
        // EditorComponent — fully RHI, uses RhiCmdList directly.
        uint32_t selNode = sceneParams_
            ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;
        int gizMode = sceneParams_
            ? sceneParams_->gizmoMode.load(std::memory_order_relaxed) : 0;
        int gizAxis = sceneParams_
            ? sceneParams_->gizmoActiveAxis.load(std::memory_order_relaxed) : -1;
        if (gizAxis < 0 && sceneParams_)
            gizAxis = sceneParams_->gizmoHoverAxis.load(std::memory_order_relaxed);

        // Camera position for gizmo distance scaling
        Float3 camPos{0.0f, 0.0f, 0.0f};
        if (sceneParams_) {
            camPos = {sceneParams_->camPosX, sceneParams_->camPosY, sceneParams_->camPosZ};
        }

        // Multi-select pivot (task 7.2)
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
    } else if (!paused) {
        // Legacy pyramid draw.
        rhi()->cmd_set_root_constants(cmd, 0, &worldViewProj_, 16, 0);

        const float white[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        rhi()->cmd_set_root_constants(cmd, 1, white, 4, 0);

        rhi()->cmd_set_vertex_buffer(cmd, 0, rhiVertexBuf_, 0,
                                     static_cast<uint32_t>(sizeof(Vertex)));
        rhi()->cmd_draw(cmd, 12, 1, 0, 0);
    }

    // --- Readback path ---
    if (readback) {
        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);

        const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
        RhiTextureFootprint footprint{};
        footprint.width     = width_;
        footprint.height    = height_;
        footprint.depth     = 1;
        footprint.row_pitch = alignedPitch;
        footprint.offset    = 0;
        footprint.format    = RHI_FORMAT_RGBA8_UNORM;

        rhi()->cmd_copy_texture_to_buffer(cmd, backbuffer,
                                          rhiReadbackBufs_[frameIndex_],
                                          &footprint);

        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_COPY_SRC, RHI_STATE_PRESENT);
    } else {
        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT);
    }

    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);
    rhi()->swapchain_present(0);

    if (readback) {
        performReadback();
    }
}

FrameData DX12Renderer::getLatestFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestFrame_;
}

FrameData DX12Renderer::getLastFrameCopy() const {
    return getLatestFrame();
}

void DX12Renderer::setSceneParams(SceneParams* params) {
    sceneParams_ = params;
}

void DX12Renderer::shutdown() {
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
    if (rhiVertexBuf_ != RHI_NULL) {
        rhi()->buffer_destroy(rhiVertexBuf_);
        rhiVertexBuf_ = RHI_NULL;
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

bool DX12Renderer::createPipelineState() {
    const std::string shaderDir = getShaderDir();
    DIAG_FMT("Shader dir: %s", shaderDir.c_str());

    // Read HLSL source files as raw text.
    // The RHI's pipeline_create compiles them internally (Phase 1 convention:
    // spirv_data holds HLSL source, not SPIR-V bytecode).
    const std::string vsPath = shaderDir + "basic.vs.hlsl";
    const std::string psPath = shaderDir + "basic.ps.hlsl";

    auto readFile = [](const std::string& path) -> std::string {
        FILE* f = fopen(path.c_str(), "rb");
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
// Private -- vertex buffer via RHI
// ===========================================================================

bool DX12Renderer::createVertexBuffer() {
    // Pyramid vertices — plain float arrays per CS-3.
    const Vertex pyramidVertices[] = {
        // Front face: Apex, Base2, Base0
        {{0.0f, 0.7f, 0.0f},   {1.0f, 1.0f, 0.0f}},   // apex, yellow
        {{0.52f, -0.3f, -0.3f}, {0.0f, 0.0f, 1.0f}},   // base2, blue
        {{0.0f, -0.3f, 0.6f},  {1.0f, 0.0f, 0.0f}},    // base0, red
        // Right face: Apex, Base0, Base1
        {{0.0f, 0.7f, 0.0f},    {1.0f, 1.0f, 0.0f}},
        {{0.0f, -0.3f, 0.6f},   {1.0f, 0.0f, 0.0f}},
        {{-0.52f, -0.3f, -0.3f},{0.0f, 1.0f, 0.0f}},   // base1, green
        // Left face: Apex, Base1, Base2
        {{0.0f, 0.7f, 0.0f},    {1.0f, 1.0f, 0.0f}},
        {{-0.52f, -0.3f, -0.3f},{0.0f, 1.0f, 0.0f}},
        {{0.52f, -0.3f, -0.3f}, {0.0f, 0.0f, 1.0f}},
        // Bottom face: Base0, Base2, Base1
        {{0.0f, -0.3f, 0.6f},   {1.0f, 0.0f, 0.0f}},
        {{0.52f, -0.3f, -0.3f}, {0.0f, 0.0f, 1.0f}},
        {{-0.52f, -0.3f, -0.3f},{0.0f, 1.0f, 0.0f}},
    };

    RhiBufferDesc bufDesc{};
    bufDesc.size       = sizeof(pyramidVertices);
    bufDesc.usage      = RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_UPLOAD;
    bufDesc.debug_name = "PyramidVB";

    rhiVertexBuf_ = rhi()->buffer_create(&bufDesc);
    if (rhiVertexBuf_ == RHI_NULL) return false;

    rhi()->buffer_upload(rhiVertexBuf_, pyramidVertices, sizeof(pyramidVertices));
    return true;
}

// ===========================================================================
// Private -- depth buffer via RHI
// ===========================================================================

bool DX12Renderer::createDepthBuffer() {
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

bool DX12Renderer::createReadbackBuffers() {
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

void DX12Renderer::performReadback() {
    ++readbackFenceValue_;
    rhi()->fence_signal(rhiReadbackFence_, readbackFenceValue_);
    rhi()->fence_wait(rhiReadbackFence_, readbackFenceValue_, UINT64_MAX);

    const uint32_t actualRowBytes = width_ * 4;
    const uint32_t alignedPitch   = (actualRowBytes + 255u) & ~255u;

    void* mappedData = rhi()->buffer_map(rhiReadbackBufs_[frameIndex_]);
    if (!mappedData) return;

    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const uint64_t currentMicros = static_cast<uint64_t>(
        now.QuadPart * 1'000'000 / freq.QuadPart);

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_.width       = width_;
        latestFrame_.height      = height_;
        latestFrame_.frameNumber = ++frameNumber_;
        latestFrame_.timestamp   = currentMicros - streamStartMicros_;

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

size_t DX12Renderer::loadEditorScene(std::vector<EditorNode> nodes) {
    const size_t count = editor_.loadNodes(std::move(nodes));
    editorMode_ = true;
    return count;
}

void DX12Renderer::setScene(Scene* scene) {
    scene_ = scene;
}

} // namespace uz
