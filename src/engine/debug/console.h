#pragma once

// console.h -- Developer console: register and invoke named commands.
//
// Commands are registered with a name, a callback, and a help string.
// The console maintains a scrollback buffer of output lines.
// Supports history navigation (up/down arrow) and basic tab-completion.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/result.h"

namespace uz {

// Result type for command execution.
using ConsoleResult = Result<std::string>;

// Command callback: receives a vector of string arguments, returns output string.
using ConsoleCmdFn = std::function<ConsoleResult(const std::vector<std::string>&)>;

struct ConsoleCmd {
    std::string name;   // Command name (e.g. "r.wireframe")
    std::string help;   // Short help text
    ConsoleCmdFn fn;    // The handler
};

class Console {
public:
    static constexpr uint32_t kMaxScrollback = 512;
    static constexpr uint32_t kMaxHistory    = 64;

    Console();

    // Register a command. Overwrites existing registration silently.
    void register_cmd(std::string name, std::string help, ConsoleCmdFn fn);

    // Unregister a command. No-op if not found.
    // Built-in commands ("help", "clear") cannot be unregistered.
    void unregister_cmd(const std::string& name);

    // Execute a command line string. Parses it into argv and dispatches.
    // Returns Ok(output) or Err(message). Also appends to scrollback.
    ConsoleResult execute(const std::string& line);

    // Auto-complete: given a partial name, return matching command names.
    [[nodiscard]] std::vector<std::string> complete(const std::string& partial) const;

    // Command registry query.
    [[nodiscard]] bool has_cmd(const std::string& name) const;
    [[nodiscard]] const ConsoleCmd* get_cmd(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> all_names() const;

    // Scrollback buffer (LIFO, capped at kMaxScrollback).
    [[nodiscard]] const std::vector<std::string>& scrollback() const;
    void clear_scrollback();

    // Input history (for up/down navigation).
    // push_history adds a line if it's different from the last entry.
    void push_history(const std::string& line);
    [[nodiscard]] const std::vector<std::string>& history() const;
    void clear_history();

    // Built-in commands registered in constructor:
    //   "help"         -- list all registered commands
    //   "help <name>"  -- show help for a specific command
    //   "clear"        -- clear scrollback

private:
    std::unordered_map<std::string, ConsoleCmd> cmds_;
    std::vector<std::string> scrollback_;
    std::vector<std::string> history_;

    // Parse "cmd arg1 arg2 ..." into ["cmd", "arg1", "arg2", ...]
    // Handles single-quoted and double-quoted arguments.
    static std::vector<std::string> parse_line(const std::string& line);

    void append_scrollback(const std::string& line);
};

} // namespace uz
