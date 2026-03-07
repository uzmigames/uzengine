// shader_graph.h -- Directed Acyclic Graph of shader nodes.
//
// Manages nodes, connections, validation, and serialization.
// No RHI or GPU dependencies — operates on pure data.

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"
#include "engine/renderer/shader_graph/shader_graph_types.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace uz::sg {

// Represents a connection between two node pins.
struct Connection {
    uint32_t from_node   = 0;
    int32_t  from_output = -1;
    uint32_t to_node     = 0;
    int32_t  to_input    = -1;

    bool operator==(const Connection& o) const = default;
};

// Node factory function type (used by deserialization).
using NodeFactory = std::function<std::unique_ptr<ShaderNode>(const std::string& type_name)>;

class ShaderGraph {
public:
    // Node management.
    // Returns the assigned node ID. Ownership is transferred.
    uint32_t add_node(std::unique_ptr<ShaderNode> node);
    void remove_node(uint32_t id);
    ShaderNode* get_node(uint32_t id);
    const ShaderNode* get_node(uint32_t id) const;
    const std::vector<std::unique_ptr<ShaderNode>>& nodes() const { return nodes_; }

    // Connection management.
    // connect() returns false if pins are out of range or types are incompatible.
    // Auto-disconnects existing connection on the target input.
    bool connect(uint32_t from_node, int32_t from_output,
                 uint32_t to_node, int32_t to_input);
    void disconnect(uint32_t to_node, int32_t to_input);
    void disconnect_all(uint32_t node_id);
    const std::vector<Connection>& connections() const { return connections_; }

    // Find connection to a specific input pin.
    const Connection* find_connection(uint32_t to_node, int32_t to_input) const;

    // Validation.
    bool validate_dag() const;
    bool validate_types() const;
    std::vector<std::string> get_errors() const;

    // Output node — the MaterialOutputNode.
    uint32_t output_node_id() const { return output_node_id_; }
    void set_output_node_id(uint32_t id) { output_node_id_ = id; }

    // Serialization.
    nlohmann::json serialize() const;
    static std::unique_ptr<ShaderGraph> deserialize(const nlohmann::json& j,
                                                     const NodeFactory& factory);

    // Snapshot/restore for batch rollback.
    nlohmann::json snapshot() const;
    void restore(const nlohmann::json& snap, const NodeFactory& factory);

    // Next available node ID (monotonically increasing).
    uint32_t next_id() const { return next_id_; }

private:
    std::vector<std::unique_ptr<ShaderNode>> nodes_;
    std::vector<Connection> connections_;
    uint32_t next_id_ = 1;
    uint32_t output_node_id_ = 0;
    mutable std::vector<std::string> errors_;
};

} // namespace uz::sg
