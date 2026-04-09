/// UzEngine Configuration System Implementation
///
/// Provides the Config class implementation for cascaded configuration loading:
/// 1. Parse .uzproject JSON file
/// 2. Apply CLI argument overrides
/// 3. Merge with engine defaults
/// 4. Expose via typed getters with dot-notation paths

#include "config.h"
#include "cvar.h"

#include "../logging/log.h"
#include "profiler.h"

#include "file_system.h"

#include <sstream>

/// Log category for configuration loading and parsing errors.
UZ_DEFINE_LOG_CATEGORY(Config, Trace);

namespace uz {

bool Config::LoadFromFile(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    const std::string text = FileReadAllText(path.c_str());
    if (text.empty()) {
        UZ_LOG(Config, Error, "Failed to open config: {}", path);
        return false;
    }

    try {
        m_project = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(Config, Error, "JSON parse error in {}: {}", path, e.what());
        return false;
    }

    UZ_LOG(Config, Info, "Loaded config: {}", path);
    RebuildMerged();
    return true;
}

bool Config::LoadFromString(const std::string& json) {
    UZ_PROFILE_FUNCTION();
    try {
        m_project = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(Config, Error, "JSON parse error: {}", e.what());
        return false;
    }
    RebuildMerged();
    return true;
}

void Config::ApplyCLIOverrides(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0)
            continue;

        arg = arg.substr(2); // strip "--"
        std::string key, value;

        auto eq = arg.find('=');
        if (eq != std::string::npos) {
            key = arg.substr(0, eq);
            value = arg.substr(eq + 1);
        } else if (i + 1 < argc && argv[i + 1][0] != '-') {
            key = arg;
            value = argv[++i];
        } else {
            key = arg;
            value = "true";
        }

        // Try to parse as number or bool
        try {
            m_cli[key] = nlohmann::json::parse(value);
        } catch (...) {
            m_cli[key] = value;
        }

        // --cvar name=value → forward to CVarRegistry for runtime access.
        // The Config system stores it as key="cvar", value="name=value".
        // We parse it here and apply to CVarRegistry so systems like
        // QualityPresetManager can poll CVars directly.
        if (key == "cvar") {
            auto cvarEq = value.find('=');
            if (cvarEq != std::string::npos) {
                std::string cvarName = value.substr(0, cvarEq);
                float cvarValue = static_cast<float>(std::atof(value.substr(cvarEq + 1).c_str()));
                m_pendingCvarOverrides.emplace_back(cvarName, cvarValue);
            }
        }

        UZ_LOG(Config, Debug, "CLI override: {}={}", key, value);
    }
    RebuildMerged();
}

void Config::FlushCvarOverrides() {
    if (m_pendingCvarOverrides.empty()) return;
    auto& cvars = CVarRegistry::Get();
    // Apply but DO NOT clear the pending map. CVars declared inside
    // function bodies as `static auto*` only register the first time the
    // function runs, so the same overrides may need to be re-applied
    // after each major init phase. Idempotent — `cvars.Set` skips names
    // that have not been registered yet, so unknown entries simply wait
    // for a later flush.
    for (const auto& [name, value] : m_pendingCvarOverrides) {
        cvars.Set(name, value);
        UZ_LOG(Config, Info, "CVar applied: {} = {}", name, value);
    }
}

void Config::RebuildMerged() {
    m_merged = m_defaults;
    m_merged.merge_patch(m_project);
    m_merged.merge_patch(m_cli);
}

std::string Config::GetString(const std::string& key, const std::string& defaultVal) const {
    auto* val = Resolve(key);
    if (!val) return defaultVal;
    if (val->is_string()) return val->get<std::string>();
    // CLI values like "6.0" are parsed as JSON numbers by nlohmann::json::parse().
    // Convert back to string so callers like ParseShaderModel("6.0") work.
    // Use dump() which produces "6.0" not "6.000000" (unlike std::to_string).
    if (val->is_number() || val->is_boolean()) return val->dump();
    return defaultVal;
}

int Config::GetInt(const std::string& key, int defaultVal) const {
    auto* val = Resolve(key);
    if (val && val->is_number_integer())
        return val->get<int>();
    return defaultVal;
}

float Config::GetFloat(const std::string& key, float defaultVal) const {
    auto* val = Resolve(key);
    if (val && val->is_number())
        return val->get<float>();
    return defaultVal;
}

bool Config::GetBool(const std::string& key, bool defaultVal) const {
    auto* val = Resolve(key);
    if (val && val->is_boolean())
        return val->get<bool>();
    return defaultVal;
}

bool Config::Has(const std::string& key) const {
    return Resolve(key) != nullptr;
}

const nlohmann::json* Config::Resolve(const std::string& dottedKey) const {
    return ResolvePath(m_merged, dottedKey);
}

const nlohmann::json* Config::ResolvePath(const nlohmann::json& root, const std::string& path) {
    const nlohmann::json* current = &root;

    std::istringstream stream(path);
    std::string segment;
    while (std::getline(stream, segment, '.')) {
        if (current->is_object()) {
            auto it = current->find(segment);
            if (it == current->end())
                return nullptr;
            current = &(*it);
        } else if (current->is_array()) {
            try {
                size_t idx = std::stoull(segment);
                if (idx >= current->size())
                    return nullptr;
                current = &(*current)[idx];
            } catch (...) {
                return nullptr;
            }
        } else {
            return nullptr;
        }
    }
    return current;
}

void ConfigSetEngineDefaults(Config& config) {
    nlohmann::json defaults = {
        {"name", "Untitled"},
        {"version", "1.0"},
        {"engine_version", "0.2.0"},
        {"default_scene", ""},
        {"content_root", "content/"},
        {"render",
         {
             {"backend", "dx12"},
             {"shader_model", "6_6"},
             {"resolution", {1920, 1080}},
             {"vsync", true},
             {"hdr", false},
         }},
        {"physics",
         {
             {"gravity", {0, -9.81, 0}},
             {"fixed_timestep", 0.01667},
         }},
        {"window",
         {
             {"title", "UzEngine"},
             {"width", 1280},
             {"height", 720},
             {"fullscreen", false},
             {"resizable", true},
         }},
        {"log",
         {
             {"level", "info"},
             {"file", "uzengine.log"},
         }},
    };

    // LoadFromString sets m_project, but we want m_defaults.
    // Access defaults directly through the config's LoadFromString.
    config.LoadFromString(defaults.dump());
}

} // namespace uz
