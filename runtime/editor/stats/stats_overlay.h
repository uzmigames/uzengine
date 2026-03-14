/// UzEngine Stats Overlay
///
/// Real-time performance overlay displaying FPS, frame time, draw calls,
/// GPU/CPU timing, and memory usage. Renders a text panel on the right side
/// of the screen and performance graphs on the bottom-left.
///
/// # Layout
/// - **Right panel**: Text stats (FPS, frame time, draw calls, vertices, etc.)
/// - **Bottom-left graph**: Frame time history (CPU green, GPU blue)
///
/// # Usage
/// ```cpp
/// StatsOverlay stats;
/// stats.Init(device, cmdList, uploadCtx, shaderPipeline, rtvFormat, 1280, 720);
///
/// // Per frame:
/// stats.BeginFrame(deltaTime);
/// stats.SetDrawCalls(drawCallCount);
/// stats.SetVertexCount(vertCount);
/// stats.Render(cmdList, cbRing);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the render thread.

#pragma once

#include "bitmap_font.h"
#include "text_renderer.h"
#include "rhi_config.h"

#include <array>
#include <cstdint>
#include <string>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class UploadContext;
class ShaderPipeline;

// ─────────────────────────────────────────────────────────────────────────────
// StatsOverlay
// ─────────────────────────────────────────────────────────────────────────────

/// Editor performance overlay with text stats and frame time graph.
class StatsOverlay {
public:
    StatsOverlay() = default;

    /// Initializes font, text renderer, and GPU resources using ShaderPipeline.
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `cmdList` — Command list for font atlas upload (must be recording).
    /// * `upload` — Upload context for staging.
    /// * `shaderPipeline` — Shader compilation pipeline (Windows/HLSL).
    /// * `rtvFormat` — Render target format.
    /// * `screenWidth`, `screenHeight` — Initial screen dimensions.
    /// * `bindless` — If true, uses SM6.6 bindless rendering. If false,
    ///   uses SM5 Input Assembler path for overlay pipelines.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
              uint32_t screenWidth, uint32_t screenHeight,
              bool bindless = true);

    /// Initializes font, text renderer, and GPU resources with pre-created shaders.
    ///
    /// Use this overload when ShaderPipeline is not available (e.g., macOS Metal).
    /// The font is loaded from `fontPath` and the overlay shaders are passed
    /// as pre-created handles.
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `cmdList` — Command list for font atlas upload (must be recording).
    /// * `upload` — Upload context for staging.
    /// * `fontPath` — Absolute path to a .ttf/.ttc font file.
    /// * `overlayVS` — Pre-created overlay vertex shader handle.
    /// * `textPS` — Pre-created overlay text pixel/fragment shader handle.
    /// * `rectPS` — Pre-created overlay rect pixel/fragment shader handle.
    /// * `rtvFormat` — Render target format.
    /// * `screenWidth`, `screenHeight` — Initial screen dimensions.
    /// * `bindless` — If true, uses SM6.6 bindless rendering.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              const std::string& fontPath,
              RHIShaderHandle overlayVS, RHIShaderHandle textPS, RHIShaderHandle rectPS,
              RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
              bool bindless = false);

    /// Destroys all GPU resources.
    void Shutdown();

    /// Call at the start of each frame to record timing.
    ///
    /// # Arguments
    /// * `deltaTime` — Frame delta time in seconds.
    void BeginFrame(float deltaTime);

    /// Sets the number of draw calls this frame.
    void SetDrawCalls(uint32_t count) { m_drawCalls = count; }

    /// Sets the total vertex count drawn this frame.
    void SetVertexCount(uint32_t count) { m_vertexCount = count; }

    /// Sets the total triangle count drawn this frame.
    void SetTriangleCount(uint32_t count) { m_triangleCount = count; }

    /// Sets GPU memory usage in bytes.
    void SetGPUMemoryUsage(uint64_t bytes) { m_gpuMemoryBytes = bytes; }

    /// Renders the stats overlay (text panel + graphs).
    ///
    /// # Arguments
    /// * `cmdList` — Command list with render target already set.
    /// * `cbRing` — Constant buffer allocator.
    /// * `frameIndex` — Current frame index for triple-buffered vertex selection.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, uint32_t frameIndex);

    /// Updates screen dimensions (call on resize).
    void SetScreenSize(uint32_t width, uint32_t height);

    /// Toggles overlay visibility.
    void SetVisible(bool visible) { m_visible = visible; }

    /// Returns whether the overlay is currently visible.
    [[nodiscard]] bool IsVisible() const { return m_visible; }

private:
    /// Builds the text panel content and adds it to the text renderer.
    void BuildTextPanel();

    /// Builds the frame time graph and adds it to the text renderer.
    void BuildGraph();

    IRHIDevice* m_device = nullptr;
    BitmapFont m_font;
    TextRenderer m_textRenderer;

    bool m_visible = true;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;

    // ── System info (captured once at init) ────────────────────────────
    std::string m_gpuName;           ///< GPU adapter name (e.g., "NVIDIA GeForce RTX 4090")
    std::string m_featureLevel;      ///< Feature level string (e.g., "D3D_FEATURE_LEVEL_12_2")
    std::string m_rhiBackend;        ///< RHI backend name (e.g., "DX12")
    std::string m_shaderModel;       ///< Shader model string (e.g., "SM6.8")
    std::string m_resolution;        ///< Resolution string (e.g., "1280x720")
    uint64_t m_vramTotalMB = 0;      ///< Total VRAM in MB
    bool m_bindless = false;         ///< Whether bindless is active

    /// Captures static system info from the device (called once in Init).
    void CaptureSystemInfo(IRHIDevice* device);

    // ── Frame stats ──────────────────────────────────────────────────────
    float m_deltaTime = 0.f;
    float m_fps = 0.f;
    uint32_t m_drawCalls = 0;
    uint32_t m_vertexCount = 0;
    uint32_t m_triangleCount = 0;
    uint64_t m_gpuMemoryBytes = 0;
    uint32_t m_frameNumber = 0;

    // ── Smoothed FPS (EMA) ───────────────────────────────────────────────
    float m_smoothedFPS = 0.f;
    float m_smoothedFrameTime = 0.f;

    // ── Frame time history for graph ─────────────────────────────────────
    static constexpr uint32_t kHistorySize = 120;
    std::array<float, kHistorySize> m_frameTimeHistory{};
    uint32_t m_historyIndex = 0;

    // ── Graph layout constants ───────────────────────────────────────────
    static constexpr float kGraphWidth = 240.f;
    static constexpr float kGraphHeight = 80.f;
    static constexpr float kGraphMargin = 10.f;
    static constexpr float kPanelPadding = 8.f;
    static constexpr float kFontSize = 14.f;
};

} // namespace uz
