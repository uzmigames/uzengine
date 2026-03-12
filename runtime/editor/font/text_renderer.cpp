/// UzEngine Text Renderer Implementation
///
/// Batches 2D quads (text glyphs + colored rectangles) into a single
/// StructuredBuffer and draws with two pipelines: opaque rects, then
/// alpha-blended text.

#include "text_renderer.h"
#include "constant_buffers.h"
#include "engine_shaders.h"
#include "shader_pipeline.h"
#include "log.h"

#include <cstring>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(EditorText);
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

    if (vsBytecode.empty() || textPsBytecode.empty() || rectPsBytecode.empty()) {
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

    // SM5 input layout: matches overlay_vs.hlsl VSInput {POSITION, TEXCOORD0, COLOR0}
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

        if (!bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlayRectPipeline";
        m_rectPipeline = device->CreateGraphicsPipeline(desc);
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

        if (!bindless) {
            desc.inputElements = overlayInputElements;
            desc.inputElementCount = 3;
            desc.vertexStride = sizeof(OverlayVertex);
        }

        desc.debugName = "OverlayTextPipeline";
        m_textPipeline = device->CreateGraphicsPipeline(desc);
    }

    // ── Vertex buffers (one per frame in flight, Upload heap) ──────────
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        RHIBufferDesc vbDesc{};
        vbDesc.sizeBytes = kMaxVertices * sizeof(OverlayVertex);
        vbDesc.stride = sizeof(OverlayVertex);
        vbDesc.usage = RHIBufferUsage::StructuredBuffer;
        vbDesc.memory = RHIMemoryType::Upload;
        vbDesc.debugName = "OverlayVertexBuffer";

        m_vertexBuffers[i] = device->CreateBuffer(vbDesc);
        m_vbSrvIndices[i] = device->GetBufferSRVIndex(m_vertexBuffers[i]);
    }

    // ── Sampler (linear filtering for font atlas) ────────────────────────
    RHISamplerDesc sampDesc{};
    sampDesc.filter = RHIFilterMode::Linear;
    sampDesc.addressU = RHIAddressMode::Clamp;
    sampDesc.addressV = RHIAddressMode::Clamp;
    sampDesc.debugName = "FontAtlasSampler";

    m_sampler = device->CreateSampler(sampDesc);
    m_samplerIndex = device->GetSamplerIndex(m_sampler);

    if (!m_rectPipeline.IsValid() || !m_textPipeline.IsValid() || !m_vertexBuffers[0].IsValid()) {
        UZ_LOG(EditorText, Error, "Failed to create overlay pipeline resources");
        return false;
    }

    UZ_LOG(EditorText, Info, "Text renderer initialized (maxVerts={}, screen={}x{}, bindless={})",
           kMaxVertices, screenWidth, screenHeight, bindless);
    return true;
}

void TextRenderer::Shutdown() {
    if (!m_device) return;
    m_device->DestroySampler(m_sampler);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        m_device->DestroyBuffer(m_vertexBuffers[i]);
    }
    // Pipelines/shaders cleaned up by device destruction
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame API
// ─────────────────────────────────────────────────────────────────────────────

void TextRenderer::BeginFrame(uint32_t frameIndex) {
    m_rectVertices.clear();
    m_textVertices.clear();
    m_currentFrame = frameIndex % kFramesInFlight;
}

void TextRenderer::SetScreenSize(uint32_t width, uint32_t height) {
    m_screenWidth = width;
    m_screenHeight = height;
}

void TextRenderer::AddText(const BitmapFont& font, const std::string& text,
                           float x, float y, const Vec4& color) {
    float cursorX = x;
    float cursorY = y + font.GetPixelHeight(); // Baseline offset

    for (char ch : text) {
        if (ch == '\n') {
            cursorX = x;
            cursorY += font.GetLineHeight();
            continue;
        }

        const GlyphInfo* g = font.GetGlyph(ch);
        if (!g) continue;

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

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void TextRenderer::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                          const BitmapFont& font) {
    uint32_t totalRectVerts = static_cast<uint32_t>(m_rectVertices.size());
    uint32_t totalTextVerts = static_cast<uint32_t>(m_textVertices.size());
    uint32_t totalVerts = totalRectVerts + totalTextVerts;

    if (totalVerts == 0) return;
    if (totalVerts > kMaxVertices) {
        UZ_LOG(EditorText, Warning, "Overlay vertex overflow: {} > {} (clamping)",
               totalVerts, kMaxVertices);
        // Clamp text vertices to fit
        totalTextVerts = kMaxVertices - totalRectVerts;
        totalVerts = kMaxVertices;
    }

    // ── Upload vertices to current frame's buffer ──────────────────────
    // Layout: [text vertices][rect vertices] — text at offset 0 for stable SV_VertexID
    uint32_t fi = m_currentFrame;
    void* mapped = m_device->MapBuffer(m_vertexBuffers[fi]);
    if (!mapped) {
        UZ_LOG(EditorText, Error, "Failed to map overlay vertex buffer");
        return;
    }

    // Layout: [rects][text] — rects at offset 0 (firstVertex=0), text after
    auto* dst = static_cast<OverlayVertex*>(mapped);
    if (totalRectVerts > 0) {
        std::memcpy(dst, m_rectVertices.data(), totalRectVerts * sizeof(OverlayVertex));
    }
    if (totalTextVerts > 0) {
        std::memcpy(dst + totalRectVerts, m_textVertices.data(),
                    totalTextVerts * sizeof(OverlayVertex));
    }

    // ── Constant buffer ──────────────────────────────────────────────────
    OverlayCB cb{};
    cb.invScreenSizeX = 1.f / static_cast<float>(m_screenWidth);
    cb.invScreenSizeY = 1.f / static_cast<float>(m_screenHeight);

    CBAllocation alloc = cbRing.Allocate(sizeof(OverlayCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(OverlayCB));

    if (m_bindless) {
        // ── SM6.6 bindless: vertex fetch via ResourceDescriptorHeap ──────
        if (totalRectVerts > 0) {
            cmdList->SetPipeline(m_rectPipeline);
            uint32_t rectConsts[4] = {m_vbSrvIndices[fi], 0, 0, 0};
            cmdList->SetRootConstants(0, rectConsts, 4, 0);
            cmdList->SetRootCBV(1, alloc.gpuAddr);
            cmdList->Draw(totalRectVerts);
        }

        if (totalTextVerts > 0) {
            cmdList->SetPipeline(m_textPipeline);
            uint32_t textConsts[4] = {m_vbSrvIndices[fi], font.GetAtlasSRVIndex(), m_samplerIndex, totalRectVerts};
            cmdList->SetRootConstants(0, textConsts, 4, 0);
            cmdList->SetRootCBV(1, alloc.gpuAddr);
            cmdList->Draw(totalTextVerts);
        }
    } else {
        // ── SM5: vertex data via Input Assembler, textures via descriptor table ──
        cmdList->SetVertexBuffer(0, m_vertexBuffers[fi]);
        cmdList->SetRootCBV(1, alloc.gpuAddr);

        if (totalRectVerts > 0) {
            cmdList->SetPipeline(m_rectPipeline);
            cmdList->Draw(totalRectVerts);
        }

        if (totalTextVerts > 0) {
            cmdList->SetPipeline(m_textPipeline);
            // Bind font atlas at t0 via descriptor table (root param 3)
            cmdList->SetDescriptorTable(3, font.GetAtlasSRVIndex());
            cmdList->Draw(totalTextVerts, 1, totalRectVerts);
        }
    }
}

} // namespace uz
