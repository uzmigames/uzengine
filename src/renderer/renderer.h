#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "fps_overlay.h"
#include "components/editor_component.h"
#include "components/camera_component.h"
#include "components/light_component.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "../common/types.h"
#include "../common/timer.h"
#include "scene_params.h"

namespace uz {

// ---------------------------------------------------------------------------
// Embedded HLSL shaders -- compiled at runtime via D3DCompile.
// ---------------------------------------------------------------------------

constexpr const char* kVertexShaderSrc = R"hlsl(
cbuffer TransformCB : register(b0) {
    float4x4 worldViewProj;
};
cbuffer ColorCB : register(b1) {
    float3 colorMul;
    float _pad;
};
cbuffer LightCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
};

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = mul(worldViewProj, float4(input.position, 1.0f));
    output.color    = input.color * colorMul;
    // Use object-space position as a proxy normal for wireframe/cube shading.
    output.normal   = normalize(input.position);
    return output;
}
)hlsl";

constexpr const char* kPixelShaderSrc = R"hlsl(
cbuffer LightCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float3 normal   : NORMAL;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Ambient + directional N·L diffuse.
    float ambient = 0.35;
    float ndl     = max(dot(normalize(input.normal), -normalize(lightDir)), 0.0);
    float3 lit    = input.color * (ambient + (1.0 - ambient) * ndl * lightIntensity * lightColor);
    return float4(lit, 1.0f);
}
)hlsl";

// ---------------------------------------------------------------------------
// Vertex layout for the colored triangle.
// ---------------------------------------------------------------------------

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 color;
};

// ---------------------------------------------------------------------------
// DX12Renderer -- minimal DirectX 12 renderer for a spinning colored pyramid.
//
// Lifecycle:
//   1. Construct
//   2. init(hwnd, width, height)
//   3. Loop: update(totalTime) -> render()
//   4. shutdown()
//
// The renderer owns all GPU resources via ComPtr / RAII.  shutdown() performs
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

    // Update the rotation angle using delta-time accumulation (immune to
    // frame-timing jitter).  totalTime is still available for effects that
    // need wall-clock time; deltaTime drives the rotation step.
    void update(double totalTime, double deltaTime);

    /// Record and execute the command list, present the frame.
    /// @param readback  When true, copy the rendered frame to a CPU-readable
    ///                   buffer (for JPEG streaming).  When false, skip the
    ///                   GPU copy + fence wait — much cheaper per frame.
    void render(bool readback = true);

    /// Resize the swap chain, render targets, depth buffer, and readback
    /// buffers to the given dimensions.  Must be called from the render thread
    /// (between frames, not during a render pass).  Returns true if the resize
    /// succeeded, false if the dimensions are invalid or the GPU flush failed.
    bool resize(int width, int height);

    /// Current render target width in pixels.
    [[nodiscard]] int getWidth() const noexcept { return static_cast<int>(width_); }

    /// Current render target height in pixels.
    [[nodiscard]] int getHeight() const noexcept { return static_cast<int>(height_); }

    // Drain the GPU and release all resources.  Safe to call multiple times.
    void shutdown();

    // Returns a copy of the most recently captured frame. Thread-safe via mutex.
    [[nodiscard]] FrameData getLatestFrame() const;

    // Alias for getLatestFrame() -- used by IPC screenshot handler so callers
    // do not need to know renderer internals.
    [[nodiscard]] FrameData getLastFrameCopy() const;

    // Set the external scene parameters struct.  The renderer reads from it
    // each frame (rotation speed, background color, pause state).
    // Pointer must remain valid for the lifetime of the renderer.
    void setSceneParams(SceneParams* params);

    // Set the scene pointer for scene-driven camera and lighting.
    // The scene must remain valid for the lifetime of the renderer.
    void setScene(Scene* scene);

    // Replace the editor scene nodes and switch to editor rendering mode.
    // Thread-safe: may be called from the IPC thread while the render thread
    // is running.  Returns the number of nodes loaded.
    size_t loadEditorScene(std::vector<EditorNode> nodes);

private:
    // Initialization sub-steps (called by init).
    void createDevice();
    void createCommandQueue();
    void createSwapChain(HWND hwnd);
    void createRTVHeap();
    void createRenderTargets();
    void createRootSignature();
    void createPipelineState();
    void createVertexBuffer();
    void createConstantBuffer();
    void createFence();
    void createDepthBuffer();
    void createReadbackBuffer();

    // Editor-mode geometry and pipeline (delegated to EditorComponent).

    // Perform GPU -> CPU readback of the current frame's render target.
    void performReadback();

    // GPU synchronization -- block until the current frame's GPU work is done.
    void waitForGpu();

    // Advance to the next frame and wait for its fence value.
    void moveToNextFrame();

    // -----------------------------------------------------------------------
    // Pipeline objects
    // -----------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IDXGIFactory6>           factory_;
    Microsoft::WRL::ComPtr<ID3D12Device>            device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>       commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>         swapChain_;

    // Per-frame resources (double buffered).
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>   commandAllocators_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Resource>           renderTargets_[kFrameCount];

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

    // Descriptor heap for render-target views.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    rtvHeap_;
    uint32_t rtvDescriptorSize_ = 0;

    // Depth-stencil buffer and DSV heap.
    Microsoft::WRL::ComPtr<ID3D12Resource>           depthBuffer_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>     dsvHeap_;

    // Pipeline state.
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     pipelineState_;

    // Geometry.
    Microsoft::WRL::ComPtr<ID3D12Resource>          vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW                        vertexBufferView_{};

    // Constant buffer (upload heap) for the MVP matrix.
    Microsoft::WRL::ComPtr<ID3D12Resource>          constantBuffer_;
    uint8_t*                                        cbvDataBegin_ = nullptr;

    // Synchronization.
    Microsoft::WRL::ComPtr<ID3D12Fence>             fence_;
    uint64_t                                        fenceValues_[kFrameCount]{};
    HANDLE                                          fenceEvent_ = nullptr;

    // Frame readback (double-buffered: one GPU-filling, one CPU-reading).
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffers_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Fence>    readbackFence_;
    uint64_t                               readbackFenceValue_ = 0;
    HANDLE                                 readbackEvent_      = nullptr;

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

    // Viewport and scissor rect (set once during init, reused every frame).
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT     scissorRect_{};

    // Current world-view-projection matrix, updated each frame by update().
    DirectX::XMFLOAT4X4 worldViewProj_{};

    // Stored view-projection matrix (without world transform) so that
    // per-node MVP can be composed during editor rendering.
    DirectX::XMFLOAT4X4 viewProj_{};

    // Accumulated rotation angles (radians).  Updated each frame via delta-time
    // so that animation speed is independent of frame-timing jitter.
    float rotAngleY_ = 0.0f;
    float rotAngleX_ = 0.0f;

    bool initialized_ = false;

    // External scene parameters (set via setSceneParams, nullable).
    SceneParams* sceneParams_ = nullptr;

    // External scene pointer (set via setScene, nullable).
    Scene* scene_ = nullptr;

    // Cached light state (updated each frame from scene_ via light_collect_primary).
    LightState cachedLight_;

    // CPU-side FPS overlay — stamps "FPS: XXX" into the readback pixel buffer.
    FPSOverlay fpsOverlay_;

    // Cached clear color -- updated from sceneParams_ each frame in update().
    // Default matches UE5 dark viewport background (~0.12).
    float clearR_ = 0.12f;
    float clearG_ = 0.12f;
    float clearB_ = 0.12f;

    // -----------------------------------------------------------------------
    // Render components (modular subsystems)
    // -----------------------------------------------------------------------

    EditorComponent editor_;   // Editor-only rendering (grid, wireframe cubes)

    // When true, render() draws the editor scene (grid + wireframe cubes)
    // instead of the legacy spinning pyramid.
    bool editorMode_ = true;
};

} // namespace uz
