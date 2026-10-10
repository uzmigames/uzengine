#include "vfx_tangent_curve.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

/// Hermite basis evaluated at local parameter `u` in [0, 1].
float HermiteBlend(float p0, float p1, float m0, float m1, float u) noexcept {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 =  2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h10 =  u3 - 2.0f * u2 + u;
    const float h11 =  u3 - u2;
    return h00 * p0 + h01 * p1 + h10 * m0 + h11 * m1;
}

/// Given a key index and its neighbours, compute the effective in/out
/// tangents honouring the tangent mode. Boundary keys fall back to the
/// neighbour's slope to avoid extrapolation artefacts.
void ResolveTangents(const std::vector<VfxTangentKey>& keys,
                     size_t i,
                     float& outTangent,
                     float& nextInTangent) noexcept {
    const VfxTangentKey& a = keys[i];
    const VfxTangentKey& b = keys[i + 1];
    const float dt = b.time - a.time;
    if (!(dt > 0.0f)) {
        // Coincident keys — flat segment.
        outTangent    = 0.0f;
        nextInTangent = 0.0f;
        return;
    }

    // Start with authored tangents.
    float aOut = a.outTangent;
    float bIn  = b.inTangent;

    // Linear mode uses the segment's chord slope. Zero Hermite tangents would
    // give a smoothstep ease (h01(u) = 3u^2 - 2u^3), not a straight line;
    // with M0 = M1 = (P1 - P0) the cubic terms cancel and the basis reduces
    // exactly to P0 + u * (P1 - P0).
    const float chordSlope = (b.value - a.value) / dt;
    if (a.mode == VfxTangentMode::Linear) aOut = chordSlope;
    if (b.mode == VfxTangentMode::Linear) bIn  = chordSlope;

    // Auto mode derives the slope from neighbours.
    if (a.mode == VfxTangentMode::Auto) {
        if (i == 0) {
            // Boundary: use the segment slope to avoid over/undershoot.
            aOut = (b.value - a.value) / dt;
        } else {
            const VfxTangentKey& prev = keys[i - 1];
            const float prevDt = a.time - prev.time;
            const float prevSlope = (prevDt > 0.0f) ? (a.value - prev.value) / prevDt : 0.0f;
            const float nextSlope = (b.value - a.value) / dt;
            aOut = 0.5f * (prevSlope + nextSlope);
        }
    }
    if (b.mode == VfxTangentMode::Auto) {
        if (i + 1 == keys.size() - 1) {
            bIn = (b.value - a.value) / dt;
        } else {
            const VfxTangentKey& next = keys[i + 2];
            const float nextDt = next.time - b.time;
            const float nextSlope = (nextDt > 0.0f) ? (next.value - b.value) / nextDt : 0.0f;
            const float prevSlope = (b.value - a.value) / dt;
            bIn = 0.5f * (prevSlope + nextSlope);
        }
    }

    // Hermite tangents must be scaled by segment length so authored units
    // stay in dValue/dTime regardless of key spacing.
    outTangent    = aOut * dt;
    nextInTangent = bIn  * dt;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxTangentCurve public API
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxTangentCurve::AddKey(const VfxTangentKey& key) {
    // Find the first existing key with time > key.time — insert before it.
    const auto it = std::upper_bound(
        keys.begin(), keys.end(), key,
        [](const VfxTangentKey& a, const VfxTangentKey& b) { return a.time < b.time; });
    const auto idx = static_cast<uint32_t>(std::distance(keys.begin(), it));
    keys.insert(it, key);
    return idx;
}

void VfxTangentCurve::AddLinearKey(float time, float value) {
    VfxTangentKey k;
    k.time = time;
    k.value = value;
    k.inTangent = 0.0f;
    k.outTangent = 0.0f;
    k.mode = VfxTangentMode::Linear;
    AddKey(k);
}

float VfxTangentCurve::Evaluate(float t) const noexcept {
    if (keys.empty()) return 0.0f;
    if (keys.size() == 1) return keys.front().value;

    // Clamp out-of-range t to edge-key values.
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time)  return keys.back().value;

    // Binary search for the segment that brackets t.
    size_t lo = 0;
    size_t hi = keys.size() - 1;
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    const VfxTangentKey& a = keys[lo];
    const VfxTangentKey& b = keys[lo + 1];
    const float span = b.time - a.time;
    if (!(span > 0.0f)) return b.value;  // coincident keys

    float outTan = 0.0f;
    float inTan  = 0.0f;
    ResolveTangents(keys, lo, outTan, inTan);

    const float u = (t - a.time) / span;
    return HermiteBlend(a.value, b.value, outTan, inTan, u);
}

void VfxTangentCurve::SetFromLegacyTriplet(float startValue, float peakValue,
                                            float endValue, float peakTime) noexcept {
    keys.clear();
    keys.reserve(3);

    const float clampedPeak = std::clamp(peakTime, 0.0f, 1.0f);

    VfxTangentKey k0;
    k0.time = 0.0f;
    k0.value = startValue;
    k0.mode = VfxTangentMode::Auto;
    keys.push_back(k0);

    // Only insert a separate peak key when peakTime is strictly interior;
    // otherwise a 2-key linear curve covers the shape.
    if (clampedPeak > 0.0f && clampedPeak < 1.0f) {
        VfxTangentKey k1;
        k1.time = clampedPeak;
        k1.value = peakValue;
        k1.mode = VfxTangentMode::Auto;
        keys.push_back(k1);
    }

    VfxTangentKey k2;
    k2.time = 1.0f;
    k2.value = endValue;
    k2.mode = VfxTangentMode::Auto;
    keys.push_back(k2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tangent mode string round-trip
// ─────────────────────────────────────────────────────────────────────────────

const char* VfxTangentModeToString(VfxTangentMode mode) noexcept {
    switch (mode) {
        case VfxTangentMode::Auto:   return "auto";
        case VfxTangentMode::Linear: return "linear";
        case VfxTangentMode::User:   return "user";
        case VfxTangentMode::Broken: return "broken";
    }
    return "auto";
}

bool VfxTangentModeFromString(const std::string& s, VfxTangentMode& out) noexcept {
    if (s == "auto")   { out = VfxTangentMode::Auto;   return true; }
    if (s == "linear") { out = VfxTangentMode::Linear; return true; }
    if (s == "user")   { out = VfxTangentMode::User;   return true; }
    if (s == "broken") { out = VfxTangentMode::Broken; return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// .uzcurve asset — load / save
// ─────────────────────────────────────────────────────────────────────────────

bool VfxTangentCurveAsset::LoadFromString(const std::string& jsonStr,
                                           VfxTangentCurve& outCurve) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxTangentCurveAsset: JSON parse error: {}", e.what());
        return false;
    }

    if (!root.is_object()) {
        UZ_LOG(VFX, Error, "VfxTangentCurveAsset: root must be an object");
        return false;
    }
    if (root.value("format", std::string{}) != "uzcurve") {
        UZ_LOG(VFX, Error, "VfxTangentCurveAsset: unexpected or missing 'format'");
        return false;
    }

    outCurve.Clear();
    if (!root.contains("keys") || !root["keys"].is_array()) {
        // Empty curve is valid.
        return true;
    }

    for (const auto& kJson : root["keys"]) {
        VfxTangentKey k;
        k.time       = kJson.value("t",   0.0f);
        k.value      = kJson.value("v",   0.0f);
        k.inTangent  = kJson.value("in",  0.0f);
        k.outTangent = kJson.value("out", 0.0f);

        const std::string modeStr = kJson.value("mode", std::string("auto"));
        if (!VfxTangentModeFromString(modeStr, k.mode)) {
            UZ_LOG(VFX, Warning,
                   "VfxTangentCurveAsset: unknown mode '{}', falling back to auto",
                   modeStr);
            k.mode = VfxTangentMode::Auto;
        }

        outCurve.AddKey(k);
    }
    return true;
}

bool VfxTangentCurveAsset::Load(const std::string& path,
                                 VfxTangentCurve& outCurve,
                                 IVFS* vfs) {
    std::string content;
    if (vfs) {
        const auto bytes = vfs->Read(path);
        content.assign(bytes.begin(), bytes.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VfxTangentCurveAsset: failed to read '{}'", path);
        return false;
    }
    return LoadFromString(content, outCurve);
}

std::string VfxTangentCurveAsset::SaveToString(const VfxTangentCurve& curve) {
    nlohmann::json root;
    root["format"]  = "uzcurve";
    root["version"] = 1;

    nlohmann::json keysJson = nlohmann::json::array();
    for (const auto& k : curve.keys) {
        nlohmann::json kJson;
        kJson["t"]   = k.time;
        kJson["v"]   = k.value;
        kJson["mode"] = VfxTangentModeToString(k.mode);
        if (k.mode == VfxTangentMode::User || k.mode == VfxTangentMode::Broken) {
            kJson["in"]  = k.inTangent;
            kJson["out"] = k.outTangent;
        }
        keysJson.push_back(std::move(kJson));
    }
    root["keys"] = std::move(keysJson);
    return root.dump(2);
}

bool VfxTangentCurveAsset::SaveToFile(const VfxTangentCurve& curve,
                                      const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxTangentCurveAsset: failed to open '{}' for writing", path);
        return false;
    }
    ofs << SaveToString(curve);
    return static_cast<bool>(ofs);
}

} // namespace uz
