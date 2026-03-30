/// Packed format encode/decode utilities.
///
/// Provides CPU-compatible pack/unpack functions for compressed GPU formats:
/// R10G10B10A2, R16G16, snorm/unorm conversions.
///
/// @ref GPU format specifications (DX12, Vulkan)
/// @ref Falcor PackedFormats.slang — format packing reference
#ifndef PACKING_HLSLI
#define PACKING_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// UNORM / SNORM Conversions
// ─────────────────────────────────────────────────────────────────────────────

/// Encode float [0,1] to N-bit UNORM uint.
uint FloatToUnorm(float v, uint bits) {
    float scale = float((1u << bits) - 1u);
    return uint(saturate(v) * scale + 0.5f);
}

/// Decode N-bit UNORM uint to float [0,1].
float UnormToFloat(uint v, uint bits) {
    float scale = float((1u << bits) - 1u);
    return float(v) / scale;
}

/// Encode float [-1,1] to N-bit SNORM uint (two's complement).
uint FloatToSnorm(float v, uint bits) {
    float scale = float((1u << (bits - 1u)) - 1u);
    int signed_ = int(clamp(v, -1.0f, 1.0f) * scale + (v >= 0.0f ? 0.5f : -0.5f));
    return uint(signed_) & ((1u << bits) - 1u);
}

/// Decode N-bit SNORM uint to float [-1,1].
float SnormToFloat(uint v, uint bits) {
    float scale = float((1u << (bits - 1u)) - 1u);
    int signed_ = int(v);
    if (signed_ >= int(1u << (bits - 1u)))
        signed_ -= int(1u << bits);
    return max(float(signed_) / scale, -1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// R10G10B10A2_UNORM Pack/Unpack
// ─────────────────────────────────────────────────────────────────────────────

/// Pack float4 [0,1] to R10G10B10A2_UNORM as uint32.
uint PackR10G10B10A2(float4 v) {
    uint r = FloatToUnorm(v.x, 10);
    uint g = FloatToUnorm(v.y, 10);
    uint b = FloatToUnorm(v.z, 10);
    uint a = FloatToUnorm(v.w, 2);
    return r | (g << 10u) | (b << 20u) | (a << 30u);
}

/// Unpack R10G10B10A2_UNORM uint32 to float4 [0,1].
float4 UnpackR10G10B10A2(uint packed) {
    float r = UnormToFloat(packed & 0x3FFu, 10);
    float g = UnormToFloat((packed >> 10u) & 0x3FFu, 10);
    float b = UnormToFloat((packed >> 20u) & 0x3FFu, 10);
    float a = UnormToFloat((packed >> 30u) & 0x3u, 2);
    return float4(r, g, b, a);
}

// ─────────────────────────────────────────────────────────────────────────────
// R16G16_FLOAT Pack/Unpack (via f32tof16/f16tof32 intrinsics)
// ─────────────────────────────────────────────────────────────────────────────

/// Pack two floats into a single uint32 using R16G16_FLOAT layout.
uint PackR16G16Float(float2 v) {
    uint lo = f32tof16(v.x);
    uint hi = f32tof16(v.y);
    return lo | (hi << 16u);
}

/// Unpack R16G16_FLOAT uint32 to float2.
float2 UnpackR16G16Float(uint packed) {
    float x = f16tof32(packed & 0xFFFFu);
    float y = f16tof32(packed >> 16u);
    return float2(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// R16G16_UNORM Pack/Unpack
// ─────────────────────────────────────────────────────────────────────────────

/// Pack float2 [0,1] to R16G16_UNORM as uint32.
uint PackR16G16Unorm(float2 v) {
    uint x = FloatToUnorm(v.x, 16);
    uint y = FloatToUnorm(v.y, 16);
    return x | (y << 16u);
}

/// Unpack R16G16_UNORM uint32 to float2 [0,1].
float2 UnpackR16G16Unorm(uint packed) {
    float x = UnormToFloat(packed & 0xFFFFu, 16);
    float y = UnormToFloat(packed >> 16u, 16);
    return float2(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// R16G16_SNORM Pack/Unpack
// ─────────────────────────────────────────────────────────────────────────────

/// Pack float2 [-1,1] to R16G16_SNORM as uint32.
uint PackR16G16Snorm(float2 v) {
    uint x = FloatToSnorm(v.x, 16);
    uint y = FloatToSnorm(v.y, 16);
    return x | (y << 16u);
}

/// Unpack R16G16_SNORM uint32 to float2 [-1,1].
float2 UnpackR16G16Snorm(uint packed) {
    float x = SnormToFloat(packed & 0xFFFFu, 16);
    float y = SnormToFloat(packed >> 16u, 16);
    return float2(x, y);
}

#endif
