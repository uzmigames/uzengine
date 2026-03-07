// graph_ops.cpp -- GraphOps implementation: atomic, compound, query, events.

#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"

namespace uz::sg {

GraphOps::GraphOps(ShaderGraph& graph) : graph_(graph) {}

// ============================================================================
// Event emission
// ============================================================================

void GraphOps::emit(const GraphEvent& event) {
    if (listener_) listener_(event);
}

void GraphOps::set_event_listener(GraphEventListener listener) {
    listener_ = std::move(listener);
}

// ============================================================================
// Atomic operations
// ============================================================================

OpResult GraphOps::create_node(const std::string& type_name, float x, float y) {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create(type_name);
    if (!node) {
        return {false, "Unknown node type: " + type_name, {}};
    }
    node->pos_x = x;
    node->pos_y = y;
    uint32_t id = graph_.add_node(std::move(node));

    emit({GraphEventType::NodeAdded, id, 0, -1, {}, {}});
    return {true, {}, {{"nodeId", id}}};
}

OpResult GraphOps::delete_node(uint32_t node_id) {
    auto* node = graph_.get_node(node_id);
    if (!node) {
        return {false, "Node not found: " + std::to_string(node_id), {}};
    }

    // Emit ConnectionRemoved events for each connection being removed.
    auto& conns = graph_.connections();
    for (auto& c : conns) {
        if (c.from_node == node_id || c.to_node == node_id) {
            emit({GraphEventType::ConnectionRemoved, c.from_node, c.to_node,
                  c.to_input, {}, {}});
        }
    }

    graph_.remove_node(node_id);
    emit({GraphEventType::NodeRemoved, node_id, 0, -1, {}, {}});
    return {true, {}, {}};
}

OpResult GraphOps::connect_pins(uint32_t from_node, int32_t from_output,
                                 uint32_t to_node, int32_t to_input) {
    // Check if there's an existing connection that will be auto-disconnected.
    auto* existing = graph_.find_connection(to_node, to_input);
    if (existing) {
        emit({GraphEventType::ConnectionRemoved, existing->from_node,
              existing->to_node, existing->to_input, {}, {}});
    }

    bool ok = graph_.connect(from_node, from_output, to_node, to_input);
    if (!ok) {
        return {false, "Cannot connect: invalid pins, type mismatch, or self-loop", {}};
    }

    emit({GraphEventType::ConnectionAdded, from_node, to_node, to_input, {}, {}});
    return {true, {}, {{"from", from_node}, {"fromOutput", from_output},
                       {"to", to_node}, {"toInput", to_input}}};
}

OpResult GraphOps::disconnect_pin(uint32_t node_id, int32_t input_index) {
    auto* conn = graph_.find_connection(node_id, input_index);
    if (!conn) {
        return {false, "No connection on input " + std::to_string(input_index) +
                       " of node " + std::to_string(node_id), {}};
    }

    uint32_t from = conn->from_node;
    graph_.disconnect(node_id, input_index);
    emit({GraphEventType::ConnectionRemoved, from, node_id, input_index, {}, {}});
    return {true, {}, {}};
}

OpResult GraphOps::set_param(uint32_t node_id, const std::string& key,
                              const nlohmann::json& value) {
    auto* node = graph_.get_node(node_id);
    if (!node) {
        return {false, "Node not found: " + std::to_string(node_id), {}};
    }

    // Set parameter via JSON: serialize, modify, deserialize.
    nlohmann::json j;
    node->serialize(j);
    j["params"][key] = value;
    node->deserialize(j);

    emit({GraphEventType::ParamChanged, node_id, 0, -1, key, value});
    return {true, {}, {}};
}

// ============================================================================
// Compound operations
// ============================================================================

OpResult GraphOps::insert_between(uint32_t from_node, int32_t from_output,
                                   uint32_t to_node, int32_t to_input,
                                   const std::string& new_node_type) {
    // Verify existing connection.
    auto* conn = graph_.find_connection(to_node, to_input);
    if (!conn || conn->from_node != from_node || conn->from_output != from_output) {
        return {false, "No matching connection found between specified pins", {}};
    }

    // Create the new node.
    auto create_result = create_node(new_node_type);
    if (!create_result.ok) return create_result;
    uint32_t new_id = create_result.data["nodeId"].get<uint32_t>();

    // Disconnect existing.
    disconnect_pin(to_node, to_input);

    // Wire: from -> new_node input 0, new_node output 0 -> to.
    auto r1 = connect_pins(from_node, from_output, new_id, 0);
    if (!r1.ok) {
        delete_node(new_id);
        connect_pins(from_node, from_output, to_node, to_input);
        return {false, "Failed to wire new node: " + r1.error, {}};
    }

    auto r2 = connect_pins(new_id, 0, to_node, to_input);
    if (!r2.ok) {
        delete_node(new_id);
        connect_pins(from_node, from_output, to_node, to_input);
        return {false, "Failed to wire new node output: " + r2.error, {}};
    }

    return {true, {}, {{"nodeId", new_id}}};
}

OpResult GraphOps::replace_node(uint32_t old_node_id, const std::string& new_type) {
    auto* old_node = graph_.get_node(old_node_id);
    if (!old_node) {
        return {false, "Node not found: " + std::to_string(old_node_id), {}};
    }

    // Record existing connections.
    struct ConnInfo { uint32_t from; int32_t from_out; int32_t to_in; };
    struct OutConnInfo { uint32_t to; int32_t to_in; int32_t from_out; };
    std::vector<ConnInfo> incoming;
    std::vector<OutConnInfo> outgoing;

    for (auto& c : graph_.connections()) {
        if (c.to_node == old_node_id)
            incoming.push_back({c.from_node, c.from_output, c.to_input});
        if (c.from_node == old_node_id)
            outgoing.push_back({c.to_node, c.to_input, c.from_output});
    }

    // Create replacement.
    auto create_result = create_node(new_type, old_node->pos_x, old_node->pos_y);
    if (!create_result.ok) return create_result;
    uint32_t new_id = create_result.data["nodeId"].get<uint32_t>();

    // Delete old.
    delete_node(old_node_id);

    // Re-wire compatible connections.
    auto* new_node = graph_.get_node(new_id);
    for (auto& ci : incoming) {
        if (ci.to_in < new_node->input_count()) {
            connect_pins(ci.from, ci.from_out, new_id, ci.to_in);
        }
    }
    auto new_outs = new_node->output_descs();
    for (auto& co : outgoing) {
        if (co.from_out < static_cast<int32_t>(new_outs.size())) {
            connect_pins(new_id, co.from_out, co.to, co.to_in);
        }
    }

    return {true, {}, {{"nodeId", new_id}}};
}

OpResult GraphOps::expose_parameter(uint32_t node_id, int32_t input_index,
                                     const std::string& param_name) {
    auto* node = graph_.get_node(node_id);
    if (!node) {
        return {false, "Node not found: " + std::to_string(node_id), {}};
    }
    if (input_index < 0 || input_index >= node->input_count()) {
        return {false, "Input index out of range", {}};
    }

    auto inputs = node->input_descs();
    auto pin_type = inputs[input_index].type;

    // Determine the param node type based on pin type.
    // ScalarParam outputs Float1 which is compatible with any Scalar pin.
    // For texture pins, use TextureParam.
    std::string param_type;
    if (types_compatible(pin_type, ValueType::Scalar)) {
        param_type = "ScalarParam";
    } else if (types_compatible(pin_type, ValueType::AnyTexture)) {
        param_type = "TextureParam";
    } else {
        return {false, "Cannot expose parameter for pin type", {}};
    }

    // Disconnect existing.
    auto* existing = graph_.find_connection(node_id, input_index);
    if (existing) {
        disconnect_pin(node_id, input_index);
    }

    // Create param node.
    auto create_result = create_node(param_type, node->pos_x - 200.f, node->pos_y);
    if (!create_result.ok) return create_result;
    uint32_t param_id = create_result.data["nodeId"].get<uint32_t>();

    // Set param name.
    set_param(param_id, "param_name", param_name);

    // Wire.
    auto conn_result = connect_pins(param_id, 0, node_id, input_index);
    if (!conn_result.ok) {
        delete_node(param_id);
        return {false, "Failed to wire parameter node: " + conn_result.error, {}};
    }

    return {true, {}, {{"paramNodeId", param_id}}};
}

// ============================================================================
// Query operations
// ============================================================================

OpResult GraphOps::query_graph() const {
    nlohmann::json data;
    data["nodeCount"] = graph_.nodes().size();
    data["connectionCount"] = graph_.connections().size();
    data["outputNodeId"] = graph_.output_node_id();
    data["isValidDag"] = graph_.validate_dag();

    nlohmann::json node_ids = nlohmann::json::array();
    for (auto& n : graph_.nodes()) {
        node_ids.push_back(n->id);
    }
    data["nodeIds"] = std::move(node_ids);
    return {true, {}, std::move(data)};
}

OpResult GraphOps::query_node(uint32_t node_id) const {
    auto* node = graph_.get_node(node_id);
    if (!node) {
        return {false, "Node not found: " + std::to_string(node_id), {}};
    }

    nlohmann::json data;
    data["id"] = node->id;
    data["type"] = node->type_name;
    data["x"] = node->pos_x;
    data["y"] = node->pos_y;

    auto in_descs = node->input_descs();
    auto out_descs = node->output_descs();

    auto inputs = nlohmann::json::array();
    for (int32_t i = 0; i < node->input_count(); ++i) {
        auto& inp = node->get_input(i);
        nlohmann::json ij;
        ij["name"] = in_descs[i].name;
        ij["connected"] = (inp.source_node_id != 0);
        if (inp.source_node_id != 0) {
            ij["sourceNode"] = inp.source_node_id;
            ij["sourceOutput"] = inp.source_output_index;
        }
        inputs.push_back(std::move(ij));
    }
    data["inputs"] = std::move(inputs);

    auto outputs = nlohmann::json::array();
    for (auto& od : out_descs) {
        outputs.push_back({{"name", od.name}});
    }
    data["outputs"] = std::move(outputs);

    return {true, {}, std::move(data)};
}

OpResult GraphOps::find_nodes(const std::string& type_name) const {
    nlohmann::json ids = nlohmann::json::array();
    for (auto& n : graph_.nodes()) {
        if (n->type_name == type_name) ids.push_back(n->id);
    }
    return {true, {}, {{"nodeIds", std::move(ids)}}};
}

OpResult GraphOps::list_node_types() const {
    auto& reg = NodeRegistry::instance();
    nlohmann::json types = nlohmann::json::array();
    for (auto& t : reg.all()) {
        types.push_back({{"name", t.name}, {"category", static_cast<int>(t.category)}});
    }
    return {true, {}, {{"types", std::move(types)}}};
}

OpResult GraphOps::query_connection(uint32_t to_node, int32_t to_input) const {
    auto* conn = graph_.find_connection(to_node, to_input);
    if (!conn) {
        return {true, {}, {{"connected", false}}};
    }
    return {true, {}, {{"connected", true},
                       {"from", conn->from_node},
                       {"fromOutput", conn->from_output},
                       {"to", conn->to_node},
                       {"toInput", conn->to_input}}};
}

} // namespace uz::sg
