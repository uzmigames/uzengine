#include "vfx_node_graph.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxPinType string round-trip
// ─────────────────────────────────────────────────────────────────────────────

const char* VfxPinTypeToString(VfxPinType type) noexcept {
    switch (type) {
        case VfxPinType::Float: return "float";
        case VfxPinType::Vec2:  return "vec2";
        case VfxPinType::Vec3:  return "vec3";
        case VfxPinType::Vec4:  return "vec4";
        case VfxPinType::Bool:  return "bool";
        case VfxPinType::Int:   return "int";
    }
    return "float";
}

bool VfxPinTypeFromString(const std::string& s, VfxPinType& out) noexcept {
    if (s == "float") { out = VfxPinType::Float; return true; }
    if (s == "vec2")  { out = VfxPinType::Vec2;  return true; }
    if (s == "vec3")  { out = VfxPinType::Vec3;  return true; }
    if (s == "vec4")  { out = VfxPinType::Vec4;  return true; }
    if (s == "bool")  { out = VfxPinType::Bool;  return true; }
    if (s == "int")   { out = VfxPinType::Int;   return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Endpoint parser
// ─────────────────────────────────────────────────────────────────────────────

bool VfxParseEndpoint(const std::string& endpoint,
                       std::string& outNode,
                       std::string& outPin) noexcept {
    const auto pos = endpoint.find(':');
    if (pos == std::string::npos) return false;
    if (pos == 0u || pos + 1u >= endpoint.size()) return false;
    outNode.assign(endpoint, 0, pos);
    outPin.assign(endpoint, pos + 1u, std::string::npos);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxNodeGraph editing
// ─────────────────────────────────────────────────────────────────────────────

bool VfxNodeGraph::AddNode(const VfxGraphNode& node) {
    if (node.id.empty()) return false;
    if (FindNode(node.id) != nullptr) return false;
    nodes.push_back(node);
    return true;
}

uint32_t VfxNodeGraph::RemoveNode(const std::string& id) {
    if (id.empty()) return 0u;
    uint32_t removed = 0u;

    // Delete matching nodes.
    const auto before = nodes.size();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                 [&id](const VfxGraphNode& n) {
                                     return n.id == id;
                                 }),
                 nodes.end());
    removed += static_cast<uint32_t>(before - nodes.size());

    // Delete wires that touch the removed id at either end.
    const auto wiresBefore = wires.size();
    wires.erase(std::remove_if(wires.begin(), wires.end(),
                                 [&id](const VfxGraphWire& w) {
                                     std::string node, pin;
                                     // Either endpoint references the removed node.
                                     if (VfxParseEndpoint(w.fromEndpoint, node, pin) && node == id) return true;
                                     if (VfxParseEndpoint(w.toEndpoint,   node, pin) && node == id) return true;
                                     return false;
                                 }),
                 wires.end());
    removed += static_cast<uint32_t>(wiresBefore - wires.size());
    return removed;
}

bool VfxNodeGraph::AddWire(const VfxGraphWire& wire) {
    if (wire.fromEndpoint.empty() || wire.toEndpoint.empty()) return false;
    wires.push_back(wire);
    return true;
}

uint32_t VfxNodeGraph::RemoveWiresTouching(const std::string& endpoint) {
    const auto before = wires.size();
    wires.erase(std::remove_if(wires.begin(), wires.end(),
                                 [&endpoint](const VfxGraphWire& w) {
                                     return w.fromEndpoint == endpoint ||
                                             w.toEndpoint == endpoint;
                                 }),
                 wires.end());
    return static_cast<uint32_t>(before - wires.size());
}

const VfxGraphNode* VfxNodeGraph::FindNode(const std::string& id) const noexcept {
    for (const auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxNodeGraphAsset — JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

bool VfxNodeGraphAsset::LoadFromString(const std::string& jsonStr,
                                         VfxNodeGraph& outGraph) {
    outGraph.Clear();
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VfxNodeGraphAsset: JSON parse error: {}", e.what());
        return false;
    }
    if (!root.is_object()) {
        UZ_LOG(VFX, Error, "VfxNodeGraphAsset: root must be an object");
        return false;
    }
    if (root.value("format", std::string{}) != "uzvfxgraph") {
        UZ_LOG(VFX, Error, "VfxNodeGraphAsset: unexpected or missing 'format'");
        return false;
    }

    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const auto& n : root["nodes"]) {
            if (!n.is_object()) continue;
            VfxGraphNode node;
            node.id     = n.value("id",     std::string{});
            node.module = n.value("module", std::string{});
            if (n.contains("position") && n["position"].is_object()) {
                node.position.x = n["position"].value("x", 0.0f);
                node.position.y = n["position"].value("y", 0.0f);
            }
            if (node.id.empty() || node.module.empty()) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphAsset: node missing id/module, skipping");
                continue;
            }
            if (!outGraph.AddNode(node)) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphAsset: duplicate node id '{}', skipping",
                       node.id);
            }
        }
    }

    if (root.contains("wires") && root["wires"].is_array()) {
        for (const auto& w : root["wires"]) {
            if (!w.is_object()) continue;
            VfxGraphWire wire;
            wire.fromEndpoint = w.value("from", std::string{});
            wire.toEndpoint   = w.value("to",   std::string{});
            const std::string typeStr = w.value("type", std::string("float"));
            if (!VfxPinTypeFromString(typeStr, wire.type)) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphAsset: unknown pin type '{}', falling back to float",
                       typeStr);
                wire.type = VfxPinType::Float;
            }
            if (!outGraph.AddWire(wire)) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphAsset: wire missing endpoint(s), skipping");
            }
        }
    }

    // Asset tag metadata (phase 8.2.0). Non-string entries are skipped with a
    // warning so malformed entries do not nullify the whole load.
    // @ref docs/engine/25-vfx/19-asset-tagging.md
    if (root.contains("tags")) {
        if (!root["tags"].is_array()) {
            UZ_LOG(VFX, Warning,
                   "VfxNodeGraphAsset[tags]: 'tags' must be an array, skipping");
        } else {
            for (const auto& entry : root["tags"]) {
                if (entry.is_string()) {
                    outGraph.tags.push_back(entry.get<std::string>());
                } else {
                    UZ_LOG(VFX, Warning,
                           "VfxNodeGraphAsset[tags]: non-string tag skipped");
                }
            }
        }
    }

    return true;
}

bool VfxNodeGraphAsset::Load(const std::string& path,
                               VfxNodeGraph& outGraph,
                               IVFS* vfs) {
    std::string content;
    if (vfs) {
        const auto bytes = vfs->Read(path);
        content.assign(bytes.begin(), bytes.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VfxNodeGraphAsset: failed to read '{}'", path);
        return false;
    }
    return LoadFromString(content, outGraph);
}

std::string VfxNodeGraphAsset::SaveToString(const VfxNodeGraph& graph) {
    nlohmann::json root;
    root["format"]  = "uzvfxgraph";
    root["version"] = 1;

    nlohmann::json nodesJson = nlohmann::json::array();
    for (const auto& n : graph.nodes) {
        nlohmann::json nj;
        nj["id"]     = n.id;
        nj["module"] = n.module;
        nj["position"] = { {"x", n.position.x}, {"y", n.position.y} };
        nodesJson.push_back(std::move(nj));
    }
    root["nodes"] = std::move(nodesJson);

    nlohmann::json wiresJson = nlohmann::json::array();
    for (const auto& w : graph.wires) {
        nlohmann::json wj;
        wj["from"] = w.fromEndpoint;
        wj["to"]   = w.toEndpoint;
        wj["type"] = VfxPinTypeToString(w.type);
        wiresJson.push_back(std::move(wj));
    }
    root["wires"] = std::move(wiresJson);

    // Tag metadata (phase 8.2.0): emit only when non-empty to keep legacy
    // graph files diff-minimal.
    if (!graph.tags.empty()) {
        root["tags"] = graph.tags;
    }

    return root.dump(2);
}

bool VfxNodeGraphAsset::SaveToFile(const VfxNodeGraph& graph,
                                     const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs) {
        UZ_LOG(VFX, Error, "VfxNodeGraphAsset: failed to open '{}' for writing", path);
        return false;
    }
    ofs << SaveToString(graph);
    return static_cast<bool>(ofs);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxNodeGraphClipboard — phase 8.6.0
// ─────────────────────────────────────────────────────────────────────────────

namespace {

using json = nlohmann::json;

/// Splits a `"node:pin"` endpoint string; returns true when well-formed.
bool SplitEndpoint(const std::string& endpoint,
                   std::string& nodeOut, std::string& pinOut) {
    const auto colon = endpoint.find(':');
    if (colon == std::string::npos || colon == 0 ||
        colon + 1 == endpoint.size()) {
        return false;
    }
    nodeOut = endpoint.substr(0, colon);
    pinOut  = endpoint.substr(colon + 1);
    return true;
}

/// Builds a unique id by appending `_copy`, `_copy2`, … until no collision
/// remains in `target` and the running `added` set.
std::string MakeUniqueId(const VfxNodeGraph& target,
                         const std::unordered_set<std::string>& added,
                         const std::string& wanted) {
    auto collides = [&](const std::string& cand) {
        if (target.FindNode(cand) != nullptr) return true;
        return added.count(cand) > 0;
    };
    if (!collides(wanted)) return wanted;
    for (uint32_t n = 1; n < 10000; ++n) {
        std::string cand = wanted + "_copy";
        if (n > 1) cand += std::to_string(n);
        if (!collides(cand)) return cand;
    }
    // Fall back to a numeric tag — should never happen in practice.
    return wanted + "_copy_overflow";
}

} // namespace

std::string VfxNodeGraphClipboard::Copy(
    const VfxNodeGraph& source, const std::vector<std::string>& selectedIds) {
    std::unordered_set<std::string> selectionSet(selectedIds.begin(),
                                                    selectedIds.end());

    json root;
    root["format"]  = "uzvfxclip";
    root["version"] = 1;

    json nodesJson = json::array();
    for (const auto& n : source.nodes) {
        if (selectionSet.count(n.id) == 0) continue;
        json nj;
        nj["id"]       = n.id;
        nj["module"]   = n.module;
        nj["position"] = { {"x", n.position.x}, {"y", n.position.y} };
        nodesJson.push_back(std::move(nj));
    }
    root["nodes"] = std::move(nodesJson);

    json wiresJson = json::array();
    for (const auto& w : source.wires) {
        std::string fromNode, fromPin, toNode, toPin;
        if (!SplitEndpoint(w.fromEndpoint, fromNode, fromPin)) continue;
        if (!SplitEndpoint(w.toEndpoint,   toNode,   toPin))   continue;
        // Drop wires whose endpoints escape the selection — the destination
        // graph has no way to satisfy a dangling endpoint.
        if (selectionSet.count(fromNode) == 0) continue;
        if (selectionSet.count(toNode)   == 0) continue;

        json wj;
        wj["from"] = w.fromEndpoint;
        wj["to"]   = w.toEndpoint;
        wj["type"] = VfxPinTypeToString(w.type);
        wiresJson.push_back(std::move(wj));
    }
    root["wires"] = std::move(wiresJson);

    return root.dump(2);
}

std::vector<std::string> VfxNodeGraphClipboard::Paste(
    VfxNodeGraph& target, const std::string& clipboardJson) {
    std::vector<std::string> added;

    json root;
    try {
        root = json::parse(clipboardJson);
    } catch (const json::parse_error& e) {
        UZ_LOG(VFX, Error,
               "VfxNodeGraphClipboard::Paste: JSON parse error: {}", e.what());
        return added;
    }

    if (!root.is_object() || !root.contains("format") ||
        !root["format"].is_string() ||
        root["format"].get<std::string>() != "uzvfxclip") {
        UZ_LOG(VFX, Error,
               "VfxNodeGraphClipboard::Paste: invalid format marker");
        return added;
    }

    // First pass: compute id remap (source id → new id) so wire endpoints
    // can be rewritten before we touch the target graph.
    std::unordered_map<std::string, std::string> remap;
    std::unordered_set<std::string> addedSet;
    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const auto& n : root["nodes"]) {
            if (!n.is_object()) continue;
            const std::string srcId = n.value("id", std::string{});
            if (srcId.empty()) continue;
            const std::string newId = MakeUniqueId(target, addedSet, srcId);
            remap[srcId] = newId;
            addedSet.insert(newId);
        }
    }

    // Second pass: insert nodes with remapped ids.
    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const auto& n : root["nodes"]) {
            if (!n.is_object()) continue;
            VfxGraphNode node;
            const std::string srcId = n.value("id", std::string{});
            if (srcId.empty()) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphClipboard::Paste: node missing id, skipping");
                continue;
            }
            auto remapIt = remap.find(srcId);
            if (remapIt == remap.end()) continue; // unreachable
            node.id     = remapIt->second;
            node.module = n.value("module", std::string{});
            if (n.contains("position") && n["position"].is_object()) {
                node.position.x = n["position"].value("x", 0.0f);
                node.position.y = n["position"].value("y", 0.0f);
            }
            if (node.module.empty()) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphClipboard::Paste: node '{}' missing module, skipping",
                       node.id);
                continue;
            }
            if (target.AddNode(node)) {
                added.push_back(node.id);
            }
        }
    }

    // Third pass: insert wires with endpoints rewritten to the new ids.
    auto rewriteEndpoint = [&](const std::string& ep) -> std::string {
        std::string nodeId, pin;
        if (!SplitEndpoint(ep, nodeId, pin)) return ep;
        auto it = remap.find(nodeId);
        if (it == remap.end()) return ep;
        return it->second + ":" + pin;
    };

    if (root.contains("wires") && root["wires"].is_array()) {
        for (const auto& w : root["wires"]) {
            if (!w.is_object()) continue;
            VfxGraphWire wire;
            wire.fromEndpoint = rewriteEndpoint(w.value("from", std::string{}));
            wire.toEndpoint   = rewriteEndpoint(w.value("to",   std::string{}));
            const std::string typeStr = w.value("type", std::string("float"));
            if (!VfxPinTypeFromString(typeStr, wire.type)) {
                wire.type = VfxPinType::Float;
            }
            if (!target.AddWire(wire)) {
                UZ_LOG(VFX, Warning,
                       "VfxNodeGraphClipboard::Paste: wire missing endpoint(s), skipping");
            }
        }
    }

    return added;
}

std::vector<std::string> VfxNodeGraphClipboard::PasteMany(
    VfxNodeGraph& target, const std::string& clipboardJson, uint32_t count) {
    std::vector<std::string> added;
    for (uint32_t i = 0; i < count; ++i) {
        auto batch = Paste(target, clipboardJson);
        added.insert(added.end(), batch.begin(), batch.end());
    }
    return added;
}

} // namespace uz
