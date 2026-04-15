#pragma once

/// Node-graph data model for the VFX editor — Niagara-parity.
///
/// Authors wire VFX modules together in a visual graph: each node is a
/// module or expression, each wire carries a typed value (float / Vec3 /
/// Vec4 / bool) between an output pin and an input pin. The emitter's
/// simulation stages (phase 7.6.0) consume the resolved graph; the
/// editor UX (widget rendering, drag handles, context menus) lives on
/// top of the data model frozen here.
///
/// The graph data is intentionally UI-framework-agnostic — a pure POD
/// shape that survives JSON round-trip, hot-reload, and scripted graph
/// generation. The visual widget (deferred to
/// `phase7_7.15.1-vfx-node-graph-editor-widget`) renders the model; the
/// runtime compiler that lowers the graph into HLSL stages is tracked
/// by the existing phase 7.6.0 follow-up.
///
/// # Value types
///
/// Pins carry a single typed value. Only a handful of types — enough to
/// express 90% of authored modules — are supported; richer types
/// (textures, structured buffers) bind through data interfaces rather
/// than wires.
///
/// # Canonical JSON
///
/// ```json
/// {
///   "format":  "uzvfxgraph",
///   "version": 1,
///   "nodes": [
///     { "id": "n0", "module": "Spawn",      "position": {"x": 0,   "y": 0} },
///     { "id": "n1", "module": "ColorRamp",  "position": {"x": 300, "y": 0} },
///     { "id": "n2", "module": "Billboard",  "position": {"x": 600, "y": 0} }
///   ],
///   "wires": [
///     { "from": "n0:spawnCount", "to": "n1:particleCount", "type": "float" },
///     { "from": "n1:color",      "to": "n2:color",         "type": "vec4"  }
///   ]
/// }
/// ```
///
/// @ref UE5 SNiagaraGraph + FNiagaraEmitterGraphEditor
/// @ref phase7_7.15.1-vfx-node-graph-editor-widget

#include "vector2.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class IVFS;

// ─────────────────────────────────────────────────────────────────────────────
// Pin value types — the wire schema.
// ─────────────────────────────────────────────────────────────────────────────

/// Supported wire data types. Richer types (textures, structured
/// buffers) bind via Data Interfaces; the wire format covers authored
/// module-to-module plumbing.
enum class VfxPinType : uint8_t {
    Float = 0,
    Vec2  = 1,
    Vec3  = 2,
    Vec4  = 3,
    Bool  = 4,
    Int   = 5,
};

/// Human-readable + JSON-round-trip helpers for the pin type enum.
[[nodiscard]] const char* VfxPinTypeToString(VfxPinType type) noexcept;
[[nodiscard]] bool VfxPinTypeFromString(const std::string& s, VfxPinType& out) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Node + wire data model
// ─────────────────────────────────────────────────────────────────────────────

/// Single node in the graph. `id` is a stable string used to reference
/// the node from wires + editor persistence; must be unique within a
/// graph. `module` selects the runtime module class (matched case-
/// sensitive against the module registry at compile time).
struct VfxGraphNode {
    std::string id;
    std::string module;
    Vec2        position {};  ///< Editor layout cache; 0,0 on fresh nodes.
};

/// Connection between two pins. Endpoints use the `<nodeId>:<pinName>`
/// form so the graph is flat-text-diffable and stable under pin
/// reordering.
struct VfxGraphWire {
    std::string fromEndpoint;  ///< e.g. "n0:spawnCount"
    std::string toEndpoint;    ///< e.g. "n1:particleCount"
    VfxPinType  type = VfxPinType::Float;
};

// ─────────────────────────────────────────────────────────────────────────────
// Graph container
// ─────────────────────────────────────────────────────────────────────────────

/// Full graph owned by an emitter or system asset. Mutations preserve
/// key invariants (unique node IDs); helpers exist so authors don't have
/// to write the validation boilerplate at every call site.
struct VfxNodeGraph {
    std::vector<VfxGraphNode> nodes;
    std::vector<VfxGraphWire> wires;

    /// Inserts a new node preserving the unique-`id` invariant. Returns
    /// `true` on success; `false` (and does not insert) when the id is
    /// empty or already in use.
    bool AddNode(const VfxGraphNode& node);

    /// Removes the node with the given id and every wire touching it.
    /// Returns the number of deleted entries (nodes + wires).
    uint32_t RemoveNode(const std::string& id);

    /// Inserts a new wire. Returns `false` when either endpoint is empty.
    /// Does NOT validate that the endpoints resolve to existing nodes —
    /// that's the compiler's concern; graph editing UX can surface
    /// dangling wires separately.
    bool AddWire(const VfxGraphWire& wire);

    /// Drops every wire touching `endpoint` (exact match). Useful when
    /// an author disconnects a pin in the UI. Returns count removed.
    uint32_t RemoveWiresTouching(const std::string& endpoint);

    /// Finds a node by id. Returns `nullptr` when not found.
    [[nodiscard]] const VfxGraphNode* FindNode(const std::string& id) const noexcept;

    /// Node count.
    [[nodiscard]] size_t NodeCount() const noexcept { return nodes.size(); }

    /// Wire count.
    [[nodiscard]] size_t WireCount() const noexcept { return wires.size(); }

    /// Clears every node + wire.
    void Clear() noexcept { nodes.clear(); wires.clear(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// JSON asset — round-trip for editor persistence.
// ─────────────────────────────────────────────────────────────────────────────

/// Endpoint string parser. Returns `true` when the `<node>:<pin>` form
/// is well-formed (non-empty node, colon, non-empty pin). Captures the
/// two halves into `outNode` / `outPin` on success.
[[nodiscard]] bool VfxParseEndpoint(const std::string& endpoint,
                                      std::string& outNode,
                                      std::string& outPin) noexcept;

/// `.uzvfxgraph` JSON load / save helpers.
struct VfxNodeGraphAsset {
    [[nodiscard]] static bool LoadFromString(const std::string& jsonStr,
                                              VfxNodeGraph& outGraph);
    [[nodiscard]] static bool Load(const std::string& path,
                                    VfxNodeGraph& outGraph,
                                    IVFS* vfs = nullptr);

    [[nodiscard]] static std::string SaveToString(const VfxNodeGraph& graph);
    [[nodiscard]] static bool SaveToFile(const VfxNodeGraph& graph,
                                          const std::string& path);
};

} // namespace uz
