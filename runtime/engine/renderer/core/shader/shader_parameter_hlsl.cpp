/// UzEngine Shader Parameter HLSL Generation — Implementation.

#include "shader_parameter_hlsl.h"

#include <sstream>

namespace uz {
namespace {

/// Infers an HLSL type name from a scalar member's byte size.
///
/// This is a best-effort heuristic — 4 bytes could be float or uint, but float
/// is the more common case in constant buffers. The generated HLSL is for
/// documentation/validation, not compilation, so approximate types are acceptable.
const char* InferHlslType(uint32_t sizeBytes) {
    switch (sizeBytes) {
    case 4:  return "float";
    case 8:  return "float2";
    case 12: return "float3";
    case 16: return "float4";
    case 32: return "float4x2";
    case 48: return "float4x3";
    case 64: return "float4x4";
    default: return "float4";
    }
}

/// Returns the HLSL type name for a resource member.
const char* ResourceHlslType(ShaderParamType type) {
    switch (type) {
    case ShaderParamType::Texture2D:   return "Texture2D";
    case ShaderParamType::TextureCube: return "TextureCube";
    case ShaderParamType::Sampler:     return "SamplerState";
    case ShaderParamType::UAV:         return "RWTexture2D<float4>";
    default:                           return "/* unknown */";
    }
}

/// Returns the HLSL register prefix for a resource member.
char ResourceRegisterPrefix(ShaderParamType type) {
    switch (type) {
    case ShaderParamType::Texture2D:   return 't';
    case ShaderParamType::TextureCube: return 't';
    case ShaderParamType::Sampler:     return 's';
    case ShaderParamType::UAV:         return 'u';
    default:                           return '?';
    }
}

} // anonymous namespace

std::string GenerateHLSL(const ShaderParameterMetadata& meta,
                          const char* structName,
                          uint32_t cbRegister)
{
    std::ostringstream out;

    // Emit cbuffer header
    out << "cbuffer " << structName << " : register(b" << cbRegister << ") {\n";

    // Emit scalar members
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const ShaderParamMember& m = meta.members[i];
        if (m.type != ShaderParamType::Scalar) {
            continue;
        }

        out << "    " << InferHlslType(m.size) << " " << m.name
            << ";  // offset: " << m.offset << ", size: " << m.size << "\n";
    }

    out << "};\n";

    // Emit resource declarations
    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const ShaderParamMember& m = meta.members[i];
        if (m.type == ShaderParamType::Scalar) {
            continue;
        }

        out << ResourceHlslType(m.type) << " " << m.name
            << " : register(" << ResourceRegisterPrefix(m.type)
            << m.resourceSlot << ");\n";
    }

    return out.str();
}

} // namespace uz
