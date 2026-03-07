// ipc_preview_handlers.cpp -- IPC handlers for shader graph preview system.

#include "engine/editor/ipc_preview_handlers.h"
#include "engine/renderer/preview/shader_graph_preview.h"
#include "engine/renderer/preview/shader_update_controller.h"
#include "engine/renderer/shader_graph/graph_ops.h"
#include "common/ipc_registry.h"

#include <nlohmann/json.hpp>

namespace uz {

void preview_register_handlers(IpcRegistry& registry,
                                sg::ShaderGraphPreview* preview,
                                sg::ShaderUpdateController* controller,
                                sg::GraphOps* /*ops*/) {
    // shader_graph.preview — start or get status of preview.
    registry.add("shader_graph.preview",
        [preview](const nlohmann::json& args) -> nlohmann::json {
            if (preview->is_active()) {
                return {{"ok", true}, {"viewportId", preview->viewport_id()}};
            }
            // Not yet active — return error (preview is started by renderer).
            return {{"ok", false}, {"error", "Preview not initialized"}};
        });

    // shader_graph.stopPreview — stop the preview.
    registry.add("shader_graph.stopPreview",
        [preview](const nlohmann::json&) -> nlohmann::json {
            if (preview->is_active()) {
                preview->shutdown();
            }
            return {{"ok", true}};
        });

    // shader_graph.setPreviewMesh — change preview geometry.
    registry.add("shader_graph.setPreviewMesh",
        [preview](const nlohmann::json& args) -> nlohmann::json {
            std::string mesh = args.value("mesh", "sphere");
            sg::PreviewMesh pm = sg::PreviewMesh::Sphere;
            if (mesh == "cube") pm = sg::PreviewMesh::Cube;
            else if (mesh == "plane") pm = sg::PreviewMesh::Plane;
            preview->set_preview_mesh(pm);
            return {{"ok", true}};
        });

    // shader_graph.setPreviewCamera — camera control.
    registry.add("shader_graph.setPreviewCamera",
        [preview](const nlohmann::json& args) -> nlohmann::json {
            if (args.contains("autoRotate")) {
                preview->set_auto_rotate(args["autoRotate"].get<bool>());
            }
            if (args.contains("yaw") || args.contains("pitch") || args.contains("distance")) {
                float yaw = args.value("yaw", 0.f);
                float pitch = args.value("pitch", 0.3f);
                float distance = args.value("distance", 3.0f);
                preview->set_camera_orbit(yaw, pitch, distance);
            }
            return {{"ok", true}};
        });

    // shader_graph.setPreviewParam — update material parameter.
    registry.add("shader_graph.setPreviewParam",
        [preview](const nlohmann::json& args) -> nlohmann::json {
            std::string name = args.value("name", "");
            if (name.empty()) {
                return {{"ok", false}, {"error", "Missing 'name'"}};
            }
            if (args.contains("value")) {
                auto& val = args["value"];
                if (val.is_number()) {
                    preview->set_scalar_param(name, val.get<float>());
                } else if (val.is_array()) {
                    std::vector<float> v;
                    for (auto& e : val) v.push_back(e.get<float>());
                    preview->set_vector_param(name, v.data(), static_cast<int>(v.size()));
                } else if (val.is_string()) {
                    preview->set_texture_param(name, val.get<std::string>());
                }
            }
            return {{"ok", true}};
        });

    // shader_graph.compile — manual compilation trigger.
    registry.add("shader_graph.compile",
        [controller](const nlohmann::json&) -> nlohmann::json {
            auto r = controller->compile_and_apply();
            nlohmann::json result;
            result["ok"] = r.ok;
            if (r.ok) {
                result["vertexHlsl"] = r.vertex_hlsl;
                result["pixelHlsl"] = r.pixel_hlsl;
            }
            if (!r.errors.empty()) {
                result["errors"] = r.errors;
            }
            return result;
        });
}

} // namespace uz
