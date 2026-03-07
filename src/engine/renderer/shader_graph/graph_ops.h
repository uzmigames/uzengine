// graph_ops.h -- Graph Control Layer: typed API for graph manipulation.
//
// All graph mutations go through GraphOps. This is the same API a visual
// editor would call — prompts just translate to these operations.

#pragma once

#include "engine/renderer/shader_graph/shader_graph.h"
#include "engine/renderer/shader_graph/node_registry.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz::sg {

// Result of a graph operation.
struct OpResult {
    bool ok = false;
    std::string error;
    nlohmann::json data;  // Operation-specific data (e.g., nodeId, connection info).
};

// Event types emitted on graph mutations.
enum class GraphEventType : uint8_t {
    NodeAdded,
    NodeRemoved,
    ConnectionAdded,
    ConnectionRemoved,
    ParamChanged,
};

struct GraphEvent {
    GraphEventType type;
    uint32_t       node_id    = 0;
    uint32_t       node_id_2  = 0;  // For connections: the other node.
    int32_t        pin_index  = -1;
    std::string    key;             // For ParamChanged: param key.
    nlohmann::json value;           // For ParamChanged: new value.
};

using GraphEventListener = std::function<void(const GraphEvent&)>;

class GraphOps {
public:
    explicit GraphOps(ShaderGraph& graph);

    // --- Atomic operations ---
    OpResult create_node(const std::string& type_name, float x = 0.f, float y = 0.f);
    OpResult delete_node(uint32_t node_id);
    OpResult connect_pins(uint32_t from_node, int32_t from_output,
                          uint32_t to_node, int32_t to_input);
    OpResult disconnect_pin(uint32_t node_id, int32_t input_index);
    OpResult set_param(uint32_t node_id, const std::string& key,
                       const nlohmann::json& value);

    // --- Compound operations ---
    OpResult insert_between(uint32_t from_node, int32_t from_output,
                            uint32_t to_node, int32_t to_input,
                            const std::string& new_node_type);
    OpResult replace_node(uint32_t old_node_id, const std::string& new_type);
    OpResult expose_parameter(uint32_t node_id, int32_t input_index,
                              const std::string& param_name);

    // --- Query operations ---
    OpResult query_graph() const;
    OpResult query_node(uint32_t node_id) const;
    OpResult find_nodes(const std::string& type_name) const;
    OpResult list_node_types() const;
    OpResult query_connection(uint32_t to_node, int32_t to_input) const;

    // --- Event system ---
    void set_event_listener(GraphEventListener listener);

    // Access underlying graph.
    ShaderGraph& graph() { return graph_; }
    const ShaderGraph& graph() const { return graph_; }

private:
    void emit(const GraphEvent& event);
    ShaderGraph& graph_;
    GraphEventListener listener_;
};

} // namespace uz::sg
