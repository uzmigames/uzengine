/// UzEngine HLSL Material Translator — compiles MaterialGraph to HLSL.
/// HLSL material translator pattern. NOT thread-safe. Create one per compilation.

#pragma once

#include "engine/material/material_graph.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Compilation Result
// ─────────────────────────────────────────────────────────────────────────────

/// Result of compiling a MaterialGraph to HLSL.
struct MaterialCompileResult {
    /// Context inputs read by emitted, output-reachable expressions.
    enum ContextBits : uint32_t {
        VertexColor = 1u << 0,
        Camera = 1u << 1,
        ObjectPosition = 1u << 2,
        ScreenPosition = 1u << 3,
        Time = 1u << 4,
    };

    bool success = false;
    std::string hlslSource;    ///< Generated HLSL body (ComputeMaterialOutputs function)
    std::string errorMessage;  ///< Error description if !success
    uint64_t contentHash = 0;  ///< Graph content hash for cache keying
    uint32_t textureSlotCount = 0;  ///< Number of texture slots used
    uint32_t parameterCount = 0;    ///< Number of material parameters exposed
    uint32_t contextMask = 0;       ///< OR of ContextBits; includes implicit Fresnel camera/Panner time reads.

    /// Parameter metadata for runtime binding.
    struct ParamInfo {
        std::string name;
        MatPinType type;
        uint32_t cbSlotIndex = 0;  ///< Index into materialParams[] CB array
        float defaultValue[4] = {0.f, 0.f, 0.f, 0.f};
    };
    std::vector<ParamInfo> parameters;

    /// Texture slot metadata.
    struct TextureSlotInfo {
        std::string paramName;    ///< Empty for non-parameter textures
        std::string defaultPath;  ///< Default texture asset path
        uint32_t slotIndex = 0;
        bool isSRGB = false;      ///< True for color slots (Albedo/Emissive/Diffuse), false for data slots (Normal/MetalRoughness/AO/ORM)
    };
    std::vector<TextureSlotInfo> textureSlots;
};

// ─────────────────────────────────────────────────────────────────────────────
// Translator
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles a MaterialGraph into a `ComputeMaterialOutputs()` HLSL function.
/// Compiles a material expression graph into HLSL source.
class HlslMaterialTranslator {
public:
    /// Compile a material graph to HLSL.
    [[nodiscard]] MaterialCompileResult Compile(const MaterialGraph& graph);

private:
    /// Emit HLSL code for a single expression. Returns the variable name prefix (_nodeN).
    std::string EmitExpression(const MaterialExpression& expr);

    /// Get the HLSL expression for an input pin value (either connected node output or default).
    [[nodiscard]] std::string GetInputValue(const MatPin& inputPin, MatPinType desiredType);

    /// Type promotion: convert an HLSL expression from srcType to dstType.
    [[nodiscard]] std::string PromoteType(const std::string& expr,
                                          MatPinType srcType, MatPinType dstType) const;

    /// Generate a unique variable name for an expression output.
    [[nodiscard]] std::string VarName(uint32_t exprId, uint32_t pinId) const;

    /// Allocate a texture slot index and return it.
    uint32_t AllocateTextureSlot(const std::string& paramName, const std::string& defaultPath, bool isSRGB = false);

    /// Allocate a parameter CB slot and return the index.
    uint32_t AllocateParamSlot(const std::string& name, MatPinType type, const float* defaultVal);

    /// Format a float literal for HLSL (avoids scientific notation, ensures decimal point).
    [[nodiscard]] static std::string FloatLiteral(float value);

    /// Emit a line of HLSL code with proper indentation.
    void EmitLine(const std::string& line);

    /// Determine the output type of a ComponentMask given its channel selection.
    [[nodiscard]] static MatPinType ComponentMaskOutputType(int r, int g, int b, int a);

    /// Count selected channels in a ComponentMask.
    [[nodiscard]] static uint32_t CountMaskChannels(int r, int g, int b, int a);

    // ── State (reset per Compile call) ──
    std::string code_;                           ///< Accumulated HLSL lines
    std::unordered_map<uint32_t, bool> emitted_; ///< Memoization: already-emitted expression IDs
    const MaterialGraph* graph_ = nullptr;
    std::vector<uint32_t> sortedOrder_;
    uint32_t nextTextureSlot_ = 0;
    uint32_t nextParamSlot_ = 0;
    MaterialCompileResult result_;
};

} // namespace uz
