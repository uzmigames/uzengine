/// UzEngine common shader constants.
///
/// Mathematical constants, engine-wide sentinel values, and GPU configuration
/// parameters shared by all runtime shaders.

#ifndef UZ_COMMON_CONSTANTS_HLSLI
#define UZ_COMMON_CONSTANTS_HLSLI

// ─── Mathematical constants ────────────────────────────────────────────────

/// pi
static const float PI = 3.14159265358979323846f;

/// 1 / pi
static const float M_1_PI = 0.31830988618379067154f;

/// 2 * pi
static const float M_2PI = 6.28318530717958647693f;

/// pi / 2
static const float M_PI_2 = 1.57079632679489661923f;

/// sqrt(2)
static const float M_SQRT2 = 1.41421356237309504880f;

/// Smallest float epsilon where 1.0 + FLT_EPSILON != 1.0
static const float FLT_EPSILON = 1.192092896e-07f;

/// Largest representable 32-bit float
static const float FLT_MAX = 3.402823466e+38f;

// ─── Engine constants ──────────────────────────────────────────────────────

/// Unit conversion: centimeters per meter (UzEngine uses cm world units)
static const float kCmPerMeter = 100.0f;

/// Minimum cosine of angle between normal and light/view direction.
/// Prevents division by zero in BRDF evaluation.
static const float kMinCosTheta = 1e-6f;

/// Minimum GGX roughness alpha to avoid singularities in the NDF.
/// @ref NVIDIA Falcor StandardBSDF.slang — kMinGGXAlpha
static const float kMinGGXAlpha = 0.0064f;

/// Sentinel SRV descriptor heap index indicating "no texture bound".
/// PerFrameCB fields use this value when a texture slot is empty.
static const uint kSentinelSrvIndex = 0xFFFFFFFFu;

#endif // UZ_COMMON_CONSTANTS_HLSLI
