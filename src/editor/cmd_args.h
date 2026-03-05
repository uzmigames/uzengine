// ---------------------------------------------------------------------------
// cmd_args.h -- Command-line argument parsing helpers.
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::vector<std::string> parseCmdLine(const char* cmdLine);

std::string getCmdArg(const std::vector<std::string>& args,
                      const std::string& name,
                      const std::string& defaultVal);

uint16_t getCmdArgU16(const std::vector<std::string>& args,
                      const std::string& name,
                      uint16_t defaultVal);
