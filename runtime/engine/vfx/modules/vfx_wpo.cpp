#include "vfx_wpo.h"

#include "vector3.h"

#include <cmath>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// HLSL splice emission
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxWPOEmitEvaluateBody(const VfxWPOMaterialAttributes& attrs) {
    if (attrs.IsEnabled()) {
        // Wrap the author expression in a `return` so the material pin
        // accepts any float3 expression (e.g. `sin(...)`, `input * 2`).
        return "return " + attrs.hlslExpression + ";";
    }
    return "return float3(0.0, 0.0, 0.0);";
}

std::string VfxWPOSpliceVS(const VfxWPOMaterialAttributes& attrs) {
    if (!attrs.IsEnabled()) {
        // Emit nothing when WPO is disabled so the optimiser sees the
        // identity vertex path. Pure no-op; no +=.
        return {};
    }
    return
        "float3 wpo = EvaluateWPO(objectPos, worldPos, time, normalizedAge);\n"
        "worldPos += wpo;\n";
}

std::string VfxWPOSplicePrevVS(const VfxWPOMaterialAttributes& attrs) {
    if (!attrs.IsEnabled()) {
        return {};
    }
    return
        "float3 prevWpo = EvaluateWPO(objectPos, prevWorldPos, prevTime, prevNormalizedAge);\n"
        "prevWorldPos += prevWpo;\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU WPO evaluator (for tests)
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxEvaluateWPOBuiltin(const char* exprId,
                             const Vec3& objectPos,
                             const Vec3& worldPos,
                             float time,
                             float normalizedAge) noexcept {
    (void)objectPos; // unused by current built-ins but part of API
    if (exprId == nullptr) return Vec3{0, 0, 0};

    if (std::strcmp(exprId, "zero") == 0) {
        return Vec3{0, 0, 0};
    }
    if (std::strcmp(exprId, "sin_time_x") == 0) {
        return Vec3{0.0f, 0.0f, std::sin(time + worldPos.x)};
    }
    if (std::strcmp(exprId, "breathe") == 0) {
        const float scale = 0.1f * std::sin(time);
        return Vec3{worldPos.x * scale, worldPos.y * scale, worldPos.z * scale};
    }
    if (std::strcmp(exprId, "twist") == 0) {
        const float angle = normalizedAge * 6.2831853f; // 2π
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        // Rotate in XY; Z unchanged. Offset = rotated - original.
        const float rx = worldPos.x * c - worldPos.y * s;
        const float ry = worldPos.x * s + worldPos.y * c;
        return Vec3{rx - worldPos.x, ry - worldPos.y, 0.0f};
    }
    return Vec3{0, 0, 0};
}

} // namespace uz
