/// UzEngine Shader Parameter HLSL Generation — Generates HLSL declarations from metadata.
///
/// Converts a `ShaderParameterMetadata` into HLSL source text: a `cbuffer` declaration
/// for scalar members and `Texture2D`/`TextureCube`/`SamplerState` declarations for
/// resource members. This is used for documentation and layout validation — shaders
/// still use hand-written HLSL, but the generated text can verify C++/HLSL agreement.
///
/// # See Also
/// - `shader_parameters.h` — Metadata types.
/// - `shader_parameter_macros.h` — Macros that generate metadata.

#pragma once

#include "shader_parameters.h"

#include <cstdint>
#include <string>

namespace uz {

/// Generates HLSL `cbuffer` + texture/sampler declarations from metadata.
///
/// # Arguments
/// * `meta` — Metadata describing the parameter struct layout.
/// * `structName` — Name for the HLSL cbuffer (e.g., "PerFrameCB").
/// * `cbRegister` — HLSL register index for the cbuffer (e.g., 1 for `b1`).
///
/// # Returns
/// A string containing valid HLSL declarations. Example output:
/// ```hlsl
/// cbuffer PerFrameCB : register(b1) {
///     float4x4 viewProj;      // offset: 0, size: 64
///     float4 cameraPos;        // offset: 64, size: 16
///     float exposure;          // offset: 252, size: 4
/// };
/// Texture2D albedoMap : register(t0);
/// TextureCube envCubemap : register(t5);
/// SamplerState linearSampler : register(s0);
/// ```
///
/// # Notes
/// - Scalar member HLSL types are inferred from their byte size (heuristic).
/// - The function does NOT validate that the struct layout matches HLSL packing rules.
/// - Resource members use their `ShaderParamType` to determine the HLSL type.
///
/// # Thread Safety
/// Thread-safe — constructs and returns a new string.
[[nodiscard]] std::string GenerateHLSL(const ShaderParameterMetadata& meta,
                                        const char* structName,
                                        uint32_t cbRegister);

} // namespace uz
