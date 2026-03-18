/// UzEngine Material Expression — Factory and Pin Lookup
///
/// Implements the Create() factory that sets up correct pins per expression
/// type, and pin search helpers. Each expression type has a well-defined
/// set of inputs and outputs matching UE5 material expression semantics.

#include "material_expression.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Pin construction helpers
// ─────────────────────────────────────────────────────────────────────────────

static MatPin MakeInput(uint32_t id, const char* name, MatPinType type,
                        float d0 = 0.f, float d1 = 0.f, float d2 = 0.f, float d3 = 0.f) {
    MatPin pin;
    pin.id           = id;
    pin.name         = name;
    pin.type         = type;
    pin.direction    = MatPinDir::Input;
    pin.defaultValue = {d0, d1, d2, d3};
    return pin;
}

static MatPin MakeOutput(uint32_t id, const char* name, MatPinType type) {
    MatPin pin;
    pin.id        = id;
    pin.name      = name;
    pin.type      = type;
    pin.direction = MatPinDir::Output;
    return pin;
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression setup helpers (grouped by category)
// ─────────────────────────────────────────────────────────────────────────────

/// Sets up PBR output node with 7 material inputs and no outputs.
static void SetupPBROutput(MaterialExpression& expr) {
    expr.name = "PBR Output";
    expr.inputs = {
        MakeInput(0, "BaseColor",  MatPinType::Float3, 1.f, 1.f, 1.f),
        MakeInput(1, "Metallic",   MatPinType::Float,  0.f),
        MakeInput(2, "Roughness",  MatPinType::Float,  0.5f),
        MakeInput(3, "Normal",     MatPinType::Float3, 0.f, 0.f, 1.f),
        MakeInput(4, "Emissive",   MatPinType::Float3, 0.f, 0.f, 0.f),
        MakeInput(5, "Opacity",    MatPinType::Float,  1.f),
        MakeInput(6, "AO",         MatPinType::Float,  1.f),
    };
}

/// Sets up a constant expression with no inputs and a single output.
static void SetupScalarConstant(MaterialExpression& expr) {
    expr.name = "Scalar Constant";
    expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
    expr.scalarParams["value"] = 0.f;
}

static void SetupVector2Constant(MaterialExpression& expr) {
    expr.name = "Vector2 Constant";
    expr.outputs = {MakeOutput(0, "Value", MatPinType::Float2)};
    expr.scalarParams["x"] = 0.f;
    expr.scalarParams["y"] = 0.f;
}

static void SetupVector3Constant(MaterialExpression& expr) {
    expr.name = "Vector3 Constant";
    expr.outputs = {MakeOutput(0, "Value", MatPinType::Float3)};
    expr.scalarParams["x"] = 0.f;
    expr.scalarParams["y"] = 0.f;
    expr.scalarParams["z"] = 0.f;
}

static void SetupVector4Constant(MaterialExpression& expr) {
    expr.name = "Vector4 Constant";
    expr.outputs = {MakeOutput(0, "Value", MatPinType::Float4)};
    expr.scalarParams["x"] = 0.f;
    expr.scalarParams["y"] = 0.f;
    expr.scalarParams["z"] = 0.f;
    expr.scalarParams["w"] = 0.f;
}

static void SetupColorConstant(MaterialExpression& expr) {
    expr.name = "Color Constant";
    expr.outputs = {MakeOutput(0, "Color", MatPinType::Float3)};
    expr.scalarParams["r"] = 1.f;
    expr.scalarParams["g"] = 1.f;
    expr.scalarParams["b"] = 1.f;
}

/// Sets up a binary math op (Add, Subtract, Multiply, Divide, Min, Max, Pow).
static void SetupBinaryMath(MaterialExpression& expr, const char* name,
                            const char* inputA = "A", const char* inputB = "B") {
    expr.name = name;
    expr.inputs = {
        MakeInput(0, inputA, MatPinType::Float),
        MakeInput(1, inputB, MatPinType::Float),
    };
    expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
}

/// Sets up a unary math op (Saturate, Abs, OneMinus, Frac, Floor, Ceil, Sqrt, Normalize, Length).
static void SetupUnaryMath(MaterialExpression& expr, const char* name) {
    expr.name = name;
    expr.inputs  = {MakeInput(0, "Value", MatPinType::Float)};
    expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
}

// ─────────────────────────────────────────────────────────────────────────────
// MaterialExpression::Create
// ─────────────────────────────────────────────────────────────────────────────

MaterialExpression MaterialExpression::Create(MatExprType type, uint32_t id) {
    MaterialExpression expr;
    expr.id   = id;
    expr.type = type;

    switch (type) {

    // ── Output ──────────────────────────────────────────────────────────────
    case MatExprType::PBROutput:
        SetupPBROutput(expr);
        break;

    // ── Constants ───────────────────────────────────────────────────────────
    case MatExprType::ScalarConstant:  SetupScalarConstant(expr);  break;
    case MatExprType::Vector2Constant: SetupVector2Constant(expr); break;
    case MatExprType::Vector3Constant: SetupVector3Constant(expr); break;
    case MatExprType::Vector4Constant: SetupVector4Constant(expr); break;
    case MatExprType::ColorConstant:   SetupColorConstant(expr);   break;

    // ── Binary math ─────────────────────────────────────────────────────────
    case MatExprType::Add:      SetupBinaryMath(expr, "Add");      break;
    case MatExprType::Subtract: SetupBinaryMath(expr, "Subtract"); break;
    case MatExprType::Multiply: SetupBinaryMath(expr, "Multiply"); break;
    case MatExprType::Divide:   SetupBinaryMath(expr, "Divide");   break;
    case MatExprType::Min:      SetupBinaryMath(expr, "Min");      break;
    case MatExprType::Max:      SetupBinaryMath(expr, "Max");      break;

    case MatExprType::Pow:
        SetupBinaryMath(expr, "Pow", "Base", "Exp");
        break;

    case MatExprType::Step:
        SetupBinaryMath(expr, "Step", "Edge", "X");
        break;

    // ── Ternary math ────────────────────────────────────────────────────────
    case MatExprType::Lerp:
        expr.name = "Lerp";
        expr.inputs = {
            MakeInput(0, "A",     MatPinType::Float),
            MakeInput(1, "B",     MatPinType::Float),
            MakeInput(2, "Alpha", MatPinType::Float),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
        break;

    case MatExprType::Clamp:
        expr.name = "Clamp";
        expr.inputs = {
            MakeInput(0, "Value", MatPinType::Float),
            MakeInput(1, "Min",   MatPinType::Float, 0.f),
            MakeInput(2, "Max",   MatPinType::Float, 1.f),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
        break;

    case MatExprType::SmoothStep:
        expr.name = "SmoothStep";
        expr.inputs = {
            MakeInput(0, "Edge0", MatPinType::Float),
            MakeInput(1, "Edge1", MatPinType::Float),
            MakeInput(2, "X",     MatPinType::Float),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
        break;

    // ── Unary math ──────────────────────────────────────────────────────────
    case MatExprType::Saturate:  SetupUnaryMath(expr, "Saturate");  break;
    case MatExprType::Abs:       SetupUnaryMath(expr, "Abs");       break;
    case MatExprType::OneMinus:  SetupUnaryMath(expr, "OneMinus");  break;
    case MatExprType::Frac:      SetupUnaryMath(expr, "Frac");      break;
    case MatExprType::Floor:     SetupUnaryMath(expr, "Floor");     break;
    case MatExprType::Ceil:      SetupUnaryMath(expr, "Ceil");      break;
    case MatExprType::Sqrt:      SetupUnaryMath(expr, "Sqrt");      break;
    case MatExprType::Length:    SetupUnaryMath(expr, "Length");     break;

    case MatExprType::Normalize:
        expr.name = "Normalize";
        expr.inputs  = {MakeInput(0, "Value", MatPinType::Float3)};
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float3)};
        break;

    // ── Vector math ─────────────────────────────────────────────────────────
    case MatExprType::Dot:
        expr.name = "Dot";
        expr.inputs = {
            MakeInput(0, "A", MatPinType::Float3),
            MakeInput(1, "B", MatPinType::Float3),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
        break;

    case MatExprType::Cross:
        expr.name = "Cross";
        expr.inputs = {
            MakeInput(0, "A", MatPinType::Float3),
            MakeInput(1, "B", MatPinType::Float3),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float3)};
        break;

    // ── Texture ─────────────────────────────────────────────────────────────
    case MatExprType::TextureSample:
        expr.name = "Texture Sample";
        expr.inputs = {MakeInput(0, "UV", MatPinType::Float2)};
        expr.outputs = {
            MakeOutput(0, "RGBA", MatPinType::Float4),
            MakeOutput(1, "R",    MatPinType::Float),
            MakeOutput(2, "G",    MatPinType::Float),
            MakeOutput(3, "B",    MatPinType::Float),
            MakeOutput(4, "A",    MatPinType::Float),
        };
        expr.stringParams["texturePath"] = "";
        break;

    // ── Coordinates ─────────────────────────────────────────────────────────
    case MatExprType::TexCoord:
        expr.name = "TexCoord";
        expr.outputs = {MakeOutput(0, "UV", MatPinType::Float2)};
        expr.intParams["uvIndex"] = 0;
        break;

    case MatExprType::WorldPosition:
        expr.name = "World Position";
        expr.outputs = {MakeOutput(0, "Position", MatPinType::Float3)};
        break;

    case MatExprType::ObjectPosition:
        expr.name = "Object Position";
        expr.outputs = {MakeOutput(0, "Position", MatPinType::Float3)};
        break;

    case MatExprType::VertexNormal:
        expr.name = "Vertex Normal";
        expr.outputs = {MakeOutput(0, "Normal", MatPinType::Float3)};
        break;

    case MatExprType::VertexColor:
        expr.name = "Vertex Color";
        expr.outputs = {MakeOutput(0, "Color", MatPinType::Float4)};
        break;

    case MatExprType::CameraVector:
        expr.name = "Camera Vector";
        expr.outputs = {MakeOutput(0, "Direction", MatPinType::Float3)};
        break;

    case MatExprType::ScreenPosition:
        expr.name = "Screen Position";
        expr.outputs = {MakeOutput(0, "UV", MatPinType::Float2)};
        break;

    case MatExprType::Time:
        expr.name = "Time";
        expr.outputs = {MakeOutput(0, "Seconds", MatPinType::Float)};
        break;

    // ── Procedural ──────────────────────────────────────────────────────────
    case MatExprType::CheckerPattern:
        expr.name = "Checker Pattern";
        expr.inputs = {
            MakeInput(0, "UV",    MatPinType::Float2),
            MakeInput(1, "Scale", MatPinType::Float, 10.f),
        };
        expr.outputs = {MakeOutput(0, "Mask", MatPinType::Float)};
        break;

    case MatExprType::NoisePerlin:
        expr.name = "Perlin Noise";
        expr.inputs = {
            MakeInput(0, "UV",    MatPinType::Float2),
            MakeInput(1, "Scale", MatPinType::Float, 1.f),
        };
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
        break;

    case MatExprType::GradientLinear:
        expr.name = "Linear Gradient";
        expr.inputs  = {MakeInput(0, "UV", MatPinType::Float2)};
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
        break;

    case MatExprType::GradientRadial:
        expr.name = "Radial Gradient";
        expr.inputs  = {MakeInput(0, "UV", MatPinType::Float2)};
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
        break;

    // ── Utility ─────────────────────────────────────────────────────────────
    case MatExprType::TriPlanarMapping:
        expr.name = "Tri-Planar Mapping";
        expr.inputs = {
            MakeInput(0, "WorldPos",    MatPinType::Float3),
            MakeInput(1, "WorldNormal", MatPinType::Float3),
            MakeInput(2, "Sharpness",   MatPinType::Float, 4.f),
        };
        expr.outputs = {
            MakeOutput(0, "UV",           MatPinType::Float2),
            MakeOutput(1, "BlendWeights", MatPinType::Float3),
        };
        break;

    case MatExprType::Fresnel:
        expr.name = "Fresnel";
        expr.inputs  = {MakeInput(0, "Exponent", MatPinType::Float, 5.f)};
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
        break;

    case MatExprType::Panner:
        expr.name = "Panner";
        expr.inputs = {
            MakeInput(0, "UV",     MatPinType::Float2),
            MakeInput(1, "SpeedX", MatPinType::Float, 0.f),
            MakeInput(2, "SpeedY", MatPinType::Float, 0.f),
        };
        expr.outputs = {MakeOutput(0, "UV", MatPinType::Float2)};
        break;

    case MatExprType::ComponentMask:
        expr.name = "Component Mask";
        expr.inputs  = {MakeInput(0, "Input", MatPinType::Float4)};
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float)};
        expr.intParams["r"] = 1;
        expr.intParams["g"] = 1;
        expr.intParams["b"] = 1;
        expr.intParams["a"] = 0;
        break;

    case MatExprType::Append:
        expr.name = "Append";
        expr.inputs = {
            MakeInput(0, "A", MatPinType::Float),
            MakeInput(1, "B", MatPinType::Float),
        };
        expr.outputs = {MakeOutput(0, "Result", MatPinType::Float2)};
        break;

    // ── Parameters ──────────────────────────────────────────────────────────
    case MatExprType::ScalarParameter:
        expr.name = "Scalar Parameter";
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float)};
        expr.stringParams["paramName"] = "Param";
        expr.scalarParams["default"]   = 0.5f;
        break;

    case MatExprType::VectorParameter:
        expr.name = "Vector Parameter";
        expr.outputs = {MakeOutput(0, "Value", MatPinType::Float3)};
        expr.stringParams["paramName"] = "Param";
        expr.scalarParams["defaultR"]  = 0.f;
        expr.scalarParams["defaultG"]  = 0.f;
        expr.scalarParams["defaultB"]  = 0.f;
        break;

    case MatExprType::TextureParameter:
        expr.name = "Texture Parameter";
        expr.outputs = {MakeOutput(0, "Texture", MatPinType::Texture)};
        expr.stringParams["paramName"]   = "Texture";
        expr.stringParams["texturePath"] = "";
        break;
    }

    return expr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pin lookup
// ─────────────────────────────────────────────────────────────────────────────

MatPin* MaterialExpression::FindInput(const std::string& pinName) {
    for (auto& pin : inputs) {
        if (pin.name == pinName) return &pin;
    }
    return nullptr;
}

const MatPin* MaterialExpression::FindInput(const std::string& pinName) const {
    for (const auto& pin : inputs) {
        if (pin.name == pinName) return &pin;
    }
    return nullptr;
}

MatPin* MaterialExpression::FindOutput(const std::string& pinName) {
    for (auto& pin : outputs) {
        if (pin.name == pinName) return &pin;
    }
    return nullptr;
}

const MatPin* MaterialExpression::FindOutput(const std::string& pinName) const {
    for (const auto& pin : outputs) {
        if (pin.name == pinName) return &pin;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// MatExprType string conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Mapping table entry for type ↔ string conversion.
struct ExprTypeEntry {
    MatExprType type;
    const char* name;
};

static constexpr ExprTypeEntry kExprTypeTable[] = {
    {MatExprType::PBROutput,         "PBROutput"},
    {MatExprType::ScalarConstant,    "ScalarConstant"},
    {MatExprType::Vector2Constant,   "Vector2Constant"},
    {MatExprType::Vector3Constant,   "Vector3Constant"},
    {MatExprType::Vector4Constant,   "Vector4Constant"},
    {MatExprType::ColorConstant,     "ColorConstant"},
    {MatExprType::Add,               "Add"},
    {MatExprType::Subtract,          "Subtract"},
    {MatExprType::Multiply,          "Multiply"},
    {MatExprType::Divide,            "Divide"},
    {MatExprType::Lerp,              "Lerp"},
    {MatExprType::Clamp,             "Clamp"},
    {MatExprType::Saturate,          "Saturate"},
    {MatExprType::Abs,               "Abs"},
    {MatExprType::Pow,               "Pow"},
    {MatExprType::Sqrt,              "Sqrt"},
    {MatExprType::OneMinus,          "OneMinus"},
    {MatExprType::Frac,              "Frac"},
    {MatExprType::Floor,             "Floor"},
    {MatExprType::Ceil,              "Ceil"},
    {MatExprType::Min,               "Min"},
    {MatExprType::Max,               "Max"},
    {MatExprType::Dot,               "Dot"},
    {MatExprType::Cross,             "Cross"},
    {MatExprType::Normalize,         "Normalize"},
    {MatExprType::Length,            "Length"},
    {MatExprType::Step,              "Step"},
    {MatExprType::SmoothStep,        "SmoothStep"},
    {MatExprType::TextureSample,     "TextureSample"},
    {MatExprType::TexCoord,          "TexCoord"},
    {MatExprType::WorldPosition,     "WorldPosition"},
    {MatExprType::ObjectPosition,    "ObjectPosition"},
    {MatExprType::VertexNormal,      "VertexNormal"},
    {MatExprType::VertexColor,       "VertexColor"},
    {MatExprType::CameraVector,      "CameraVector"},
    {MatExprType::ScreenPosition,    "ScreenPosition"},
    {MatExprType::Time,              "Time"},
    {MatExprType::CheckerPattern,    "CheckerPattern"},
    {MatExprType::NoisePerlin,       "NoisePerlin"},
    {MatExprType::GradientLinear,    "GradientLinear"},
    {MatExprType::GradientRadial,    "GradientRadial"},
    {MatExprType::TriPlanarMapping,  "TriPlanarMapping"},
    {MatExprType::Fresnel,           "Fresnel"},
    {MatExprType::Panner,            "Panner"},
    {MatExprType::ComponentMask,     "ComponentMask"},
    {MatExprType::Append,            "Append"},
    {MatExprType::ScalarParameter,   "ScalarParameter"},
    {MatExprType::VectorParameter,   "VectorParameter"},
    {MatExprType::TextureParameter,  "TextureParameter"},
};

const char* MatExprTypeToString(MatExprType type) {
    for (const auto& entry : kExprTypeTable) {
        if (entry.type == type) return entry.name;
    }
    return "Unknown";
}

MatExprType MatExprTypeFromString(const char* str) {
    for (const auto& entry : kExprTypeTable) {
        if (std::strcmp(entry.name, str) == 0) return entry.type;
    }
    return MatExprType::PBROutput;
}

} // namespace uz
