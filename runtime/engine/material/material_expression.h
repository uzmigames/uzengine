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
    PBROutput = 0, ///< Final PBR output node connecting to the material surface.

    // ── Constants ──
    ScalarConstant = 100, ///< Single float constant value.
    Vector2Constant,      ///< 2-component vector constant (UV, direction).
    Vector3Constant,      ///< 3-component vector constant (color, position).
    Vector4Constant,      ///< 4-component vector constant (color+alpha).
    ColorConstant,        ///< sRGB color constant with alpha.

    // ── Math ──
    Add = 200,   ///< Component-wise addition (A + B).
    Subtract,    ///< Component-wise subtraction (A - B).
    Multiply,    ///< Component-wise multiplication (A * B).
    Divide,      ///< Component-wise division (A / B).
    Lerp,        ///< Linear interpolation (lerp(A, B, Alpha)).
    Clamp,       ///< Clamp value between min and max.
    Saturate,    ///< Clamp to [0, 1] range.
    Abs,         ///< Absolute value.
    Pow,         ///< Power (Base ^ Exponent).
    Sqrt,        ///< Square root.
    OneMinus,    ///< One minus input (1 - X).
    Frac,        ///< Fractional part (frac(X)).
    Floor,       ///< Round down to nearest integer.
    Ceil,        ///< Round up to nearest integer.
    Min,         ///< Component-wise minimum of two inputs.
    Max,         ///< Component-wise maximum of two inputs.
    Dot,         ///< Dot product of two vectors.
    Cross,       ///< Cross product of two 3-component vectors.
    Normalize,   ///< Normalize vector to unit length.
    Length,      ///< Vector length (magnitude).
    Step,        ///< Step function (0 if X < Edge, else 1).
    SmoothStep,  ///< Hermite interpolation between two edges.

    // ── Texture ──
    TextureSample = 300, ///< Sample a 2D texture at given UV coordinates.

    // ── Coordinates ──
    TexCoord = 400,  ///< Mesh UV texture coordinates (channel selectable).
    WorldPosition,   ///< World-space position of the shaded pixel.
    ObjectPosition,  ///< Object-space (local) position of the shaded pixel.
    VertexNormal,    ///< Interpolated vertex normal in world space.
    VertexColor,     ///< Per-vertex color attribute.
    CameraVector,    ///< Normalized direction from pixel to camera.
    ScreenPosition,  ///< Screen-space UV position [0,1].
    Time,            ///< Global engine time in seconds.

    // ── Procedural ──
    CheckerPattern = 500, ///< Procedural black-and-white checkerboard pattern.
    NoisePerlin,          ///< Perlin noise generator.
    GradientLinear,       ///< Linear gradient along a configurable axis.
    GradientRadial,       ///< Radial gradient from center outward.

    // ── Utility ──
    TriPlanarMapping = 600, ///< Tri-planar texture projection (avoids UV seams on non-planar meshes).
    Fresnel,                ///< Fresnel effect based on view angle (rim lighting).
    Panner,                 ///< Animated UV offset (scrolling textures).
    ComponentMask,          ///< Select/swizzle individual RGBA components.
    Append,                 ///< Concatenate two vectors (e.g., Float2 + Float -> Float3).

    // ── Parameters (runtime-tweakable per MaterialInstance) ──
    ScalarParameter = 700, ///< Exposed scalar parameter for MaterialInstance override.
    VectorParameter,       ///< Exposed vector parameter for MaterialInstance override.
    TextureParameter,      ///< Exposed texture parameter for MaterialInstance override.
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
