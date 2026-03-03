// ipc_shader_handlers.cpp -- IPC handler implementations for shader.* commands.
//
// Each handler receives a JSON "args" object and returns a JSON result.
// All handlers capture the ShaderManager* by value (pointer copy). The manager
// must remain valid for the lifetime of the registry.
//
// Handlers:
//   shader.reload    — {path: string} -> {ok, errors, elapsed_ms}
//   shader.getErrors — {path: string} -> {ok, errors}
//   shader.list      — {} -> {ok, shaders: [{name, vs_path, ps_path, is_valid}]}
//   shader.getSource — {path: string} -> {ok, source}
//   shader.setSource — {path: string, source: string} -> {ok, error}

#include "common/ipc_shader_handlers.h"
#include "common/ipc_registry.h"
#include "engine/renderer/shader_manager.h"
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

/// Convert a vector of ShaderErrors to a JSON array.
nlohmann::json errors_to_json(const std::vector<ShaderError>& errors) {
    auto arr = nlohmann::json::array();
    for (const auto& err : errors) {
        arr.push_back({
            {"file",    err.file},
            {"line",    err.line},
            {"column",  err.column},
            {"message", err.message}
        });
    }
    return arr;
}

} // anonymous namespace

void shader_register_handlers(IpcRegistry& registry, ShaderManager* mgr) {

    // ----------------------------------------------------------------
    // shader.reload -- {path: string} -> {ok, errors, elapsed_ms}
    // ----------------------------------------------------------------
    registry.add("shader.reload", [mgr](const nlohmann::json& args) -> nlohmann::json {
        if (!mgr) {
            return make_error("no shader manager");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();

        const double t0 = platform::get_time_seconds();
        const bool affected = mgr->reload(path);
        const double t1 = platform::get_time_seconds();
        const double elapsed_ms = (t1 - t0) * 1000.0;

        if (!affected) {
            return {
                {"ok",         true},
                {"affected",   false},
                {"errors",     nlohmann::json::array()},
                {"elapsed_ms", elapsed_ms}
            };
        }

        auto errors = mgr->get_errors(path);
        return {
            {"ok",         true},
            {"affected",   true},
            {"errors",     errors_to_json(errors)},
            {"elapsed_ms", elapsed_ms}
        };
    });

    // ----------------------------------------------------------------
    // shader.getErrors -- {path: string} -> {ok, errors}
    // ----------------------------------------------------------------
    registry.add("shader.getErrors", [mgr](const nlohmann::json& args) -> nlohmann::json {
        if (!mgr) {
            return make_error("no shader manager");
        }

        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();
        auto errors = mgr->get_errors(path);

        return {
            {"ok",     true},
            {"errors", errors_to_json(errors)}
        };
    });

    // ----------------------------------------------------------------
    // shader.list -- {} -> {ok, shaders}
    // ----------------------------------------------------------------
    registry.add("shader.list", [mgr](const nlohmann::json& /*args*/) -> nlohmann::json {
        if (!mgr) {
            return make_error("no shader manager");
        }

        auto names = mgr->list();
        auto shaders_arr = nlohmann::json::array();

        for (const auto& name : names) {
            const ShaderInfo* info = mgr->get_info(name);
            if (info) {
                shaders_arr.push_back({
                    {"name",     info->name},
                    {"vs_path",  info->vs_path},
                    {"ps_path",  info->ps_path},
                    {"is_valid", info->is_valid}
                });
            }
        }

        return {
            {"ok",      true},
            {"shaders", shaders_arr}
        };
    });

    // ----------------------------------------------------------------
    // shader.getSource -- {path: string} -> {ok, source}
    // ----------------------------------------------------------------
    registry.add("shader.getSource", [](const nlohmann::json& args) -> nlohmann::json {
        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }

        const std::string path = args["path"].get<std::string>();

        size_t size = 0;
        char* data = platform::file_read_all(path.c_str(), &size);
        if (!data) {
            return make_error("failed to read file: " + path);
        }

        std::string source(data, size);
        free(data);

        return {
            {"ok",     true},
            {"source", source}
        };
    });

    // ----------------------------------------------------------------
    // shader.setSource -- {path: string, source: string} -> {ok}
    // ----------------------------------------------------------------
    registry.add("shader.setSource", [](const nlohmann::json& args) -> nlohmann::json {
        if (!args.contains("path") || !args["path"].is_string()) {
            return make_error("missing or invalid 'path'");
        }
        if (!args.contains("source") || !args["source"].is_string()) {
            return make_error("missing or invalid 'source'");
        }

        const std::string path   = args["path"].get<std::string>();
        const std::string source = args["source"].get<std::string>();

        bool ok = platform::file_write_all(path.c_str(), source.data(), source.size());
        if (!ok) {
            return make_error("failed to write file: " + path);
        }

        return {{"ok", true}};
    });
}

} // namespace uz
