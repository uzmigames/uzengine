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

/// Returns the string representation of a MaterialDomain.
[[nodiscard]] const char* MaterialDomainToString(MaterialDomain domain);

/// Parses a MaterialDomain from a string. Returns Surface for unknown input.
[[nodiscard]] MaterialDomain MaterialDomainFromString(const char* str);

/// Returns the string representation of a MaterialShadingModel.
[[nodiscard]] const char* MaterialShadingModelToString(MaterialShadingModel model);

/// Parses a MaterialShadingModel from a string. Returns DefaultLit for unknown input.
[[nodiscard]] MaterialShadingModel MaterialShadingModelFromString(const char* str);

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
    std::string name;  ///< Material name for display/debug

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
