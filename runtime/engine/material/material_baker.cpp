/// UzEngine Material Baker — CPU-Side Graph Evaluator Implementation
///
/// Walks the material graph in topological order and evaluates each expression
/// node for a given UV coordinate. The result is a 4-component float value
/// per texel, which is then quantized to RGBA8.

#include "material_baker.h"

#include "../logging/log.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal: Per-texel evaluation value (up to 4 components)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// A 4-component value produced by evaluating an expression at a given UV.
/// Scalar expressions set only .x; vector expressions set .x/.y/.z/.w.
/// The number of meaningful components is tracked by `components`.
struct EvalValue {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;
    uint32_t components = 1;

    /// Construct a scalar value.
    static EvalValue Scalar(float v) { return {v, v, v, v, 1}; }

    /// Construct a 2-component value.
    static EvalValue Vec2(float a, float b) { return {a, b, 0.f, 0.f, 2}; }

    /// Construct a 3-component value.
    static EvalValue Vec3(float a, float b, float c) { return {a, b, c, 0.f, 3}; }

    /// Construct a 4-component value.
    static EvalValue Vec4(float a, float b, float c, float d) { return {a, b, c, d, 4}; }

    /// Promote this value to the target component count by splatting/extending.
    /// Matches HLSL implicit promotion rules.
    [[nodiscard]] EvalValue PromoteTo(uint32_t targetComponents) const {
        if (targetComponents <= components) return *this;
        if (components == 1) {
            // Scalar -> splat to all channels
            return {x, x, x, x, targetComponents};
        }
        // Extend with zeros (Float2 -> Float3 adds z=0, etc.)
        EvalValue result = *this;
        result.components = targetComponents;
        return result;
    }
};

/// Clamp a float to [0, 1].
[[nodiscard]] float Saturate(float v) {
    return (v < 0.f) ? 0.f : (v > 1.f) ? 1.f : v;
}

/// Quantize a [0,1] float to [0,255] uint8_t.
[[nodiscard]] uint8_t FloatToU8(float v) {
    return static_cast<uint8_t>(Saturate(v) * 255.f + 0.5f);
}

/// Simple 2D hash for Perlin noise (gradient generation).
/// Based on the classic hash approach used in GPU shader noise.
[[nodiscard]] float PerlinHash(float x, float y) {
    // Use a simple sin-based hash — sufficient for material preview quality
    float h = x * 127.1f + y * 311.7f;
    return std::fmod(std::sin(h) * 43758.5453123f, 1.0f);
}

/// 2D Perlin-like noise function for material preview.
/// Not a true Perlin implementation but produces visually acceptable results
/// for baking material previews.
[[nodiscard]] float PerlinNoise2D(float x, float y) {
    float ix = std::floor(x);
    float iy = std::floor(y);
    float fx = x - ix;
    float fy = y - iy;

    // Smoothstep interpolation
    float ux = fx * fx * (3.f - 2.f * fx);
    float uy = fy * fy * (3.f - 2.f * fy);

    float n00 = PerlinHash(ix, iy);
    float n10 = PerlinHash(ix + 1.f, iy);
    float n01 = PerlinHash(ix, iy + 1.f);
    float n11 = PerlinHash(ix + 1.f, iy + 1.f);

    float nx0 = n00 + (n10 - n00) * ux;
    float nx1 = n01 + (n11 - n01) * ux;

    return nx0 + (nx1 - nx0) * uy;
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression Evaluator
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates a single expression at a given UV coordinate.
///
/// Uses a cache of previously evaluated expressions (filled in topological order)
/// to resolve input pin connections.
///
/// @param expr     The expression to evaluate.
/// @param graph    The full material graph (for resolving connections).
/// @param u        Texel U coordinate [0, 1].
/// @param v        Texel V coordinate [0, 1].
/// @param cache    Map from (exprId, pinId) -> evaluated value.
///
/// @returns The evaluated value for this expression's output pin 0.
[[nodiscard]] EvalValue EvaluateExpression(
    const MaterialExpression& expr,
    const MaterialGraph& graph,
    float u, float v,
    const std::unordered_map<uint64_t, EvalValue>& cache)
{
    // Helper: resolve an input pin's value — either from a connected expression
    // or from the pin's default value.
    auto resolveInput = [&](uint32_t pinIndex) -> EvalValue {
        if (pinIndex >= expr.inputs.size()) {
            return EvalValue::Scalar(0.f);
        }
        const MatPin& pin = expr.inputs[pinIndex];
        if (pin.IsConnected()) {
            uint64_t key = (static_cast<uint64_t>(pin.connectedExprId) << 32) |
                           static_cast<uint64_t>(pin.connectedPinId);
            auto it = cache.find(key);
            if (it != cache.end()) {
                return it->second;
            }
        }
        // Use pin default value
        uint32_t comp = MatPinTypeComponents(pin.type);
        switch (comp) {
            case 1: return EvalValue::Scalar(pin.defaultValue[0]);
            case 2: return EvalValue::Vec2(pin.defaultValue[0], pin.defaultValue[1]);
            case 3: return EvalValue::Vec3(pin.defaultValue[0], pin.defaultValue[1], pin.defaultValue[2]);
            case 4: return EvalValue::Vec4(pin.defaultValue[0], pin.defaultValue[1], pin.defaultValue[2], pin.defaultValue[3]);
            default: return EvalValue::Scalar(0.f);
        }
    };

    // Helper: get a scalar parameter from the expression.
    auto getScalar = [&](const char* name, float fallback) -> float {
        auto it = expr.scalarParams.find(name);
        return (it != expr.scalarParams.end()) ? it->second : fallback;
    };

    switch (expr.type) {
        // ── Constants ────────────────────────────────────────────────────
        case MatExprType::ScalarConstant:
            return EvalValue::Scalar(getScalar("value", 0.f));

        case MatExprType::Vector2Constant:
            return EvalValue::Vec2(getScalar("x", 0.f), getScalar("y", 0.f));

        case MatExprType::Vector3Constant:
            return EvalValue::Vec3(getScalar("x", 0.f), getScalar("y", 0.f), getScalar("z", 0.f));

        case MatExprType::Vector4Constant:
            return EvalValue::Vec4(getScalar("x", 0.f), getScalar("y", 0.f),
                                   getScalar("z", 0.f), getScalar("w", 0.f));

        case MatExprType::ColorConstant:
            return EvalValue::Vec3(getScalar("r", 0.f), getScalar("g", 0.f), getScalar("b", 0.f));

        // ── Coordinates ──────────────────────────────────────────────────
        case MatExprType::TexCoord:
            return EvalValue::Vec2(u, v);

        case MatExprType::WorldPosition:
        case MatExprType::ObjectPosition:
            // Cannot evaluate without mesh data — return default (0,0,0)
            return EvalValue::Vec3(0.f, 0.f, 0.f);

        case MatExprType::VertexNormal:
            // Default tangent-space normal (pointing up)
            return EvalValue::Vec3(0.f, 0.f, 1.f);

        case MatExprType::VertexColor:
            return EvalValue::Vec4(1.f, 1.f, 1.f, 1.f);

        case MatExprType::CameraVector:
            return EvalValue::Vec3(0.f, 0.f, 1.f);

        case MatExprType::ScreenPosition:
            return EvalValue::Vec2(u, v);

        case MatExprType::Time:
            return EvalValue::Scalar(0.f);

        // ── Binary Math ──────────────────────────────────────────────────
        case MatExprType::Add: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w, c};
        }

        case MatExprType::Subtract: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w, c};
        }

        case MatExprType::Multiply: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w, c};
        }

        case MatExprType::Divide: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            auto safeDivide = [](float num, float den) -> float {
                return (std::abs(den) < 1e-10f) ? 0.f : num / den;
            };
            return {safeDivide(a.x, b.x), safeDivide(a.y, b.y),
                    safeDivide(a.z, b.z), safeDivide(a.w, b.w), c};
        }

        case MatExprType::Min: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            return {std::min(a.x, b.x), std::min(a.y, b.y),
                    std::min(a.z, b.z), std::min(a.w, b.w), c};
        }

        case MatExprType::Max: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            return {std::max(a.x, b.x), std::max(a.y, b.y),
                    std::max(a.z, b.z), std::max(a.w, b.w), c};
        }

        case MatExprType::Pow: {
            EvalValue base = resolveInput(0);
            EvalValue exp  = resolveInput(1);
            uint32_t c = std::max(base.components, exp.components);
            base = base.PromoteTo(c);
            exp  = exp.PromoteTo(c);
            return {std::pow(base.x, exp.x), std::pow(base.y, exp.y),
                    std::pow(base.z, exp.z), std::pow(base.w, exp.w), c};
        }

        case MatExprType::Step: {
            EvalValue edge = resolveInput(0);
            EvalValue x    = resolveInput(1);
            uint32_t c = std::max(edge.components, x.components);
            edge = edge.PromoteTo(c);
            x    = x.PromoteTo(c);
            auto step = [](float e, float val) -> float { return val >= e ? 1.f : 0.f; };
            return {step(edge.x, x.x), step(edge.y, x.y),
                    step(edge.z, x.z), step(edge.w, x.w), c};
        }

        // ── Ternary Math ─────────────────────────────────────────────────
        case MatExprType::Lerp: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            EvalValue t = resolveInput(2);
            uint32_t c = std::max({a.components, b.components, t.components});
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            t = t.PromoteTo(c);
            return {a.x + (b.x - a.x) * t.x,
                    a.y + (b.y - a.y) * t.y,
                    a.z + (b.z - a.z) * t.z,
                    a.w + (b.w - a.w) * t.w, c};
        }

        case MatExprType::Clamp: {
            EvalValue val    = resolveInput(0);
            EvalValue lo     = resolveInput(1);
            EvalValue hi     = resolveInput(2);
            uint32_t c = std::max({val.components, lo.components, hi.components});
            val = val.PromoteTo(c);
            lo  = lo.PromoteTo(c);
            hi  = hi.PromoteTo(c);
            auto clampF = [](float v, float lo, float hi) -> float {
                return (v < lo) ? lo : (v > hi) ? hi : v;
            };
            return {clampF(val.x, lo.x, hi.x), clampF(val.y, lo.y, hi.y),
                    clampF(val.z, lo.z, hi.z), clampF(val.w, lo.w, hi.w), c};
        }

        case MatExprType::SmoothStep: {
            EvalValue edge0 = resolveInput(0);
            EvalValue edge1 = resolveInput(1);
            EvalValue x     = resolveInput(2);
            uint32_t c = std::max({edge0.components, edge1.components, x.components});
            edge0 = edge0.PromoteTo(c);
            edge1 = edge1.PromoteTo(c);
            x     = x.PromoteTo(c);
            auto smoothstep = [](float e0, float e1, float val) -> float {
                float t = (std::abs(e1 - e0) < 1e-10f) ? 0.f :
                          Saturate((val - e0) / (e1 - e0));
                return t * t * (3.f - 2.f * t);
            };
            return {smoothstep(edge0.x, edge1.x, x.x),
                    smoothstep(edge0.y, edge1.y, x.y),
                    smoothstep(edge0.z, edge1.z, x.z),
                    smoothstep(edge0.w, edge1.w, x.w), c};
        }

        // ── Unary Math ───────────────────────────────────────────────────
        case MatExprType::Saturate: {
            EvalValue a = resolveInput(0);
            return {Saturate(a.x), Saturate(a.y), Saturate(a.z), Saturate(a.w), a.components};
        }

        case MatExprType::Abs: {
            EvalValue a = resolveInput(0);
            return {std::abs(a.x), std::abs(a.y), std::abs(a.z), std::abs(a.w), a.components};
        }

        case MatExprType::Sqrt: {
            EvalValue a = resolveInput(0);
            auto safeSqrt = [](float f) -> float { return f >= 0.f ? std::sqrt(f) : 0.f; };
            return {safeSqrt(a.x), safeSqrt(a.y), safeSqrt(a.z), safeSqrt(a.w), a.components};
        }

        case MatExprType::OneMinus: {
            EvalValue a = resolveInput(0);
            return {1.f - a.x, 1.f - a.y, 1.f - a.z, 1.f - a.w, a.components};
        }

        case MatExprType::Frac: {
            EvalValue a = resolveInput(0);
            auto frac = [](float f) -> float { return f - std::floor(f); };
            return {frac(a.x), frac(a.y), frac(a.z), frac(a.w), a.components};
        }

        case MatExprType::Floor: {
            EvalValue a = resolveInput(0);
            return {std::floor(a.x), std::floor(a.y), std::floor(a.z), std::floor(a.w), a.components};
        }

        case MatExprType::Ceil: {
            EvalValue a = resolveInput(0);
            return {std::ceil(a.x), std::ceil(a.y), std::ceil(a.z), std::ceil(a.w), a.components};
        }

        // ── Vector Math ──────────────────────────────────────────────────
        case MatExprType::Dot: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t c = std::max(a.components, b.components);
            a = a.PromoteTo(c);
            b = b.PromoteTo(c);
            float d = a.x * b.x + a.y * b.y;
            if (c >= 3) d += a.z * b.z;
            if (c >= 4) d += a.w * b.w;
            return EvalValue::Scalar(d);
        }

        case MatExprType::Cross: {
            EvalValue a = resolveInput(0).PromoteTo(3);
            EvalValue b = resolveInput(1).PromoteTo(3);
            return EvalValue::Vec3(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
        }

        case MatExprType::Normalize: {
            EvalValue a = resolveInput(0);
            float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w);
            if (len < 1e-10f) return a;
            float inv = 1.f / len;
            return {a.x * inv, a.y * inv, a.z * inv, a.w * inv, a.components};
        }

        case MatExprType::Length: {
            EvalValue a = resolveInput(0);
            float len = 0.f;
            len += a.x * a.x;
            if (a.components >= 2) len += a.y * a.y;
            if (a.components >= 3) len += a.z * a.z;
            if (a.components >= 4) len += a.w * a.w;
            return EvalValue::Scalar(std::sqrt(len));
        }

        // ── Procedural ───────────────────────────────────────────────────
        case MatExprType::CheckerPattern: {
            EvalValue uv    = resolveInput(0).PromoteTo(2);
            EvalValue scale = resolveInput(1);
            float s = scale.x;
            // CheckerPattern: fmod(floor(u * scale) + floor(v * scale), 2.0)
            // Returns 0.0 or 1.0
            float fx = std::floor(uv.x * s);
            float fy = std::floor(uv.y * s);
            float check = std::fmod(fx + fy, 2.0f);
            // fmod can return negative for negative inputs
            if (check < 0.f) check += 2.f;
            return EvalValue::Scalar(check < 1.f ? 0.f : 1.f);
        }

        case MatExprType::NoisePerlin: {
            EvalValue uv    = resolveInput(0).PromoteTo(2);
            EvalValue scale = resolveInput(1);
            float s = scale.x;
            return EvalValue::Scalar(PerlinNoise2D(uv.x * s, uv.y * s));
        }

        case MatExprType::GradientLinear: {
            EvalValue uv = resolveInput(0).PromoteTo(2);
            return EvalValue::Scalar(Saturate(uv.x));
        }

        case MatExprType::GradientRadial: {
            EvalValue uv = resolveInput(0).PromoteTo(2);
            float dx = uv.x - 0.5f;
            float dy = uv.y - 0.5f;
            float dist = std::sqrt(dx * dx + dy * dy) * 2.f; // 0 at center, 1 at edge
            return EvalValue::Scalar(Saturate(dist));
        }

        // ── Texture (unsupported on CPU — return default) ────────────────
        case MatExprType::TextureSample:
            return EvalValue::Vec4(1.f, 1.f, 1.f, 1.f);

        // ── Utility ──────────────────────────────────────────────────────
        case MatExprType::Fresnel: {
            // Without actual normal/view vectors, return a default Fresnel value
            EvalValue power = resolveInput(1);
            return EvalValue::Scalar(std::pow(1.f - 0.7071f, power.x)); // ~45 degree view angle
        }

        case MatExprType::Panner: {
            // Panner requires time — return UV as-is (time=0)
            EvalValue uv = resolveInput(0).PromoteTo(2);
            return uv;
        }

        case MatExprType::ComponentMask: {
            EvalValue input = resolveInput(0).PromoteTo(4);
            auto getInt = [&](const char* name, int32_t fallback) -> int32_t {
                auto it = expr.intParams.find(name);
                return (it != expr.intParams.end()) ? it->second : fallback;
            };
            int r = getInt("r", 1);
            int g = getInt("g", 1);
            int b = getInt("b", 0);
            int a = getInt("a", 0);

            // Collect selected channels in order
            float channels[4] = {};
            uint32_t count = 0;
            if (r) channels[count++] = input.x;
            if (g) channels[count++] = input.y;
            if (b) channels[count++] = input.z;
            if (a) channels[count++] = input.w;

            if (count == 0) return EvalValue::Scalar(0.f);
            return {channels[0],
                    count > 1 ? channels[1] : 0.f,
                    count > 2 ? channels[2] : 0.f,
                    count > 3 ? channels[3] : 0.f,
                    count};
        }

        case MatExprType::Append: {
            EvalValue a = resolveInput(0);
            EvalValue b = resolveInput(1);
            uint32_t total = a.components + b.components;
            if (total > 4) total = 4;
            float result[4] = {};
            uint32_t idx = 0;
            const float* aArr = &a.x;
            const float* bArr = &b.x;
            for (uint32_t i = 0; i < a.components && idx < 4; ++i) result[idx++] = aArr[i];
            for (uint32_t i = 0; i < b.components && idx < 4; ++i) result[idx++] = bArr[i];
            return {result[0], result[1], result[2], result[3], total};
        }

        case MatExprType::TriPlanarMapping:
            // Requires world-space position and normal — return default
            return EvalValue::Vec4(1.f, 1.f, 1.f, 1.f);

        // ── Parameters (use default values) ──────────────────────────────
        case MatExprType::ScalarParameter:
            return EvalValue::Scalar(getScalar("default", 0.f));

        case MatExprType::VectorParameter:
            return EvalValue::Vec4(
                getScalar("defaultR", 0.f), getScalar("defaultG", 0.f),
                getScalar("defaultB", 0.f), getScalar("defaultA", 0.f));

        case MatExprType::TextureParameter:
            return EvalValue::Vec4(1.f, 1.f, 1.f, 1.f);

        // ── PBROutput: passthrough — should not be evaluated directly ────
        case MatExprType::PBROutput:
            return EvalValue::Vec3(0.f, 0.f, 0.f);
    }

    return EvalValue::Scalar(0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Bake Logic
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes a specific PBROutput pin to an RGBA8 pixel buffer.
///
/// @param graph      The material graph to evaluate.
/// @param pinIndex   PBROutput input pin index (0=BaseColor, 1=Metallic, 2=Roughness, etc.).
/// @param width      Output width in texels.
/// @param height     Output height in texels.
/// @param isColor    If true, the output is treated as Float3 color (RGB).
///                   If false, the output is treated as scalar (replicated to RGB).
[[nodiscard]] BakeResult BakePBRPin(const MaterialGraph& graph,
                                    uint32_t pinIndex,
                                    uint32_t width, uint32_t height,
                                    bool isColor) {
    BakeResult result;
    result.width  = width;
    result.height = height;

    if (width == 0 || height == 0) {
        UZ_LOG(Material, Error, "MaterialBaker: invalid dimensions {}x{}", width, height);
        return result;
    }

    // Get the PBR output expression
    const MaterialExpression& pbrOutput = graph.GetPBROutput();
    if (pbrOutput.type != MatExprType::PBROutput) {
        UZ_LOG(Material, Error, "MaterialBaker: graph has no PBROutput node");
        return result;
    }

    // Check if the requested pin has a connection
    if (pinIndex >= pbrOutput.inputs.size()) {
        UZ_LOG(Material, Warning, "MaterialBaker: PBROutput pin {} out of range (max {})",
               pinIndex, pbrOutput.inputs.size());
        return result;
    }

    const MatPin& targetPin = pbrOutput.inputs[pinIndex];

    // If the pin is not connected, bake the default value as a solid color
    bool pinConnected = targetPin.IsConnected();

    // Topological sort for evaluation order
    std::vector<uint32_t> topoOrder = graph.TopologicalSort();
    if (topoOrder.empty()) {
        UZ_LOG(Material, Error, "MaterialBaker: graph has cycles or is empty");
        return result;
    }

    // Allocate pixel buffer
    result.pixels.resize(static_cast<size_t>(width) * height * 4);

    // Evaluate for each texel
    for (uint32_t py = 0; py < height; ++py) {
        float v = (static_cast<float>(py) + 0.5f) / static_cast<float>(height);
        for (uint32_t px = 0; px < width; ++px) {
            float u = (static_cast<float>(px) + 0.5f) / static_cast<float>(width);

            EvalValue finalValue;

            if (pinConnected) {
                // Evaluate all expressions in topological order
                std::unordered_map<uint64_t, EvalValue> cache;
                cache.reserve(topoOrder.size());

                for (uint32_t exprId : topoOrder) {
                    const MaterialExpression* expr = graph.FindExpression(exprId);
                    if (!expr) continue;

                    // Skip PBROutput — we resolve its inputs directly
                    if (expr->type == MatExprType::PBROutput) continue;

                    EvalValue val = EvaluateExpression(*expr, graph, u, v, cache);

                    // Store the result for output pin 0 (all expressions have at most 1 output)
                    uint64_t key = (static_cast<uint64_t>(exprId) << 32) | 0u;
                    cache[key] = val;
                }

                // Resolve the PBROutput input pin
                uint64_t srcKey = (static_cast<uint64_t>(targetPin.connectedExprId) << 32) |
                                  static_cast<uint64_t>(targetPin.connectedPinId);
                auto it = cache.find(srcKey);
                finalValue = (it != cache.end()) ? it->second : EvalValue::Scalar(0.f);
            } else {
                // Use the pin's default value
                if (isColor) {
                    finalValue = EvalValue::Vec3(targetPin.defaultValue[0],
                                                 targetPin.defaultValue[1],
                                                 targetPin.defaultValue[2]);
                } else {
                    finalValue = EvalValue::Scalar(targetPin.defaultValue[0]);
                }
            }

            // Write pixel
            size_t offset = (static_cast<size_t>(py) * width + px) * 4;
            if (isColor) {
                result.pixels[offset + 0] = FloatToU8(finalValue.x);
                result.pixels[offset + 1] = FloatToU8(finalValue.y);
                result.pixels[offset + 2] = FloatToU8(finalValue.z);
            } else {
                // Scalar: replicate to RGB
                uint8_t val = FloatToU8(finalValue.x);
                result.pixels[offset + 0] = val;
                result.pixels[offset + 1] = val;
                result.pixels[offset + 2] = val;
            }
            result.pixels[offset + 3] = 255; // Alpha always opaque
        }
    }

    result.success = true;
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

BakeResult MaterialBaker::BakeBaseColor(const MaterialGraph& graph,
                                        uint32_t width, uint32_t height) {
    return BakePBRPin(graph, 0, width, height, /*isColor=*/true);
}

BakeResult MaterialBaker::BakeRoughness(const MaterialGraph& graph,
                                        uint32_t width, uint32_t height) {
    return BakePBRPin(graph, 2, width, height, /*isColor=*/false);
}

BakeResult MaterialBaker::BakeMetallic(const MaterialGraph& graph,
                                       uint32_t width, uint32_t height) {
    return BakePBRPin(graph, 1, width, height, /*isColor=*/false);
}

BakeResult MaterialBaker::BakeNormal(const MaterialGraph& graph,
                                     uint32_t width, uint32_t height) {
    return BakePBRPin(graph, 3, width, height, /*isColor=*/true);
}

BakeResult MaterialBaker::BakeAO(const MaterialGraph& graph,
                                 uint32_t width, uint32_t height) {
    return BakePBRPin(graph, 6, width, height, /*isColor=*/false);
}

} // namespace uz
