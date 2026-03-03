// ipc_asset_handlers.cpp -- IPC handler implementations for asset.* commands.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// All handlers capture the AssetRegistry* by value (pointer copy). The registry
// must remain valid for the lifetime of the IpcRegistry.
//
// Handlers:
//   asset.list           — { type? } -> { ok, assets: [...] }
//   asset.import         — { path }  -> { ok, error? }
//   asset.getInfo        — { path }  -> { ok, info: {...} }
//   asset.createMaterial — { name }  -> { ok, path }
//   asset.createScene    — { name }  -> { ok, path }
//   asset.createShader   — { name }  -> { ok, path }

#include "engine/editor/ipc_asset_handlers.h"
#include "engine/editor/asset_registry.h"
#include "common/ipc_registry.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace uz {

namespace {

/// Build a standard error response.
nlohmann::json make_error(const char* msg) {
    return {{"ok", false}, {"error", msg}};
}

/// Build a standard error response from std::string.
nlohmann::json make_error(const std::string& msg) {
    return {{"ok", false}, {"error", msg}};
}

/// Read a template file from the assets/templates/ directory.
/// Returns the file content as a string, or empty on failure.
std::string read_template(const AssetRegistry* reg, const char* template_name) {
    // Templates live alongside the project root in assets/templates/.
    // The project_root might be e.g. "/project/assets/", so we look for
    // the template relative to the project root.
    const std::string& root = reg->project_root();
    const std::string template_path = root + "templates/" + template_name;

    size_t size = 0;
    char* data = platform::file_read_all(template_path.c_str(), &size);
    if (!data) return {};

    std::string result(data, size);
    std::free(data);
    return result;
}

/// Replace all occurrences of {{NAME}} with the given name in a template.
std::string apply_template(const std::string& tpl, const std::string& name) {
    std::string result = tpl;
    const std::string placeholder = "{{NAME}}";
    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
        result.replace(pos, placeholder.size(), name);
        pos += name.size();
    }
    return result;
}

} // anonymous namespace

void asset_register_handlers(IpcRegistry& registry, AssetRegistry* reg) {

    // ----------------------------------------------------------------
    // asset.list -- { type? } -> { ok, assets: [...] }
    // ----------------------------------------------------------------
    registry.add("asset.list", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        // Optional type filter.
        AssetType filter = AssetType::Unknown;
        bool has_filter = false;
        if (args.contains("type") && args["type"].is_string()) {
            filter = asset_type_from_string(args["type"].get<std::string>().c_str());
            if (filter != AssetType::Unknown) {
                has_filter = true;
            }
        }

        auto assets_arr = nlohmann::json::array();
        for (const auto& entry : reg->entries()) {
            if (has_filter && entry.type != filter) continue;

            assets_arr.push_back({
                {"path",   entry.path},
                {"type",   asset_type_to_string(entry.type)},
                {"status", import_status_to_string(entry.status)}
            });
        }

        return {
            {"ok",     true},
            {"assets", assets_arr}
        };
    });

    // ----------------------------------------------------------------
    // asset.import -- { path } -> { ok, error? }
    // ----------------------------------------------------------------
    registry.add("asset.import", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();

        if (!reg->get(path)) {
            return make_error("asset not found: " + path);
        }

        if (!reg->import_asset(path)) {
            const AssetEntry* entry = reg->get(path);
            std::string err_msg = "import failed";
            if (entry && !entry->error_msg.empty()) {
                err_msg = entry->error_msg;
            }
            return make_error(err_msg);
        }

        return {{"ok", true}};
    });

    // ----------------------------------------------------------------
    // asset.getInfo -- { path } -> { ok, info: {...} }
    // ----------------------------------------------------------------
    registry.add("asset.getInfo", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();

        const std::string json_str = reg->read_import_json(path);
        if (json_str.empty()) {
            return make_error("no import info for: " + path);
        }

        try {
            auto info = nlohmann::json::parse(json_str);
            return {
                {"ok",   true},
                {"info", info}
            };
        } catch (const nlohmann::json::exception& e) {
            return make_error(std::string("failed to parse import info: ") + e.what());
        }
    });

    // ----------------------------------------------------------------
    // asset.createMaterial -- { name } -> { ok, path }
    // ----------------------------------------------------------------
    registry.add("asset.createMaterial", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        if (!args.contains("name") || !args["name"].is_string()) {
            return make_error("missing or invalid 'name'");
        }

        const std::string name = args["name"].get<std::string>();
        const std::string rel_path = name + ".uzmat";
        const std::string abs_path = reg->project_root() + rel_path;

        // Read template.
        std::string content = read_template(reg, "material.uzmat");
        if (content.empty()) {
            // Fallback: generate a minimal template inline.
            content = "[material]\nname = " + name + "\nshader = unlit\n\n"
                      "[properties]\nalbedo_color = 1.0 1.0 1.0 1.0\n";
        } else {
            content = apply_template(content, name);
        }

        if (!platform::file_write_all(abs_path.c_str(), content.data(), content.size())) {
            return make_error("failed to write material: " + abs_path);
        }

        // Re-scan so the new asset appears in the registry.
        reg->scan(reg->project_root());

        return {
            {"ok",   true},
            {"path", rel_path}
        };
    });

    // ----------------------------------------------------------------
    // asset.createScene -- { name } -> { ok, path }
    // ----------------------------------------------------------------
    registry.add("asset.createScene", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        if (!args.contains("name") || !args["name"].is_string()) {
            return make_error("missing or invalid 'name'");
        }

        const std::string name = args["name"].get<std::string>();
        const std::string rel_path = name + ".uzscene";
        const std::string abs_path = reg->project_root() + rel_path;

        // Read template.
        std::string content = read_template(reg, "scene.uzscene");
        if (content.empty()) {
            // Fallback: generate a minimal scene inline.
            content = "[uzscene]\nname = " + name + "\nversion = 1\n\n"
                      "[node]\nname = Root\ntype = Node3D\nparent =\n"
                      "position = 0.0 0.0 0.0\nrotation = 0.0 0.0 0.0 1.0\n"
                      "scale = 1.0 1.0 1.0\nactive = true\nvisible = true\n";
        } else {
            content = apply_template(content, name);
        }

        if (!platform::file_write_all(abs_path.c_str(), content.data(), content.size())) {
            return make_error("failed to write scene: " + abs_path);
        }

        // Re-scan so the new asset appears in the registry.
        reg->scan(reg->project_root());

        return {
            {"ok",   true},
            {"path", rel_path}
        };
    });

    // ----------------------------------------------------------------
    // asset.createShader -- { name } -> { ok, path }
    // ----------------------------------------------------------------
    registry.add("asset.createShader", [reg](const nlohmann::json& args) -> nlohmann::json {
        if (!reg) {
            return make_error("no asset registry");
        }

        if (!args.contains("name") || !args["name"].is_string()) {
            return make_error("missing or invalid 'name'");
        }

        const std::string name = args["name"].get<std::string>();
        const std::string rel_path = name + ".hlsl";
        const std::string abs_path = reg->project_root() + rel_path;

        // Read template.
        std::string content = read_template(reg, "shader_unlit.hlsl");
        if (content.empty()) {
            // Fallback: generate a minimal shader inline.
            content = "// " + name + " -- minimal shader\n\n"
                      "float4 VSMain(float3 pos : POSITION) : SV_POSITION {\n"
                      "    return float4(pos, 1.0);\n}\n\n"
                      "float4 PSMain() : SV_TARGET {\n"
                      "    return float4(1.0, 1.0, 1.0, 1.0);\n}\n";
        } else {
            content = apply_template(content, name);
        }

        if (!platform::file_write_all(abs_path.c_str(), content.data(), content.size())) {
            return make_error("failed to write shader: " + abs_path);
        }

        // Re-scan so the new asset appears in the registry.
        reg->scan(reg->project_root());

        return {
            {"ok",   true},
            {"path", rel_path}
        };
    });
}

} // namespace uz
