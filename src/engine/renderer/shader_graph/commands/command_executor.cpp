// command_executor.cpp -- CommandExecutor implementation.

#include "engine/renderer/shader_graph/commands/command_executor.h"
#include "engine/renderer/shader_graph/node_registry.h"
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"

namespace uz::sg {

CommandExecutor::CommandExecutor(GraphOps& ops) : ops_(ops) {}

// ============================================================================
// Public API
// ============================================================================

CommandResult CommandExecutor::execute(const GraphCommand& cmd) {
    // Temporarily collect events.
    collected_events_.clear();
    auto prev_listener = [](const GraphEvent&) {};  // placeholder
    ops_.set_event_listener([this](const GraphEvent& e) {
        nlohmann::json ej;
        ej["type"] = static_cast<int>(e.type);
        ej["nodeId"] = e.node_id;
        if (e.node_id_2 != 0) ej["nodeId2"] = e.node_id_2;
        if (e.pin_index >= 0) ej["pinIndex"] = e.pin_index;
        if (!e.key.empty()) ej["key"] = e.key;
        collected_events_.push_back(std::move(ej));
    });

    auto result = dispatch(cmd);
    result.events = collected_events_;
    collected_events_.clear();
    return result;
}

CommandResult CommandExecutor::execute_batch(const std::vector<GraphCommand>& commands) {
    // Take snapshot for rollback.
    auto snap = ops_.graph().snapshot();
    auto factory = [](const std::string& type) {
        return NodeRegistry::instance().create(type);
    };

    collected_events_.clear();
    ops_.set_event_listener([this](const GraphEvent& e) {
        nlohmann::json ej;
        ej["type"] = static_cast<int>(e.type);
        ej["nodeId"] = e.node_id;
        if (e.node_id_2 != 0) ej["nodeId2"] = e.node_id_2;
        if (e.pin_index >= 0) ej["pinIndex"] = e.pin_index;
        if (!e.key.empty()) ej["key"] = e.key;
        collected_events_.push_back(std::move(ej));
    });

    std::vector<CommandResult> results;
    results.reserve(commands.size());

    for (auto& cmd : commands) {
        // Resolve variable references.
        GraphCommand resolved = cmd;
        resolved.params = resolve_refs(cmd.params, results);

        auto result = dispatch(resolved);
        if (!result.ok) {
            // Rollback.
            ops_.graph().restore(snap, factory);
            CommandResult batch_result;
            batch_result.ok = false;
            batch_result.error = "Batch failed at command " +
                                  std::to_string(results.size()) + ": " + result.error;
            batch_result.events = collected_events_;
            collected_events_.clear();
            return batch_result;
        }
        results.push_back(std::move(result));
    }

    CommandResult batch_result;
    batch_result.ok = true;
    nlohmann::json results_arr = nlohmann::json::array();
    for (auto& r : results) {
        results_arr.push_back(r.data);
    }
    batch_result.data = {{"results", std::move(results_arr)}};
    batch_result.events = collected_events_;
    collected_events_.clear();
    return batch_result;
}

CommandResult CommandExecutor::execute_json(const nlohmann::json& j) {
    auto cmd = parse_command(j);
    if (cmd.type == CommandType::BATCH) {
        return execute_batch(cmd.sub_commands);
    }
    return execute(cmd);
}

// ============================================================================
// Dispatch
// ============================================================================

CommandResult CommandExecutor::dispatch(const GraphCommand& cmd) {
    switch (cmd.type) {
        case CommandType::ADD_NODE:       return do_add_node(cmd.params);
        case CommandType::DELETE_NODE:    return do_delete_node(cmd.params);
        case CommandType::CONNECT:        return do_connect(cmd.params);
        case CommandType::DISCONNECT:     return do_disconnect(cmd.params);
        case CommandType::SET_PARAM:      return do_set_param(cmd.params);
        case CommandType::INSERT_BETWEEN: return do_insert_between(cmd.params);
        case CommandType::REPLACE_NODE:   return do_replace_node(cmd.params);
        case CommandType::EXPOSE_PARAM:   return do_expose_param(cmd.params);
        case CommandType::QUERY:          return do_query(cmd.params);
        case CommandType::COMPILE: {
            HlslTranslator translator;
            auto cr = translator.compile(ops_.graph());
            if (!cr.ok) {
                nlohmann::json err_data;
                err_data["errors"] = cr.errors;
                return {false, cr.errors.empty() ? "Compilation failed" : cr.errors[0], err_data, {}};
            }
            nlohmann::json data;
            data["vertexHlsl"] = cr.vertex_hlsl;
            data["pixelHlsl"] = cr.pixel_hlsl;
            if (!cr.errors.empty()) data["warnings"] = cr.errors;
            return {true, {}, data, {}};
        }
        case CommandType::BATCH:          return execute_batch(cmd.sub_commands);
    }
    return {false, "Unknown command type", {}};
}

// ============================================================================
// Individual command handlers
// ============================================================================

CommandResult CommandExecutor::do_add_node(const nlohmann::json& p) {
    std::string type = p.value("nodeType", "");
    float x = p.value("x", 0.f);
    float y = p.value("y", 0.f);
    auto r = ops_.create_node(type, x, y);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_delete_node(const nlohmann::json& p) {
    uint32_t id = p.value("nodeId", 0u);
    auto r = ops_.delete_node(id);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_connect(const nlohmann::json& p) {
    uint32_t from = p.value("from", 0u);
    int32_t from_out = p.value("fromOutput", 0);
    uint32_t to = p.value("to", 0u);
    int32_t to_in = p.value("toInput", 0);
    auto r = ops_.connect_pins(from, from_out, to, to_in);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_disconnect(const nlohmann::json& p) {
    uint32_t id = p.value("nodeId", 0u);
    int32_t idx = p.value("inputIndex", 0);
    auto r = ops_.disconnect_pin(id, idx);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_set_param(const nlohmann::json& p) {
    uint32_t id = p.value("nodeId", 0u);
    std::string key = p.value("key", "");
    auto r = ops_.set_param(id, key, p.value("value", nlohmann::json()));
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_insert_between(const nlohmann::json& p) {
    uint32_t from = p.value("from", 0u);
    int32_t from_out = p.value("fromOutput", 0);
    uint32_t to = p.value("to", 0u);
    int32_t to_in = p.value("toInput", 0);
    std::string type = p.value("nodeType", "");
    auto r = ops_.insert_between(from, from_out, to, to_in, type);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_replace_node(const nlohmann::json& p) {
    uint32_t id = p.value("nodeId", 0u);
    std::string type = p.value("newType", "");
    auto r = ops_.replace_node(id, type);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_expose_param(const nlohmann::json& p) {
    uint32_t id = p.value("nodeId", 0u);
    int32_t idx = p.value("inputIndex", 0);
    std::string name = p.value("paramName", "");
    auto r = ops_.expose_parameter(id, idx, name);
    return {r.ok, r.error, r.data, {}};
}

CommandResult CommandExecutor::do_query(const nlohmann::json& p) {
    std::string query = p.value("query", "graph");
    OpResult r;
    if (query == "graph") {
        r = ops_.query_graph();
    } else if (query == "node") {
        r = ops_.query_node(p.value("nodeId", 0u));
    } else if (query == "findNodes") {
        r = ops_.find_nodes(p.value("nodeType", ""));
    } else if (query == "nodeTypes") {
        r = ops_.list_node_types();
    } else if (query == "connection") {
        r = ops_.query_connection(p.value("nodeId", 0u), p.value("inputIndex", 0));
    } else {
        return {false, "Unknown query type: " + query, {}, {}};
    }
    return {r.ok, r.error, r.data, {}};
}

// ============================================================================
// Variable reference resolution
// ============================================================================

nlohmann::json CommandExecutor::resolve_refs(const nlohmann::json& params,
                                              const std::vector<CommandResult>& results) {
    if (params.is_null()) return params;

    nlohmann::json resolved = params;
    for (auto& [key, val] : resolved.items()) {
        if (val.is_string()) {
            std::string s = val.get<std::string>();
            if (s.size() >= 2 && s[0] == '$') {
                // Resolve $N to nodeId from result N.
                int idx = std::stoi(s.substr(1));
                if (idx >= 0 && idx < static_cast<int>(results.size())) {
                    auto& data = results[idx].data;
                    if (data.contains("nodeId")) {
                        resolved[key] = data["nodeId"];
                    }
                }
            }
        }
    }
    return resolved;
}

} // namespace uz::sg
