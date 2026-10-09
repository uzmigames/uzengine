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
#include "engine/vfx/editor/vfx_icon_atlas.h"
#include "msdf_font.h"
#include "rhi.h"
#include "rhi_types.h"
#include "vector2.h"
#include "vector4.h"

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

/// Vertex for rounded-rect SDF rendering. Every vertex of a single
/// quad replicates the per-quad parameters — simpler than a separate
/// structured buffer of per-quad state and keeps the bindless path
/// trivial. 6 verts per quad (two triangles).
struct OverlayRrectVertex {
    Vec2  position;        ///< Screen-space position in pixels.
    Vec2  localPos;        ///< Signed offset from box centre, pixels.
    Vec2  halfExtent;      ///< Half-size of the box, pixels.
    Vec2  shadowOffset;    ///< Shadow offset, pixels.
    float cornerRadius;    ///< Corner radius, pixels.
    float borderThickness; ///< Border width, pixels (0 disables border).
    float shadowBlur;      ///< Shadow blur radius, pixels (0 disables shadow).
    float _pad0;
    Vec4  fillColor;
    Vec4  borderColor;
    Vec4  shadowColor;
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

    /// Initializes GPU resources using ShaderPipeline for shader compilation.
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `shaderPipeline` — Shader compilation pipeline (Windows/HLSL).
    /// * `rtvFormat` — Render target format for pipeline creation.
    /// * `screenWidth` / `screenHeight` — Initial screen dimensions.
    /// * `bindless` — If true, uses SM6.6 bindless path (SV_VertexID +
    ///   ResourceDescriptorHeap). If false, uses SM5 Input Assembler path
    ///   with explicit vertex layout and descriptor table binding.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device, ShaderPipeline& shaderPipeline,
              RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
              bool bindless = true);

    /// Initializes GPU resources with pre-created shader handles.
    ///
    /// Use this overload when ShaderPipeline is not available (e.g., on macOS
    /// with MSL shaders compiled directly via CreateShader).
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `overlayVS` — Pre-created vertex shader handle.
    /// * `textPS` — Pre-created text pixel/fragment shader handle.
    /// * `rectPS` — Pre-created rect pixel/fragment shader handle.
    /// * `rtvFormat` — Render target format for pipeline creation.
    /// * `screenWidth` / `screenHeight` — Initial screen dimensions.
    /// * `bindless` — If true, uses SM6.6 bindless path.
    ///
    /// # Returns
    /// `true` on success.
    bool Init(IRHIDevice* device, RHIShaderHandle overlayVS,
              RHIShaderHandle textPS, RHIShaderHandle rectPS,
              RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
              bool bindless = false);

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
    /// * `text` — UTF-8 string to render.
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `color` — Text color (RGB, alpha from font atlas).
    void AddText(const BitmapFont& font, const std::string& text,
                 float x, float y, const Vec4& color);

    /// Phase 13.4.0 — adds a text string rendered via the MSDF path.
    ///
    /// Crisp at any render scale via `fwidth`-based AA — use this
    /// when text is drawn above 16 px or below 12 px (i.e. anywhere
    /// the bitmap bake is not the native size). Smaller sizes should
    /// prefer `AddText(BitmapFont, ...)` for the sharper native bake.
    ///
    /// # Arguments
    /// * `font` — MsdfFont whose atlas is bound for this call.
    /// * `text` — UTF-8 string to render.
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `pixelHeight` — Desired render height in pixels. Scales the
    ///   atlas-space glyph metrics uniformly.
    /// * `color` — Text color (RGB, alpha from SDF edge).
    void AddMsdfText(const MsdfFont& font, const std::string& text,
                     float x, float y, float pixelHeight,
                     const Vec4& color);

    /// Adds a filled rectangle at the given screen position.
    ///
    /// # Arguments
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `w`, `h` — Width and height in pixels.
    /// * `color` — Fill color with alpha.
    void AddRect(float x, float y, float w, float h, const Vec4& color);

    /// Adds a rounded-rectangle quad drawn via an SDF shader.
    ///
    /// Every arg beyond `fillColor` is optional — pass zero / transparent
    /// to opt out. `cornerRadius == 0 && borderThickness == 0 &&
    /// shadowBlur == 0` degenerates to an axis-aligned `AddRect` (same
    /// shader path, same vertex layout), so this primitive carries no
    /// extra cost when nothing SDF-specific is requested.
    ///
    /// # Arguments
    /// * `x`, `y` — Top-left of the rect, screen pixels.
    /// * `w`, `h` — Width / height, screen pixels.
    /// * `cornerRadius` — Radius in pixels. Clamped to `min(w, h) / 2`.
    /// * `fillColor` — Interior colour.
    /// * `borderThickness` — Border width in pixels. 0 disables.
    /// * `borderColor` — Border colour (alpha also gates visibility).
    /// * `shadowOffset` — 2D offset in pixels.
    /// * `shadowBlur` — Exponential blur radius. 0 disables.
    /// * `shadowColor` — Shadow colour (alpha also gates visibility).
    void AddRoundedRect(float x, float y, float w, float h,
                        float cornerRadius,
                        const Vec4& fillColor,
                        float borderThickness = 0.0f,
                        const Vec4& borderColor = Vec4{0, 0, 0, 0},
                        const Vec2& shadowOffset = Vec2{0, 0},
                        float shadowBlur = 0.0f,
                        const Vec4& shadowColor = Vec4{0, 0, 0, 0});

    /// Adds a sprite quad sampled from the VFX icon atlas.
    ///
    /// The atlas must have been loaded via `LoadIconAtlas()`. Renders a
    /// tinted monochrome icon — the atlas stores white-on-transparent
    /// glyphs, so the sampled RGB is multiplied by `tint.rgb` and alpha
    /// by `tint.a`. If the atlas is not loaded, the call is silently
    /// skipped.
    ///
    /// # Arguments
    /// * `icon` — Icon enumerated in `VfxUiIcon`.
    /// * `x`, `y` — Top-left corner in screen pixels.
    /// * `w`, `h` — Width and height in pixels (usually 16-24 for toolbar
    ///   buttons, up to 48 for hero buttons).
    /// * `tint` — RGBA tint; white (1,1,1,1) passes the atlas through.
    void AddSprite(VfxUiIcon icon, float x, float y, float w, float h,
                   const Vec4& tint);

    /// Generates the procedural VFX icon atlas and uploads it to GPU.
    ///
    /// Creates a 256x256 RGBA8 texture + SRV and records a copy of the
    /// procedurally generated pixels into it via the provided upload
    /// context. Must be called once after `Init()` (and before the first
    /// `AddSprite`) to enable sprite rendering.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in a recording state; the copy + state
    ///   transition to `ShaderResource` are recorded into it.
    /// * `upload` — Upload context that allocates the staging buffer.
    ///
    /// # Returns
    /// `true` on success.
    bool LoadIconAtlas(IRHICommandList* cmdList, class UploadContext& upload);

    /// Uploads vertex data and draws all batched geometry.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state, with render target set.
    /// * `cbRing` — Constant buffer allocator for per-draw constants.
    /// * `font` — BitmapFont whose atlas texture is bound for text rendering.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, const BitmapFont& font);

    /// Updates screen dimensions (call on resize).
    void SetScreenSize(uint32_t width, uint32_t height);

    // ── Testing accessors ───────────────────────────────────────────────
    // These expose the per-frame batches for unit tests that inspect
    // emission before Render() uploads them. Not intended for general
    // use.

    [[nodiscard]] const std::vector<OverlayVertex>& GetRectVerticesForTest() const {
        return m_rectVertices;
    }
    [[nodiscard]] const std::vector<OverlayVertex>& GetSpriteVerticesForTest() const {
        return m_spriteVertices;
    }
    [[nodiscard]] const std::vector<OverlayVertex>& GetTextVerticesForTest() const {
        return m_textVertices;
    }
    [[nodiscard]] const std::vector<OverlayRrectVertex>& GetRrectVerticesForTest() const {
        return m_rrectVertices;
    }

private:
    /// Shared pipeline/buffer/sampler creation logic used by both Init overloads.
    bool InitPipelines(RHIShaderHandle textPs, RHIShaderHandle rectPs,
                       RHIShaderHandle spritePs, RHIShaderHandle rrectVs,
                       RHIShaderHandle rrectPs, RHIShaderHandle msdfPs,
                       RHIFormat rtvFormat);

    IRHIDevice* m_device = nullptr;

    RHIShaderHandle m_vs{};
    RHIShaderHandle m_ps{};
    RHIShaderHandle m_rrectVs{};
    RHIPipelineHandle m_textPipeline{};
    RHIPipelineHandle m_rectPipeline{};
    RHIPipelineHandle m_spritePipeline{};
    RHIPipelineHandle m_rrectPipeline{};
    RHIPipelineHandle m_msdfPipeline{};
    RHISamplerHandle m_sampler{};

    RHITextureHandle m_iconAtlasTexture{};
    uint32_t m_iconAtlasSrvIndex = 0;
    bool m_iconAtlasLoaded = false;

    static constexpr uint32_t kFramesInFlight = 3;
    /// Upper bound on distinct `BeginFrame`→`Render` cycles that a caller
    /// may issue within a single real frame. The VFX editor overlay uses
    /// 3 (base UI / dropdown / context menu). 4 gives a safety margin for
    /// future modal overlays. Each ring slot owns its own GPU upload
    /// buffer so back-to-back passes in the same frame do not stomp each
    /// other's vertex data while the GPU is still reading the previous
    /// pass's draw call.
    static constexpr uint32_t kMaxPassesPerFrame = 4;
    static constexpr uint32_t kMaxSlots = kFramesInFlight * kMaxPassesPerFrame;
    RHIBufferHandle m_vertexBuffers[kMaxSlots]{};
    uint32_t m_vbSrvIndices[kMaxSlots] = {};
    RHIBufferHandle m_rrectVertexBuffers[kMaxSlots]{};
    uint32_t m_rrectVbSrvIndices[kMaxSlots] = {};
    /// Advanced by every `BeginFrame` call so each upload lands in a
    /// distinct ring slot. Initial value `kMaxSlots - 1` makes the first
    /// `BeginFrame` wrap to slot 0.
    uint32_t m_currentSlot = kMaxSlots - 1;
    uint32_t m_samplerIndex = 0;
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;

    static constexpr uint32_t kMaxVertices = 32768;
    static constexpr uint32_t kMaxRrectVertices = 8192;
    bool m_bindless = true;  ///< True = SM6.6 bindless, false = SM5 IA path.

    /// Separate batches: rrect drawn first (shadows land behind everything),
    /// then rects, sprites, bitmap text, and finally MSDF text (own atlas).
    std::vector<OverlayVertex>      m_rectVertices;
    std::vector<OverlayVertex>      m_spriteVertices;
    std::vector<OverlayVertex>      m_textVertices;
    std::vector<OverlayRrectVertex> m_rrectVertices;
    std::vector<OverlayVertex>      m_msdfVertices;

    /// Tracks which MSDF atlas was used for `AddMsdfText` this frame.
    /// Bound at render time; null = no MSDF text emitted.
    const MsdfFont*                 m_msdfFontThisFrame = nullptr;

public:
    [[nodiscard]] const std::vector<OverlayVertex>& GetMsdfVerticesForTest() const {
        return m_msdfVertices;
    }
};

} // namespace uz
