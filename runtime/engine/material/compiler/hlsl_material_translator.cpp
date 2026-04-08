/// HLSL Material Translator — graph-to-HLSL compilation

#include "hlsl_material_translator.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string HlslMaterialTranslator::FloatLiteral(float value) {
    // Handle special values that would produce invalid HLSL.
    if (std::isnan(value)) return "0.0";
    if (std::isinf(value)) return value > 0.f ? "3.402823466e+38" : "-3.402823466e+38";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    std::string s(buf);
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

void HlslMaterialTranslator::EmitLine(const std::string& line) {
    code_ += "    ";
    code_ += line;
    code_ += "\n";
}

std::string HlslMaterialTranslator::VarName(uint32_t exprId, uint32_t pinId) const {
    return "_node" + std::to_string(exprId) + "_out" + std::to_string(pinId);
}

MatPinType HlslMaterialTranslator::ComponentMaskOutputType(int r, int g, int b, int a) {
    const uint32_t count = CountMaskChannels(r, g, b, a);
    switch (count) {
        case 1: return MatPinType::Float;
        case 2: return MatPinType::Float2;
        case 3: return MatPinType::Float3;
        default: return MatPinType::Float4;
    }
}

uint32_t HlslMaterialTranslator::CountMaskChannels(int r, int g, int b, int a) {
    return static_cast<uint32_t>(r != 0) + static_cast<uint32_t>(g != 0) +
           static_cast<uint32_t>(b != 0) + static_cast<uint32_t>(a != 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Type Promotion
// ─────────────────────────────────────────────────────────────────────────────

std::string HlslMaterialTranslator::PromoteType(const std::string& expr,
                                                 MatPinType srcType,
                                                 MatPinType dstType) const {
    if (srcType == dstType) return expr;

    const uint32_t srcComp = MatPinTypeComponents(srcType);
    const uint32_t dstComp = MatPinTypeComponents(dstType);
    const char* dstHlsl = MatPinTypeToHlsl(dstType);

    // Scalar splat: float -> floatN (splat)
    if (srcComp == 1 && dstComp > 1) {
        return std::string(dstHlsl) + "(" + expr + ", " + expr +
               (dstComp >= 3 ? ", " + expr : "") +
               (dstComp >= 4 ? ", " + expr : "") + ")";
    }

    // Truncation: floatN -> float (take .x component)
    if (srcComp > 1 && dstComp == 1) {
        return "(" + expr + ").x";
    }

    // Extend: float2 -> float3 (append 0), float2 -> float4 (append 0,0), float3 -> float4 (append 1)
    if (srcComp < dstComp) {
        std::string result = std::string(dstHlsl) + "(" + expr;
        for (uint32_t i = srcComp; i < dstComp; ++i) {
            // Last component of float4 defaults to 1 (alpha), others to 0.
            const float fill = (dstType == MatPinType::Float4 && i == 3) ? 1.f : 0.f;
            result += ", " + FloatLiteral(fill);
        }
        result += ")";
        return result;
    }

    // Shrink: float4 -> float3 (.xyz), float4 -> float2 (.xy), float3 -> float2 (.xy)
    if (srcComp > dstComp && dstComp > 1) {
        static const char* kSwizzles[] = {"", ".x", ".xy", ".xyz"};
        return "(" + expr + ")" + kSwizzles[dstComp];
    }

    return expr;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetInputValue
// ─────────────────────────────────────────────────────────────────────────────

std::string HlslMaterialTranslator::GetInputValue(const MatPin& inputPin, MatPinType desiredType) {
    if (inputPin.IsConnected()) {
        const auto* srcExpr = graph_->FindExpression(inputPin.connectedExprId);
        if (!srcExpr) {
            UZ_LOG(Material, Error, "Dangling connection: expr {} pin {} -> missing expr {}",
                   0, inputPin.id, inputPin.connectedExprId);
            return FloatLiteral(inputPin.defaultValue[0]);
        }

        // Find the connected output pin to know its type.
        const MatPin* srcPin = nullptr;
        for (const auto& pin : srcExpr->outputs) {
            if (pin.id == inputPin.connectedPinId) {
                srcPin = &pin;
                break;
            }
        }

        if (!srcPin) {
            UZ_LOG(Material, Error, "Connected output pin {} not found on expr {}",
                   inputPin.connectedPinId, srcExpr->id);
            return FloatLiteral(inputPin.defaultValue[0]);
        }

        const std::string var = VarName(inputPin.connectedExprId, inputPin.connectedPinId);
        return PromoteType(var, srcPin->type, desiredType);
    }

    // Unconnected — emit default value literal.
    const auto& d = inputPin.defaultValue;
    switch (desiredType) {
        case MatPinType::Float:
            return FloatLiteral(d[0]);
        case MatPinType::Float2:
            return "float2(" + FloatLiteral(d[0]) + ", " + FloatLiteral(d[1]) + ")";
        case MatPinType::Float3:
            return "float3(" + FloatLiteral(d[0]) + ", " + FloatLiteral(d[1]) + ", " +
                   FloatLiteral(d[2]) + ")";
        case MatPinType::Float4:
            return "float4(" + FloatLiteral(d[0]) + ", " + FloatLiteral(d[1]) + ", " +
                   FloatLiteral(d[2]) + ", " + FloatLiteral(d[3]) + ")";
        case MatPinType::Texture:
            return "0";  // Invalid — texture pins must be connected.
    }
    return "0.0";
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture / Parameter Slot Allocation
// ─────────────────────────────────────────────────────────────────────────────

uint32_t HlslMaterialTranslator::AllocateTextureSlot(const std::string& paramName,
                                                      const std::string& defaultPath) {
    const uint32_t slot = nextTextureSlot_++;
    MaterialCompileResult::TextureSlotInfo info;
    info.paramName = paramName;
    info.defaultPath = defaultPath;
    info.slotIndex = slot;
    result_.textureSlots.push_back(std::move(info));
    return slot;
}

uint32_t HlslMaterialTranslator::AllocateParamSlot(const std::string& name, MatPinType type,
                                                     const float* defaultVal) {
    const uint32_t slot = nextParamSlot_++;
    MaterialCompileResult::ParamInfo info;
    info.name = name;
    info.type = type;
    info.cbSlotIndex = slot;
    info.defaultValue[0] = defaultVal[0];
    info.defaultValue[1] = defaultVal[1];
    info.defaultValue[2] = defaultVal[2];
    info.defaultValue[3] = defaultVal[3];
    result_.parameters.push_back(std::move(info));
    return slot;
}

// ─────────────────────────────────────────────────────────────────────────────
// EmitExpression
// ─────────────────────────────────────────────────────────────────────────────

std::string HlslMaterialTranslator::EmitExpression(const MaterialExpression& expr) {
    // Memoization: skip if already emitted.
    if (emitted_.count(expr.id)) return VarName(expr.id, 0);

    emitted_[expr.id] = true;
    const std::string prefix = "_node" + std::to_string(expr.id);

    // Ensure all input dependencies are emitted first.
    for (const auto& pin : expr.inputs) {
        if (pin.IsConnected()) {
            const auto* srcExpr = graph_->FindExpression(pin.connectedExprId);
            if (srcExpr) EmitExpression(*srcExpr);
        }
    }

    auto getScalar = [&](const char* key, float fallback) -> float {
        const auto it = expr.scalarParams.find(key);
        return (it != expr.scalarParams.end()) ? it->second : fallback;
    };

    auto getString = [&](const char* key) -> std::string {
        const auto it = expr.stringParams.find(key);
        return (it != expr.stringParams.end()) ? it->second : std::string();
    };

    auto getInt = [&](const char* key, int32_t fallback) -> int32_t {
        const auto it = expr.intParams.find(key);
        return (it != expr.intParams.end()) ? it->second : fallback;
    };

    switch (expr.type) {
    // ── PBROutput ────────────────────────────────────────────────────────────
    // Handled separately in Compile() after all other expressions.
    case MatExprType::PBROutput:
        break;

    // ── Constants ────────────────────────────────────────────────────────────
    case MatExprType::ScalarConstant: {
        const float val = getScalar("value", 0.f);
        EmitLine("float " + VarName(expr.id, 0) + " = " + FloatLiteral(val) + ";");
        break;
    }

    case MatExprType::Vector2Constant: {
        const float x = getScalar("x", 0.f);
        const float y = getScalar("y", 0.f);
        EmitLine("float2 " + VarName(expr.id, 0) + " = float2(" +
                 FloatLiteral(x) + ", " + FloatLiteral(y) + ");");
        break;
    }

    case MatExprType::Vector3Constant: {
        const float x = getScalar("x", 0.f);
        const float y = getScalar("y", 0.f);
        const float z = getScalar("z", 0.f);
        EmitLine("float3 " + VarName(expr.id, 0) + " = float3(" +
                 FloatLiteral(x) + ", " + FloatLiteral(y) + ", " + FloatLiteral(z) + ");");
        break;
    }

    case MatExprType::Vector4Constant: {
        const float x = getScalar("x", 0.f);
        const float y = getScalar("y", 0.f);
        const float z = getScalar("z", 0.f);
        const float w = getScalar("w", 0.f);
        EmitLine("float4 " + VarName(expr.id, 0) + " = float4(" +
                 FloatLiteral(x) + ", " + FloatLiteral(y) + ", " +
                 FloatLiteral(z) + ", " + FloatLiteral(w) + ");");
        break;
    }

    case MatExprType::ColorConstant: {
        const float r = getScalar("r", 1.f);
        const float g = getScalar("g", 1.f);
        const float b = getScalar("b", 1.f);
        EmitLine("float3 " + VarName(expr.id, 0) + " = float3(" +
                 FloatLiteral(r) + ", " + FloatLiteral(g) + ", " + FloatLiteral(b) + ");");
        break;
    }

    // ── Binary Math ──────────────────────────────────────────────────────────
    case MatExprType::Add:
    case MatExprType::Subtract:
    case MatExprType::Multiply:
    case MatExprType::Divide:
    case MatExprType::Min:
    case MatExprType::Max:
    case MatExprType::Pow:
    case MatExprType::Step: {
        const MatPinType outType = expr.outputs[0].type;
        const std::string a = GetInputValue(expr.inputs[0], outType);
        const std::string b = GetInputValue(expr.inputs[1], outType);
        const std::string hlslType(MatPinTypeToHlsl(outType));
        const std::string var = VarName(expr.id, 0);

        // Operator or intrinsic function call.
        const char* op = nullptr;
        switch (expr.type) {
            case MatExprType::Add:      op = "+"; break;
            case MatExprType::Subtract: op = "-"; break;
            case MatExprType::Multiply: op = "*"; break;
            case MatExprType::Divide:   op = "/"; break;
            default: op = nullptr; break;
        }

        if (op) {
            EmitLine(hlslType + " " + var + " = " + a + " " + op + " " + b + ";");
        } else {
            // Function-style: min, max, pow, step
            const char* fn = "min";
            switch (expr.type) {
                case MatExprType::Min:  fn = "min";  break;
                case MatExprType::Max:  fn = "max";  break;
                case MatExprType::Pow:  fn = "pow";  break;
                case MatExprType::Step: fn = "step"; break;
                default: break;
            }
            EmitLine(hlslType + " " + var + " = " + fn + "(" + a + ", " + b + ");");
        }
        break;
    }

    // ── Ternary Math ─────────────────────────────────────────────────────────
    case MatExprType::Lerp:
    case MatExprType::Clamp:
    case MatExprType::SmoothStep: {
        const MatPinType outType = expr.outputs[0].type;
        const std::string p0 = GetInputValue(expr.inputs[0], outType);
        const std::string p1 = GetInputValue(expr.inputs[1], outType);
        const std::string p2 = GetInputValue(expr.inputs[2], outType);
        const char* fn = expr.type == MatExprType::Lerp ? "lerp" :
                         expr.type == MatExprType::Clamp ? "clamp" : "smoothstep";
        EmitLine(std::string(MatPinTypeToHlsl(outType)) + " " + VarName(expr.id, 0) +
                 " = " + fn + "(" + p0 + ", " + p1 + ", " + p2 + ");");
        break;
    }

    // ── Unary Math ───────────────────────────────────────────────────────────
    case MatExprType::Saturate:
    case MatExprType::Abs:
    case MatExprType::Frac:
    case MatExprType::Floor:
    case MatExprType::Ceil:
    case MatExprType::Sqrt: {
        const MatPinType outType = expr.outputs[0].type;
        const std::string val = GetInputValue(expr.inputs[0], outType);
        const char* fn = nullptr;
        switch (expr.type) {
            case MatExprType::Saturate: fn = "saturate"; break;
            case MatExprType::Abs:      fn = "abs";      break;
            case MatExprType::Frac:     fn = "frac";     break;
            case MatExprType::Floor:    fn = "floor";    break;
            case MatExprType::Ceil:     fn = "ceil";     break;
            case MatExprType::Sqrt:     fn = "sqrt";     break;
            default: fn = ""; break;
        }
        EmitLine(std::string(MatPinTypeToHlsl(outType)) + " " + VarName(expr.id, 0) +
                 " = " + fn + "(" + val + ");");
        break;
    }

    case MatExprType::OneMinus: {
        const MatPinType outType = expr.outputs[0].type;
        const std::string val = GetInputValue(expr.inputs[0], outType);
        EmitLine(std::string(MatPinTypeToHlsl(outType)) + " " + VarName(expr.id, 0) +
                 " = 1.0 - " + val + ";");
        break;
    }

    case MatExprType::Length: {
        const std::string val = GetInputValue(expr.inputs[0], expr.inputs[0].type);
        EmitLine("float " + VarName(expr.id, 0) + " = length(" + val + ");");
        break;
    }

    case MatExprType::Normalize: {
        const std::string val = GetInputValue(expr.inputs[0], MatPinType::Float3);
        EmitLine("float3 " + VarName(expr.id, 0) + " = normalize(" + val + ");");
        break;
    }

    // ── Vector Math ──────────────────────────────────────────────────────────
    case MatExprType::Dot: {
        const std::string a = GetInputValue(expr.inputs[0], MatPinType::Float3);
        const std::string b = GetInputValue(expr.inputs[1], MatPinType::Float3);
        EmitLine("float " + VarName(expr.id, 0) + " = dot(" + a + ", " + b + ");");
        break;
    }

    case MatExprType::Cross: {
        const std::string a = GetInputValue(expr.inputs[0], MatPinType::Float3);
        const std::string b = GetInputValue(expr.inputs[1], MatPinType::Float3);
        EmitLine("float3 " + VarName(expr.id, 0) + " = cross(" + a + ", " + b + ");");
        break;
    }

    // ── Texture ──────────────────────────────────────────────────────────────
    case MatExprType::TextureSample: {
        const std::string texPath = getString("texturePath");
        const uint32_t slot = AllocateTextureSlot("", texPath);
        // Default UV: use the `uv` parameter from ComputeMaterialOutputs()
        // when the UV input pin is not connected.
        const std::string uvValue = expr.inputs[0].IsConnected()
            ? GetInputValue(expr.inputs[0], MatPinType::Float2)
            : "uv";

        // Cast ResourceDescriptorHeap entry to Texture2D<float4> before sampling.
        // DXC's bindless ResourceDescriptorHeap returns a generic Resource type —
        // an explicit cast is required to call .Sample() on it.
        const std::string texVar = "_tex" + std::to_string(expr.id);
        EmitLine("Texture2D<float4> " + texVar +
                 " = ResourceDescriptorHeap[getTexIndex(" + std::to_string(slot) + ")];");
        EmitLine("float4 " + VarName(expr.id, 0) +
                 " = " + texVar + ".Sample(linearSampler, " + uvValue + ");");
        EmitLine("float " + VarName(expr.id, 1) + " = " + VarName(expr.id, 0) + ".r;");
        EmitLine("float " + VarName(expr.id, 2) + " = " + VarName(expr.id, 0) + ".g;");
        EmitLine("float " + VarName(expr.id, 3) + " = " + VarName(expr.id, 0) + ".b;");
        EmitLine("float " + VarName(expr.id, 4) + " = " + VarName(expr.id, 0) + ".a;");
        break;
    }

    // ── Coordinates ──────────────────────────────────────────────────────────
    case MatExprType::TexCoord:
        EmitLine("float2 " + VarName(expr.id, 0) + " = uv;");
        break;

    case MatExprType::WorldPosition:
        EmitLine("float3 " + VarName(expr.id, 0) + " = positionWS;");
        break;

    case MatExprType::ObjectPosition:
        EmitLine("float3 " + VarName(expr.id, 0) + " = objectPosition;");
        break;

    case MatExprType::VertexNormal:
        EmitLine("float3 " + VarName(expr.id, 0) + " = normalWS;");
        break;

    case MatExprType::VertexColor:
        EmitLine("float4 " + VarName(expr.id, 0) + " = vertexColor;");
        break;

    case MatExprType::CameraVector: {
        EmitLine("float3 " + VarName(expr.id, 0) +
                 " = normalize(cameraPosition - positionWS);");
        break;
    }

    case MatExprType::ScreenPosition:
        EmitLine("float2 " + VarName(expr.id, 0) + " = screenUV;");
        break;

    case MatExprType::Time:
        EmitLine("float " + VarName(expr.id, 0) + " = totalTime;");
        break;

    // ── Procedural ───────────────────────────────────────────────────────────
    case MatExprType::CheckerPattern: {
        const std::string uv = GetInputValue(expr.inputs[0], MatPinType::Float2);
        const std::string scale = GetInputValue(expr.inputs[1], MatPinType::Float);
        const std::string tmpUV = prefix + "_cuv";
        const std::string tmpScale = prefix + "_cscale";
        EmitLine("float2 " + tmpUV + " = " + uv + ";");
        EmitLine("float " + tmpScale + " = " + scale + ";");
        EmitLine("float " + VarName(expr.id, 0) +
                 " = fmod(floor(" + tmpUV + ".x * " + tmpScale +
                 ") + floor(" + tmpUV + ".y * " + tmpScale + "), 2.0);");
        break;
    }

    case MatExprType::NoisePerlin: {
        const std::string uv = GetInputValue(expr.inputs[0], MatPinType::Float2);
        const std::string scale = GetInputValue(expr.inputs[1], MatPinType::Float);
        // Hash-based noise approximation using sin.
        EmitLine("float " + VarName(expr.id, 0) +
                 " = frac(sin(dot(" + uv + " * " + scale +
                 ", float2(12.9898, 78.233))) * 43758.5453);");
        break;
    }

    case MatExprType::GradientLinear: {
        const std::string uv = GetInputValue(expr.inputs[0], MatPinType::Float2);
        EmitLine("float " + VarName(expr.id, 0) + " = " + uv + ".x;");
        break;
    }

    case MatExprType::GradientRadial: {
        const std::string uv = GetInputValue(expr.inputs[0], MatPinType::Float2);
        EmitLine("float " + VarName(expr.id, 0) +
                 " = length(" + uv + " - 0.5) * 2.0;");
        break;
    }

    // ── Utility ──────────────────────────────────────────────────────────────
    case MatExprType::TriPlanarMapping: {
        const std::string worldPos = GetInputValue(expr.inputs[0], MatPinType::Float3);
        const std::string worldNormal = GetInputValue(expr.inputs[1], MatPinType::Float3);
        const std::string sharpness = GetInputValue(expr.inputs[2], MatPinType::Float);

        const std::string tmpBlend = prefix + "_tpblend";
        EmitLine("float3 " + tmpBlend + " = pow(abs(" + worldNormal + "), " + sharpness + ");");
        EmitLine(tmpBlend + " /= (" + tmpBlend + ".x + " + tmpBlend + ".y + " + tmpBlend + ".z);");
        // UV output: primary axis projection (YZ plane for X-dominant)
        EmitLine("float2 " + VarName(expr.id, 0) + " = (" + worldPos + ").yz;");
        // Blend weights output
        EmitLine("float3 " + VarName(expr.id, 1) + " = " + tmpBlend + ";");
        break;
    }

    case MatExprType::Fresnel: {
        const std::string exponent = GetInputValue(expr.inputs[0], MatPinType::Float);
        EmitLine("float " + VarName(expr.id, 0) +
                 " = pow(1.0 - saturate(dot(normalWS, normalize(cameraPosition - positionWS))), " +
                 exponent + ");");
        break;
    }

    case MatExprType::Panner: {
        const std::string uv = GetInputValue(expr.inputs[0], MatPinType::Float2);
        const std::string speedX = GetInputValue(expr.inputs[1], MatPinType::Float);
        const std::string speedY = GetInputValue(expr.inputs[2], MatPinType::Float);
        EmitLine("float2 " + VarName(expr.id, 0) + " = " + uv + " + float2(" +
                 speedX + ", " + speedY + ") * totalTime;");
        break;
    }

    case MatExprType::ComponentMask: {
        const int r = getInt("r", 1);
        const int g = getInt("g", 1);
        const int b = getInt("b", 1);
        const int a = getInt("a", 0);
        const std::string input = GetInputValue(expr.inputs[0], MatPinType::Float4);
        const uint32_t count = CountMaskChannels(r, g, b, a);
        const MatPinType outType = ComponentMaskOutputType(r, g, b, a);

        // Build swizzle string from selected channels.
        std::string swizzle;
        if (r) swizzle += 'r';
        if (g) swizzle += 'g';
        if (b) swizzle += 'b';
        if (a) swizzle += 'a';

        if (count == 0) {
            // Degenerate: no channels selected — emit 0.
            EmitLine("float " + VarName(expr.id, 0) + " = 0.0;");
        } else {
            EmitLine(std::string(MatPinTypeToHlsl(outType)) + " " + VarName(expr.id, 0) +
                     " = (" + input + ")." + swizzle + ";");
        }
        break;
    }

    case MatExprType::Append: {
        const MatPinType outType = expr.outputs[0].type;
        const std::string a = GetInputValue(expr.inputs[0], expr.inputs[0].type);
        const std::string b = GetInputValue(expr.inputs[1], expr.inputs[1].type);
        const uint32_t aComp = MatPinTypeComponents(expr.inputs[0].type);
        const uint32_t bComp = MatPinTypeComponents(expr.inputs[1].type);

        // Append concatenates the components of A and B.
        // Output type is determined by total component count (capped at float4).
        const uint32_t totalComp = std::min(aComp + bComp, 4u);
        const char* hlslType = MatPinTypeToHlsl(outType);

        if (aComp == 1 && bComp == 1) {
            EmitLine(std::string(hlslType) + " " + VarName(expr.id, 0) +
                     " = float2(" + a + ", " + b + ");");
        } else {
            // General case: construct from components.
            std::string components = a;
            components += ", ";
            components += b;
            EmitLine(std::string(hlslType) + " " + VarName(expr.id, 0) +
                     " = " + hlslType + "(" + components + ");");
        }
        break;
    }

    // ── Parameters ───────────────────────────────────────────────────────────
    case MatExprType::ScalarParameter: {
        const std::string paramName = getString("paramName");
        const float defaultVal = getScalar("default", 0.5f);
        const float defaults[4] = {defaultVal, 0.f, 0.f, 0.f};
        const uint32_t slot = AllocateParamSlot(paramName, MatPinType::Float, defaults);
        EmitLine("float " + VarName(expr.id, 0) + " = materialParams[" +
                 std::to_string(slot) + "].x;");
        break;
    }

    case MatExprType::VectorParameter: {
        const std::string paramName = getString("paramName");
        const float r = getScalar("defaultR", 0.f);
        const float g = getScalar("defaultG", 0.f);
        const float b = getScalar("defaultB", 0.f);
        const float defaults[4] = {r, g, b, 0.f};
        const uint32_t slot = AllocateParamSlot(paramName, MatPinType::Float3, defaults);
        EmitLine("float3 " + VarName(expr.id, 0) + " = materialParams[" +
                 std::to_string(slot) + "].xyz;");
        break;
    }

    case MatExprType::TextureParameter: {
        const std::string paramName = getString("paramName");
        const std::string texPath = getString("texturePath");
        const uint32_t slot = AllocateTextureSlot(paramName, texPath);
        // TextureParameter outputs a Texture object reference — downstream TextureSample
        // nodes would use this. For code generation, we just store the slot index.
        EmitLine("// TextureParameter '" + paramName + "' -> texIndex[" + std::to_string(slot) + "]");
        break;
    }

    } // switch

    return VarName(expr.id, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile
// ─────────────────────────────────────────────────────────────────────────────

MaterialCompileResult HlslMaterialTranslator::Compile(const MaterialGraph& graph) {
    // Reset state.
    code_.clear();
    emitted_.clear();
    graph_ = &graph;
    sortedOrder_.clear();
    nextTextureSlot_ = 0;
    nextParamSlot_ = 0;
    result_ = MaterialCompileResult{};

    // Step 1: Validate.
    std::string validationError;
    if (!graph.Validate(validationError)) {
        result_.success = false;
        result_.errorMessage = "Graph validation failed: " + validationError;
        UZ_LOG(Material, Error, "Material compilation failed: {}", result_.errorMessage);
        return result_;
    }

    // Step 2: Topological sort (dead-code eliminated).
    sortedOrder_ = graph.TopologicalSort();
    if (sortedOrder_.empty()) {
        result_.success = false;
        result_.errorMessage = "Topological sort failed (graph may contain cycles)";
        UZ_LOG(Material, Error, "Material compilation failed: {}", result_.errorMessage);
        return result_;
    }

    // Step 3: Emit all expressions in dependency order (PBROutput last).
    code_ += "    // --- Node computations ---\n";
    for (const uint32_t exprId : sortedOrder_) {
        const auto* expr = graph.FindExpression(exprId);
        if (!expr) continue;

        // PBR output is handled after all other nodes.
        if (expr->type == MatExprType::PBROutput) continue;

        EmitExpression(*expr);
    }

    // Step 4: Emit PBR output assignments.
    const auto& pbrOutput = graph.GetPBROutput();
    code_ += "\n    // --- PBR Output ---\n";

    // Each PBR output pin: if connected, use the connected value; otherwise,
    // emit a sensible PBR default. Without this, unwired `out` parameters in
    // the generated function leave the consumer's locals UNINITIALIZED, which
    // shows up as splotchy register garbage in the GBuffer (notably the AO
    // channel for materials that only declare albedo/metallic/roughness/normal,
    // such as the stock Sponza .uzmat files).
    struct PbrPin {
        const char* hlslVar;
        uint32_t    pinIndex;
        MatPinType  type;
        const char* defaultExpr;  ///< HLSL literal used when the pin is not connected.
    };
    static constexpr PbrPin kPbrPins[] = {
        {"out_baseColor", 0, MatPinType::Float3, "float3(1.0, 1.0, 1.0)"},
        {"out_metallic",  1, MatPinType::Float,  "0.0"},
        {"out_roughness", 2, MatPinType::Float,  "0.5"},
        // Tangent-space "no normal map" = (0, 0, 1). Stored in [0,1] space
        // because the consumer decodes via `xy * 2 - 1`, so a flat normal
        // encodes as (0.5, 0.5, 1.0).
        {"out_normal",    3, MatPinType::Float3, "float3(0.5, 0.5, 1.0)"},
        {"out_emissive",  4, MatPinType::Float3, "float3(0.0, 0.0, 0.0)"},
        {"out_opacity",   5, MatPinType::Float,  "1.0"},
        // AO defaults to 1.0 (no occlusion). When a material lacks an AO map
        // it must NOT contribute to dimming. @ref Karis 2013 — Real Shading in UE4.
        {"out_ao",        6, MatPinType::Float,  "1.0"},
    };

    for (const auto& pbrPin : kPbrPins) {
        const bool connected = (pbrPin.pinIndex < pbrOutput.inputs.size())
            && pbrOutput.inputs[pbrPin.pinIndex].IsConnected();
        if (connected) {
            const auto& inputPin = pbrOutput.inputs[pbrPin.pinIndex];
            const std::string value = GetInputValue(inputPin, pbrPin.type);
            EmitLine(std::string(pbrPin.hlslVar) + " = " + value + ";");
        } else {
            // Pin not declared in the .uzmat (or zero-source) — emit the
            // canonical PBR default so the `out` parameter is always written.
            EmitLine(std::string(pbrPin.hlslVar) + " = " + pbrPin.defaultExpr + ";");
        }
    }

    // ── Apply MaterialParams (UE5-style post-multiplications) ────────────
    // Standard PBR knobs that every material exposes for tuning. Default
    // values render identical to a graph without overrides.
    // @ref Karis 2013 "Real Shading in Unreal Engine 4"
    // @ref glTF 2.0 PBR — pbrMetallicRoughness factor fields
    code_ += "\n    // --- MaterialParams (post-multiplications) ---\n";
    {
        char buf[256];
        const auto& mp = graph.params;

        std::snprintf(buf, sizeof(buf),
            "    out_baseColor *= float3(%g, %g, %g);",
            mp.baseColorTint[0], mp.baseColorTint[1], mp.baseColorTint[2]);
        code_ += buf; code_ += "\n";

        std::snprintf(buf, sizeof(buf), "    out_metallic  = saturate(out_metallic  * %g);",
                      mp.metallicMultiplier);
        code_ += buf; code_ += "\n";

        std::snprintf(buf, sizeof(buf), "    out_roughness = saturate(out_roughness * %g);",
                      mp.roughnessMultiplier);
        code_ += buf; code_ += "\n";

        std::snprintf(buf, sizeof(buf), "    out_emissive *= %g;",
                      mp.emissiveMultiplier);
        code_ += buf; code_ += "\n";

        std::snprintf(buf, sizeof(buf), "    out_ao = saturate(out_ao * %g);",
                      mp.aoMultiplier);
        code_ += buf; code_ += "\n";

        // Normal strength: lerp the tangent-space normal toward (0.5,0.5,1)
        // (flat) when strength < 1. Done in encoded space because the
        // gbuffer decodes via xy*2-1 downstream.
        if (mp.normalStrength != 1.0f) {
            std::snprintf(buf, sizeof(buf),
                "    out_normal = lerp(float3(0.5, 0.5, 1.0), out_normal, %g);",
                mp.normalStrength);
            code_ += buf; code_ += "\n";
        }

        // Specular multiplier — UE5 "Specular" param. Stored as the
        // material's effective F0 baseline (default 1.0 = 4% F0). Pushed
        // through the .a channel of the normal RT below would require an
        // RT layout change; for now we bake it into the dielectric path
        // by multiplying it into out_roughness=>spec response is nontrivial,
        // so we expose it as a separate output planned in a follow-up.
        // @ref Karis 2013 — Specular = 0.5 → 0.04 baseline F0.
        (void)mp.specularMultiplier;
    }

    // Step 5: Wrap in function signature.
    const uint64_t contentHash = graph.ComputeContentHash();
    char hashBuf[32];
    std::snprintf(hashBuf, sizeof(hashBuf), "0x%016llX",
                  static_cast<unsigned long long>(contentHash));

    std::string header = "/// Auto-generated by UzEngine MaterialCompiler\n";
    if (!graph.name.empty()) header += "/// Material: \"" + graph.name + "\"\n";
    header += std::string("/// Hash: ") + hashBuf + "\n";

    // Declare texture index array if the graph uses any TextureSample nodes.
    // The renderer uploads SRV indices to this cbuffer before the draw call.
    if (nextTextureSlot_ > 0) {
        header += "// Material texture slots (" + std::to_string(nextTextureSlot_) + " textures)\n";
        header += "// texIndex[] is declared in the combined shader (MaterialTextureCB at b4)\n\n";
    }

    header +=
        "void ComputeMaterialOutputs(\n"
        "    in float2 uv, in float3 positionWS, in float3 normalWS,\n"
        "    in float4 vertexColor, in float3 cameraPosition,\n"
        "    in float3 objectPosition, in float2 screenUV, in float totalTime,\n"
        "    in float4 materialParams[16],\n"
        "    out float3 out_baseColor, out float out_metallic, out float out_roughness,\n"
        "    out float3 out_normal, out float3 out_emissive,\n"
        "    out float out_opacity, out float out_ao)\n{\n";

    std::string fullSource = header + code_ + "}\n";

    // Step 6: Populate result.
    result_.success = true;
    result_.hlslSource = std::move(fullSource);
    result_.contentHash = contentHash;
    result_.textureSlotCount = nextTextureSlot_;
    result_.parameterCount = nextParamSlot_;

    UZ_LOG(Material, Info, "Material '{}' compiled: {} tex slots, {} params, hash={}",
           graph.name, result_.textureSlotCount, result_.parameterCount, hashBuf);

    return result_;
}

} // namespace uz
