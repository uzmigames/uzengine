// graph_command.cpp -- Command type parsing.

#include "engine/renderer/shader_graph/commands/graph_command.h"

namespace uz::sg {

CommandType command_type_from_string(const std::string& s) {
    if (s == "ADD_NODE")       return CommandType::ADD_NODE;
    if (s == "DELETE_NODE")    return CommandType::DELETE_NODE;
    if (s == "CONNECT")        return CommandType::CONNECT;
    if (s == "DISCONNECT")     return CommandType::DISCONNECT;
    if (s == "SET_PARAM")      return CommandType::SET_PARAM;
    if (s == "INSERT_BETWEEN") return CommandType::INSERT_BETWEEN;
    if (s == "REPLACE_NODE")   return CommandType::REPLACE_NODE;
    if (s == "EXPOSE_PARAM")   return CommandType::EXPOSE_PARAM;
    if (s == "COMPILE")        return CommandType::COMPILE;
    if (s == "QUERY")          return CommandType::QUERY;
    if (s == "BATCH")          return CommandType::BATCH;
    return CommandType::ADD_NODE;  // Default fallback.
}

const char* command_type_to_string(CommandType t) {
    switch (t) {
        case CommandType::ADD_NODE:       return "ADD_NODE";
        case CommandType::DELETE_NODE:    return "DELETE_NODE";
        case CommandType::CONNECT:        return "CONNECT";
        case CommandType::DISCONNECT:     return "DISCONNECT";
        case CommandType::SET_PARAM:      return "SET_PARAM";
        case CommandType::INSERT_BETWEEN: return "INSERT_BETWEEN";
        case CommandType::REPLACE_NODE:   return "REPLACE_NODE";
        case CommandType::EXPOSE_PARAM:   return "EXPOSE_PARAM";
        case CommandType::COMPILE:        return "COMPILE";
        case CommandType::QUERY:          return "QUERY";
        case CommandType::BATCH:          return "BATCH";
    }
    return "UNKNOWN";
}

GraphCommand parse_command(const nlohmann::json& j) {
    GraphCommand cmd;
    if (j.contains("type")) {
        cmd.type = command_type_from_string(j["type"].get<std::string>());
    }
    if (j.contains("params")) {
        cmd.params = j["params"];
    }
    if (j.contains("commands") && cmd.type == CommandType::BATCH) {
        for (auto& sub : j["commands"]) {
            cmd.sub_commands.push_back(parse_command(sub));
        }
    }
    return cmd;
}

} // namespace uz::sg
