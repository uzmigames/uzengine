// ipc_shader_cmd_handlers.cpp -- IPC handler implementations for shader_cmd.* commands.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// All handlers capture the CommandExecutor* by value (pointer copy). The executor
// must remain valid for the lifetime of the IpcRegistry.
//
// Handlers:
//   shader_cmd.execute — { type, params }          -> { ok, data?, error?, events? }
//   shader_cmd.batch   — { commands: [...] }       -> { ok, results?, error?, events? }
//   shader_cmd.query   — { query, ...params }      -> { ok, data?, error? }

#include "engine/editor/ipc_shader_cmd_handlers.h"
#include "engine/renderer/shader_graph/commands/command_executor.h"
#include "common/ipc_registry.h"

#include <nlohmann/json.hpp>

namespace uz {

namespace {

/// Convert a CommandResult to a JSON response.
nlohmann::json result_to_json(const sg::CommandResult& r) {
    nlohmann::json j;
    j["ok"] = r.ok;
    if (!r.ok) {
        j["error"] = r.error;
    }
    if (!r.data.is_null()) {
        j["data"] = r.data;
    }
    if (!r.events.empty()) {
        j["events"] = r.events;
    }
    return j;
}

} // anonymous namespace

void shader_cmd_register_handlers(IpcRegistry& registry, sg::CommandExecutor* executor) {
    // shader_cmd.execute — execute a single command.
    // Args: { type: "ADD_NODE", params: { nodeType: "Multiply" } }
    registry.add("shader_cmd.execute",
        [executor](const nlohmann::json& args) -> nlohmann::json {
            auto r = executor->execute_json(args);
            return result_to_json(r);
        });

    // shader_cmd.batch — execute a batch of commands with rollback.
    // Args: { commands: [ { type: "ADD_NODE", params: {...} }, ... ] }
    registry.add("shader_cmd.batch",
        [executor](const nlohmann::json& args) -> nlohmann::json {
            if (!args.contains("commands") || !args["commands"].is_array()) {
                return {{"ok", false}, {"error", "Missing or invalid 'commands' array"}};
            }

            // Parse each command in the batch.
            std::vector<sg::GraphCommand> commands;
            for (auto& cj : args["commands"]) {
                commands.push_back(sg::parse_command(cj));
            }

            auto r = executor->execute_batch(commands);
            return result_to_json(r);
        });

    // shader_cmd.query — convenience wrapper for QUERY commands.
    // Args: { query: "graph" } or { query: "node", nodeId: 1 }
    registry.add("shader_cmd.query",
        [executor](const nlohmann::json& args) -> nlohmann::json {
            // Wrap as a QUERY command and execute.
            nlohmann::json cmd_json = {
                {"type", "QUERY"},
                {"params", args}
            };
            auto r = executor->execute_json(cmd_json);
            return result_to_json(r);
        });
}

} // namespace uz
