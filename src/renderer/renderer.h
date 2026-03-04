#pragma once

// renderer.h -- Renderer using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  No direct DX12 / Vulkan / Metal
// includes leak into this header.  EditorComponent still uses raw DX12 as a
// temporary measure and will be ported in a follow-up phase.

#include <cstdint>
#include <mutex>
#include <vector>

#include "fps_overlay.h"
#include "components/editor_component.h"
#include "components/camera_component.h"
#include "components/light_component.h"

// DX12 types still needed temporarily for EditorComponent bridge and math
#include <d3d12.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "../common/types.h"
#include "../common/timer.h"
#include "scene_params.h"

// RHI abstraction
extern "C" {
#include "engine/renderer/rhi/rhi.h"
}

namespace uz {

// ---------------------------------------------------------------------------
// Shaders are in standalone .hlsl files per Rule 6 / CS-15:
//   shaders/builtin/basic.vs.hlsl   (vertex shader)
//   shaders/builtin/basic.ps.hlsl   (pixel shader)
// Compiled at runtime via D3DCompileFromFile() in createPipelineState().
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Vertex layout for the colored triangle.
// ---------------------------------------------------------------------------

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
};

// Frame count for double/triple buffering.

// Frame count from common/types.h is used for readback buffer dimensions.
// DX12Renderer -- Renderer using the RHI abstraction layer.
//
// Lifecycle:
//   1. Construct
//   2. init(hwnd, width, height)
//   3. Loop: update(totalTime) -> render()
//   4. shutdown()
//
// All GPU resources are managed through RHI handles.  shutdown() performs
// an explicit GPU drain so that destruction order is safe.
// ---------------------------------------------------------------------------

class DX12Renderer {
public:
    DX12Renderer()  = default;
    ~DX12Renderer() = default;

    // Non-copyable, non-movable -- GPU resource handles are not trivially relocatable.
    DX12Renderer(const DX12Renderer&)            = delete;
    DX12Renderer& operator=(const DX12Renderer&) = delete;
    DX12Renderer(DX12Renderer&&)                 = delete;
    DX12Renderer& operator=(DX12Renderer&&)      = delete;

    // EditorNode is now defined in EditorComponent.
    using EditorNode = EditorComponent::EditorNode;

    // Initialize the renderer and all GPU resources for the given window.
    void init(HWND hwnd, uint32_t width, uint32_t height);

    // Update the rotation angle using delta-time accumulation.
    void update(double totalTime, double deltaTime);

    /// Record and execute the command list, present the frame.
    /// @param readback  When true, copy the rendered frame to a CPU-readable
    ///                   buffer (for JPEG streaming).
    void render(bool readback = true);

    /// Resize the swap chain, render targets, depth buffer, and readback
    /// buffers to the given dimensions.
    bool resize(int width, int height);

    /// Current render target width in pixels.
    [[nodiscard]] int getWidth() const noexcept { return static_cast<int>(width_); }

    /// Current render target height in pixels.
    [[nodiscard]] int getHeight() const noexcept { return static_cast<int>(height_); }

    // Drain the GPU and release all resources.  Safe to call multiple times.
    void shutdown();

    // Returns a copy of the most recently captured frame. Thread-safe via mutex.
    [[nodiscard]] FrameData getLatestFrame() const;

    // Alias for getLatestFrame().
    [[nodiscard]] FrameData getLastFrameCopy() const;

    // Set the external scene parameters struct.
    void setSceneParams(SceneParams* params);

    // Set the scene pointer for scene-driven camera and lighting.
    void setScene(Scene* scene);

    // Replace the editor scene nodes and switch to editor rendering mode.
    size_t loadEditorScene(std::vector<EditorNode> nodes);

    // Returns a copy of the current editor nodes. Thread-safe.
    std::vector<EditorNode> getEditorNodes() const { return editor_.getNodes(); }

    // Replace editor nodes (thread-safe delegate). For gizmo drag updates.
    void loadEditorNodes(std::vector<EditorNode> nodes) { editor_.loadNodes(std::move(nodes)); }

private:
    // Initialization sub-steps (called by init).
    void createPipelineState();
    void createVertexBuffer();
    void createDepthBuffer();
    void createReadbackBuffers();

    // Perform GPU -> CPU readback of the current frame's render target.
    void performReadback();

    // -----------------------------------------------------------------------
    // RHI resource handles
    // -----------------------------------------------------------------------
    RhiPipeline rhiPipeline_  = RHI_NULL;    // Main triangle PSO
    RhiBuffer   rhiVertexBuf_ = RHI_NULL;    // Vertex buffer (upload heap)
    RhiTexture  rhiDepthBuf_  = RHI_NULL;    // Depth-stencil texture
    RhiFence    rhiReadbackFence_ = RHI_NULL; // Readback synchronization
    RhiBuffer   rhiReadbackBufs_[kFrameCount] = {};  // Per-frame readback

    uint64_t    readbackFenceValue_ = 0;

    // -----------------------------------------------------------------------
    // Legacy DX12 members (kept temporarily for EditorComponent bridge)
    // -----------------------------------------------------------------------
    // EditorComponent still needs raw DX12 device + root signature.
    // These are retrieved from the RHI internal state during init().
    ID3D12Device*        legacyDevice_    = nullptr;  // borrowed, NOT owned
    ID3D12RootSignature* legacyRootSig_   = nullptr;  // borrowed from pipeline slot

    // Latest captured frame (protected by mutex for cross-thread access).
    mutable std::mutex frameMutex_;
    FrameData          latestFrame_;
    uint32_t           frameNumber_       = 0;
    uint64_t           streamStartMicros_ = 0;

    // Frame tracking.
    uint32_t frameIndex_ = 0;

    // Render dimensions.
    uint32_t width_  = 0;
    uint32_t height_ = 0;

    // Current world-view-projection matrix, updated each frame by update().
    DirectX::XMFLOAT4X4 worldViewProj_{};

    // Stored view-projection matrix (without world transform).
    DirectX::XMFLOAT4X4 viewProj_{};

    // Accumulated rotation angles (radians).
    float rotAngleY_ = 0.0f;
    float rotAngleX_ = 0.0f;

    bool initialized_ = false;

    // External scene parameters (set via setSceneParams, nullable).
    SceneParams* sceneParams_ = nullptr;

    // External scene pointer (set via setScene, nullable).
    Scene* scene_ = nullptr;

    // Cached light state.
    LightState cachedLight_;

    // CPU-side FPS overlay.
    FPSOverlay fpsOverlay_;

    // Cached clear color -- updated from sceneParams_ each frame in update().
    float clearR_ = 0.12f;
    float clearG_ = 0.12f;
    float clearB_ = 0.12f;

    // -----------------------------------------------------------------------
    // Render components (modular subsystems)
    // -----------------------------------------------------------------------

    EditorComponent editor_;

    // When true, render() draws the editor scene.
    bool editorMode_ = true;
};

} // namespace uz
