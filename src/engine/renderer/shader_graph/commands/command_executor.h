// command_executor.h -- Executes structured commands against GraphOps.
//
// Supports single commands, batches with rollback, and variable references.

#pragma once

#include "engine/renderer/shader_graph/commands/graph_command.h"
#include "engine/renderer/shader_graph/graph_ops.h"

#include <vector>

namespace uz::sg {

class CommandExecutor {
public:
    explicit CommandExecutor(GraphOps& ops);

    // Execute a single command.
    CommandResult execute(const GraphCommand& cmd);

    // Execute a batch with rollback on failure.
    CommandResult execute_batch(const std::vector<GraphCommand>& commands);

    // Parse and execute from JSON.
    CommandResult execute_json(const nlohmann::json& j);

private:
    CommandResult dispatch(const GraphCommand& cmd);
    CommandResult do_add_node(const nlohmann::json& params);
    CommandResult do_delete_node(const nlohmann::json& params);
    CommandResult do_connect(const nlohmann::json& params);
    CommandResult do_disconnect(const nlohmann::json& params);
    CommandResult do_set_param(const nlohmann::json& params);
    CommandResult do_insert_between(const nlohmann::json& params);
    CommandResult do_replace_node(const nlohmann::json& params);
    CommandResult do_expose_param(const nlohmann::json& params);
    CommandResult do_query(const nlohmann::json& params);

    // Resolve variable references ($0, $1, etc.) in params using batch results.
    nlohmann::json resolve_refs(const nlohmann::json& params,
                                 const std::vector<CommandResult>& results);

    GraphOps& ops_;
    std::vector<nlohmann::json> collected_events_;
};

} // namespace uz::sg
