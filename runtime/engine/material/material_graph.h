/// UzEngine Material Graph — Node Graph for Procedural Material Authoring
///
/// A MaterialGraph owns a flat vector of MaterialExpression objects and
/// provides methods to add/remove expressions, connect pins, validate
/// topology, compute content hashes, and serialize to/from JSON (.uzmat).
///
/// The graph always contains exactly one PBROutput expression (id=1) which
/// serves as the root for topological sort and dead-code elimination.
///
/// Owns an array of material expressions with connection topology.
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only. The compiler takes
/// a const reference to produce HLSL output without mutation.

#pragma once

#include "material_expression.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Material Domain & Shading Model
// ─────────────────────────────────────────────────────────────────────────────

/// Material domain — determines the rendering context.
enum class MaterialDomain : uint8_t {
    Surface,      ///< Standard lit geometry (deferred/forward)
    PostProcess,  ///< Fullscreen post-process effect
    Decal,        ///< Projected decal
    UI,           ///< 2D UI widget
};

/// Material shading model.
enum class MaterialShadingModel : uint8_t {
    DefaultLit,   ///< Standard PBR (Cook-Torrance + Burley)
    Unlit,        ///< No lighting, emissive only
    Subsurface,   ///< SSS approximation
};

/// How the material composites against what is already in the target.
///
/// Consumers beyond blending care about this too: `Translucent` and `Additive`
/// materials are excluded from shadow casting and from the distance-field
/// occluder set, because treating glass as a solid occluder blocks both the
/// shadow map and the GI trace.
enum class MaterialGraphBlendMode : uint8_t { Opaque, Masked, Translucent, Additive };

/// Returns the string representation of a MaterialDomain.
[[nodiscard]] const char* MaterialDomainToString(MaterialDomain domain);

/// Parses a MaterialDomain from a string. Returns Surface for unknown input.
[[nodiscard]] MaterialDomain MaterialDomainFromString(const char* str);

/// Returns the string representation of a MaterialShadingModel.
[[nodiscard]] const char* MaterialShadingModelToString(MaterialShadingModel model);

/// Parses a MaterialShadingModel from a string. Returns DefaultLit for unknown input.
[[nodiscard]] MaterialShadingModel MaterialShadingModelFromString(const char* str);

// ─────────────────────────────────────────────────────────────────────────────
// MaterialParams — standard PBR material knobs (UE5-style)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-material multipliers / tints applied AFTER the graph computes its
/// raw outputs. Mirrors the standard UE5 / Unity / glTF material parameters
/// every authored material needs even when it's just a texture passthrough.
///
/// Wired into the generated `ComputeMaterialOutputs()` epilogue by the
/// HLSL translator: each output is multiplied by its corresponding scalar
/// (or color) at the end of the function. With all defaults at 1.0 the
/// material renders identical to a graph without overrides.
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — F0 baseline 0.04
/// @ref glTF 2.0 PBR specification — pbrMetallicRoughness factor fields
struct MaterialParams {
    /// Multiplied into baseColor at the very end of the graph. Default white.
    float baseColorTint[3] = {1.0f, 1.0f, 1.0f};

    /// Multiplied into roughness. Useful for making glTF assets less rough.
    /// Default 1.0.
    float roughnessMultiplier = 1.0f;

    /// Multiplied into metallic. Useful for clamping spurious metal noise.
    /// Default 1.0.
    float metallicMultiplier  = 1.0f;

    /// Multiplied into emissive RGB. Default 1.0.
    float emissiveMultiplier  = 1.0f;

    /// Tangent-space normal strength (0 = flat, 1 = full bumps).
    /// Default 1.0.
    float normalStrength      = 1.0f;

    /// Specular intensity multiplier (UE5 "Specular" param). Scales the
    /// dielectric F0 baseline (0.04). Default 1.0 = standard 4% F0.
    /// @ref Karis 2013
    float specularMultiplier  = 1.0f;

    /// Multiplied into AO. Use < 1 to soften baked AO. Default 1.0.
    float aoMultiplier        = 1.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialGraph
// ─────────────────────────────────────────────────────────────────────────────

/// A material graph — flat array of expressions with connection topology.
///
/// Owns an array of material expressions with connection topology.
/// The graph always contains exactly one PBROutput expression (id=1).
class MaterialGraph {
public:
    MaterialGraph();

    /// Add an expression of the given type. Returns a reference to the new expression.
    /// Adding another expression may invalidate references; retain IDs and use FindExpression.
    MaterialExpression& AddExpression(MatExprType type);

    /// Remove an expression by ID. Disconnects all its pins first.
    /// Cannot remove PBROutput (id=1).
    void RemoveExpression(uint32_t exprId);

    /// Finds an expression node by ID. Returns nullptr if not found.
    [[nodiscard]] MaterialExpression* FindExpression(uint32_t exprId);
    /// Const overload — finds an expression node by ID.
    [[nodiscard]] const MaterialExpression* FindExpression(uint32_t exprId) const;

    /// Connect srcExpr.output[srcPin] -> dstExpr.input[dstPin].
    ///
    /// Returns false if: expression not found, pin not found, would create
    /// cycle, or types are incompatible. Writes reason to outError if non-null.
    bool Connect(uint32_t srcExprId, uint32_t srcPinId,
                 uint32_t dstExprId, uint32_t dstPinId,
                 std::string* outError = nullptr);

    /// Disconnect an input pin, clearing its connection source.
    void Disconnect(uint32_t exprId, uint32_t inputPinId);

    /// Validate the graph structure.
    ///
    /// Checks: PBROutput exists, no dangling connections, no cycles.
    /// Returns true if valid; writes reason to outError on failure.
    [[nodiscard]] bool Validate(std::string& outError) const;

    /// Topological sort of expression IDs (leaf constants first, PBROutput last).
    ///
    /// Only includes expressions reachable from the PBR output node
    /// (dead-code elimination). Returns empty vector if graph has cycles.
    [[nodiscard]] std::vector<uint32_t> TopologicalSort() const;

    /// Compute content hash (xxHash64) over graph topology and parameters.
    ///
    /// Deterministic: same graph always produces the same hash regardless
    /// of expression insertion order (expressions are sorted by ID).
    [[nodiscard]] uint64_t ComputeContentHash() const;

    /// Serialize to JSON string (pretty-printed).
    [[nodiscard]] std::string ToJson() const;

    /// Deserialize from JSON string. Returns false on parse error.
    bool FromJson(const std::string& json, std::string* outError = nullptr);

    /// Save to .uzmat file (writes JSON to disk).
    [[nodiscard]] bool SaveToFile(const std::string& path) const;

    /// Load from .uzmat file (reads JSON from disk).
    bool LoadFromFile(const std::string& path, std::string* outError = nullptr);

    /// Returns the PBR output expression (always exists, id=1).
    [[nodiscard]] MaterialExpression& GetPBROutput() { return expressions_[0]; }
    /// Const overload — returns the PBR output expression.
    [[nodiscard]] const MaterialExpression& GetPBROutput() const { return expressions_[0]; }

    /// All expressions in the graph.
    [[nodiscard]] const std::vector<MaterialExpression>& GetExpressions() const { return expressions_; }

    /// Graph metadata.
    MaterialDomain domain = MaterialDomain::Surface;
    MaterialShadingModel shadingModel = MaterialShadingModel::DefaultLit;
    MaterialGraphBlendMode blendMode = MaterialGraphBlendMode::Opaque;
    bool twoSided = false;
    std::string name;  ///< Material name for display/debug

    /// Standard PBR knobs (UE5-style). Applied as post-multiplications by
    /// the HLSL translator after the graph computes its raw outputs. See
    /// MaterialParams above.
    MaterialParams params;

private:
    std::vector<MaterialExpression> expressions_;
    uint32_t nextExprId_ = 2;  ///< 1 is reserved for PBROutput

    /// Check if connecting srcExprId -> dstExprId would create a cycle.
    [[nodiscard]] bool WouldCreateCycle(uint32_t srcExprId, uint32_t dstExprId) const;

    /// Check if source pin type can connect to destination pin type.
    ///
    /// Allows auto-promotion: Float -> Float2/3/4 (splat), Float2 -> Float3/4 (extend).
    /// Texture only connects to Texture.
    [[nodiscard]] bool IsTypeCompatible(MatPinType src, MatPinType dst) const;

    /// Recursive DFS helper for topological sort.
    void TopoVisit(uint32_t exprId, std::vector<bool>& visited,
                   std::vector<bool>& inStack, std::vector<uint32_t>& order,
                   bool& hasCycle) const;
};

} // namespace uz
