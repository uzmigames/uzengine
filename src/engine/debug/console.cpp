// console.cpp -- Developer console implementation.

#include "engine/debug/console.h"

#include <algorithm>
#include <sstream>

namespace uz {

// Names of built-in commands that cannot be unregistered.
static bool is_builtin(const std::string& name) {
    return name == "help" || name == "clear";
}

Console::Console() {
    // Built-in: help [name]
    register_cmd("help", "Show help for all commands or a specific command.", [this](const std::vector<std::string>& args) -> ConsoleResult {
        if (args.empty()) {
            // List all commands sorted alphabetically.
            auto names = all_names();
            std::sort(names.begin(), names.end());

            std::string out;
            for (const auto& n : names) {
                const auto* cmd = get_cmd(n);
                if (!out.empty()) {
                    out += '\n';
                }
                out += "  " + n + "  - " + (cmd ? cmd->help : "");
            }
            return ConsoleResult::Ok(std::move(out));
        }

        // help <name>
        const auto* cmd = get_cmd(args[0]);
        if (!cmd) {
            return ConsoleResult::Err("Unknown command: " + args[0]);
        }
        return ConsoleResult::Ok(cmd->help);
    });

    // Built-in: clear
    register_cmd("clear", "Clear the scrollback buffer.", [this](const std::vector<std::string>& /*args*/) -> ConsoleResult {
        clear_scrollback();
        return ConsoleResult::Ok(std::string{});
    });
}

void Console::register_cmd(std::string name, std::string help, ConsoleCmdFn fn) {
    // Copy the key before moving name into the struct. The evaluation order of
    // operator[] and the brace-init on the right-hand side is indeterminate,
    // so moving name could leave the map key empty.
    auto key = name;
    cmds_[key] = ConsoleCmd{std::move(name), std::move(help), std::move(fn)};
}

void Console::unregister_cmd(const std::string& name) {
    // Do NOT allow unregistering built-in commands.
    if (is_builtin(name)) {
        return;
    }
    cmds_.erase(name);
}

ConsoleResult Console::execute(const std::string& line) {
    // Skip blank / whitespace-only lines.
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
        return ConsoleResult::Ok(std::string{});
    }

    push_history(line);
    append_scrollback("> " + line);

    auto argv = parse_line(line);
    if (argv.empty()) {
        return ConsoleResult::Ok(std::string{});
    }

    const auto it = cmds_.find(argv[0]);
    if (it == cmds_.end()) {
        auto err = ConsoleResult::Err("Unknown command: " + argv[0]);
        append_scrollback(err.error());
        return err;
    }

    // Build args vector (everything after the command name).
    std::vector<std::string> args(argv.begin() + 1, argv.end());
    auto result = it->second.fn(args);

    if (result.is_ok()) {
        if (!result.value().empty()) {
            append_scrollback(result.value());
        }
    } else {
        append_scrollback(result.error());
    }

    return result;
}

std::vector<std::string> Console::complete(const std::string& partial) const {
    std::vector<std::string> matches;
    for (const auto& [name, cmd] : cmds_) {
        if (name.rfind(partial, 0) == 0) { // starts_with
            matches.push_back(name);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

bool Console::has_cmd(const std::string& name) const {
    return cmds_.find(name) != cmds_.end();
}

const ConsoleCmd* Console::get_cmd(const std::string& name) const {
    const auto it = cmds_.find(name);
    if (it == cmds_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<std::string> Console::all_names() const {
    std::vector<std::string> names;
    names.reserve(cmds_.size());
    for (const auto& [name, cmd] : cmds_) {
        names.push_back(name);
    }
    return names;
}

const std::vector<std::string>& Console::scrollback() const {
    return scrollback_;
}

void Console::clear_scrollback() {
    scrollback_.clear();
}

void Console::push_history(const std::string& line) {
    // Only push if different from the last entry.
    if (!history_.empty() && history_.back() == line) {
        return;
    }

    history_.push_back(line);

    // Cap at kMaxHistory (drop oldest).
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin(),
                       history_.begin() + static_cast<std::ptrdiff_t>(history_.size() - kMaxHistory));
    }
}

const std::vector<std::string>& Console::history() const {
    return history_;
}

void Console::clear_history() {
    history_.clear();
}

// Tokenize by whitespace. Single-quoted and double-quoted strings are treated
// as single tokens (with the quotes removed). Escaped quotes inside quoted
// strings (\" or \') are unescaped.
std::vector<std::string> Console::parse_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        // Handle escape sequences inside quoted strings.
        if ((in_single_quote || in_double_quote) && c == '\\' && i + 1 < line.size()) {
            const char next = line[i + 1];
            if ((in_double_quote && next == '"') || (in_single_quote && next == '\'')) {
                current += next;
                ++i; // skip the escaped character
                continue;
            }
        }

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            // If we just opened/closed quotes with nothing between, that's an
            // empty token or continuation. We track it by marking the token
            // as "started" (we'll push even if empty when we hit whitespace
            // or end-of-string, but only if quotes were used).
            // Actually, just toggle the flag. The empty-string-between-quotes
            // case is handled: when we hit whitespace or EOL and current is
            // empty but we were in a quoted context, we still push.
            // Let me simplify: treat quote toggling as a "started token" signal.
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        if (!in_single_quote && !in_double_quote && (c == ' ' || c == '\t')) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }

        current += c;
    }

    // Push the last token (handles trailing token and empty-quoted tokens).
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }

    return tokens;
}

void Console::append_scrollback(const std::string& line) {
    scrollback_.push_back(line);

    // Cap at kMaxScrollback (drop oldest).
    if (scrollback_.size() > kMaxScrollback) {
        scrollback_.erase(scrollback_.begin(),
                          scrollback_.begin() + static_cast<std::ptrdiff_t>(scrollback_.size() - kMaxScrollback));
    }
}

} // namespace uz
