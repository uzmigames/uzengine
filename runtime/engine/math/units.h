/// UzEngine Unit Conversion Constants — Single source of truth for all unit conversions.
///
/// The engine uses centimeters as its world unit (standard). All conversion
/// factors between centimeters and other unit systems are defined here to prevent
/// magic numbers scattered across the codebase.
///
/// # Coordinate System
/// Z-up, X-forward, left-handed (standard).
///
/// # Unit Table
/// | Category    | Unit         | Notes                          |
/// |-------------|--------------|--------------------------------|
/// | Distance    | Centimeters  | 1 UE unit = 1 cm               |
/// | Atmosphere  | Kilometers   | Scattering, planet radius       |
/// | Angles (C++)| Degrees      | ECS/component data              |
/// | Angles (GPU)| Radians      | Shaders                         |
/// | Temperature | Kelvin       | Light color temperature          |
/// | Time        | Seconds      | Delta time, timers               |
/// | Color       | Linear RGB   | HDR, pre-tonemapped              |

#pragma once

namespace uz::Units {

// ─── Distance conversions ──────────────────────────────────────────────────

/// Centimeters to Kilometers (1 cm = 0.00001 km).
/// CM_TO_SKY_UNIT = 0.00001
constexpr float kCmToKm = 0.00001f;

/// Kilometers to Centimeters (1 km = 100,000 cm).
constexpr float kKmToCm = 100000.0f;

/// Centimeters to Meters (1 cm = 0.01 m).
constexpr float kCmToM = 0.01f;

/// Meters to Centimeters (1 m = 100 cm).
constexpr float kMToCm = 100.0f;

// ─── Common engine distances (in centimeters) ──────────────────────────────

/// Default near clip plane (10 cm).
/// GNearClippingPlane default = 10
constexpr float kDefaultNearPlaneCm = 10.0f;

/// Default far clip plane (1 km = 100,000 cm).
constexpr float kDefaultFarPlaneCm = 100000.0f;

// ─── Atmosphere constants (in kilometers) ──────────────────────────────────

/// Earth bottom radius (km).
/// @ref Bruneton 2008 — BottomRadius default = 6360 km
constexpr float kEarthBottomRadiusKm = 6360.0f;

/// Atmosphere height above sea level (km).
/// @ref Bruneton 2008 — AtmosphereHeight default = 60 km
constexpr float kAtmosphereHeightKm = 60.0f;

/// Planet radius offset to keep camera above surface (km).
/// PlanetRadiusOffset = 0.005 km (5 meters) — keeps camera above surface
constexpr float kPlanetRadiusOffsetKm = 0.005f;

} // namespace uz::Units
