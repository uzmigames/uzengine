/// UzEngine Stats Overlay Implementation
///
/// Collects per-frame performance metrics and renders a text panel (right)
/// with a frame time graph (bottom-left) using the BitmapFont text renderer.

#include "stats_overlay.h"
#include "upload_context.h"
#include "constant_buffers.h"
#include "log.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

UZ_DECLARE_LOG_CATEGORY(EditorStats);

namespace uz {

UZ_DEFINE_LOG_CATEGORY(EditorStats, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
bool StatsOverlay::Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
                        ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        bool bindless) {
    m_device = device;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // ── Font atlas ───────────────────────────────────────────────────────
    std::string fontPath = "C:/Windows/Fonts/consola.ttf";
    if (!m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(EditorStats, Error, "Failed to init stats font");
        return false;
    }

    // ── Text renderer ────────────────────────────────────────────────────
    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat, screenWidth, screenHeight, bindless)) {
        UZ_LOG(EditorStats, Error, "Failed to init text renderer");
        return false;
    }

    m_frameTimeHistory.fill(0.f);

    UZ_LOG(EditorStats, Info, "Stats overlay initialized ({}x{})", screenWidth, screenHeight);
    return true;
}
#endif // _WIN32

bool StatsOverlay::Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
                        const std::string& fontPath,
                        RHIShaderHandle overlayVS, RHIShaderHandle textPS, RHIShaderHandle rectPS,
                        RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
                        bool bindless) {
    m_device = device;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // ── Font atlas ───────────────────────────────────────────────────────
    if (!m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(EditorStats, Error, "Failed to init stats font: path='{}'", fontPath);
        return false;
    }

    // ── Text renderer (using pre-created shader handles) ─────────────────
    if (!m_textRenderer.Init(device, overlayVS, textPS, rectPS, rtvFormat,
                              screenWidth, screenHeight, bindless)) {
        UZ_LOG(EditorStats, Error, "Failed to init text renderer");
        return false;
    }

    m_frameTimeHistory.fill(0.f);

    UZ_LOG(EditorStats, Info, "Stats overlay initialized ({}x{}, font='{}')",
           screenWidth, screenHeight, fontPath);
    return true;
}

void StatsOverlay::Shutdown() {
    m_textRenderer.Shutdown();
    if (m_device) {
        m_font.Shutdown(m_device);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame
// ─────────────────────────────────────────────────────────────────────────────

void StatsOverlay::BeginFrame(float deltaTime) {
    m_deltaTime = deltaTime;
    m_frameNumber++;

    // Record frame time history
    m_frameTimeHistory[m_historyIndex % kHistorySize] = deltaTime;
    m_historyIndex++;

    // Exponential moving average (smoothing factor 0.05)
    constexpr float kAlpha = 0.05f;
    m_smoothedFrameTime = m_smoothedFrameTime * (1.f - kAlpha) + deltaTime * kAlpha;
    m_smoothedFPS = (m_smoothedFrameTime > 0.f) ? (1.f / m_smoothedFrameTime) : 0.f;

    // Raw FPS for display
    m_fps = (deltaTime > 0.f) ? (1.f / deltaTime) : 0.f;
}

void StatsOverlay::SetScreenSize(uint32_t width, uint32_t height) {
    m_screenWidth = width;
    m_screenHeight = height;
    m_textRenderer.SetScreenSize(width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void StatsOverlay::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, uint32_t frameIndex) {
    if (!m_visible) return;

    m_textRenderer.BeginFrame(frameIndex);
    BuildTextPanel();
    BuildGraph();
    m_textRenderer.Render(cmdList, cbRing, m_font);
}

// ─────────────────────────────────────────────────────────────────────────────
// Text Panel (right side)
// ─────────────────────────────────────────────────────────────────────────────

void StatsOverlay::BuildTextPanel() {
    constexpr float kPanelWidth = 220.f;
    float panelX = static_cast<float>(m_screenWidth) - kPanelWidth - kPanelPadding;
    float panelY = kPanelPadding;

    // Background
    constexpr float kLineH = 18.f;
    constexpr int kLineCount = 8;
    float panelH = kLineH * kLineCount + kPanelPadding * 2.f;
    m_textRenderer.AddRect(panelX, panelY, kPanelWidth, panelH,
                           {0.05f, 0.05f, 0.08f, 0.85f});

    float textX = panelX + kPanelPadding;
    float textY = panelY + kPanelPadding;

    // Color scheme
    const Vec4 white  = {1.f, 1.f, 1.f, 1.f};
    const Vec4 green  = {0.4f, 1.f, 0.4f, 1.f};
    const Vec4 yellow = {1.f, 1.f, 0.4f, 1.f};
    const Vec4 cyan   = {0.4f, 1.f, 1.f, 1.f};
    const Vec4 gray   = {0.7f, 0.7f, 0.7f, 1.f};

    char buf[64];

    // FPS (smoothed, prominent)
    snprintf(buf, sizeof(buf), "FPS: %.0f", m_smoothedFPS);
    m_textRenderer.AddText(m_font, buf, textX, textY,
                           m_smoothedFPS >= 60.f ? green : (m_smoothedFPS >= 30.f ? yellow : Vec4{1.f, 0.3f, 0.3f, 1.f}));
    textY += kLineH;

    // Frame time
    snprintf(buf, sizeof(buf), "Frame: %.2f ms", m_smoothedFrameTime * 1000.f);
    m_textRenderer.AddText(m_font, buf, textX, textY, white);
    textY += kLineH;

    // Raw FPS (instant)
    snprintf(buf, sizeof(buf), "FPS (raw): %.0f", m_fps);
    m_textRenderer.AddText(m_font, buf, textX, textY, gray);
    textY += kLineH;

    // Draw calls
    snprintf(buf, sizeof(buf), "Draw calls: %u", m_drawCalls);
    m_textRenderer.AddText(m_font, buf, textX, textY, cyan);
    textY += kLineH;

    // Vertices
    snprintf(buf, sizeof(buf), "Vertices: %u", m_vertexCount);
    m_textRenderer.AddText(m_font, buf, textX, textY, cyan);
    textY += kLineH;

    // Triangles
    snprintf(buf, sizeof(buf), "Triangles: %u", m_triangleCount);
    m_textRenderer.AddText(m_font, buf, textX, textY, cyan);
    textY += kLineH;

    // GPU memory
    if (m_gpuMemoryBytes > 0) {
        float mb = static_cast<float>(m_gpuMemoryBytes) / (1024.f * 1024.f);
        snprintf(buf, sizeof(buf), "VRAM: %.1f MB", mb);
        m_textRenderer.AddText(m_font, buf, textX, textY, yellow);
        textY += kLineH;
    }

    // Frame number
    snprintf(buf, sizeof(buf), "Frame: %u", m_frameNumber);
    m_textRenderer.AddText(m_font, buf, textX, textY, gray);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Time Graph (bottom-left)
// ─────────────────────────────────────────────────────────────────────────────

void StatsOverlay::BuildGraph() {
    float graphX = kGraphMargin;
    float graphY = static_cast<float>(m_screenHeight) - kGraphHeight - kGraphMargin;

    // Background
    m_textRenderer.AddRect(graphX, graphY, kGraphWidth, kGraphHeight,
                           {0.f, 0.f, 0.f, 0.7f});

    // Title
    m_textRenderer.AddText(m_font, "Frame Time (ms)", graphX + 4.f, graphY - kFontSize - 2.f,
                           {0.8f, 0.8f, 0.8f, 1.f});

    // Reference lines: 16.6ms (60fps) and 33.3ms (30fps)
    constexpr float kMaxMs = 50.f; // Graph max scale

    auto msToY = [&](float ms) -> float {
        float t = std::clamp(ms / kMaxMs, 0.f, 1.f);
        return graphY + kGraphHeight * (1.f - t);
    };

    // 60 FPS reference line (16.6ms)
    float y60 = msToY(16.66f);
    m_textRenderer.AddRect(graphX, y60, kGraphWidth, 1.f, {0.2f, 0.6f, 0.2f, 0.5f});

    // 30 FPS reference line (33.3ms)
    float y30 = msToY(33.33f);
    m_textRenderer.AddRect(graphX, y30, kGraphWidth, 1.f, {0.6f, 0.6f, 0.2f, 0.5f});

    // Frame time bars
    float barWidth = kGraphWidth / static_cast<float>(kHistorySize);
    uint32_t count = std::min(m_historyIndex, kHistorySize);

    for (uint32_t i = 0; i < count; ++i) {
        // Read from oldest to newest
        uint32_t idx = (m_historyIndex - count + i) % kHistorySize;
        float ms = m_frameTimeHistory[idx] * 1000.f;

        float barX = graphX + static_cast<float>(i) * barWidth;
        float barTop = msToY(ms);
        float barH = (graphY + kGraphHeight) - barTop;

        // Color: green < 16.6ms, yellow < 33.3ms, red > 33.3ms
        Vec4 barColor;
        if (ms <= 16.66f) {
            barColor = {0.2f, 0.8f, 0.3f, 0.9f};
        } else if (ms <= 33.33f) {
            float t = (ms - 16.66f) / 16.66f;
            barColor = {0.2f + t * 0.8f, 0.8f - t * 0.3f, 0.3f - t * 0.2f, 0.9f};
        } else {
            barColor = {1.f, 0.3f, 0.2f, 0.9f};
        }

        if (barH > 0.f) {
            m_textRenderer.AddRect(barX, barTop, std::max(barWidth - 0.5f, 1.f), barH, barColor);
        }
    }

    // Graph labels
    char buf[32];
    snprintf(buf, sizeof(buf), "60");
    m_textRenderer.AddText(m_font, buf, graphX + kGraphWidth + 2.f, y60 - kFontSize * 0.5f,
                           {0.4f, 0.8f, 0.4f, 0.8f});
    snprintf(buf, sizeof(buf), "30");
    m_textRenderer.AddText(m_font, buf, graphX + kGraphWidth + 2.f, y30 - kFontSize * 0.5f,
                           {0.8f, 0.8f, 0.4f, 0.8f});
}

} // namespace uz
