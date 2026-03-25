/// UzEngine Shader Permutation Key
///
/// A 64-bit packed integer that uniquely identifies a shader permutation based on
/// material properties and rendering feature flags. Used as the lookup key for
/// the `ShaderPermutationCache` to avoid redundant PSO compilations.
///
/// # Bit Layout
/// ```
/// Bits [3:0]   - shadingModel (4 bits, EShadingModel 0-9)
/// Bits [6:4]   - blendMode (3 bits, EBlendMode 0-4)
/// Bits [7]     - bindless (1 bit)
/// Bits [8]     - instanced (1 bit)
/// Bits [9]     - pom (1 bit, parallax occlusion mapping)
/// Bits [10]    - materialGraph (1 bit)
/// Bits [11]    - alphaToCoverage (1 bit)
/// Bits [12]    - ditheredOpacity (1 bit)
/// Bits [13]    - hasUV1 (1 bit)
/// Bits [14]    - hasUV2 (1 bit)
/// Bits [15]    - hasTangentMap (1 bit)
/// Bits [63:16] - reserved (must be 0)
/// ```
///
/// # Thread Safety
/// Value type. Safe to copy and compare from any thread. Construction is not
/// thread-safe if the `MaterialInterface` being read is mutated concurrently.

#pragma once

#include "engine/renderer/material/material_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class MaterialInterface;

// ---------------------------------------------------------------------------
// ShaderDefine -- a single -D flag for DXC compilation
// ---------------------------------------------------------------------------

/// Represents a preprocessor define passed to the shader compiler.
///
/// # Examples
/// ```cpp
/// ShaderDefine{"UZ_SHADING_MODEL", "2"}
/// ShaderDefine{"UZ_BINDLESS", "1"}
/// ```
struct ShaderDefine {
    std::string name;   ///< Preprocessor macro name (e.g., "UZ_SHADING_MODEL").
    std::string value;  ///< Preprocessor macro value (e.g., "2").
};

// ---------------------------------------------------------------------------
// ShaderPermutationKey -- 64-bit packed permutation identifier
// ---------------------------------------------------------------------------

/// A 64-bit packed key that uniquely identifies a shader permutation.
///
/// The key encodes material shading model, blend mode, and a set of boolean
/// feature flags into a single 64-bit integer. This allows O(1) hash-map lookups
/// in the `ShaderPermutationCache` without string comparisons or multi-field hashing.
///
/// @ref Cook & Torrance 1982 "A Reflectance Model for Computer Graphics" -- shading
///      model dimension; Karis 2013 "Real Shading in Unreal Engine 4" -- permutation-
///      based pipeline selection for material features.
class ShaderPermutationKey {
public:
    // -- Bit field positions and masks --

    static constexpr uint32_t kShadingModelBits  = 4;
    static constexpr uint32_t kShadingModelShift = 0;
    static constexpr uint64_t kShadingModelMask  = ((1ULL << kShadingModelBits) - 1) << kShadingModelShift;

    static constexpr uint32_t kBlendModeBits  = 3;
    static constexpr uint32_t kBlendModeShift = 4;
    static constexpr uint64_t kBlendModeMask  = ((1ULL << kBlendModeBits) - 1) << kBlendModeShift;

    static constexpr uint32_t kBindlessBit        = 7;
    static constexpr uint32_t kInstancedBit       = 8;
    static constexpr uint32_t kPomBit             = 9;
    static constexpr uint32_t kMaterialGraphBit   = 10;
    static constexpr uint32_t kAlphaToCoverageBit = 11;
    static constexpr uint32_t kDitheredOpacityBit = 12;
    static constexpr uint32_t kHasUV1Bit          = 13;
    static constexpr uint32_t kHasUV2Bit          = 14;
    static constexpr uint32_t kHasTangentMapBit   = 15;

    /// Mask covering all defined bits [15:0]. Reserved bits [63:16] must be zero.
    static constexpr uint64_t kUsedBitsMask = 0x0000'0000'0000'FFFFULL;

    // -- Construction --

    /// Default-constructs an all-zero key (Unlit, Opaque, no features).
    constexpr ShaderPermutationKey() noexcept = default;

    /// Constructs a permutation key from individual fields.
    ///
    /// # Parameters
    /// - `shadingModel`: Material shading model (EShadingModel, 0-9).
    /// - `blendMode`: Material blend mode (EBlendMode, 0-4).
    /// - `bindless`: True if bindless resource access is enabled.
    /// - `instanced`: True if GPU instancing is active for this draw.
    /// - `pom`: True if parallax occlusion mapping is enabled.
    /// - `materialGraph`: True if the material uses a node-graph shader.
    /// - `alphaToCoverage`: True if alpha-to-coverage MSAA is enabled.
    /// - `ditheredOpacity`: True if dithered opacity (LOD fade) is active.
    /// - `hasUV1`: True if the mesh has a second UV channel (TEXCOORD1).
    /// - `hasUV2`: True if the mesh has a third UV channel (TEXCOORD2).
    /// - `hasTangentMap`: True if a tangent-space normal map is bound.
    ShaderPermutationKey(
        EShadingModel shadingModel,
        EBlendMode    blendMode,
        bool bindless        = false,
        bool instanced       = false,
        bool pom             = false,
        bool materialGraph   = false,
        bool alphaToCoverage = false,
        bool ditheredOpacity = false,
        bool hasUV1          = false,
        bool hasUV2          = false,
        bool hasTangentMap   = false) noexcept
        : m_bits(Pack(shadingModel, blendMode, bindless, instanced, pom,
                       materialGraph, alphaToCoverage, ditheredOpacity,
                       hasUV1, hasUV2, hasTangentMap))
    {}

    // -- Factory --

    /// Constructs a permutation key from a material interface and per-draw flags.
    ///
    /// Reads shading model, blend mode, and material-level feature flags from
    /// the material. Per-draw flags (bindless, instanced, mesh UV channels) are
    /// supplied by the caller since they depend on the draw context, not the material.
    ///
    /// # Parameters
    /// - `mat`: Material to query for shading model, blend mode, and features.
    /// - `bindless`: Whether the current draw uses bindless resource access.
    /// - `instanced`: Whether the current draw is GPU-instanced.
    /// - `hasUV1`: Whether the mesh provides TEXCOORD1.
    /// - `hasUV2`: Whether the mesh provides TEXCOORD2.
    [[nodiscard]] static ShaderPermutationKey FromMaterial(
        const MaterialInterface& mat,
        bool bindless  = false,
        bool instanced = false,
        bool hasUV1    = false,
        bool hasUV2    = false);

    // -- Getters --

    /// Returns the shading model encoded in this key.
    [[nodiscard]] constexpr EShadingModel GetShadingModel() const noexcept {
        return static_cast<EShadingModel>((m_bits & kShadingModelMask) >> kShadingModelShift);
    }

    /// Returns the blend mode encoded in this key.
    [[nodiscard]] constexpr EBlendMode GetBlendMode() const noexcept {
        return static_cast<EBlendMode>((m_bits & kBlendModeMask) >> kBlendModeShift);
    }

    /// Returns true if bindless resource access is enabled.
    [[nodiscard]] constexpr bool IsBindless() const noexcept        { return (m_bits >> kBindlessBit) & 1; }

    /// Returns true if GPU instancing is active.
    [[nodiscard]] constexpr bool IsInstanced() const noexcept       { return (m_bits >> kInstancedBit) & 1; }

    /// Returns true if parallax occlusion mapping is enabled.
    [[nodiscard]] constexpr bool IsPom() const noexcept             { return (m_bits >> kPomBit) & 1; }

    /// Returns true if the material uses a node-graph shader.
    [[nodiscard]] constexpr bool IsMaterialGraph() const noexcept   { return (m_bits >> kMaterialGraphBit) & 1; }

    /// Returns true if alpha-to-coverage MSAA is enabled.
    [[nodiscard]] constexpr bool IsAlphaToCoverage() const noexcept { return (m_bits >> kAlphaToCoverageBit) & 1; }

    /// Returns true if dithered opacity (LOD fade) is active.
    [[nodiscard]] constexpr bool IsDitheredOpacity() const noexcept { return (m_bits >> kDitheredOpacityBit) & 1; }

    /// Returns true if the mesh has UV channel 1 (TEXCOORD1).
    [[nodiscard]] constexpr bool HasUV1() const noexcept            { return (m_bits >> kHasUV1Bit) & 1; }

    /// Returns true if the mesh has UV channel 2 (TEXCOORD2).
    [[nodiscard]] constexpr bool HasUV2() const noexcept            { return (m_bits >> kHasUV2Bit) & 1; }

    /// Returns true if a tangent-space normal map is bound.
    [[nodiscard]] constexpr bool HasTangentMap() const noexcept     { return (m_bits >> kHasTangentMapBit) & 1; }

    // -- Raw access --

    /// Returns the raw 64-bit packed value. Useful for serialization.
    [[nodiscard]] constexpr uint64_t GetRawBits() const noexcept { return m_bits; }

    // -- Hashing --

    /// Returns a hash suitable for use in `std::unordered_map` and similar containers.
    ///
    /// Since the key is already a well-distributed 64-bit integer with low entropy
    /// concentrated in the lower 16 bits, we apply a finalizer mix (splitmix64) to
    /// spread the bits across the full 64-bit range.
    [[nodiscard]] uint64_t GetHash() const noexcept {
        // splitmix64 finalizer -- distributes low-entropy bits across full range
        uint64_t x = m_bits;
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBULL;
        x ^= x >> 31;
        return x;
    }

    // -- Comparison --

    [[nodiscard]] constexpr bool operator==(const ShaderPermutationKey& other) const noexcept {
        return m_bits == other.m_bits;
    }

    [[nodiscard]] constexpr bool operator!=(const ShaderPermutationKey& other) const noexcept {
        return m_bits != other.m_bits;
    }

    // -- Shader defines --

    /// Converts this key into a list of preprocessor defines for DXC compilation.
    ///
    /// Each material/feature dimension produces a define:
    /// - `UZ_SHADING_MODEL=N` (0-9)
    /// - `UZ_BLEND_MODE=N` (0-4)
    /// - `UZ_BINDLESS=0|1`
    /// - `UZ_INSTANCED=0|1`
    /// - `UZ_POM=0|1`
    /// - `UZ_MATERIAL_GRAPH=0|1`
    /// - `UZ_ALPHA_TO_COVERAGE=0|1`
    /// - `UZ_DITHERED_OPACITY=0|1`
    /// - `UZ_HAS_UV1=0|1`
    /// - `UZ_HAS_UV2=0|1`
    /// - `UZ_HAS_TANGENT_MAP=0|1`
    ///
    /// # Returns
    /// A vector of `ShaderDefine` structs, one per dimension. Always 11 entries.
    [[nodiscard]] std::vector<ShaderDefine> ToDefineList() const;

    /// Returns a human-readable string describing this permutation key.
    ///
    /// Format: `"DefaultLit|Opaque|Bindless|POM"` — only enabled boolean flags
    /// are listed. Shading model and blend mode are always present.
    ///
    /// Intended for debug logging, not serialization.
    [[nodiscard]] std::string ToString() const;

private:
    /// Packs all fields into a 64-bit integer.
    [[nodiscard]] static constexpr uint64_t Pack(
        EShadingModel shadingModel,
        EBlendMode    blendMode,
        bool bindless,
        bool instanced,
        bool pom,
        bool materialGraph,
        bool alphaToCoverage,
        bool ditheredOpacity,
        bool hasUV1,
        bool hasUV2,
        bool hasTangentMap) noexcept
    {
        uint64_t bits = 0;
        bits |= (static_cast<uint64_t>(shadingModel) & 0xF) << kShadingModelShift;
        bits |= (static_cast<uint64_t>(blendMode) & 0x7)    << kBlendModeShift;
        bits |= static_cast<uint64_t>(bindless)        << kBindlessBit;
        bits |= static_cast<uint64_t>(instanced)       << kInstancedBit;
        bits |= static_cast<uint64_t>(pom)             << kPomBit;
        bits |= static_cast<uint64_t>(materialGraph)   << kMaterialGraphBit;
        bits |= static_cast<uint64_t>(alphaToCoverage) << kAlphaToCoverageBit;
        bits |= static_cast<uint64_t>(ditheredOpacity) << kDitheredOpacityBit;
        bits |= static_cast<uint64_t>(hasUV1)          << kHasUV1Bit;
        bits |= static_cast<uint64_t>(hasUV2)          << kHasUV2Bit;
        bits |= static_cast<uint64_t>(hasTangentMap)   << kHasTangentMapBit;
        return bits;
    }

    uint64_t m_bits = 0;  ///< Packed bit field. See class-level documentation for layout.
};

} // namespace uz

// ---------------------------------------------------------------------------
// std::hash specialization for use with std::unordered_map/set
// ---------------------------------------------------------------------------

template <>
/// std::hash specialization for ShaderPermutationKey.
/// Delegates to `GetHash()` which applies a splitmix64 finalizer to the packed bits.
struct std::hash<uz::ShaderPermutationKey> {
    std::size_t operator()(const uz::ShaderPermutationKey& key) const noexcept {
        return static_cast<std::size_t>(key.GetHash());
    }
};
