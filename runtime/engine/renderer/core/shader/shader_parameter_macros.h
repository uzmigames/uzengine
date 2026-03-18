/// UzEngine Shader Parameter Macros — UE5-style declarative CB + resource binding.
///
/// Provides macros for declaring shader parameter structs with automatic metadata
/// generation. Each macro-declared struct has a static `GetMetadata()` method that
/// returns a `ShaderParameterMetadata` describing which members are scalar (copied
/// to the constant buffer) and which are resource handles (bound as SRV/sampler/UAV).
///
/// # Design
/// Follows UE5's `BEGIN_SHADER_PARAMETER_STRUCT` / `SHADER_PARAMETER` pattern,
/// simplified for UzEngine. Instead of UE5's typechain trick, each macro appends
/// to a static member list built inside `GetMetadata()`.
///
/// The struct is split into two sections:
/// 1. **Scalar section** (offset 0 to `cbSize`): Contiguous memory copied to the CB.
///    Must be laid out first. Padded to 256-byte alignment.
/// 2. **Resource section** (after scalar section): RHI handles for textures, samplers,
///    and UAVs. NOT copied to the CB — bound via descriptor binding calls.
///
/// # Usage
/// ```cpp
/// BEGIN_SHADER_PARAMETER_STRUCT(MyPassParams)
///     SHADER_PARAMETER(Mat4, viewProj)
///     SHADER_PARAMETER(Vec4, cameraPos)
///     SHADER_PARAMETER(float, exposure)
///     SHADER_PARAMETER_ARRAY(float, _pad, 3)  // explicit padding
/// SHADER_PARAMETER_SCALAR_END()
///     SHADER_PARAMETER_TEXTURE(TextureCube, envCubemap, 5)
///     SHADER_PARAMETER_TEXTURE(Texture2D, brdfLut, 7)
///     SHADER_PARAMETER_SAMPLER(SamplerState, linearSampler, 0)
/// END_SHADER_PARAMETER_STRUCT()
/// ```
///
/// # Alignment
/// - The scalar section size is 256-byte aligned (D3D12 CB alignment).
/// - Users must manually pad the scalar section to 16-byte boundaries for HLSL
///   compatibility (float4 alignment within cbuffers).
///
/// # Limitations
/// - Members must be declared in the exact order: all scalars first, then all resources.
/// - No automatic padding injection — the user is responsible for HLSL-compatible alignment.
/// - No compile-time member enumeration (metadata is constructed at first `GetMetadata()` call).
///
/// # See Also
/// - `shader_parameters.h` — `ShaderParameterMetadata` and `ShaderParamMember` types.
/// - `shader_parameter_binder.h` — Binds macro-declared structs to command lists.

#pragma once

#include "shader_parameters.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Type trait: maps C++ types to ShaderParamType for scalar members
// ─────────────────────────────────────────────────────────────────────────────

/// Type trait that classifies a C++ type as a scalar shader parameter type.
///
/// All types used in SHADER_PARAMETER macros must be trivially copyable.
/// The trait returns `ShaderParamType::Scalar` for all non-resource types.
/// Resource types (RHITextureHandle, RHISamplerHandle) are handled by
/// separate macros (SHADER_PARAMETER_TEXTURE, SHADER_PARAMETER_SAMPLER).
template <typename T>
struct ShaderParamTypeTrait {
    static constexpr ShaderParamType value = ShaderParamType::Scalar;
    static_assert(std::is_trivially_copyable_v<T>,
                  "Shader parameter types must be trivially copyable");
};

// ─────────────────────────────────────────────────────────────────────────────
// HLSL type name mapping — used by GenerateHLSL()
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the HLSL type name for a C++ type used in a shader parameter struct.
///
/// Specializations exist for common types (float, uint32_t, Vec2, Vec3, Vec4, Mat4).
/// The default returns "float4" as a safe fallback for unknown types.
template <typename T>
struct HlslTypeName {
    static constexpr const char* value = "/* unknown */";
};

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// BEGIN_SHADER_PARAMETER_STRUCT — opens the struct definition
// ─────────────────────────────────────────────────────────────────────────────

/// Opens a shader parameter struct definition.
///
/// Must be followed by SHADER_PARAMETER declarations for scalar members,
/// then SHADER_PARAMETER_SCALAR_END(), then resource members, then
/// END_SHADER_PARAMETER_STRUCT().
///
/// The struct is 16-byte aligned to satisfy GPU constant buffer rules.
#define BEGIN_SHADER_PARAMETER_STRUCT(StructName)                              \
    struct alignas(16) StructName {                                            \
        using ThisType = StructName;                                           \
        static constexpr const char* kStructName = #StructName;

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER — declares a scalar member (goes into CB)
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a scalar member that will be copied into the constant buffer.
///
/// Must appear between BEGIN_SHADER_PARAMETER_STRUCT and SHADER_PARAMETER_SCALAR_END.
/// The member name should match the HLSL variable name for documentation/validation.
///
/// # Arguments
/// - `Type`: C++ type (float, uint32_t, Vec4, Mat4, etc.)
/// - `Name`: Member name (matches HLSL variable for validation)
#define SHADER_PARAMETER(Type, Name) Type Name {};

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER_ARRAY — declares a scalar array member (goes into CB)
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a scalar array member that will be copied into the constant buffer.
///
/// Useful for array uniforms (e.g., cascade matrices) and explicit padding.
///
/// # Arguments
/// - `Type`: C++ element type (float, Vec4, Mat4, etc.)
/// - `Name`: Member name
/// - `Count`: Number of elements in the array
#define SHADER_PARAMETER_ARRAY(Type, Name, Count) Type Name[Count] {};

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER_SCALAR_END — marks the boundary between scalar and resource sections
// ─────────────────────────────────────────────────────────────────────────────

/// Marks the conceptual boundary between scalar and resource sections.
///
/// This is a documentation-only macro — it produces no code. The actual CB size
/// is specified in END_SHADER_PARAMETER_METADATA. All SHADER_PARAMETER_TEXTURE
/// and SHADER_PARAMETER_SAMPLER declarations must come AFTER this marker.
#define SHADER_PARAMETER_SCALAR_END() /* boundary marker — scalar section ends here */

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER_TEXTURE — declares a texture resource member
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a texture resource member (NOT copied to CB).
///
/// The texture is bound by `ShaderParameterBinder` at the specified HLSL
/// register slot (tN) on SM5. On SM6.6 bindless, binding is skipped.
///
/// # Arguments
/// - `HlslType`: HLSL texture type as a name token (Texture2D or TextureCube)
/// - `Name`: Member name
/// - `Slot`: HLSL register slot index (the N in `register(tN)`)
#define SHADER_PARAMETER_TEXTURE(HlslType, Name, Slot)                       \
    ::uz::RHITextureHandle Name;

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER_SAMPLER — declares a sampler resource member
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a sampler resource member (NOT copied to CB).
///
/// # Arguments
/// - `Name`: Member name
/// - `Slot`: HLSL register slot index (the N in `register(sN)`)
#define SHADER_PARAMETER_SAMPLER(Name, Slot)                                  \
    ::uz::RHISamplerHandle Name;

// ─────────────────────────────────────────────────────────────────────────────
// SHADER_PARAMETER_UAV — declares a UAV resource member
// ─────────────────────────────────────────────────────────────────────────────

/// Declares a UAV resource member (NOT copied to CB).
///
/// # Arguments
/// - `Name`: Member name
/// - `Slot`: HLSL register slot index (the N in `register(uN)`)
#define SHADER_PARAMETER_UAV(Name, Slot)                                      \
    ::uz::RHITextureHandle Name;

// ─────────────────────────────────────────────────────────────────────────────
// END_SHADER_PARAMETER_STRUCT — closes the struct and generates GetMetadata()
// ─────────────────────────────────────────────────────────────────────────────

/// Closes the shader parameter struct definition.
///
/// Generates the `GetMetadata()` static method stub. The actual metadata
/// must be defined in a .cpp file using IMPLEMENT_SHADER_PARAMETER_METADATA.
#define END_SHADER_PARAMETER_STRUCT()                                         \
        static const ::uz::ShaderParameterMetadata& GetMetadata();            \
    };

// ─────────────────────────────────────────────────────────────────────────────
// IMPLEMENT_SHADER_PARAMETER_METADATA — defines GetMetadata() in a .cpp file
// ─────────────────────────────────────────────────────────────────────────────

/// Helper macro to begin a metadata member array definition in a .cpp file.
///
/// Usage in .cpp:
/// ```cpp
/// BEGIN_SHADER_PARAMETER_METADATA(MyPassParams)
///     METADATA_SCALAR(MyPassParams, viewProj, Mat4)
///     METADATA_SCALAR(MyPassParams, exposure, float)
///     METADATA_TEXTURE(MyPassParams, envCubemap, TextureCube, 5)
/// END_SHADER_PARAMETER_METADATA(MyPassParams, 256, 1, 0, 0)
/// ```
#define BEGIN_SHADER_PARAMETER_METADATA(StructName)                           \
    const ::uz::ShaderParameterMetadata& StructName::GetMetadata() {          \
        static const ::uz::ShaderParamMember kMembers[] = {

/// Declares a scalar member entry in the metadata member array.
///
/// # Arguments
/// - `Struct`: The parameter struct type name
/// - `Name`: Member variable name (must match the struct field)
/// - `Type`: C++ type (used for sizeof)
#define METADATA_SCALAR(Struct, Name, Type)                                   \
    {#Name, ::uz::ShaderParamType::Scalar,                                    \
     static_cast<uint32_t>(offsetof(Struct, Name)),                           \
     static_cast<uint32_t>(sizeof(Type)), 0},

/// Declares a scalar array member entry in the metadata member array.
///
/// # Arguments
/// - `Struct`: The parameter struct type name
/// - `Name`: Member variable name
/// - `Type`: C++ element type
/// - `Count`: Number of elements
#define METADATA_SCALAR_ARRAY(Struct, Name, Type, Count)                      \
    {#Name, ::uz::ShaderParamType::Scalar,                                    \
     static_cast<uint32_t>(offsetof(Struct, Name)),                           \
     static_cast<uint32_t>(sizeof(Type) * (Count)), 0},

/// Declares a Texture2D member entry in the metadata.
#define METADATA_TEXTURE2D(Struct, Name, Slot)                                \
    {#Name, ::uz::ShaderParamType::Texture2D,                                 \
     static_cast<uint32_t>(offsetof(Struct, Name)), 0, (Slot)},

/// Declares a TextureCube member entry in the metadata.
#define METADATA_TEXTURE_CUBE(Struct, Name, Slot)                             \
    {#Name, ::uz::ShaderParamType::TextureCube,                               \
     static_cast<uint32_t>(offsetof(Struct, Name)), 0, (Slot)},

/// Declares a Sampler member entry in the metadata.
#define METADATA_SAMPLER(Struct, Name, Slot)                                  \
    {#Name, ::uz::ShaderParamType::Sampler,                                   \
     static_cast<uint32_t>(offsetof(Struct, Name)), 0, (Slot)},

/// Declares a UAV member entry in the metadata.
#define METADATA_UAV(Struct, Name, Slot)                                      \
    {#Name, ::uz::ShaderParamType::UAV,                                       \
     static_cast<uint32_t>(offsetof(Struct, Name)), 0, (Slot)},

/// Closes the metadata member array and creates the static metadata instance.
///
/// # Arguments
/// - `StructName`: The parameter struct type name
/// - `CbSize`: Constant buffer size in bytes (must be 256-byte aligned)
/// - `TexCount`: Number of Texture2D + TextureCube members
/// - `SamplerCount`: Number of Sampler members
/// - `UavCount`: Number of UAV members
#define END_SHADER_PARAMETER_METADATA(StructName, CbSize, TexCount,           \
                                       SamplerCount, UavCount)                \
        };                                                                    \
        static_assert((CbSize) % 256 == 0,                                   \
                      #StructName " cbSize must be 256-byte aligned");        \
        static const ::uz::ShaderParameterMetadata kMetadata = {              \
            kMembers,                                                         \
            static_cast<uint32_t>(sizeof(kMembers) / sizeof(kMembers[0])),    \
            (CbSize), (TexCount), (SamplerCount), (UavCount),                 \
        };                                                                    \
        return kMetadata;                                                     \
    }
