// ---------------------------------------------------------------------------
// renderer.cpp -- DX12Renderer implementation using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  Direct DX12 usage is limited to
// the EditorComponent bridge (temporary) and shader compilation via
// D3DCompileFromFile (will move into the RHI in Phase 2).
// ---------------------------------------------------------------------------

#include "renderer.h"

#include <stdexcept>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

// DX12 bridge (temporary) for EditorComponent
extern "C" {
#include "engine/renderer/rhi/rhi_dx12.h"
}

// D3DCompileFromFile for shader compilation (kept until RHI handles this)
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

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

void DX12Renderer::init(HWND hwnd, uint32_t width, uint32_t height) {
    if (initialized_) return;

    width_  = width;
    height_ = height;

    // Initialize the RHI backend (device, swapchain, heaps, cmd queue).
    if (!rhi()->init((void*)hwnd, width, height)) {
        throw std::runtime_error("RHI init failed");
    }

    // Create renderer-specific resources via RHI.
    createPipelineState();
    createVertexBuffer();
    createDepthBuffer();
    createReadbackBuffers();

    // Retrieve legacy DX12 pointers for EditorComponent bridge.
    legacyDevice_  = rhi_dx12_get_device();
    legacyRootSig_ = rhi_dx12_get_pipeline_root_sig(rhiPipeline_);

    // Editor-mode rendering component (grid + wireframe cubes).
    editor_.init(legacyDevice_, legacyRootSig_);

    // Identity matrix as a safe default before the first update().
    XMStoreFloat4x4(&worldViewProj_, XMMatrixIdentity());
    XMStoreFloat4x4(&viewProj_, XMMatrixIdentity());

    // Record the stream start time.
    {
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        streamStartMicros_ = static_cast<uint64_t>(
            now.QuadPart * 1'000'000 / freq.QuadPart);
    }

    initialized_ = true;
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

    // Let the RHI handle the swap chain resize.
    rhi()->resize(newW, newH);

    width_  = newW;
    height_ = newH;

    // Recreate depth buffer and readback at new dimensions.
    createDepthBuffer();
    createReadbackBuffers();

    return true;
}

void DX12Renderer::update(double /*totalTime*/, double deltaTime) {
    // Update the FPS counter (rolling average over 60 samples).
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
        const float dt = std::min(static_cast<float>(deltaTime), 0.1f);
        CameraState camState = camera_update_fly(sceneParams_, aspect, dt);

        static_assert(sizeof(Mat4) == sizeof(XMFLOAT4X4), "Mat4/XMFLOAT4X4 size mismatch");
        XMFLOAT4X4 vp4x4;
        std::memcpy(&vp4x4, &camState.view_proj, sizeof(Mat4));
        viewProj_      = vp4x4;
        worldViewProj_ = vp4x4;
    } else {
        const float dt = static_cast<float>(deltaTime);
        rotAngleY_ += dt * rotYSpeed;
        rotAngleX_ += dt * rotXTilt;

        const XMMATRIX world = XMMatrixRotationY(rotAngleY_)
                             * XMMatrixRotationX(0.3f + rotAngleX_);
        const XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -3.0f, 1.0f);
        const XMVECTOR at  = XMVectorSet(0.0f, 0.0f,  0.0f, 1.0f);
        const XMVECTOR up  = XMVectorSet(0.0f, 1.0f,  0.0f, 0.0f);
        const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.0f);
        const XMMATRIX vp = view * proj;
        XMStoreFloat4x4(&viewProj_, vp);
        XMStoreFloat4x4(&worldViewProj_, world * vp);
    }

    // Collect primary directional light from the scene.
    cachedLight_ = light_collect_primary(scene_);
}

void DX12Renderer::render(bool readback) {
    // --- Begin command recording ---
    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) return;

    // Set pipeline (binds PSO + root signature).
    rhi()->cmd_set_pipeline(cmd, rhiPipeline_);

    // Viewport and scissor.
    rhi()->cmd_set_viewport(cmd, 0.0f, 0.0f,
                           static_cast<float>(width_),
                           static_cast<float>(height_),
                           0.0f, 1.0f);
    rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

    // Get current backbuffer texture.
    RhiTexture backbuffer = rhi()->swapchain_backbuffer();

    // Transition: PRESENT -> RENDER_TARGET.
    rhi()->cmd_barrier_texture(cmd, backbuffer,
                               RHI_STATE_PRESENT, RHI_STATE_RENDER_TARGET);

    // Clear render target and depth.
    rhi()->cmd_clear_render_target(cmd, backbuffer,
                                   clearR_, clearG_, clearB_, 1.0f);
    rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);

    // Bind render target + depth.
    rhi()->cmd_set_render_targets(cmd, &backbuffer, 1, rhiDepthBuf_);

    // --- Draw scene ---
    const bool paused = sceneParams_ &&
        sceneParams_->paused.load(std::memory_order_relaxed);

    // Light data (shared between editor and legacy modes).
    struct LightCBData {
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
        // EditorComponent still uses raw DX12 command list (temporary bridge).
        ID3D12GraphicsCommandList* rawCmdList = rhi_dx12_get_cmd_list(cmd);
        uint32_t selNode = sceneParams_
            ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;
        int gizMode = sceneParams_
            ? sceneParams_->gizmoMode.load(std::memory_order_relaxed) : 0;
        int gizAxis = sceneParams_
            ? sceneParams_->gizmoActiveAxis.load(std::memory_order_relaxed) : -1;
        if (gizAxis < 0 && sceneParams_)
            gizAxis = sceneParams_->gizmoHoverAxis.load(std::memory_order_relaxed);

        editor_.draw(rawCmdList, viewProj_, selNode, gizMode, gizAxis);
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
        // RENDER_TARGET -> COPY_SOURCE.
        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);

        // Copy texture to readback buffer.
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

        // COPY_SOURCE -> PRESENT.
        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_COPY_SRC, RHI_STATE_PRESENT);
    } else {
        // RENDER_TARGET -> PRESENT.
        rhi()->cmd_barrier_texture(cmd, backbuffer,
                                   RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT);
    }

    // --- Close & submit ---
    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);

    // --- Present ---
    rhi()->swapchain_present(0);

    // --- Readback: wait and copy pixels ---
    if (readback) {
        performReadback();
    }

    // Frame index tracks which backbuffer we'll render to next.
    // The RHI's swapchain_present() handles fence signaling and waiting.
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

    // Editor resources.
    editor_.shutdown();

    // Destroy renderer-owned RHI resources.
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

    legacyDevice_  = nullptr;
    legacyRootSig_ = nullptr;

    // Shut down the RHI (releases device, swapchain, heaps, etc).
    rhi()->shutdown();

    initialized_ = false;
}

// ===========================================================================
// Private -- pipeline state via RHI
// ===========================================================================

void DX12Renderer::createPipelineState() {
    // Compile shaders from standalone .hlsl files.
    const std::string shaderDir = getShaderDir();

    UINT compileFlags = 0;
#ifdef UZENGINE_DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    const std::wstring vsPath = toWide(shaderDir + "basic.vs.hlsl");
    const std::wstring psPath = toWide(shaderDir + "basic.ps.hlsl");

    ComPtr<ID3DBlob> vertexShader, pixelShader, errors;

    HRESULT hr = D3DCompileFromFile(vsPath.c_str(), nullptr, nullptr,
        "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errors);
    if (FAILED(hr)) {
        std::string msg = "VS compile failed: " + shaderDir + "basic.vs.hlsl";
        if (errors) msg += std::string(": ") +
            static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }

    errors.Reset();
    hr = D3DCompileFromFile(psPath.c_str(), nullptr, nullptr,
        "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errors);
    if (FAILED(hr)) {
        std::string msg = "PS compile failed: " + shaderDir + "basic.ps.hlsl";
        if (errors) msg += std::string(": ") +
            static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }

    // Build pipeline through RHI.
    RhiPipelineDesc desc{};
    desc.vertex_shader.spirv_data  = vertexShader->GetBufferPointer();
    desc.vertex_shader.spirv_size  = vertexShader->GetBufferSize();
    desc.vertex_shader.entry_point = "VSMain";
    desc.pixel_shader.spirv_data   = pixelShader->GetBufferPointer();
    desc.pixel_shader.spirv_size   = pixelShader->GetBufferSize();
    desc.pixel_shader.entry_point  = "PSMain";

    // Vertex layout: POSITION (float3) + COLOR (float3).
    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0] = {
        "POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3, 0, 0
    };
    desc.vertex_layout.attribs[1] = {
        "COLOR", 0, RHI_VERTEX_FORMAT_FLOAT3,
        static_cast<uint32_t>(sizeof(XMFLOAT3)), 0
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

    rhiPipeline_ = rhi()->pipeline_create(&desc);
    if (rhiPipeline_ == RHI_NULL) {
        throw std::runtime_error("RHI pipeline_create failed");
    }
}

// ===========================================================================
// Private -- vertex buffer via RHI
// ===========================================================================

void DX12Renderer::createVertexBuffer() {
    constexpr XMFLOAT3 apex  = {  0.0f,   0.7f,  0.0f  };
    constexpr XMFLOAT3 base0 = {  0.0f,  -0.3f,  0.6f  };
    constexpr XMFLOAT3 base1 = { -0.52f, -0.3f, -0.3f  };
    constexpr XMFLOAT3 base2 = {  0.52f, -0.3f, -0.3f  };

    constexpr XMFLOAT3 cYellow = { 1.0f, 1.0f, 0.0f };
    constexpr XMFLOAT3 cRed    = { 1.0f, 0.0f, 0.0f };
    constexpr XMFLOAT3 cGreen  = { 0.0f, 1.0f, 0.0f };
    constexpr XMFLOAT3 cBlue   = { 0.0f, 0.0f, 1.0f };

    const Vertex pyramidVertices[] = {
        { apex,  cYellow }, { base2, cBlue }, { base0, cRed },
        { apex,  cYellow }, { base0, cRed  }, { base1, cGreen },
        { apex,  cYellow }, { base1, cGreen }, { base2, cBlue },
        { base0, cRed },   { base2, cBlue },  { base1, cGreen },
    };

    RhiBufferDesc bufDesc{};
    bufDesc.size       = sizeof(pyramidVertices);
    bufDesc.usage      = RHI_BUFFER_USAGE_VERTEX | RHI_BUFFER_USAGE_UPLOAD;
    bufDesc.debug_name = "PyramidVB";

    rhiVertexBuf_ = rhi()->buffer_create(&bufDesc);
    if (rhiVertexBuf_ == RHI_NULL)
        throw std::runtime_error("RHI buffer_create (VB) failed");

    rhi()->buffer_upload(rhiVertexBuf_, pyramidVertices, sizeof(pyramidVertices));
}

// ===========================================================================
// Private -- depth buffer via RHI
// ===========================================================================

void DX12Renderer::createDepthBuffer() {
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
    if (rhiDepthBuf_ == RHI_NULL)
        throw std::runtime_error("RHI texture_create (depth) failed");
}

// ===========================================================================
// Private -- readback buffers via RHI
// ===========================================================================

void DX12Renderer::createReadbackBuffers() {
    const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
    const uint64_t bufSize = static_cast<uint64_t>(alignedPitch) * height_;

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        RhiBufferDesc desc{};
        desc.size       = bufSize;
        desc.usage      = RHI_BUFFER_USAGE_READBACK;
        desc.debug_name = "ReadbackBuf";

        rhiReadbackBufs_[i] = rhi()->buffer_create(&desc);
        if (rhiReadbackBufs_[i] == RHI_NULL)
            throw std::runtime_error("RHI buffer_create (readback) failed");
    }

    // Readback fence for CPU/GPU sync.
    rhiReadbackFence_ = rhi()->fence_create();
    if (rhiReadbackFence_ == RHI_NULL)
        throw std::runtime_error("RHI fence_create (readback) failed");

    readbackFenceValue_ = 0;
}

// ===========================================================================
// Private -- GPU -> CPU readback
// ===========================================================================

void DX12Renderer::performReadback() {
    // Signal the readback fence.
    ++readbackFenceValue_;
    rhi()->fence_signal(rhiReadbackFence_, readbackFenceValue_);

    // Wait for the GPU to complete the copy.
    rhi()->fence_wait(rhiReadbackFence_, readbackFenceValue_,
                      UINT64_MAX);

    // Map the readback buffer and copy pixel data row-by-row.
    const uint32_t actualRowBytes = width_ * 4;
    const uint32_t alignedPitch   = (actualRowBytes + 255u) & ~255u;

    void* mappedData = rhi()->buffer_map(rhiReadbackBufs_[frameIndex_]);
    if (!mappedData) return;

    // Capture timestamp.
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
