#include "vfx_node_graph.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <fstream>

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

} // namespace uz
