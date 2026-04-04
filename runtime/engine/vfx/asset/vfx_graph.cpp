/// VFX Graph asset implementation -- JSON parsing and module instantiation.

#include "vfx_graph.h"

#include "engine/vfx/modules/module_registry.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include "file_system.h"
#include "vfs.h"

#include <sstream>

UZ_DECLARE_LOG_CATEGORY(VFX);

using json = nlohmann::json;

namespace uz {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// JSON parsing helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Parses the "emitter" section from the root JSON object.
///
/// Missing fields use VFXEmitterConfig defaults.
VFXEmitterConfig ParseEmitterConfig(const json& root) {
    VFXEmitterConfig config;

    if (!root.contains("emitter") || !root["emitter"].is_object()) {
        return config;
    }

    const auto& emitter = root["emitter"];

    if (emitter.contains("max_particles") && emitter["max_particles"].is_number_integer()) {
        config.maxParticles = emitter["max_particles"].get<uint32_t>();
    }

    if (emitter.contains("lifespan_default") && emitter["lifespan_default"].is_number()) {
        config.lifespanDefault = emitter["lifespan_default"].get<float>();
    }

    return config;
}

/// Parses the "modules" array and instantiates each via the registry.
///
/// Unknown module types are logged and skipped. Config is applied via
/// IVFXModule::ApplyJsonConfig() for each module that overrides it.
std::vector<std::unique_ptr<IVFXModule>> ParseModules(
    const json& root, const ModuleRegistry& registry)
{
    std::vector<std::unique_ptr<IVFXModule>> modules;

    if (!root.contains("modules") || !root["modules"].is_array()) {
        return modules;
    }

    for (const auto& entry : root["modules"]) {
        if (!entry.is_object() || !entry.contains("type")) {
            UZ_LOG(VFX, Warning, "VFXGraph: skipping module entry without 'type' field");
            continue;
        }

        const auto typeName = entry["type"].get<std::string>();
        auto mod = registry.Create(typeName.c_str());

        if (!mod) {
            UZ_LOG(VFX, Warning, "VFXGraph: unknown module type '{}', skipping", typeName);
            continue;
        }

        // Apply per-module config from JSON if present.
        if (entry.contains("config") && entry["config"].is_object()) {
            mod->ApplyJsonConfig(entry["config"]);
        }

        modules.push_back(std::move(mod));
    }

    return modules;
}

/// Converts a type name string to a ParameterType enum value.
///
/// # Returns
/// The matching ParameterType, or Float as a safe default for unknown types.
ParameterType ParseParameterType(const std::string& typeStr) {
    if (typeStr == "float") return ParameterType::Float;
    if (typeStr == "vec3")  return ParameterType::Vec3;
    if (typeStr == "vec4")  return ParameterType::Vec4;
    if (typeStr == "int")   return ParameterType::Int;
    if (typeStr == "bool")  return ParameterType::Bool;
    if (typeStr == "curve") return ParameterType::Curve;

    UZ_LOG(VFX, Warning, "VFXGraph: unknown parameter type '{}', defaulting to Float", typeStr);
    return ParameterType::Float;
}

/// Parses a single parameter definition from a JSON object.
VFXParameterDef ParseOneParameter(const json& entry) {
    VFXParameterDef param;

    param.name = entry.value("name", "");
    param.type = ParseParameterType(entry.value("type", "float"));
    param.defaultFloat = entry.value("default", 0.0f);
    param.minValue = entry.value("min", 0.0f);
    param.maxValue = entry.value("max", 1.0f);

    // Parse Vec3 default: "default_vec3": [x, y, z]
    if (entry.contains("default_vec3") && entry["default_vec3"].is_array()) {
        const auto& arr = entry["default_vec3"];
        if (arr.size() >= 3) {
            param.defaultVec3 = Vec3{arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>()};
        }
    }

    // Parse Vec4 default: "default_vec4": [x, y, z, w]
    if (entry.contains("default_vec4") && entry["default_vec4"].is_array()) {
        const auto& arr = entry["default_vec4"];
        if (arr.size() >= 4) {
            param.defaultVec4 = Vec4{
                arr[0].get<float>(), arr[1].get<float>(),
                arr[2].get<float>(), arr[3].get<float>()};
        }
    }

    return param;
}

/// Parses the "parameters" array from the root JSON object.
std::vector<VFXParameterDef> ParseParameters(const json& root) {
    std::vector<VFXParameterDef> params;

    if (!root.contains("parameters") || !root["parameters"].is_array()) {
        return params;
    }

    for (const auto& entry : root["parameters"]) {
        if (!entry.is_object()) {
            UZ_LOG(VFX, Warning, "VFXGraph: skipping non-object parameter entry");
            continue;
        }
        params.push_back(ParseOneParameter(entry));
    }

    return params;
}

/// Parses the "events" array from the root JSON object.
std::vector<VFXEventDef> ParseEvents(const json& root) {
    std::vector<VFXEventDef> events;

    if (!root.contains("events") || !root["events"].is_array()) {
        return events;
    }

    for (const auto& entry : root["events"]) {
        if (!entry.is_object()) {
            UZ_LOG(VFX, Warning, "VFXGraph: skipping non-object event entry");
            continue;
        }

        VFXEventDef event;
        event.name = entry.value("name", "");
        event.emissionCount = entry.value("emission_count", 100u);
        event.velocityScale = entry.value("velocity_scale", 1.0f);
        events.push_back(std::move(event));
    }

    return events;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// VFXGraph::Load -- file-based loading
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXGraph> VFXGraph::Load(
    const std::string& path, const ModuleRegistry& registry,
    IVFS* vfs)
{
    // Read via VFS when available (supports .uzpak), fallback to disk FileReadAll.
    std::vector<uint8_t> fileData;
    if (vfs) {
        fileData = vfs->Read(path);
    } else {
        fileData = FileReadAll(path.c_str());
    }
    if (fileData.empty()) {
        UZ_LOG(VFX, Error, "VFXGraph::Load: failed to read '{}' (vfs={})",
               path, vfs ? vfs->Name() : "disk");
        return nullptr;
    }

    std::string jsonStr(fileData.begin(), fileData.end());
    auto graph = LoadFromString(jsonStr, registry);
    if (graph) {
        graph->sourcePath_ = path;

        // Derive name from filename if not set.
        if (graph->name_.empty()) {
            const auto slash = path.find_last_of("/\\");
            const auto dot = path.rfind('.');
            const auto start = (slash != std::string::npos) ? slash + 1 : 0;
            const auto end = (dot != std::string::npos && dot > start) ? dot : path.size();
            graph->name_ = path.substr(start, end - start);
        }
    }

    return graph;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXGraph::LoadFromString -- JSON parsing entry point
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXGraph> VFXGraph::LoadFromString(
    const std::string& jsonStr, const ModuleRegistry& registry)
{
    json root;
    try {
        root = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        UZ_LOG(VFX, Error, "VFXGraph::LoadFromString: JSON parse error: {}", e.what());
        return nullptr;
    }

    if (!root.is_object()) {
        UZ_LOG(VFX, Error, "VFXGraph::LoadFromString: root is not a JSON object");
        return nullptr;
    }

    // Validate format marker. Check type explicitly because nlohmann's
    // value() throws on type mismatch (e.g., integer where string expected).
    if (!root.contains("format") || !root["format"].is_string() ||
        root["format"].get<std::string>() != "uzvfx") {
        const std::string formatStr = root.contains("format") ? root["format"].dump() : "<missing>";
        UZ_LOG(VFX, Error,
               "VFXGraph::LoadFromString: invalid format {}, expected 'uzvfx'", formatStr);
        return nullptr;
    }

    auto graph = std::make_unique<VFXGraph>();
    graph->version_ = root.value("version", 0u);
    graph->name_ = root.value("name", "");
    graph->emitterConfig_ = ParseEmitterConfig(root);
    graph->modules_ = ParseModules(root, registry);
    graph->parameters_ = ParseParameters(root);
    graph->events_ = ParseEvents(root);

    UZ_LOG(VFX, Info,
           "VFXGraph loaded: version={}, modules={}, params={}, events={}",
           graph->version_, graph->modules_.size(),
           graph->parameters_.size(), graph->events_.size());

    return graph;
}

} // namespace uz
