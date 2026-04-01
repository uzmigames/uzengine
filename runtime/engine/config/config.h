#pragma once

/// UzEngine Configuration System
///
/// Loads and manages engine/project configuration with cascading precedence:
/// ```
/// CLI flags (highest priority)
///   ↓ overrides
/// Scene-specific overrides
///   ↓ overrides
/// .uzproject file
///   ↓ overrides
/// Engine defaults (lowest priority)
/// ```
///
/// # Format
/// Configuration is JSON (human-readable, LLM-friendly).
/// .uzproject files live at project root.
/// CLI overrides use `--key=value` or `--key value` syntax.
///
/// # Access
/// Use typed getters (GetString, GetInt, etc.) with defaults.
/// For nested values use dot-separated paths: `"render.viewport.width"`.

#include <json.hpp>
#include <optional>
#include <string>

namespace uz {

/// Main configuration manager (singleton at runtime).
///
/// Merges configuration from multiple sources with cascading priority.
/// Thread-safe: multiple threads can safely read config after initialization.
class Config {
  public:
    /// Loads .uzproject configuration file.
    ///
    /// # Arguments
    /// * `path` - File path to .uzproject (JSON format)
    ///
    /// # Returns
    /// - true if successfully parsed and merged
    /// - false if file doesn't exist or JSON parse error (check logs)
    ///
    /// # Behavior
    /// Merges loaded config with existing defaults and CLI overrides.
    /// Rebuilds the merged config after loading.
    bool LoadFromFile(const std::string& path);

    /// Loads configuration from a JSON string (primarily for testing).
    ///
    /// # Arguments
    /// * `json` - JSON string to parse
    ///
    /// # Returns
    /// - true if valid JSON successfully parsed
    /// - false on parse error
    bool LoadFromString(const std::string& json);

    /// Applies command-line argument overrides to config.
    ///
    /// # Arguments
    /// * `argc`, `argv` - Standard main() arguments
    ///
    /// # Format
    /// - `--key=value` : Sets key to value
    /// - `--key value` : Sets key to value (consumes next arg)
    /// - `--no-key` : Sets key to false
    /// - `--key` : Sets key to true (boolean flag)
    ///
    /// # Example
    /// ```
    /// ./engine --render.vsync=false --debug=true
    /// ```
    void ApplyCLIOverrides(int argc, const char* const* argv);

    /// Gets a string value from config with default fallback.
    ///
    /// # Arguments
    /// * `key` - Configuration key (dot-separated for nested: "render.api")
    /// * `defaultVal` - Returned if key not found
    ///
    /// # Returns
    /// Configuration value as string, or defaultVal if key missing/wrong type.
    std::string GetString(const std::string& key, const std::string& defaultVal = "") const;

    /// Gets an integer value from config with default fallback.
    int GetInt(const std::string& key, int defaultVal = 0) const;

    /// Gets a float value from config with default fallback.
    float GetFloat(const std::string& key, float defaultVal = 0.f) const;

    /// Gets a boolean value from config with default fallback.
    bool GetBool(const std::string& key, bool defaultVal = false) const;

    /// Checks if a configuration key exists (regardless of type).
    ///
    /// # Returns
    /// true if key resolves to a value in merged config.
    bool Has(const std::string& key) const;

    /// Raw JSON pointer to a configuration value (for advanced use).
    ///
    /// # Arguments
    /// * `dottedKey` - Dot-separated path (e.g., "render.resolution.width")
    ///
    /// # Returns
    /// Pointer to nlohmann::json value, or nullptr if not found.
    /// Lifetime is tied to Config lifetime.
    const nlohmann::json* Resolve(const std::string& dottedKey) const;

    /// Gets the full merged configuration object.
    ///
    /// # Returns
    /// Reference to merged JSON (CLI + project + defaults).
    /// Read-only after initialization.
    const nlohmann::json& GetRoot() const { return m_merged; }

  private:
    nlohmann::json m_defaults = nlohmann::json::object(); ///< Engine defaults
    nlohmann::json m_project = nlohmann::json::object();  ///< .uzproject loaded config
    nlohmann::json m_cli = nlohmann::json::object();      ///< CLI overrides
    nlohmann::json m_merged = nlohmann::json::object();   ///< Cascaded result

    /// Pending CVar overrides from --cvar CLI args (applied after CVarRegistry is initialized).
    std::vector<std::pair<std::string, float>> m_pendingCvarOverrides;

public:
    /// Flush pending --cvar overrides to CVarRegistry.
    /// Call after CVarRegistry has all CVars registered (after SceneRenderer::Init).
    void FlushCvarOverrides();

private:
    /// Rebuilds m_merged from all sources (called after any load/override).
    void RebuildMerged();

    /// Helper: resolves dot-separated path in a JSON object recursively.
    /// Returns pointer to value or nullptr if path invalid.
    static const nlohmann::json* ResolvePath(const nlohmann::json& root, const std::string& path);
};

/// Initializes config with built-in engine defaults.
///
/// # Behavior
/// Populates Config with sensible defaults for all engine subsystems.
/// Should be called once at engine startup before loading .uzproject.
///
/// # Arguments
/// * `config` - Config instance to populate with defaults
void ConfigSetEngineDefaults(Config& config);

} // namespace uz
