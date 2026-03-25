/// VFX Sprite Render Module implementation -- billboard size and color evaluation.

#include "sprite_render_module.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code -- HLSL snippet for sprite size/color evaluation
// ---------------------------------------------------------------------------

static constexpr const char* kSpriteRenderShaderCode = R"hlsl(
// --- SpriteRenderModule ---
float ageNorm = Ages[particleIdx] / max(Lifespans[particleIdx], 0.001f);
float size = cbSpriteWidth;
float4 color = Colors[particleIdx];
Sizes[particleIdx] = size;
Colors[particleIdx] = color;

// --- Flipbook / SubUV parameters ---
// These constants are consumed by the billboard pixel shader (task 2.3)
// when sampling the sprite sheet atlas with vfx_flipbook.hlsli functions.
uint  fbRows        = cbFlipbookRows;
uint  fbCols        = cbFlipbookCols;
uint  fbTotalFrames = cbFlipbookTotalFrames;
float fbFPS         = cbFlipbookFPS;
uint  fbBlendMode   = cbFlipbookBlendMode;
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's unique name for registration and serialization.
const char* SpriteRenderModule::GetName() const noexcept {
    return "sprite_render";
}

/// Returns the HLSL snippet for evaluating per-particle sprite size and color.
/// Computes normalized age for future curve sampling. Currently uses flat
/// cbSpriteWidth and passes through the existing color attribute.
std::string_view SpriteRenderModule::GetShaderCode() const {
    return kSpriteRenderShaderCode;
}

/// Declares sprite rendering parameters for editor UI and constant buffer layout.
std::vector<ParameterDef> SpriteRenderModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(8);

    {
        ParameterDef p;
        p.name = "sprite_width";
        p.type = ParameterType::Float;
        p.defaultFloat = spriteWidth;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "sprite_height";
        p.type = ParameterType::Float;
        p.defaultFloat = spriteHeight;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "depth_sort";
        p.type = ParameterType::Bool;
        p.defaultBool = depthSort;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "flipbook_rows";
        p.type = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(flipbookRows);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "flipbook_cols";
        p.type = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(flipbookCols);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "flipbook_total_frames";
        p.type = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(flipbookTotalFrames);
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "flipbook_fps";
        p.type = ParameterType::Float;
        p.defaultFloat = flipbookFPS;
        params.push_back(p);
    }
    {
        ParameterDef p;
        p.name = "flipbook_blend_mode";
        p.type = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(flipbookBlendMode);
        params.push_back(p);
    }

    return params;
}

/// No-op on CPU side. Sprite configuration is consumed by the particle
/// render pass when building draw commands.
void SpriteRenderModule::Execute(const ExecuteContext& /*ctx*/) {
    // Rendering configuration -- consumed by particle render pass, not here.
}

} // namespace uz
