#pragma once

/// UzEngine Config System
/// .uzproject JSON loader with cascading resolution:
///   CLI flags → Scene overrides → .uzproject → Engine defaults

#include <json.hpp>
#include <string>
#include <optional>

namespace uz {

class Config {
public:
    /// Load .uzproject file. Returns false on parse error.
    bool LoadFromFile(const std::string& path);

    /// Load from JSON string (for testing).
    bool LoadFromString(const std::string& json);

    /// Apply CLI arg overrides. Format: "--key=value" or "--key value".
    void ApplyCLIOverrides(int argc, const char* const* argv);

    /// Typed getters with defaults.
    std::string GetString(const std::string& key, const std::string& defaultVal = "") const;
    int GetInt(const std::string& key, int defaultVal = 0) const;
    float GetFloat(const std::string& key, float defaultVal = 0.f) const;
    bool GetBool(const std::string& key, bool defaultVal = false) const;

    /// Check if key exists.
    bool Has(const std::string& key) const;

    /// Raw JSON access (dot-separated path: "render.resolution.0").
    const nlohmann::json* Resolve(const std::string& dottedKey) const;

    /// Get the full merged config.
    const nlohmann::json& GetRoot() const { return m_merged; }

private:
    nlohmann::json m_defaults = nlohmann::json::object();
    nlohmann::json m_project = nlohmann::json::object();
    nlohmann::json m_cli = nlohmann::json::object();
    nlohmann::json m_merged = nlohmann::json::object();

    void RebuildMerged();

    /// Resolve dotted path in a JSON object.
    static const nlohmann::json* ResolvePath(const nlohmann::json& root, const std::string& path);
};

/// Built-in engine defaults.
void ConfigSetEngineDefaults(Config& config);

} // namespace uz
