/// Light structs for forward pass. Stub — forward lighting at Step 7.
#ifndef LIGHT_TYPES_HLSLI
#define LIGHT_TYPES_HLSLI
static const uint MAX_LIGHTS = 8;
struct LightData {
    float4 position;
    float4 direction;
    float4 color;
    float4 params;
};
#endif
