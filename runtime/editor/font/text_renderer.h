/// UzEngine Text Renderer
///
/// Batches screen-space text quads and colored rectangles for 2D overlay
/// rendering. Uses the BitmapFont atlas for glyph texturing and a simple
/// vertex format (pos2D + UV + color).
///
/// # Rendering Model
/// All positions are in screen pixels with origin at top-left.
/// Text and rectangles are batched into a single vertex buffer per frame
/// and drawn with two draw calls (opaque rects first, then alpha-blended text).
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the render thread.

#pragma once

#include "bitmap_font.h"
#include "rhi.h"
#include "rhi_types.h"
#include "engine_math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class CBRingAllocator;
class ShaderPipeline;

// ─────────────────────────────────────────────────────────────────────────────
// Overlay Vertex
// ─────────────────────────────────────────────────────────────────────────────

/// Vertex for 2D overlay rendering (text + colored rects).
struct OverlayVertex {
    Vec2 position;   ///< Screen-space position in pixels (top-left origin).
    Vec2 texcoord;   ///< UV coordinates into font atlas (0,0 for solid rects).
    Vec4 color;      ///< RGBA color (premultiplied alpha for text).
};

// ─────────────────────────────────────────────────────────────────────────────
// TextRenderer
// ─────────────────────────────────────────────────────────────────────────────

/// Batched 2D text and rectangle renderer for editor overlays.
///
/// Maintains a CPU-side vertex buffer that is uploaded each frame.
/// Call `BeginFrame()` to reset, add text/rects, then `Render()` to draw.
class TextRenderer {
public:
    TextRenderer() = default;

    /// Initializes GPU resources (pipeline, shaders, vertex buffer).
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `shaderPipeline` — Shader compilation pipeline.
    /// * `rtvFormat` — Render target format for pipeline creation.
    /// * `screenWidth` / `screenHeight` — Initial screen dimensions.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device, ShaderPipeline& shaderPipeline,
              RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight);

    /// Destroys GPU resources.
    void Shutdown();

    /// Resets the vertex batch for a new frame.
    ///
    /// # Arguments
    /// * `frameIndex` — Current frame index from the render loop, used to
    ///   select the correct triple-buffered vertex buffer (frameIndex % 3).
    void BeginFrame(uint32_t frameIndex);

    /// Adds a text string at the given screen position.
    ///
    /// # Arguments
    /// * `font` — BitmapFont to use for glyph layout.
    /// * `text` — String to render (only printable ASCII).
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `color` — Text color (RGB, alpha from font atlas).
    void AddText(const BitmapFont& font, const std::string& text,
                 float x, float y, const Vec4& color);

    /// Adds a filled rectangle at the given screen position.
    ///
    /// # Arguments
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `w`, `h` — Width and height in pixels.
    /// * `color` — Fill color with alpha.
    void AddRect(float x, float y, float w, float h, const Vec4& color);

    /// Uploads vertex data and draws all batched geometry.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state, with render target set.
    /// * `cbRing` — Constant buffer allocator for per-draw constants.
    /// * `font` — BitmapFont whose atlas texture is bound for text rendering.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, const BitmapFont& font);

    /// Updates screen dimensions (call on resize).
    void SetScreenSize(uint32_t width, uint32_t height);

private:
    IRHIDevice* m_device = nullptr;

    RHIShaderHandle m_vs{};
    RHIShaderHandle m_ps{};
    RHIPipelineHandle m_textPipeline{};
    RHIPipelineHandle m_rectPipeline{};
    RHISamplerHandle m_sampler{};

    static constexpr uint32_t kFramesInFlight = 3;
    RHIBufferHandle m_vertexBuffers[kFramesInFlight]{};
    uint32_t m_vbSrvIndices[kFramesInFlight] = {};
    uint32_t m_currentFrame = 0;
    uint32_t m_samplerIndex = 0;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;

    static constexpr uint32_t kMaxVertices = 16384;

    /// Separate batches: rects drawn first (opaque), then text (alpha blended).
    std::vector<OverlayVertex> m_rectVertices;
    std::vector<OverlayVertex> m_textVertices;
};

} // namespace uz
