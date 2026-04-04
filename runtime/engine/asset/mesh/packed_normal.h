/// UzEngine Packed Normal/Tangent Encoding
///
/// Compresses float3 normals and float4 tangents from 28 bytes to 8 bytes
/// per vertex (71% savings). Uses int8x4 encoding: each component is
/// `int8(v * 127)`, giving 1/127 ≈ 0.8° angular precision.
///
/// @ref UE5 FPackedNormal — same encoding used by Unreal Engine

#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace uz {

/// Packed normal: 4 bytes (int8 x4). w = 0 for normals.
/// @ref UE5 FPackedNormal (Engine/Core/Public/Math/PackedVector.h)
struct PackedNormal {
    int8_t x, y, z, w;

    /// Encode a unit float3 normal to packed int8x4.
    static PackedNormal Encode(float nx, float ny, float nz) {
        return {
            static_cast<int8_t>(std::clamp(static_cast<int>(nx * 127.0f), -127, 127)),
            static_cast<int8_t>(std::clamp(static_cast<int>(ny * 127.0f), -127, 127)),
            static_cast<int8_t>(std::clamp(static_cast<int>(nz * 127.0f), -127, 127)),
            0
        };
    }

    /// Decode packed int8x4 to float3 normal.
    void Decode(float& nx, float& ny, float& nz) const {
        nx = static_cast<float>(x) / 127.0f;
        ny = static_cast<float>(y) / 127.0f;
        nz = static_cast<float>(z) / 127.0f;
    }
};

static_assert(sizeof(PackedNormal) == 4, "PackedNormal must be 4 bytes");

/// Packed tangent: 4 bytes (int8 x4). w = bitangent sign (+127 or -127).
struct PackedTangent {
    int8_t x, y, z, w;

    /// Encode a unit float3 tangent + sign to packed int8x4.
    static PackedTangent Encode(float tx, float ty, float tz, float sign) {
        return {
            static_cast<int8_t>(std::clamp(static_cast<int>(tx * 127.0f), -127, 127)),
            static_cast<int8_t>(std::clamp(static_cast<int>(ty * 127.0f), -127, 127)),
            static_cast<int8_t>(std::clamp(static_cast<int>(tz * 127.0f), -127, 127)),
            static_cast<int8_t>(sign >= 0.0f ? 127 : -127)
        };
    }

    /// Decode packed int8x4 to float4 tangent (xyz + sign w).
    void Decode(float& tx, float& ty, float& tz, float& sign) const {
        tx = static_cast<float>(x) / 127.0f;
        ty = static_cast<float>(y) / 127.0f;
        tz = static_cast<float>(z) / 127.0f;
        sign = (w >= 0) ? 1.0f : -1.0f;
    }
};

static_assert(sizeof(PackedTangent) == 4, "PackedTangent must be 4 bytes");

} // namespace uz
