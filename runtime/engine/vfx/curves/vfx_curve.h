#pragma once

/// VFX Curve System -- keyframe-based animation curves for particle systems.
///
/// Supports float and Vec4 curves with Linear, Cubic (Catmull-Rom), and
/// Smoothstep interpolation. Curves can be baked to 1D GPU textures for
/// efficient per-particle sampling on the GPU.
///
/// # Usage
/// ```cpp
/// VFXCurve curve({
///     {0.0f, 1.0f},
///     {0.5f, 2.5f},
///     {1.0f, 0.0f}
/// }, InterpolationMode::Smoothstep);
///
/// float val = curve.SampleFloat(0.25f);  // interpolated between keys
/// curve.BakeToTexture(device, 256);      // bake for GPU sampling
/// ```

#include "engine/math/vector4.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

// Forward declaration -- avoids pulling in full rhi.h for the interface
class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolation mode between keyframes.
///
/// Determines how values are blended between adjacent keys when sampling.
enum class InterpolationMode : uint8_t {
    Linear,     ///< Linear interpolation (lerp) between adjacent keys.
    Cubic,      ///< Catmull-Rom cubic interpolation using 4 surrounding keys.
    Smoothstep, ///< Hermite smoothstep: `t*t*(3-2*t)` applied before lerp.
};

/// Type of data stored in the curve's keyframes.
enum class CurveType : uint8_t {
    Float, ///< Scalar float keyframes (e.g., size over lifetime).
    Vec3,  ///< 3D vector keyframes stored as Vec4 with w=0 (e.g., velocity).
    Vec4,  ///< 4D vector keyframes (e.g., color with alpha).
};

// ─────────────────────────────────────────────────────────────────────────────
// Keyframe Structs
// ─────────────────────────────────────────────────────────────────────────────

/// Scalar keyframe at a normalized time position.
///
/// # Fields
/// * `time` - Normalized time in [0, 1]. Keys must be sorted by time.
/// * `value` - Float value at this time.
struct Keyframe {
    float time;  ///< Normalized time [0, 1].
    float value; ///< Scalar value at this time.
};

/// Vector keyframe at a normalized time position.
///
/// For Vec3 curves, w is stored as 0.0. For Vec4 curves, all components are used.
///
/// # Fields
/// * `time` - Normalized time in [0, 1]. Keys must be sorted by time.
/// * `value` - Vec4 value at this time.
struct KeyframeVec4 {
    float time; ///< Normalized time [0, 1].
    Vec4 value; ///< Vector value at this time (w=0 for Vec3 curves).
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXCurve Class
// ─────────────────────────────────────────────────────────────────────────────

/// Keyframe-based animation curve for VFX particle properties.
///
/// Stores sorted keyframes and interpolates between them at any normalized
/// time t in [0, 1]. Values outside [0, 1] are clamped to the first/last key.
///
/// Curves can be baked to a 1D GPU texture via `BakeToTexture()` so that
/// particle shaders sample the curve with a single texture fetch instead of
/// per-particle keyframe search.
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded setup on the CPU, then
/// baked to a texture for GPU consumption.
class VFXCurve {
public:
    /// Constructs an empty float curve with Linear interpolation.
    VFXCurve();

    /// Constructs a float curve from pre-sorted keyframes.
    ///
    /// # Arguments
    /// * `keys` - Keyframes sorted by ascending time. Must have >= 2 entries.
    /// * `mode` - Interpolation mode between keys.
    explicit VFXCurve(std::vector<Keyframe> keys,
                      InterpolationMode mode = InterpolationMode::Linear);

    /// Constructs a Vec4 curve from pre-sorted keyframes.
    ///
    /// # Arguments
    /// * `keys` - Vec4 keyframes sorted by ascending time. Must have >= 2 entries.
    /// * `type` - CurveType::Vec3 or CurveType::Vec4.
    /// * `mode` - Interpolation mode between keys.
    explicit VFXCurve(std::vector<KeyframeVec4> keys,
                      CurveType type = CurveType::Vec4,
                      InterpolationMode mode = InterpolationMode::Linear);

    /// Samples the float curve at normalized time t.
    ///
    /// Clamps t to [0, 1]. Returns 0.0f if the curve is empty or is a Vec4 curve.
    ///
    /// # Arguments
    /// * `t` - Normalized time in [0, 1].
    ///
    /// # Returns
    /// Interpolated float value at time t.
    [[nodiscard]] float SampleFloat(float t) const;

    /// Samples the Vec4 curve at normalized time t.
    ///
    /// Clamps t to [0, 1]. Returns zero vector if the curve is empty or is a float curve.
    ///
    /// # Arguments
    /// * `t` - Normalized time in [0, 1].
    ///
    /// # Returns
    /// Interpolated Vec4 value at time t.
    [[nodiscard]] Vec4 SampleVec4(float t) const;

    /// Replaces all float keyframes. Invalidates any baked texture.
    ///
    /// # Arguments
    /// * `keys` - New keyframes, must be sorted by ascending time.
    void SetKeys(std::vector<Keyframe> keys);

    /// Replaces all Vec4 keyframes. Invalidates any baked texture.
    ///
    /// # Arguments
    /// * `keys` - New Vec4 keyframes, must be sorted by ascending time.
    void SetKeysVec4(std::vector<KeyframeVec4> keys);

    /// Returns true if the curve has no keyframes.
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// Returns the number of keyframes (float or vec4, depending on type).
    [[nodiscard]] size_t GetKeyCount() const noexcept;

    /// Returns the data type of this curve's keyframes.
    [[nodiscard]] CurveType GetType() const noexcept;

    /// Returns the current interpolation mode.
    [[nodiscard]] InterpolationMode GetInterpolation() const noexcept;

    /// Changes the interpolation mode. Invalidates any baked texture.
    ///
    /// # Arguments
    /// * `mode` - New interpolation mode.
    void SetInterpolation(InterpolationMode mode);

    /// Bakes the curve to a 1D GPU texture for shader sampling.
    ///
    /// Creates a texture with `texelCount` width. Format is R32_FLOAT for float
    /// curves, RGBA32_FLOAT for Vec3/Vec4 curves.
    ///
    /// # Arguments
    /// * `device` - RHI device used to create the texture. Must not be null.
    /// * `texelCount` - Number of texels in the 1D texture (default 256).
    ///
    /// # Returns
    /// `true` if the texture was created successfully, `false` on failure.
    ///
    /// # Side Effects
    /// Stores the baked texture handle internally. A previous baked texture is
    /// NOT destroyed -- the caller is responsible for managing GPU resource lifetime.
    bool BakeToTexture(IRHIDevice* device, uint32_t texelCount = 256);

    /// Returns the handle to the baked 1D texture, or an invalid handle if
    /// `BakeToTexture()` has not been called or failed.
    [[nodiscard]] RHITextureHandle GetBakedTexture() const noexcept;

    /// Returns the float keyframes for serialization or inspection.
    [[nodiscard]] const std::vector<Keyframe>& GetFloatKeys() const noexcept {
        return floatKeys_;
    }

    /// Returns the Vec4 keyframes for serialization or inspection.
    [[nodiscard]] const std::vector<KeyframeVec4>& GetVec4Keys() const noexcept {
        return vec4Keys_;
    }

private:
    std::vector<Keyframe> floatKeys_;
    std::vector<KeyframeVec4> vec4Keys_;
    InterpolationMode interpolation_ = InterpolationMode::Linear;
    CurveType type_ = CurveType::Float;
    RHITextureHandle bakedTexture_;
};

} // namespace uz
