// graph_command.h -- Structured command types for the Prompt Command Interface.
//
// Commands are parsed from JSON and dispatched to GraphOps.

#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace uz::sg {

enum class CommandType : uint8_t {
    ADD_NODE,
    DELETE_NODE,
    CONNECT,
    DISCONNECT,
    SET_PARAM,
    INSERT_BETWEEN,
    REPLACE_NODE,
    EXPOSE_PARAM,
    COMPILE,
    QUERY,
    BATCH,
};

struct GraphCommand {
    CommandType type = CommandType::ADD_NODE;
    nlohmann::json params;
    std::vector<GraphCommand> sub_commands;  // For BATCH type.
};

struct CommandResult {
    bool ok = false;
    std::string error;
    nlohmann::json data;
    std::vector<nlohmann::json> events;  // Events emitted during execution.
};

// Parse CommandType from string.
CommandType command_type_from_string(const std::string& s);
const char* command_type_to_string(CommandType t);

// Parse a GraphCommand from JSON.
GraphCommand parse_command(const nlohmann::json& j);

} // namespace uz::sg
