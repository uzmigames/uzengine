/// VFX Curve System -- implementation of keyframe interpolation and GPU bake.

#include "vfx_curve.h"

#include "engine/logging/log.h"
#include "engine/rhi/rhi.h"

#include <algorithm>
#include <cmath>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Interpolation Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Clamps a value to [lo, hi].
[[nodiscard]] constexpr float Clamp(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/// Standard linear interpolation.
[[nodiscard]] constexpr float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/// Hermite smoothstep remap: `t*t*(3 - 2*t)`.
[[nodiscard]] constexpr float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

/// Catmull-Rom spline evaluation between p1 and p2, with surrounding p0/p3.
///
/// t is the fractional position between p1 (t=0) and p2 (t=1).
[[nodiscard]] float CatmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
                    + (-p0 + p2) * t
                    + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                    + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/// Component-wise Vec4 linear interpolation.
[[nodiscard]] Vec4 LerpVec4(const Vec4& a, const Vec4& b, float t) {
    return Vec4{
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t),
        Lerp(a.w, b.w, t)
    };
}

/// Component-wise Vec4 Catmull-Rom interpolation.
[[nodiscard]] Vec4 CatmullRomVec4(const Vec4& p0, const Vec4& p1,
                                   const Vec4& p2, const Vec4& p3, float t) {
    return Vec4{
        CatmullRom(p0.x, p1.x, p2.x, p3.x, t),
        CatmullRom(p0.y, p1.y, p2.y, p3.y, t),
        CatmullRom(p0.z, p1.z, p2.z, p3.z, t),
        CatmullRom(p0.w, p1.w, p2.w, p3.w, t)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Binary Search -- find the interval containing t
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the index of the key just before or at time t.
///
/// The returned index i satisfies: keys[i].time <= t < keys[i+1].time
/// (or i == count-2 if t >= keys[count-1].time).
template <typename KeyType>
[[nodiscard]] size_t FindInterval(const std::vector<KeyType>& keys, float t) {
    const size_t count = keys.size();

    // Edge cases: clamp to valid interval range
    if (count <= 1) {
        return 0;
    }
    if (t <= keys[0].time) {
        return 0;
    }
    if (t >= keys[count - 1].time) {
        return count - 2;
    }

    // Binary search for the interval
    size_t lo = 0;
    size_t hi = count - 1;
    while (lo < hi - 1) {
        const size_t mid = lo + (hi - lo) / 2;
        if (keys[mid].time <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/// Computes the fractional position of t within the interval [keys[i], keys[i+1]].
template <typename KeyType>
[[nodiscard]] float FractionalT(const std::vector<KeyType>& keys, size_t i, float t) {
    const float dt = keys[i + 1].time - keys[i].time;
    if (dt <= 0.0f) {
        return 0.0f;
    }
    return (t - keys[i].time) / dt;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructors
// ─────────────────────────────────────────────────────────────────────────────

VFXCurve::VFXCurve()
    : interpolation_(InterpolationMode::Linear)
    , type_(CurveType::Float)
    , bakedTexture_(RHITextureHandle::Invalid()) {}

VFXCurve::VFXCurve(std::vector<Keyframe> keys, InterpolationMode mode)
    : floatKeys_(std::move(keys))
    , interpolation_(mode)
    , type_(CurveType::Float)
    , bakedTexture_(RHITextureHandle::Invalid()) {}

VFXCurve::VFXCurve(std::vector<KeyframeVec4> keys, CurveType type,
                   InterpolationMode mode)
    : vec4Keys_(std::move(keys))
    , interpolation_(mode)
    , type_(type)
    , bakedTexture_(RHITextureHandle::Invalid()) {}

// ─────────────────────────────────────────────────────────────────────────────
// SampleFloat
// ─────────────────────────────────────────────────────────────────────────────

float VFXCurve::SampleFloat(float t) const {
    if (type_ != CurveType::Float || floatKeys_.empty()) {
        return 0.0f;
    }

    t = Clamp(t, 0.0f, 1.0f);

    if (floatKeys_.size() == 1) {
        return floatKeys_[0].value;
    }

    const size_t i = FindInterval(floatKeys_, t);
    float frac = FractionalT(floatKeys_, i, t);

    switch (interpolation_) {
        case InterpolationMode::Smoothstep:
            frac = Smoothstep(frac);
            return Lerp(floatKeys_[i].value, floatKeys_[i + 1].value, frac);

        case InterpolationMode::Cubic: {
            // Catmull-Rom: clamp boundary indices to stay in range
            const size_t last = floatKeys_.size() - 1;
            const size_t i0 = (i > 0) ? (i - 1) : 0;
            const size_t i2 = i + 1;
            const size_t i3 = (i2 < last) ? (i2 + 1) : last;
            return CatmullRom(floatKeys_[i0].value, floatKeys_[i].value,
                              floatKeys_[i2].value, floatKeys_[i3].value, frac);
        }

        case InterpolationMode::Linear:
        default:
            return Lerp(floatKeys_[i].value, floatKeys_[i + 1].value, frac);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleVec4
// ─────────────────────────────────────────────────────────────────────────────

Vec4 VFXCurve::SampleVec4(float t) const {
    if ((type_ != CurveType::Vec3 && type_ != CurveType::Vec4) || vec4Keys_.empty()) {
        return Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    }

    t = Clamp(t, 0.0f, 1.0f);

    if (vec4Keys_.size() == 1) {
        return vec4Keys_[0].value;
    }

    const size_t i = FindInterval(vec4Keys_, t);
    float frac = FractionalT(vec4Keys_, i, t);

    switch (interpolation_) {
        case InterpolationMode::Smoothstep:
            frac = Smoothstep(frac);
            return LerpVec4(vec4Keys_[i].value, vec4Keys_[i + 1].value, frac);

        case InterpolationMode::Cubic: {
            const size_t last = vec4Keys_.size() - 1;
            const size_t i0 = (i > 0) ? (i - 1) : 0;
            const size_t i2 = i + 1;
            const size_t i3 = (i2 < last) ? (i2 + 1) : last;
            return CatmullRomVec4(vec4Keys_[i0].value, vec4Keys_[i].value,
                                  vec4Keys_[i2].value, vec4Keys_[i3].value, frac);
        }

        case InterpolationMode::Linear:
        default:
            return LerpVec4(vec4Keys_[i].value, vec4Keys_[i + 1].value, frac);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutators
// ─────────────────────────────────────────────────────────────────────────────

void VFXCurve::SetKeys(std::vector<Keyframe> keys) {
    floatKeys_ = std::move(keys);
    type_ = CurveType::Float;
    bakedTexture_ = RHITextureHandle::Invalid();
}

void VFXCurve::SetKeysVec4(std::vector<KeyframeVec4> keys) {
    vec4Keys_ = std::move(keys);
    // Preserve existing type_ if it's already Vec3 or Vec4; default to Vec4
    if (type_ == CurveType::Float) {
        type_ = CurveType::Vec4;
    }
    bakedTexture_ = RHITextureHandle::Invalid();
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

bool VFXCurve::IsEmpty() const noexcept {
    return (type_ == CurveType::Float) ? floatKeys_.empty() : vec4Keys_.empty();
}

size_t VFXCurve::GetKeyCount() const noexcept {
    return (type_ == CurveType::Float) ? floatKeys_.size() : vec4Keys_.size();
}

CurveType VFXCurve::GetType() const noexcept {
    return type_;
}

InterpolationMode VFXCurve::GetInterpolation() const noexcept {
    return interpolation_;
}

void VFXCurve::SetInterpolation(InterpolationMode mode) {
    interpolation_ = mode;
    bakedTexture_ = RHITextureHandle::Invalid();
}

// ─────────────────────────────────────────────────────────────────────────────
// BakeToTexture
// ─────────────────────────────────────────────────────────────────────────────

bool VFXCurve::BakeToTexture(IRHIDevice* device, uint32_t texelCount) {
    if (device == nullptr) {
        UZ_LOG(VFX, Error, "BakeToTexture: device is null");
        return false;
    }

    if (IsEmpty()) {
        UZ_LOG(VFX, Warning, "BakeToTexture: curve has no keyframes");
        return false;
    }

    if (texelCount == 0) {
        UZ_LOG(VFX, Error, "BakeToTexture: texelCount must be > 0");
        return false;
    }

    const bool isFloat = (type_ == CurveType::Float);

    RHITextureDesc desc{};
    desc.width = texelCount;
    desc.height = 1;
    desc.depthOrArraySize = 1;
    desc.mipLevels = 1;
    desc.format = isFloat ? RHIFormat::R32Float : RHIFormat::RGBA32Float;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = "VFXCurve_Baked1D";

    RHITextureHandle handle = device->CreateTexture(desc);
    if (!handle.IsValid()) {
        UZ_LOG(VFX, Error, "BakeToTexture: failed to create {}x1 texture",
               texelCount);
        return false;
    }

    // TODO(vfx): Upload sampled curve data to the texture.
    // Currently the texture is created but not filled because the mock RHI
    // device returns nullptr from MapBuffer. When real upload support is
    // available, sample the curve at each texel center and write the data:
    //   for (uint32_t x = 0; x < texelCount; ++x) {
    //       float t = static_cast<float>(x) / static_cast<float>(texelCount - 1);
    //       if (isFloat) texels[x] = SampleFloat(t);
    //       else         vec4Texels[x] = SampleVec4(t);
    //   }
    //   device->UpdateTexture(handle, texelData, dataSize);

    bakedTexture_ = handle;

    UZ_LOG(VFX, Info, "Baked {} curve to {}x1 texture (format={})",
           isFloat ? "float" : "vec4", texelCount,
           isFloat ? "R32_FLOAT" : "RGBA32_FLOAT");

    return true;
}

RHITextureHandle VFXCurve::GetBakedTexture() const noexcept {
    return bakedTexture_;
}

} // namespace uz
