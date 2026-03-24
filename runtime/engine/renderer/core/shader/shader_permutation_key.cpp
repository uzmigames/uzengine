/// UzEngine Shader Permutation Key -- implementation.

#include "shader_permutation_key.h"

#include "engine/renderer/material/material_interface.h"

#include <sstream>

namespace uz {

// ---------------------------------------------------------------------------
// FromMaterial -- factory that reads material properties into a packed key
// ---------------------------------------------------------------------------

ShaderPermutationKey ShaderPermutationKey::FromMaterial(
    const MaterialInterface& mat,
    bool bindless,
    bool instanced,
    bool hasUV1,
    bool hasUV2)
{
    const EShadingModel shadingModel = mat.GetShadingModel();
    const EBlendMode    blendMode    = mat.GetBlendMode();

    // POM is enabled when the material has a displacement/heightmap texture bound.
    // The texture slot name follows the convention used in material_asset.h.
    const bool pom = !mat.GetTexturePath("Displacement").empty()
                  || !mat.GetTexturePath("Heightmap").empty();

    // Material graph flag -- not yet exposed on MaterialInterface. Reserved for
    // future node-graph materials. Always false for fixed-function materials.
    const bool materialGraph = false;

    // Alpha-to-coverage is used with masked blend mode on MSAA targets.
    // The material signals masked via blend mode; the caller should set this
    // when MSAA is active, but we derive a reasonable default here.
    const bool alphaToCoverage = (blendMode == EBlendMode::Masked);

    // Dithered opacity -- used for LOD cross-fade. Not yet exposed on
    // MaterialInterface; driven by the draw command's LOD transition state.
    const bool ditheredOpacity = false;

    // Tangent map is considered present when a normal map texture is bound.
    const bool hasTangentMap = !mat.GetTexturePath("Normal").empty();

    return ShaderPermutationKey(
        shadingModel, blendMode,
        bindless, instanced, pom, materialGraph,
        alphaToCoverage, ditheredOpacity,
        hasUV1, hasUV2, hasTangentMap);
}

// ---------------------------------------------------------------------------
// ToDefineList -- generates DXC preprocessor defines
// ---------------------------------------------------------------------------

std::vector<ShaderDefine> ShaderPermutationKey::ToDefineList() const
{
    std::vector<ShaderDefine> defines;
    defines.reserve(11);

    defines.push_back({"UZ_SHADING_MODEL",    std::to_string(static_cast<int>(GetShadingModel()))});
    defines.push_back({"UZ_BLEND_MODE",        std::to_string(static_cast<int>(GetBlendMode()))});
    defines.push_back({"UZ_BINDLESS",          IsBindless()        ? "1" : "0"});
    defines.push_back({"UZ_INSTANCED",         IsInstanced()       ? "1" : "0"});
    defines.push_back({"UZ_POM",               IsPom()             ? "1" : "0"});
    defines.push_back({"UZ_MATERIAL_GRAPH",    IsMaterialGraph()   ? "1" : "0"});
    defines.push_back({"UZ_ALPHA_TO_COVERAGE", IsAlphaToCoverage() ? "1" : "0"});
    defines.push_back({"UZ_DITHERED_OPACITY",  IsDitheredOpacity() ? "1" : "0"});
    defines.push_back({"UZ_HAS_UV1",           HasUV1()            ? "1" : "0"});
    defines.push_back({"UZ_HAS_UV2",           HasUV2()            ? "1" : "0"});
    defines.push_back({"UZ_HAS_TANGENT_MAP",   HasTangentMap()     ? "1" : "0"});

    return defines;
}

// ---------------------------------------------------------------------------
// ToString -- human-readable debug string
// ---------------------------------------------------------------------------

std::string ShaderPermutationKey::ToString() const
{
    std::ostringstream oss;
    oss << ShadingModelToString(GetShadingModel())
        << '|' << BlendModeToString(GetBlendMode());

    if (IsBindless())        oss << "|Bindless";
    if (IsInstanced())       oss << "|Instanced";
    if (IsPom())             oss << "|POM";
    if (IsMaterialGraph())   oss << "|MaterialGraph";
    if (IsAlphaToCoverage()) oss << "|AlphaToCoverage";
    if (IsDitheredOpacity()) oss << "|DitheredOpacity";
    if (HasUV1())            oss << "|UV1";
    if (HasUV2())            oss << "|UV2";
    if (HasTangentMap())     oss << "|TangentMap";

    return oss.str();
}

} // namespace uz
