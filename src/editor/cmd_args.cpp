// ---------------------------------------------------------------------------
// cmd_args.cpp -- Command-line argument parsing helpers.
// ---------------------------------------------------------------------------
#include "cmd_args.h"

#include <string>

std::vector<std::string> parseCmdLine(const char* cmdLine) {
    std::vector<std::string> args;
    if (!cmdLine || !*cmdLine) return args;
    std::string current;
    bool inQuote = false;
    for (const char* p = cmdLine; *p; ++p) {
        if (*p == '"') {
            inQuote = !inQuote;
        } else if (*p == ' ' && !inQuote) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += *p;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

std::string getCmdArg(const std::vector<std::string>& args,
                      const std::string& name,
                      const std::string& defaultVal) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return defaultVal;
}

uint16_t getCmdArgU16(const std::vector<std::string>& args,
                      const std::string& name,
                      uint16_t defaultVal) {
    std::string val = getCmdArg(args, name, "");
    if (val.empty()) return defaultVal;
    try { return static_cast<uint16_t>(std::stoi(val)); }
    catch (...) { return defaultVal; }
}
