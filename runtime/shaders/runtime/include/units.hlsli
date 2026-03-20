/// UzEngine Shader Unit Constants — Single source of truth for GPU-side unit conversions.
///
/// Mirror of engine/math/units.h for HLSL. All conversion factors between
/// centimeters and other unit systems are defined here.
///
/// Include this file instead of hardcoding magic numbers like 0.00001f or 100000.0f.
///
/// @ref SI unit conversions — standard metric system factors

#ifndef UNITS_HLSLI
#define UNITS_HLSLI

// ─── Distance conversions ──────────────────────────────────────────────────

/// Centimeters to Kilometers (1 cm = 0.00001 km).
/// @ref SI unit conversion — 1 cm = 1e-5 km
/// @constants 0.00001 (SI definition)
/// @deviations NONE
static const float UZ_CM_TO_KM = 0.00001f;

/// Kilometers to Centimeters (1 km = 100,000 cm).
static const float UZ_KM_TO_CM = 100000.0f;

/// Centimeters to Meters (1 cm = 0.01 m).
static const float UZ_CM_TO_M = 0.01f;

/// Meters to Centimeters (1 m = 100 cm).
static const float UZ_M_TO_CM = 100.0f;

// ─── Common engine distances (in centimeters) ──────────────────────────────

/// Default far clip plane (1 km = 100,000 cm).
static const float UZ_DEFAULT_FAR_PLANE_CM = 100000.0f;

// ─── Atmosphere constants (in kilometers) ──────────────────────────────────

/// Planet radius offset to keep camera above surface (km).
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — offset to avoid surface intersection
/// @constants 0.005 km = 5 meters above surface
/// @deviations NONE
static const float UZ_PLANET_RADIUS_OFFSET_KM = 0.005f;

/// Small epsilon to prevent division by zero in atmospheric integration.
static const float UZ_EXTINCTION_EPS = 1e-7f;

#endif // UNITS_HLSLI
