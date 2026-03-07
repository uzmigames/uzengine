// shader_graph.cpp -- ShaderGraph implementation: DAG operations, validation, serialization.

#include "engine/renderer/shader_graph/shader_graph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace uz::sg {

// ============================================================================
// Node management
// ============================================================================

uint32_t ShaderGraph::add_node(std::unique_ptr<ShaderNode> node) {
    if (!node) return 0;
    uint32_t id = next_id_++;
    node->id = id;
    nodes_.push_back(std::move(node));
    return id;
}

void ShaderGraph::remove_node(uint32_t id) {
    disconnect_all(id);
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [id](const auto& n) { return n->id == id; });
    if (it != nodes_.end()) {
        nodes_.erase(it);
    }
    if (output_node_id_ == id) {
        output_node_id_ = 0;
    }
}

ShaderNode* ShaderGraph::get_node(uint32_t id) {
    for (auto& n : nodes_) {
        if (n->id == id) return n.get();
    }
    return nullptr;
}

const ShaderNode* ShaderGraph::get_node(uint32_t id) const {
    for (auto& n : nodes_) {
        if (n->id == id) return n.get();
    }
    return nullptr;
}

// ============================================================================
// Connection management
// ============================================================================

bool ShaderGraph::connect(uint32_t from_node, int32_t from_output,
                          uint32_t to_node, int32_t to_input) {
    auto* src = get_node(from_node);
    auto* dst = get_node(to_node);
    if (!src || !dst) return false;

    // Validate pin indices.
    auto src_pins = src->output_descs();
    auto dst_pins = dst->input_descs();
    if (from_output < 0 || from_output >= static_cast<int32_t>(src_pins.size()))
        return false;
    if (to_input < 0 || to_input >= static_cast<int32_t>(dst_pins.size()))
        return false;

    // Check type compatibility.
    if (!types_compatible(src_pins[from_output].type, dst_pins[to_input].type))
        return false;

    // Self-connection is not allowed.
    if (from_node == to_node)
        return false;

    // Auto-disconnect existing connection on target input.
    disconnect(to_node, to_input);

    // Create connection and update node input.
    connections_.push_back({from_node, from_output, to_node, to_input});
    auto& inp = dst->get_input(to_input);
    inp.source_node_id = from_node;
    inp.source_output_index = from_output;
    return true;
}

void ShaderGraph::disconnect(uint32_t to_node, int32_t to_input) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [to_node, to_input](const Connection& c) {
            return c.to_node == to_node && c.to_input == to_input;
        });
    if (it != connections_.end()) {
        connections_.erase(it);
    }

    auto* dst = get_node(to_node);
    if (dst && to_input >= 0 && to_input < dst->input_count()) {
        auto& inp = dst->get_input(to_input);
        inp.source_node_id = 0;
        inp.source_output_index = -1;
    }
}

void ShaderGraph::disconnect_all(uint32_t node_id) {
    // Remove connections where this node is source or destination.
    auto it = std::remove_if(connections_.begin(), connections_.end(),
        [node_id](const Connection& c) {
            return c.from_node == node_id || c.to_node == node_id;
        });

    // Clear NodeInput references for removed connections.
    for (auto rem = it; rem != connections_.end(); ++rem) {
        if (rem->to_node != node_id) {
            auto* dst = get_node(rem->to_node);
            if (dst && rem->to_input >= 0 && rem->to_input < dst->input_count()) {
                auto& inp = dst->get_input(rem->to_input);
                inp.source_node_id = 0;
                inp.source_output_index = -1;
            }
        }
    }

    connections_.erase(it, connections_.end());
}

const Connection* ShaderGraph::find_connection(uint32_t to_node, int32_t to_input) const {
    for (auto& c : connections_) {
        if (c.to_node == to_node && c.to_input == to_input)
            return &c;
    }
    return nullptr;
}

// ============================================================================
// Validation — DAG cycle detection (DFS with 3-state coloring)
// ============================================================================

bool ShaderGraph::validate_dag() const {
    enum class Color : uint8_t { White, Gray, Black };
    std::unordered_map<uint32_t, Color> color;
    for (auto& n : nodes_) color[n->id] = Color::White;

    // Build adjacency list: node -> list of downstream node IDs.
    std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
    for (auto& c : connections_) {
        adj[c.from_node].push_back(c.to_node);
    }

    // DFS from each unvisited node.
    std::function<bool(uint32_t)> dfs = [&](uint32_t u) -> bool {
        color[u] = Color::Gray;
        if (adj.contains(u)) {
            for (uint32_t v : adj[u]) {
                if (color[v] == Color::Gray) return false;  // Back edge = cycle.
                if (color[v] == Color::White && !dfs(v)) return false;
            }
        }
        color[u] = Color::Black;
        return true;
    };

    for (auto& n : nodes_) {
        if (color[n->id] == Color::White) {
            if (!dfs(n->id)) return false;
        }
    }
    return true;
}

// ============================================================================
// Validation — Type compatibility check
// ============================================================================

bool ShaderGraph::validate_types() const {
    errors_.clear();
    bool valid = true;
    for (auto& c : connections_) {
        auto* src = get_node(c.from_node);
        auto* dst = get_node(c.to_node);
        if (!src || !dst) {
            errors_.push_back("Connection references missing node (from=" +
                              std::to_string(c.from_node) + " to=" +
                              std::to_string(c.to_node) + ")");
            valid = false;
            continue;
        }
        auto src_pins = src->output_descs();
        auto dst_pins = dst->input_descs();
        if (c.from_output < 0 || c.from_output >= static_cast<int32_t>(src_pins.size()) ||
            c.to_input < 0 || c.to_input >= static_cast<int32_t>(dst_pins.size())) {
            errors_.push_back("Connection has out-of-range pin index (from_node=" +
                              std::to_string(c.from_node) + " to_node=" +
                              std::to_string(c.to_node) + ")");
            valid = false;
            continue;
        }
        if (!types_compatible(src_pins[c.from_output].type, dst_pins[c.to_input].type)) {
            errors_.push_back("Type mismatch: " + src->type_name + "." +
                              src_pins[c.from_output].name + " -> " +
                              dst->type_name + "." + dst_pins[c.to_input].name);
            valid = false;
        }
    }
    return valid;
}

std::vector<std::string> ShaderGraph::get_errors() const {
    return errors_;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json ShaderGraph::serialize() const {
    nlohmann::json j;
    j["version"] = 1;
    j["output_node_id"] = output_node_id_;

    auto& nodes_arr = j["nodes"];
    nodes_arr = nlohmann::json::array();
    for (auto& n : nodes_) {
        nlohmann::json nj;
        n->serialize(nj);
        nodes_arr.push_back(std::move(nj));
    }

    auto& conn_arr = j["connections"];
    conn_arr = nlohmann::json::array();
    for (auto& c : connections_) {
        conn_arr.push_back({
            {"from", c.from_node},
            {"fromOutput", c.from_output},
            {"to", c.to_node},
            {"toInput", c.to_input}
        });
    }

    return j;
}

std::unique_ptr<ShaderGraph> ShaderGraph::deserialize(const nlohmann::json& j,
                                                       const NodeFactory& factory) {
    auto graph = std::make_unique<ShaderGraph>();

    if (j.contains("output_node_id")) {
        graph->output_node_id_ = j["output_node_id"].get<uint32_t>();
    }

    uint32_t max_id = 0;
    if (j.contains("nodes")) {
        for (auto& nj : j["nodes"]) {
            std::string type = nj["type"].get<std::string>();
            auto node = factory(type);
            if (!node) continue;

            uint32_t id = nj["id"].get<uint32_t>();
            node->id = id;
            node->type_name = type;
            node->deserialize(nj);

            if (id > max_id) max_id = id;
            graph->nodes_.push_back(std::move(node));
        }
    }
    graph->next_id_ = max_id + 1;

    if (j.contains("connections")) {
        for (auto& cj : j["connections"]) {
            uint32_t from = cj["from"].get<uint32_t>();
            int32_t from_out = cj["fromOutput"].get<int32_t>();
            uint32_t to = cj["to"].get<uint32_t>();
            int32_t to_in = cj["toInput"].get<int32_t>();
            graph->connect(from, from_out, to, to_in);
        }
    }

    return graph;
}

nlohmann::json ShaderGraph::snapshot() const {
    return serialize();
}

void ShaderGraph::restore(const nlohmann::json& snap, const NodeFactory& factory) {
    auto restored = deserialize(snap, factory);
    if (restored) {
        nodes_ = std::move(restored->nodes_);
        connections_ = std::move(restored->connections_);
        next_id_ = restored->next_id_;
        output_node_id_ = restored->output_node_id_;
    }
}

} // namespace uz::sg
