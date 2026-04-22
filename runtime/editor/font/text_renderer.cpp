/// UzEngine Text Renderer Implementation
///
/// Batches 2D quads (text glyphs + colored rectangles) into a single
/// StructuredBuffer and draws with two pipelines: opaque rects, then
/// alpha-blended text.

#include "text_renderer.h"
#include "constant_buffers.h"
#include "engine/renderer/upload/upload_context.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"
#include "log.h"

#ifdef _WIN32
#include "engine_shaders.h"
#include "shader_pipeline.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(EditorText);

namespace uz {

UZ_DEFINE_LOG_CATEGORY(EditorText, Info);

// ─────────────────────────────────────────────────────────────────────────────
// CB layout matching overlay_vs.hlsl
// ─────────────────────────────────────────────────────────────────────────────

struct OverlayCB {
    float invScreenSizeX;
    float invScreenSizeY;
    float padding[2];
};

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

/// Creates pipelines, vertex buffers, and sampler from pre-created shader handles.
/// Shared by both Init overloads — call after m_vs is set.
bool TextRenderer::InitPipelines(RHIShaderHandle textPs, RHIShaderHandle rectPs,
                                  RHIShaderHandle spritePs, RHIShaderHandle rrectVs,
                                  RHIShaderHandle rrectPs, RHIShaderHandle msdfPs,
                                  RHIFormat rtvFormat) {
    m_rrectVs = rrectVs;
    // SM5 input layout: matches overlay_vs VSInput {POSITION, TEXCOORD0, COLOR0}
    RHIInputElementDesc overlayInputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RG32Float,  0, 0  },  // float2 position
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,  0, 8  },  // float2 texcoord
        { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 16 }, // float4 color
    };

    // ── Rect pipeline (alpha blend for semi-transparent backgrounds) ─────
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = m_vs;
        desc.pixelShader = rectPs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

        if (!m_bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlayRectPipeline";
        m_rectPipeline = m_device->CreateGraphicsPipeline(desc);
    }

    // ── Text pipeline (alpha blend with font atlas alpha) ────────────────
    {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = m_vs;
        desc.pixelShader = textPs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

        if (!m_bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlayTextPipeline";
        m_textPipeline = m_device->CreateGraphicsPipeline(desc);
    }

    // ── Sprite pipeline (alpha blend with RGBA atlas) ────────────────────
    if (spritePs.IsValid()) {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = m_vs;
        desc.pixelShader = spritePs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

        if (!m_bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlaySpritePipeline";
        m_spritePipeline = m_device->CreateGraphicsPipeline(desc);
    }

    // ── MSDF text pipeline (reuses overlay_vs + overlay_msdf_ps) ─────────
    if (msdfPs.IsValid()) {
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = m_vs;
        desc.pixelShader = msdfPs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

        if (!m_bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlayMsdfPipeline";
        m_msdfPipeline = m_device->CreateGraphicsPipeline(desc);
    }

    // ── Rounded-rect SDF pipeline ───────────────────────────────────────
    if (rrectPs.IsValid() && rrectVs.IsValid()) {
        // SM5 input layout mirrors OverlayRrectVertex field order.
        RHIInputElementDesc rrectElements[] = {
            { RHIInputSemantic::Position, 0, RHIFormat::RG32Float,   0,   0 }, // position
            { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0,   8 }, // localPos
            { RHIInputSemantic::Texcoord, 1, RHIFormat::RG32Float,   0,  16 }, // halfExtent
            { RHIInputSemantic::Texcoord, 2, RHIFormat::RG32Float,   0,  24 }, // shadowOffset
            { RHIInputSemantic::Texcoord, 3, RHIFormat::R32Float,    0,  32 }, // cornerRadius
            { RHIInputSemantic::Texcoord, 4, RHIFormat::R32Float,    0,  36 }, // borderThickness
            { RHIInputSemantic::Texcoord, 5, RHIFormat::R32Float,    0,  40 }, // shadowBlur
            { RHIInputSemantic::Texcoord, 6, RHIFormat::R32Float,    0,  44 }, // _pad0
            { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0,  48 }, // fillColor
            { RHIInputSemantic::Color,    1, RHIFormat::RGBA32Float, 0,  64 }, // borderColor
            { RHIInputSemantic::Color,    2, RHIFormat::RGBA32Float, 0,  80 }, // shadowColor
        };

        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = rrectVs;
        desc.pixelShader  = rrectPs;
        desc.renderTargetFormats[0] = rtvFormat;
        desc.renderTargetCount = 1;
        desc.depthStencilFormat = RHIFormat::Unknown;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        desc.rasterizer.cullMode = RHICullMode::None;

        desc.renderTargetBlend[0].blendEnable = true;
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
        desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
        desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
        desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

        if (!m_bindless) {
            desc.inputElements = rrectElements;
            desc.inputElementCount = 11;
            desc.vertexStride = sizeof(OverlayRrectVertex);
        }

        desc.debugName = "OverlayRrectPipeline";
        m_rrectPipeline = m_device->CreateGraphicsPipeline(desc);
    }

    // ── Vertex buffers (one per ring slot, Upload heap) ────────────────
    // `kMaxSlots = kFramesInFlight * kMaxPassesPerFrame` so that each
    // `BeginFrame` → `Render` pair within a single real frame lands in
    // its own upload buffer. Reusing one slot per frame caused pass 2's
    // upload to stomp pass 1's vertex data before the GPU executed the
    // already-queued Draw() for pass 1.
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        RHIBufferDesc vbDesc{};
        vbDesc.sizeBytes = kMaxVertices * sizeof(OverlayVertex);
        vbDesc.stride = sizeof(OverlayVertex);
        vbDesc.usage = m_bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
        vbDesc.memory = RHIMemoryType::Upload;
        vbDesc.debugName = "OverlayVertexBuffer";

        m_vertexBuffers[i] = m_device->CreateBuffer(vbDesc);
        m_vbSrvIndices[i] = m_device->GetBufferSRVIndex(m_vertexBuffers[i]);
    }

    // ── Rounded-rect vertex buffers (separate stride) ──────────────────
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        RHIBufferDesc vbDesc{};
        vbDesc.sizeBytes = kMaxRrectVertices * sizeof(OverlayRrectVertex);
        vbDesc.stride = sizeof(OverlayRrectVertex);
        vbDesc.usage = m_bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
        vbDesc.memory = RHIMemoryType::Upload;
        vbDesc.debugName = "OverlayRrectVertexBuffer";

        m_rrectVertexBuffers[i] = m_device->CreateBuffer(vbDesc);
        m_rrectVbSrvIndices[i] = m_device->GetBufferSRVIndex(m_rrectVertexBuffers[i]);
    }

    // ── Sampler (linear filtering for font atlas) ────────────────────────
    RHISamplerDesc sampDesc{};
    sampDesc.filter = RHIFilterMode::Linear;
    sampDesc.addressU = RHIAddressMode::Clamp;
    sampDesc.addressV = RHIAddressMode::Clamp;
    sampDesc.debugName = "FontAtlasSampler";

    m_sampler = m_device->CreateSampler(sampDesc);
    m_samplerIndex = m_device->GetSamplerIndex(m_sampler);

    if (!m_rectPipeline.IsValid() || !m_textPipeline.IsValid() || !m_vertexBuffers[0].IsValid()) {
        UZ_LOG(EditorText, Error, "Failed to create overlay pipeline resources");
        return false;
    }

    UZ_LOG(EditorText, Info, "Text renderer initialized (maxVerts={}, screen={}x{}, bindless={})",
           kMaxVertices, m_screenWidth, m_screenHeight, m_bindless);
    return true;
}

#ifdef _WIN32
bool TextRenderer::Init(IRHIDevice* device, ShaderPipeline& shaderPipeline,
                        RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
                        bool bindless) {
    m_device = device;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_bindless = bindless;

    // ── Compile embedded shaders ──────────────────────────────────────────
    auto vsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayVS),
        EngineShaders::GetSource(EngineShader::OverlayVS));
    auto textPsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayTextPS),
        EngineShaders::GetSource(EngineShader::OverlayTextPS));
    auto rectPsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayRectPS),
        EngineShaders::GetSource(EngineShader::OverlayRectPS));
    auto spritePsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlaySpritePS),
        EngineShaders::GetSource(EngineShader::OverlaySpritePS));
    auto rrectVsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayRrectVS),
        EngineShaders::GetSource(EngineShader::OverlayRrectVS));
    auto rrectPsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayRrectPS),
        EngineShaders::GetSource(EngineShader::OverlayRrectPS));
    auto msdfPsBytecode = shaderPipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::OverlayMsdfPS),
        EngineShaders::GetSource(EngineShader::OverlayMsdfPS));

    if (vsBytecode.empty() || textPsBytecode.empty() || rectPsBytecode.empty() ||
        spritePsBytecode.empty() || rrectVsBytecode.empty() ||
        rrectPsBytecode.empty() || msdfPsBytecode.empty()) {
        UZ_LOG(EditorText, Error, "Failed to compile overlay shaders");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.debugName = "overlay_vs";
    m_vs = device->CreateShader(vsDesc);

    RHIShaderDesc textPsDesc{};
    textPsDesc.bytecode = textPsBytecode.data();
    textPsDesc.bytecodeSize = textPsBytecode.size();
    textPsDesc.stage = RHIShaderStage::Pixel;
    textPsDesc.debugName = "overlay_text_ps";
    RHIShaderHandle textPs = device->CreateShader(textPsDesc);

    RHIShaderDesc rectPsDesc{};
    rectPsDesc.bytecode = rectPsBytecode.data();
    rectPsDesc.bytecodeSize = rectPsBytecode.size();
    rectPsDesc.stage = RHIShaderStage::Pixel;
    rectPsDesc.debugName = "overlay_rect_ps";
    RHIShaderHandle rectPs = device->CreateShader(rectPsDesc);

    RHIShaderDesc spritePsDesc{};
    spritePsDesc.bytecode = spritePsBytecode.data();
    spritePsDesc.bytecodeSize = spritePsBytecode.size();
    spritePsDesc.stage = RHIShaderStage::Pixel;
    spritePsDesc.debugName = "overlay_sprite_ps";
    RHIShaderHandle spritePs = device->CreateShader(spritePsDesc);

    RHIShaderDesc rrectVsDesc{};
    rrectVsDesc.bytecode = rrectVsBytecode.data();
    rrectVsDesc.bytecodeSize = rrectVsBytecode.size();
    rrectVsDesc.stage = RHIShaderStage::Vertex;
    rrectVsDesc.debugName = "overlay_rrect_vs";
    RHIShaderHandle rrectVs = device->CreateShader(rrectVsDesc);

    RHIShaderDesc rrectPsDesc{};
    rrectPsDesc.bytecode = rrectPsBytecode.data();
    rrectPsDesc.bytecodeSize = rrectPsBytecode.size();
    rrectPsDesc.stage = RHIShaderStage::Pixel;
    rrectPsDesc.debugName = "overlay_rrect_ps";
    RHIShaderHandle rrectPs = device->CreateShader(rrectPsDesc);

    RHIShaderDesc msdfPsDesc{};
    msdfPsDesc.bytecode = msdfPsBytecode.data();
    msdfPsDesc.bytecodeSize = msdfPsBytecode.size();
    msdfPsDesc.stage = RHIShaderStage::Pixel;
    msdfPsDesc.debugName = "overlay_msdf_ps";
    RHIShaderHandle msdfPs = device->CreateShader(msdfPsDesc);

    return InitPipelines(textPs, rectPs, spritePs, rrectVs, rrectPs, msdfPs, rtvFormat);
}
#endif // _WIN32

bool TextRenderer::Init(IRHIDevice* device, RHIShaderHandle overlayVS,
                        RHIShaderHandle textPS, RHIShaderHandle rectPS,
                        RHIFormat rtvFormat, uint32_t screenWidth, uint32_t screenHeight,
                        bool bindless) {
    m_device = device;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_bindless = bindless;
    m_vs = overlayVS;

    // Sprite + rrect pipelines are only created by the Windows overload
    // that compiles their shaders from source; the MSL/macOS overload
    // leaves them invalid. AddSprite + AddRoundedRect silently degrade
    // in that case (rrect fallback emits a plain rect quad).
    return InitPipelines(textPS, rectPS, RHIShaderHandle{},
                         RHIShaderHandle{}, RHIShaderHandle{},
                         RHIShaderHandle{}, rtvFormat);
}

void TextRenderer::Shutdown() {
    if (!m_device) return;
    if (m_iconAtlasTexture.IsValid()) {
        m_device->DestroyTexture(m_iconAtlasTexture);
        m_iconAtlasTexture = {};
        m_iconAtlasLoaded = false;
    }
    m_device->DestroySampler(m_sampler);
    for (uint32_t i = 0; i < kMaxSlots; ++i) {
        m_device->DestroyBuffer(m_vertexBuffers[i]);
        m_device->DestroyBuffer(m_rrectVertexBuffers[i]);
    }
    // Pipelines/shaders cleaned up by device destruction
    m_device = nullptr;
}

bool TextRenderer::LoadIconAtlas(IRHICommandList* cmdList, UploadContext& upload) {
    if (!m_device) {
        UZ_LOG(EditorText, Error, "LoadIconAtlas: renderer not initialized");
        return false;
    }
    if (m_iconAtlasLoaded) return true;
    if (!m_spritePipeline.IsValid()) {
        UZ_LOG(EditorText, Warning,
               "LoadIconAtlas: sprite pipeline unavailable on this backend — "
               "sprite rendering disabled");
        return false;
    }

    // ── Generate procedural RGBA atlas pixels ─────────────────────────────
    const uint32_t w = kVfxIconAtlasWidth;
    const uint32_t h = kVfxIconAtlasHeight;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4u, 0);
    VfxGenerateIconAtlas(pixels.data());

    // ── Create GPU texture ────────────────────────────────────────────────
    RHITextureDesc texDesc{};
    texDesc.width = w;
    texDesc.height = h;
    texDesc.format = RHIFormat::RGBA8Unorm;
    texDesc.usage = RHITextureUsage::ShaderResource;
    texDesc.debugName = "VfxIconAtlas";

    m_iconAtlasTexture = m_device->CreateTexture(texDesc);
    if (!m_iconAtlasTexture.IsValid()) {
        UZ_LOG(EditorText, Error, "Failed to create VFX icon atlas texture");
        return false;
    }
    m_iconAtlasSrvIndex = m_device->GetTextureSRVIndex(m_iconAtlasTexture);

    // ── Upload pixels (staging + CopyBufferToTexture) ─────────────────────
    const uint64_t sizeBytes = static_cast<uint64_t>(w) * h * 4ull;
    if (!upload.UploadTexture(cmdList, m_iconAtlasTexture, pixels.data(), sizeBytes)) {
        UZ_LOG(EditorText, Error, "Failed to upload VFX icon atlas pixels");
        m_device->DestroyTexture(m_iconAtlasTexture);
        m_iconAtlasTexture = {};
        return false;
    }

    // Transition atlas from CopyDst to ShaderResource for sampling.
    RHIBarrierDesc barrier{};
    barrier.texture = m_iconAtlasTexture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    m_iconAtlasLoaded = true;
    UZ_LOG(EditorText, Info, "VFX icon atlas created: {}x{} RGBA8 (srv={})",
           w, h, m_iconAtlasSrvIndex);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame API
// ─────────────────────────────────────────────────────────────────────────────

void TextRenderer::BeginFrame(uint32_t /*frameIndex*/) {
    m_rectVertices.clear();
    m_spriteVertices.clear();
    m_textVertices.clear();
    m_rrectVertices.clear();
    m_msdfVertices.clear();
    m_msdfFontThisFrame = nullptr;
    // Advance to the next ring slot so multiple `BeginFrame` → `Render`
    // cycles within a single real frame (base UI / dropdown / context
    // menu) each own a distinct GPU upload buffer. `kMaxSlots` covers
    // `kFramesInFlight` frames of `kMaxPassesPerFrame` passes each, so
    // a slot is only reused once its original draw has fully executed.
    m_currentSlot = (m_currentSlot + 1) % kMaxSlots;
}

void TextRenderer::SetScreenSize(uint32_t width, uint32_t height) {
    m_screenWidth = width;
    m_screenHeight = height;
}

void TextRenderer::AddText(const BitmapFont& font, const std::string& text,
                           float x, float y, const Vec4& color) {
    float cursorX = x;
    float cursorY = y + font.GetPixelHeight(); // Baseline offset

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        // Fast-path ASCII newline (UTF-8 compatible — \n is 0x0A).
        const char lead = text[i];
        if (lead == '\n') {
            cursorX = x;
            cursorY += font.GetLineHeight();
            ++i;
            continue;
        }

        uint32_t cp = DecodeUtf8Codepoint(text, i);
        const GlyphInfo* g = font.GetGlyph(cp);
        if (!g) {
            // Fallback: replacement glyph. Prefer '?' (always in ASCII
            // range) since the font may not have U+FFFD baked.
            g = font.GetGlyph(static_cast<uint32_t>('?'));
            if (!g) continue;
        }

        float x0 = cursorX + g->xOffset;
        float y0 = cursorY + g->yOffset;
        float x1 = x0 + g->width;
        float y1 = y0 + g->height;

        // Two triangles per glyph
        m_textVertices.push_back({{x0, y0}, {g->u0, g->v0}, color});
        m_textVertices.push_back({{x1, y0}, {g->u1, g->v0}, color});
        m_textVertices.push_back({{x1, y1}, {g->u1, g->v1}, color});

        m_textVertices.push_back({{x0, y0}, {g->u0, g->v0}, color});
        m_textVertices.push_back({{x1, y1}, {g->u1, g->v1}, color});
        m_textVertices.push_back({{x0, y1}, {g->u0, g->v1}, color});

        cursorX += g->xAdvance;
    }
}

void TextRenderer::AddMsdfText(const MsdfFont& font, const std::string& text,
                               float x, float y, float pixelHeight,
                               const Vec4& color) {
    if (!font.IsLoaded()) return;

    // Track the atlas we need for this frame's MSDF draw. The renderer
    // supports a single MSDF atlas per frame — callers must route all
    // MSDF strings through the same font. If a second font is used,
    // later calls silently no-op to avoid silently binding the wrong
    // SRV.
    if (m_msdfFontThisFrame != nullptr && m_msdfFontThisFrame != &font) {
        UZ_LOG(EditorText, Warning,
               "AddMsdfText: multiple MSDF fonts in one frame are not "
               "supported; dropping excess calls");
        return;
    }
    m_msdfFontThisFrame = &font;

    // Scale atlas-space metrics to the requested render size.
    const float scale = pixelHeight / font.GetPixelHeight();

    float cursorX = x;
    float cursorY = y + pixelHeight; // baseline offset approximates ascent

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char lead = text[i];
        if (lead == '\n') {
            cursorX = x;
            cursorY += font.GetLineHeight() * scale;
            ++i;
            continue;
        }

        uint32_t cp = DecodeUtf8Codepoint(text, i);
        const GlyphInfo* g = font.GetGlyph(cp);
        if (!g) {
            g = font.GetGlyph(static_cast<uint32_t>('?'));
            if (!g) continue;
        }

        const float x0 = cursorX + g->xOffset * scale;
        const float y0 = cursorY + g->yOffset * scale;
        const float x1 = x0 + g->width * scale;
        const float y1 = y0 + g->height * scale;

        m_msdfVertices.push_back({{x0, y0}, {g->u0, g->v0}, color});
        m_msdfVertices.push_back({{x1, y0}, {g->u1, g->v0}, color});
        m_msdfVertices.push_back({{x1, y1}, {g->u1, g->v1}, color});

        m_msdfVertices.push_back({{x0, y0}, {g->u0, g->v0}, color});
        m_msdfVertices.push_back({{x1, y1}, {g->u1, g->v1}, color});
        m_msdfVertices.push_back({{x0, y1}, {g->u0, g->v1}, color});

        cursorX += g->xAdvance * scale;
    }
}

void TextRenderer::AddRect(float x, float y, float w, float h, const Vec4& color) {
    float x0 = x, y0 = y;
    float x1 = x + w, y1 = y + h;
    Vec2 uv = {0.f, 0.f};

    m_rectVertices.push_back({{x0, y0}, uv, color});
    m_rectVertices.push_back({{x1, y0}, uv, color});
    m_rectVertices.push_back({{x1, y1}, uv, color});

    m_rectVertices.push_back({{x0, y0}, uv, color});
    m_rectVertices.push_back({{x1, y1}, uv, color});
    m_rectVertices.push_back({{x0, y1}, uv, color});
}

void TextRenderer::AddRoundedRect(float x, float y, float w, float h,
                                  float cornerRadius,
                                  const Vec4& fillColor,
                                  float borderThickness,
                                  const Vec4& borderColor,
                                  const Vec2& shadowOffset,
                                  float shadowBlur,
                                  const Vec4& shadowColor) {
    // Degenerate fast-path: no SDF features requested → plain rect.
    // Preserves byte-for-byte compatibility with AddRect so a blind
    // phase-12 call-site swap is a no-op for the rendered output.
    if (cornerRadius <= 0.0f && borderThickness <= 0.0f &&
        shadowBlur <= 0.0f) {
        AddRect(x, y, w, h, fillColor);
        return;
    }

    // Also fall back when the SDF pipeline is not available on the
    // current backend (e.g. MSL/macOS path that never compiled it).
    if (!m_rrectPipeline.IsValid()) {
        AddRect(x, y, w, h, fillColor);
        return;
    }

    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    // Inflate the quad by max(shadowOffset magnitude, 3 * shadowBlur)
    // so the PS has fragments to rasterise for the shadow halo. Same
    // for border overshoot.
    const float shadowPad = std::max(std::abs(shadowOffset.x),
                                     std::abs(shadowOffset.y)) +
                             shadowBlur * 3.0f;
    const float pad = std::max(borderThickness * 0.5f, shadowPad);
    const float qx0 = x - pad;
    const float qy0 = y - pad;
    const float qx1 = x + w + pad;
    const float qy1 = y + h + pad;
    const float halfW = (qx1 - qx0) * 0.5f;
    const float halfH = (qy1 - qy0) * 0.5f;
    // SDF is evaluated against the ORIGINAL box half-extent; the
    // inflated quad is just the rasterisation region.
    const Vec2 boxHalfExtent{ hw, hh };

    // Emit 6 verts (two triangles). Each carries the per-quad params.
    auto push = [&](float px, float py) {
        OverlayRrectVertex v{};
        v.position       = { px, py };
        // localPos is (px - boxCenter). The box centre in screen
        // space is (x + hw, y + hh); the quad we emit is centred on
        // the SAME point (padding expands symmetrically), so:
        v.localPos       = { px - (x + hw), py - (y + hh) };
        v.halfExtent     = boxHalfExtent;
        v.shadowOffset   = shadowOffset;
        v.cornerRadius   = cornerRadius;
        v.borderThickness= borderThickness;
        v.shadowBlur     = shadowBlur;
        v._pad0          = 0.0f;
        v.fillColor      = fillColor;
        v.borderColor    = borderColor;
        v.shadowColor    = shadowColor;
        m_rrectVertices.push_back(v);
    };

    // Triangle 1: TL, TR, BR.
    push(qx0, qy0); push(qx1, qy0); push(qx1, qy1);
    // Triangle 2: TL, BR, BL.
    push(qx0, qy0); push(qx1, qy1); push(qx0, qy1);
}

void TextRenderer::AddSprite(VfxUiIcon icon, float x, float y, float w, float h,
                             const Vec4& tint) {
    if (!m_iconAtlasLoaded) return;
    if (icon == VfxUiIcon::None) return;

    VfxIconUv uv = VfxGetIconUv(icon);
    if (uv.u1 <= uv.u0 || uv.v1 <= uv.v0) return;

    float x0 = x, y0 = y;
    float x1 = x + w, y1 = y + h;

    m_spriteVertices.push_back({{x0, y0}, {uv.u0, uv.v0}, tint});
    m_spriteVertices.push_back({{x1, y0}, {uv.u1, uv.v0}, tint});
    m_spriteVertices.push_back({{x1, y1}, {uv.u1, uv.v1}, tint});

    m_spriteVertices.push_back({{x0, y0}, {uv.u0, uv.v0}, tint});
    m_spriteVertices.push_back({{x1, y1}, {uv.u1, uv.v1}, tint});
    m_spriteVertices.push_back({{x0, y1}, {uv.u0, uv.v1}, tint});
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void TextRenderer::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                          const BitmapFont& font) {
    uint32_t totalRectVerts   = static_cast<uint32_t>(m_rectVertices.size());
    uint32_t totalSpriteVerts = static_cast<uint32_t>(m_spriteVertices.size());
    uint32_t totalTextVerts   = static_cast<uint32_t>(m_textVertices.size());
    uint32_t totalMsdfVerts   = static_cast<uint32_t>(m_msdfVertices.size());
    uint32_t totalRrectVerts  = static_cast<uint32_t>(m_rrectVertices.size());
    uint32_t totalStdVerts    = totalRectVerts + totalSpriteVerts + totalTextVerts
                              + totalMsdfVerts;

    if (totalStdVerts == 0 && totalRrectVerts == 0) return;
    if (totalStdVerts > kMaxVertices) {
        UZ_LOG(EditorText, Warning, "Overlay vertex overflow: {} > {} (clamping msdf/text)",
               totalStdVerts, kMaxVertices);
        // Clamp MSDF first, then bitmap text — rects + sprites are
        // structural chrome.
        uint32_t headroom = kMaxVertices - totalRectVerts - totalSpriteVerts;
        if (totalTextVerts > headroom) { totalTextVerts = headroom; totalMsdfVerts = 0; }
        else {
            uint32_t msdfRoom = headroom - totalTextVerts;
            if (totalMsdfVerts > msdfRoom) totalMsdfVerts = msdfRoom;
        }
        totalStdVerts = totalRectVerts + totalSpriteVerts + totalTextVerts + totalMsdfVerts;
    }
    if (totalRrectVerts > kMaxRrectVertices) {
        UZ_LOG(EditorText, Warning, "RRect vertex overflow: {} > {} (clamping)",
               totalRrectVerts, kMaxRrectVertices);
        totalRrectVerts = kMaxRrectVertices;
    }

    const uint32_t fi = m_currentSlot;

    // ── Upload standard vertices [rects][sprites][text] ────────────────
    if (totalStdVerts > 0) {
        void* mapped = m_device->MapBuffer(m_vertexBuffers[fi]);
        if (!mapped) {
            UZ_LOG(EditorText, Error, "Failed to map overlay vertex buffer");
            return;
        }
        auto* dst = static_cast<OverlayVertex*>(mapped);
        if (totalRectVerts > 0) {
            std::memcpy(dst, m_rectVertices.data(),
                        totalRectVerts * sizeof(OverlayVertex));
        }
        if (totalSpriteVerts > 0) {
            std::memcpy(dst + totalRectVerts, m_spriteVertices.data(),
                        totalSpriteVerts * sizeof(OverlayVertex));
        }
        if (totalTextVerts > 0) {
            std::memcpy(dst + totalRectVerts + totalSpriteVerts,
                        m_textVertices.data(),
                        totalTextVerts * sizeof(OverlayVertex));
        }
        if (totalMsdfVerts > 0) {
            std::memcpy(dst + totalRectVerts + totalSpriteVerts + totalTextVerts,
                        m_msdfVertices.data(),
                        totalMsdfVerts * sizeof(OverlayVertex));
        }
        m_device->UnmapBuffer(m_vertexBuffers[fi]);
    }

    // ── Upload rrect vertices to their own buffer ───────────────────────
    if (totalRrectVerts > 0) {
        void* mapped = m_device->MapBuffer(m_rrectVertexBuffers[fi]);
        if (mapped) {
            std::memcpy(mapped, m_rrectVertices.data(),
                        totalRrectVerts * sizeof(OverlayRrectVertex));
            m_device->UnmapBuffer(m_rrectVertexBuffers[fi]);
        } else {
            UZ_LOG(EditorText, Error, "Failed to map rrect vertex buffer");
            totalRrectVerts = 0;
        }
    }

    // ── Constant buffer ──────────────────────────────────────────────────
    OverlayCB cb{};
    cb.invScreenSizeX = 1.f / static_cast<float>(m_screenWidth);
    cb.invScreenSizeY = 1.f / static_cast<float>(m_screenHeight);

    CBAllocation alloc = cbRing.Allocate(sizeof(OverlayCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(OverlayCB));

    if (m_bindless) {
        // ── Rounded-rects first (shadows must land behind all other chrome) ──
        if (totalRrectVerts > 0 && m_rrectPipeline.IsValid()) {
            cmdList->SetPipeline(m_rrectPipeline);
            uint32_t rrectConsts[4] = {m_rrectVbSrvIndices[fi], 0,
                                        m_samplerIndex, 0};
            cmdList->SetRootConstants(0, rrectConsts, 4);
            cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                                sizeof(OverlayCB));
            cmdList->Draw(totalRrectVerts);
        }

        // ── SM6.6 bindless: vertex fetch via ResourceDescriptorHeap ──────
        // Rects use the text pipeline with font atlas + UV=(0,0) (solid
        // white pixel) so the atlas sample yields 1.0 → output = tint.
        if (totalRectVerts > 0) {
            cmdList->SetPipeline(m_textPipeline);
            uint32_t rectConsts[4] = {m_vbSrvIndices[fi], font.GetAtlasSRVIndex(),
                                       m_samplerIndex, 0};
            cmdList->SetRootConstants(0, rectConsts, 4);
            cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                                sizeof(OverlayCB));
            cmdList->Draw(totalRectVerts);
        }

        if (totalSpriteVerts > 0 && m_iconAtlasLoaded && m_spritePipeline.IsValid()) {
            cmdList->SetPipeline(m_spritePipeline);
            uint32_t spriteConsts[4] = {m_vbSrvIndices[fi], m_iconAtlasSrvIndex,
                                        m_samplerIndex, totalRectVerts};
            cmdList->SetRootConstants(0, spriteConsts, 4);
            cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                                sizeof(OverlayCB));
            cmdList->Draw(totalSpriteVerts);
        }

        if (totalTextVerts > 0) {
            cmdList->SetPipeline(m_textPipeline);
            uint32_t textConsts[4] = {m_vbSrvIndices[fi], font.GetAtlasSRVIndex(),
                                       m_samplerIndex,
                                       totalRectVerts + totalSpriteVerts};
            cmdList->SetRootConstants(0, textConsts, 4);
            cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                                sizeof(OverlayCB));
            cmdList->Draw(totalTextVerts);
        }

        if (totalMsdfVerts > 0 && m_msdfFontThisFrame != nullptr &&
            m_msdfPipeline.IsValid()) {
            cmdList->SetPipeline(m_msdfPipeline);
            uint32_t msdfConsts[4] = {m_vbSrvIndices[fi],
                                       m_msdfFontThisFrame->GetAtlasSRVIndex(),
                                       m_samplerIndex,
                                       totalRectVerts + totalSpriteVerts + totalTextVerts};
            cmdList->SetRootConstants(0, msdfConsts, 4);
            cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                                sizeof(OverlayCB));
            cmdList->Draw(totalMsdfVerts);
        }
    } else {
        // ── SM5 path ───────────────────────────────────────────────────
        cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                            sizeof(OverlayCB));

        if (totalRrectVerts > 0 && m_rrectPipeline.IsValid()) {
            cmdList->SetPipeline(m_rrectPipeline);
            cmdList->SetVertexBuffer(0, m_rrectVertexBuffers[fi]);
            cmdList->Draw(totalRrectVerts);
        }

        cmdList->SetVertexBuffer(0, m_vertexBuffers[fi]);

        if (totalRectVerts > 0) {
            cmdList->SetPipeline(m_rectPipeline);
            cmdList->Draw(totalRectVerts);
        }

        if (totalSpriteVerts > 0 && m_iconAtlasLoaded && m_spritePipeline.IsValid()) {
            cmdList->SetPipeline(m_spritePipeline);
            cmdList->SetFragmentTexture(0, m_iconAtlasTexture);
            cmdList->Draw(totalSpriteVerts, 1, totalRectVerts);
        }

        if (totalTextVerts > 0) {
            cmdList->SetPipeline(m_textPipeline);
            cmdList->SetFragmentTexture(0, font.GetAtlasTexture());
            cmdList->Draw(totalTextVerts, 1, totalRectVerts + totalSpriteVerts);
        }

        if (totalMsdfVerts > 0 && m_msdfFontThisFrame != nullptr &&
            m_msdfPipeline.IsValid()) {
            cmdList->SetPipeline(m_msdfPipeline);
            cmdList->SetFragmentTexture(0, m_msdfFontThisFrame->GetAtlasTexture());
            cmdList->Draw(totalMsdfVerts, 1,
                          totalRectVerts + totalSpriteVerts + totalTextVerts);
        }
    }
}

} // namespace uz
