/// UzEngine Material Expression — Base Node for Material Graph
///
/// An expression is a pure data container — it stores pins, parameters,
/// and connection topology but has NO compilation logic. The material
/// compiler (HlslMaterialTranslator) visits expressions to emit HLSL.
///
/// This follows the reference engine's pattern where UMaterialExpression holds pin metadata
/// and the FHLSLMaterialTranslator walks the expression tree to generate
/// shader code.
///
/// Material expression node pattern (graph-based material authoring)
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Pin Types
// ─────────────────────────────────────────────────────────────────────────────

/// Pin data type for material graph connections.
enum class MatPinType : uint8_t {
    Float,    ///< Scalar (1 component)
    Float2,   ///< 2-component vector
    Float3,   ///< 3-component vector (color, normal)
    Float4,   ///< 4-component vector (color+alpha)
    Texture,  ///< Texture object reference
};

/// Pin direction.
enum class MatPinDir : uint8_t { Input, Output };

/// A single pin (input or output) on a material expression.
struct MatPin {
    uint32_t    id        = 0;       ///< Pin index local to the expression (0, 1, 2, ...)
    std::string name;                ///< Display name ("UV", "Scale", "Result")
    MatPinType  type      = MatPinType::Float;
    MatPinDir   direction = MatPinDir::Input;
    std::array<float, 4> defaultValue = {0.f, 0.f, 0.f, 0.f};

    /// Connection source (for input pins only). 0 = unconnected.
    uint32_t connectedExprId = 0;
    uint32_t connectedPinId  = 0;

    /// Returns true if this input pin has a connection.
    [[nodiscard]] bool IsConnected() const { return connectedExprId != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Expression Type Enum
// ─────────────────────────────────────────────────────────────────────────────

/// Expression type enum. Groups match reference material expression categories.
enum class MatExprType : uint16_t {
    // ── Output ──
    PBROutput = 0,

    // ── Constants ──
    ScalarConstant = 100,
    Vector2Constant,
    Vector3Constant,
    Vector4Constant,
    ColorConstant,

    // ── Math ──
    Add = 200, Subtract, Multiply, Divide,
    Lerp, Clamp, Saturate, Abs, Pow, Sqrt,
    OneMinus, Frac, Floor, Ceil, Min, Max,
    Dot, Cross, Normalize, Length, Step, SmoothStep,

    // ── Texture ──
    TextureSample = 300,

    // ── Coordinates ──
    TexCoord = 400,
    WorldPosition,
    ObjectPosition,
    VertexNormal,
    VertexColor,
    CameraVector,
    ScreenPosition,
    Time,

    // ── Procedural ──
    CheckerPattern = 500,
    NoisePerlin,
    GradientLinear,
    GradientRadial,

    // ── Utility ──
    TriPlanarMapping = 600,
    Fresnel,
    Panner,
    ComponentMask,
    Append,

    // ── Parameters (runtime-tweakable per MaterialInstance) ──
    ScalarParameter = 700,
    VectorParameter,
    TextureParameter,
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialExpression
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for all material expressions (nodes).
///
/// An expression is a pure data container — it stores pins, parameters,
/// and connection topology. It has NO compilation logic. The material
/// compiler (HlslMaterialTranslator) visits expressions to emit HLSL.
///
/// Material expression node pattern (graph-based material authoring)
struct MaterialExpression {
    uint32_t    id   = 0;            ///< Unique ID within the graph (1, 2, 3, ...)
    MatExprType type = MatExprType::PBROutput;
    std::string name;                ///< Display name for editor
    float       posX = 0.f;          ///< Editor canvas position X
    float       posY = 0.f;          ///< Editor canvas position Y

    std::vector<MatPin> inputs;
    std::vector<MatPin> outputs;

    /// Node-specific scalar parameters (e.g., "scale" for checker, "default" for ScalarParameter).
    std::unordered_map<std::string, float> scalarParams;

    /// Node-specific string parameters (e.g., "texturePath", "paramName").
    std::unordered_map<std::string, std::string> stringParams;

    /// Node-specific integer parameters (e.g., mask channel selection).
    std::unordered_map<std::string, int32_t> intParams;

    /// Factory: creates an expression with the correct pins for the given type.
    [[nodiscard]] static MaterialExpression Create(MatExprType type, uint32_t id);

    /// Find an input pin by name. Returns nullptr if not found.
    [[nodiscard]] MatPin* FindInput(const std::string& pinName);
    [[nodiscard]] const MatPin* FindInput(const std::string& pinName) const;

    /// Find an output pin by name. Returns nullptr if not found.
    [[nodiscard]] MatPin* FindOutput(const std::string& pinName);
    [[nodiscard]] const MatPin* FindOutput(const std::string& pinName) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pin Type Utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the number of components for a pin type (1, 2, 3, or 4).
[[nodiscard]] inline uint32_t MatPinTypeComponents(MatPinType t) {
    switch (t) {
        case MatPinType::Float:   return 1;
        case MatPinType::Float2:  return 2;
        case MatPinType::Float3:  return 3;
        case MatPinType::Float4:  return 4;
        case MatPinType::Texture: return 0;
    }
    return 0;
}

/// Returns the HLSL type name for a pin type.
[[nodiscard]] inline const char* MatPinTypeToHlsl(MatPinType t) {
    switch (t) {
        case MatPinType::Float:   return "float";
        case MatPinType::Float2:  return "float2";
        case MatPinType::Float3:  return "float3";
        case MatPinType::Float4:  return "float4";
        case MatPinType::Texture: return "Texture2D<float4>";
    }
    return "float";
}

/// Returns a human-readable string for a MatExprType value.
[[nodiscard]] const char* MatExprTypeToString(MatExprType type);

/// Parses a MatExprType from its string name. Returns PBROutput on unknown input.
[[nodiscard]] MatExprType MatExprTypeFromString(const char* str);

} // namespace uz
