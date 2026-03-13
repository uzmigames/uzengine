/// Light type definitions for multi-light shading.
///
/// Separated from lighting.hlsli so that cbuffer declarations can reference
/// LightData without pulling in SampleShadowMap and other dependencies.

#ifndef LIGHT_TYPES_HLSLI
#define LIGHT_TYPES_HLSLI

/// Maximum number of dynamic lights supported per frame.
static const uint MAX_LIGHTS = 8;

/// Per-light parameters packed to 64 bytes for cbuffer alignment.
///
/// lightType encoding uses float for cbuffer compatibility:
///   0.0 = directional, 1.0 = point, 2.0 = spot.
struct LightData {
    float3 positionOrDir;   // world position (point/spot) or direction toward light (directional)
    float  range;           // attenuation radius in world units (0 = directional)
    float3 color;           // linear HDR color * intensity
    float  lightType;       // 0 = directional, 1 = point, 2 = spot
    float3 spotDirection;   // normalized cone axis (spot only)
    float  innerConeCos;    // cos(inner half-angle)
    float  outerConeCos;    // cos(outer half-angle)
    float  _pad0;           // pad to 64 bytes
    float  _pad1;
    float  _pad2;
};

#endif // LIGHT_TYPES_HLSLI
