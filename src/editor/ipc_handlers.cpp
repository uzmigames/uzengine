// ---------------------------------------------------------------------------
// ipc_handlers.cpp -- IPC handler registration for the editor.
//
// Extracted from main.cpp to keep it a pure orchestrator.
// ---------------------------------------------------------------------------
#include "ipc_handlers.h"
#include "scene_bridge.h"

#include "common/ipc_registry.h"
#include "common/ipc_poller.h"
#include "common/types.h"
#include "engine/core/crash_reporter.h"
#include "engine/core/log_streamer.h"
#include "engine/renderer/core/renderer.h"
#include "engine/renderer/scene/scene_params.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/scene/component_registry.h"
#include "engine/scene/scene_saver.h"
#include "network/ws_server.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>

void registerIpcHandlers(uz::IpcRegistry& ipcRegistry,
                         uz::SceneParams& g_scene,
                         uz::EngineMetrics& g_metrics,
                         uz::Renderer& renderer,
                         std::unique_ptr<uz::Scene>& g_sceneData,
                         uz::WsServer& wsServer,
                         const std::string& ipcBaseDir) {

    ipcRegistry.add("engine.ping", [&](const nlohmann::json&) -> nlohmann::json {
        return {{"ok", true},
                {"fps", g_metrics.fps.load()},
                {"frame", g_metrics.frameCount.load()}};
    });

    ipcRegistry.add("engine.getStatus", [&](const nlohmann::json&) -> nlohmann::json {
        float bgR = 0.1f, bgG = 0.1f, bgB = 0.1f;
        {
            std::lock_guard<std::mutex> lk(g_scene.bgMutex);
            bgR = g_scene.bgR;
            bgG = g_scene.bgG;
            bgB = g_scene.bgB;
        }
        const int geoType = g_scene.geometryType.load();
        return {
            {"ok", true},
            {"fps", g_metrics.fps.load()},
            {"frame", g_metrics.frameCount.load()},
            {"encoder", g_metrics.encoderName},
            {"resolution", g_metrics.resolution},
            {"scene", {
                {"rotation_speed", {g_scene.rotX.load(), g_scene.rotY.load(), g_scene.rotZ.load()}},
                {"background", {bgR, bgG, bgB}},
                {"geometry", (geoType == 1) ? "cube" : "pyramid"},
                {"paused", g_scene.paused.load()}
            }}
        };
    });

    ipcRegistry.add("benchmark.orbit", [&](const nlohmann::json& args) -> nlohmann::json {
        const bool enable = args.value("enable", true);
        g_scene.benchmarkOrbit.store(enable, std::memory_order_relaxed);
        return {{"ok", true}, {"enabled", enable}};
    });

    ipcRegistry.add("engine.screenshot", [&](const nlohmann::json& args) -> nlohmann::json {
        const std::string id = args.value("id", "screenshot");
        uz::FrameData frame = renderer.getLastFrameCopy();
        if (frame.pixels.empty()) {
            return {{"ok", false}, {"error", "no frame available"}};
        }

        const std::string filename = ipcBaseDir + "/frame_" + id + ".png";
        const int result = stbi_write_png(
            filename.c_str(),
            static_cast<int>(frame.width),
            static_cast<int>(frame.height),
            4,
            frame.pixels.data(),
            static_cast<int>(frame.width * 4));

        if (!result) {
            return {{"ok", false}, {"error", "stbi_write_png failed"}};
        }
        return {{"ok", true}, {"path", filename}};
    });

    ipcRegistry.add("scene.setRotation", [&](const nlohmann::json& args) -> nlohmann::json {
        if (args.contains("x")) g_scene.rotX.store(args["x"].get<float>());
        if (args.contains("y")) g_scene.rotY.store(args["y"].get<float>());
        if (args.contains("z")) g_scene.rotZ.store(args["z"].get<float>());
        return {{"ok", true}};
    });

    ipcRegistry.add("scene.setBackground", [&](const nlohmann::json& args) -> nlohmann::json {
        std::lock_guard<std::mutex> lk(g_scene.bgMutex);
        g_scene.bgR = args.value("r", g_scene.bgR);
        g_scene.bgG = args.value("g", g_scene.bgG);
        g_scene.bgB = args.value("b", g_scene.bgB);
        return {{"ok", true}};
    });

    ipcRegistry.add("scene.setPaused", [&](const nlohmann::json& args) -> nlohmann::json {
        g_scene.paused.store(args.value("paused", false));
        return {{"ok", true}};
    });

    ipcRegistry.add("scene.setGeometry", [&](const nlohmann::json& args) -> nlohmann::json {
        const std::string shape = args.value("shape", "pyramid");
        g_scene.geometryType.store(shape == "cube" ? 1 : 0);
        return {{"ok", true}, {"note", "geometry hot-swap not yet implemented, stored for future use"}};
    });

    ipcRegistry.add("scene.load", [&](const nlohmann::json& args) -> nlohmann::json {
        if (!args.contains("nodes") || !args["nodes"].is_array()) {
            return {{"ok", false}, {"error", "missing 'nodes' array"}};
        }

        // Auto-save current scene before loading new one.
        if (g_sceneData && g_sceneData->source_path[0] != '\0') {
            g_sceneData->editor_state.cam_pos_x = g_scene.camPosX;
            g_sceneData->editor_state.cam_pos_y = g_scene.camPosY;
            g_sceneData->editor_state.cam_pos_z = g_scene.camPosZ;
            g_sceneData->editor_state.cam_yaw   = g_scene.camYaw;
            g_sceneData->editor_state.cam_pitch = g_scene.camPitch;
            g_sceneData->editor_state.cam_speed = g_scene.camSpeed;
            g_sceneData->editor_state.valid     = true;
            (void)uz::scene_save_file(g_sceneData.get(), g_sceneData->source_path);
        }

        // Build the scene on the IPC thread (CPU-only work).
        // NO renderer calls here — GPU work deferred to main thread.
        auto pending = std::make_unique<PendingSceneLoad>();
        pending->scene = std::make_unique<uz::Scene>();
        uz::scene_init(pending->scene.get(), "IPCScene");

        std::vector<uz::Renderer::EditorNode> editorNodes;
        editorNodes.reserve(args["nodes"].size());

        for (const auto& n : args["nodes"]) {
            const std::string typeStr = n.value("type", "Node3D");
            const std::string nodeName = n.value("name", "Unnamed");

            uz::NodeId id = uz::scene_create_node(
                pending->scene.get(), nodeName.c_str(), typeStr.c_str());
            if (id == uz::kNodeIdInvalid) continue;

            uz::SceneNode* node = uz::scene_get_node(pending->scene.get(), id);
            if (!node) continue;

            float px = 0, py = 0, pz = 0;
            float rx = 0, ry = 0, rz = 0;
            float sx = 1, sy = 1, sz = 1;

            if (n.contains("position") && n["position"].is_array() && n["position"].size() >= 3) {
                px = n["position"][0].get<float>();
                py = n["position"][1].get<float>();
                pz = n["position"][2].get<float>();
                node->local_transform.position = {px, py, pz, 0.0f};
            }
            if (n.contains("rotation") && n["rotation"].is_array() && n["rotation"].size() >= 3) {
                rx = n["rotation"][0].get<float>();
                ry = n["rotation"][1].get<float>();
                rz = n["rotation"][2].get<float>();
                const float cy = std::cos(ry * 0.5f), sy2 = std::sin(ry * 0.5f);
                const float cp = std::cos(rx * 0.5f), sp  = std::sin(rx * 0.5f);
                const float cr = std::cos(rz * 0.5f), sr  = std::sin(rz * 0.5f);
                node->local_transform.rotation = {
                    cr * sp * cy + sr * cp * sy2,
                    cr * cp * sy2 - sr * sp * cy,
                    sr * cp * cy - cr * sp * sy2,
                    cr * cp * cy + sr * sp * sy2
                };
            }
            if (n.contains("scale") && n["scale"].is_array() && n["scale"].size() >= 3) {
                sx = n["scale"][0].get<float>();
                sy = n["scale"][1].get<float>();
                sz = n["scale"][2].get<float>();
                node->local_transform.scale = {sx, sy, sz, 0.0f};
            }

            uz::CameraData* cd = uz::component_get<uz::CameraData>(&pending->scene->components, id);
            if (cd) {
                cd->fov_y      = n.value("fov",  cd->fov_y);
                cd->near_plane = n.value("near", cd->near_plane);
                cd->far_plane  = n.value("far",  cd->far_plane);
            }
            uz::DirectionalLightData* ld = uz::component_get<uz::DirectionalLightData>(&pending->scene->components, id);
            if (ld) {
                if (n.contains("light_color") && n["light_color"].is_array()
                    && n["light_color"].size() >= 3) {
                    ld->color = {
                        n["light_color"][0].get<float>(),
                        n["light_color"][1].get<float>(),
                        n["light_color"][2].get<float>(),
                        0.0f
                    };
                }
                ld->intensity = n.value("light_intensity", ld->intensity);
            }

            uz::Renderer::EditorNode edNode;
            edNode.position = {px, py, pz};
            edNode.rotation = {rx, ry, rz};
            edNode.rotationQuat = {
                node->local_transform.rotation.x,
                node->local_transform.rotation.y,
                node->local_transform.rotation.z,
                node->local_transform.rotation.w
            };
            edNode.scale  = {sx, sy, sz};
            edNode.nodeId = static_cast<uint32_t>(id);

            const uz::NodeTypeDef* def = uz::ComponentRegistry::find(typeStr.c_str());
            if (def) {
                edNode.nodeType = def->type_index;
                edNode.color    = {def->editor_color[0], def->editor_color[1], def->editor_color[2]};
            } else {
                edNode.nodeType = 0;
                edNode.color    = {0.8f, 0.8f, 0.8f};
            }
            editorNodes.push_back(edNode);
        }

        uz::scene_update_world_transforms(pending->scene.get());
        pending->editor_nodes = std::move(editorNodes);
        int count = static_cast<int>(pending->scene->next_id - 1);

        enqueuePendingScene(std::move(pending));
        return {{"ok", true}, {"count", count}};
    });

    ipcRegistry.add("scene.loadFile", [&](const nlohmann::json& args) -> nlohmann::json {
        const std::string scenePath = args.value("path", "");
        if (scenePath.empty()) {
            return {{"ok", false}, {"error", "missing 'path' argument"}};
        }

        // Auto-save current scene before loading new one.
        // NOTE: reading cam floats from IPC thread is a benign race —
        // at worst the saved camera is one frame stale.
        if (g_sceneData && g_sceneData->source_path[0] != '\0') {
            g_sceneData->editor_state.cam_pos_x = g_scene.camPosX;
            g_sceneData->editor_state.cam_pos_y = g_scene.camPosY;
            g_sceneData->editor_state.cam_pos_z = g_scene.camPosZ;
            g_sceneData->editor_state.cam_yaw   = g_scene.camYaw;
            g_sceneData->editor_state.cam_pitch = g_scene.camPitch;
            g_sceneData->editor_state.cam_speed = g_scene.camSpeed;
            g_sceneData->editor_state.valid     = true;
            (void)uz::scene_save_file(g_sceneData.get(), g_sceneData->source_path);
        }

        // CPU-only scene parsing + enqueue for main thread.
        // No renderer calls here — GPU work happens on the main thread.
        auto result = loadSceneFileDeferred(scenePath);

        if (result.name.empty()) {
            return {{"ok", false}, {"error", "failed to load scene file"}};
        }

        return {{"ok", true},
                {"scene", result.name},
                {"nodes", result.node_count}};
    });

    ipcRegistry.add("scene.setParam", [&](const nlohmann::json& args) -> nlohmann::json {
        const std::string key = args.value("key", "");
        if (key.starts_with("rotation.")) {
            const std::string axis = key.substr(9);
            const float val = args.value("value", 0.0f);
            if (axis == "x") g_scene.rotX.store(val);
            else if (axis == "y") g_scene.rotY.store(val);
            else if (axis == "z") g_scene.rotZ.store(val);
            else return {{"ok", false}, {"error", "unknown rotation axis: " + axis}};
            return {{"ok", true}};
        }
        return {{"ok", false}, {"error", "unknown param: " + key}};
    });

    ipcRegistry.add("file.read", [&ipcBaseDir](const nlohmann::json& args) -> nlohmann::json {
        const std::string relPath = args.value("path", "");
        if (relPath.find("..") != std::string::npos) {
            return {{"ok", false}, {"error", "path traversal not allowed"}};
        }
        const std::string fullPath = ipcBaseDir + "/../" + relPath;
        std::ifstream f(fullPath, std::ios::binary);
        if (!f) {
            return {{"ok", false}, {"error", "file not found: " + relPath}};
        }
        const std::string content(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        return {{"ok", true}, {"content", content}};
    });

    ipcRegistry.add("file.write", [&ipcBaseDir](const nlohmann::json& args) -> nlohmann::json {
        const std::string relPath = args.value("path", "");
        const std::string content = args.value("content", "");
        if (relPath.find("..") != std::string::npos) {
            return {{"ok", false}, {"error", "path traversal not allowed"}};
        }
        const std::string fullPath = ipcBaseDir + "/../" + relPath;
        std::ofstream f(fullPath, std::ios::binary);
        if (!f) {
            return {{"ok", false}, {"error", "cannot write: " + relPath}};
        }
        f << content;
        return {{"ok", true}};
    });

    ipcRegistry.add("file.list", [&ipcBaseDir](const nlohmann::json& args) -> nlohmann::json {
        const std::string relPath = args.value("path", ".");
        if (relPath.find("..") != std::string::npos) {
            return {{"ok", false}, {"error", "path traversal not allowed"}};
        }
        const std::string fullPath = ipcBaseDir + "/../" + relPath;
        nlohmann::json files = nlohmann::json::array();
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((fullPath + "/*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (std::strcmp(fd.cFileName, ".") != 0
                    && std::strcmp(fd.cFileName, "..") != 0)
                {
                    files.push_back(fd.cFileName);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        return {{"ok", true}, {"files", files}};
    });

    ipcRegistry.add("shader.reload", [](const nlohmann::json& args) -> nlohmann::json {
        const std::string name = args.value("name", "");
        return {{"ok", false}, {"error", "shader hot-reload not yet implemented"}};
    });

    ipcRegistry.add("camera.getState", [&](const nlohmann::json&) -> nlohmann::json {
        return {
            {"ok", true},
            {"position", {g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ}},
            {"yaw",   g_scene.camYaw},
            {"pitch", g_scene.camPitch},
            {"speed", g_scene.camSpeed}
        };
    });

    ipcRegistry.add("camera.reset", [&](const nlohmann::json& args) -> nlohmann::json {
        g_scene.camPosX = args.value("x",     0.0f);
        g_scene.camPosY = args.value("y",    15.0f);
        g_scene.camPosZ = args.value("z",   -20.0f);
        g_scene.camYaw   = args.value("yaw",   0.0f);
        g_scene.camPitch = args.value("pitch", -0.6f);
        g_scene.camSpeed = args.value("speed", 10.0f);

        auto& ci = g_scene.cameraInput;
        ci.moveForward.store(0.0f, std::memory_order_relaxed);
        ci.moveBack.store(0.0f,    std::memory_order_relaxed);
        ci.moveLeft.store(0.0f,    std::memory_order_relaxed);
        ci.moveRight.store(0.0f,   std::memory_order_relaxed);
        ci.moveUp.store(0.0f,      std::memory_order_relaxed);
        ci.moveDown.store(0.0f,    std::memory_order_relaxed);
        ci.mouseDeltaX.store(0.0f, std::memory_order_relaxed);
        ci.mouseDeltaY.store(0.0f, std::memory_order_relaxed);
        ci.scrollDelta.store(0.0f, std::memory_order_relaxed);
        ci.rightMouseDown.store(false, std::memory_order_relaxed);
        ci.shiftDown.store(false,      std::memory_order_relaxed);

        return {{"ok", true}};
    });

    ipcRegistry.add("renderer.resize", [&](const nlohmann::json& args) -> nlohmann::json {
        const int w = args.value("width",  0);
        const int h = args.value("height", 0);
        if (w < 64 || h < 64) {
            return {{"ok", false}, {"error", "min 64x64"}};
        }
        g_scene.requestResize(w, h);
        return {{"ok", true}, {"requested", {w, h}},
                {"current", g_metrics.resolution},
                {"ws_clients", wsServer.clientCount()}};
    });

    ipcRegistry.add("renderer.info", [&](const nlohmann::json&) -> nlohmann::json {
        return {{"ok", true},
                {"resolution", g_metrics.resolution},
                {"ws_clients", wsServer.clientCount()},
                {"renderer_w", renderer.getWidth()},
                {"renderer_h", renderer.getHeight()}};
    });

    ipcRegistry.add("engine.getCrashReport", [](const nlohmann::json&) -> nlohmann::json {
        std::string report = uz::CrashReporter::read_latest_report();
        if (report.empty()) {
            return {{"ok", false}, {"error", "no crash report found"}};
        }
        // Parse the JSON string into a nlohmann::json object.
        try {
            auto parsed = nlohmann::json::parse(report);
            return {{"ok", true}, {"report", parsed}};
        } catch (const std::exception& e) {
            return {{"ok", false}, {"error", std::string("failed to parse crash report: ") + e.what()},
                    {"raw", report}};
        }
    });

    ipcRegistry.add("engine.getDredReport", [](const nlohmann::json&) -> nlohmann::json {
        // Read DRED report from the crash reports directory.
        std::string dred_dir = uz::CrashReporter::output_dir();
        std::string dred_path = std::string(dred_dir) + "/dred_report.json";

        std::ifstream f(dred_path);
        if (!f) {
            return {{"ok", false}, {"error", "no DRED report found (no GPU crash occurred)"}};
        }

        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        try {
            auto parsed = nlohmann::json::parse(content);
            return {{"ok", true}, {"report", parsed}};
        } catch (const std::exception& e) {
            return {{"ok", false}, {"error", std::string("failed to parse DRED report: ") + e.what()},
                    {"raw", content}};
        }
    });

    ipcRegistry.add("subscribe_logs", [](const nlohmann::json& args) -> nlohmann::json {
        std::string level_str = args.value("level", "INFO");
        auto min_level = uz::log_level_from_string(level_str);

        std::vector<std::string> systems;
        if (args.contains("systems") && args["systems"].is_array()) {
            for (const auto& s : args["systems"]) {
                if (s.is_string()) systems.push_back(s.get<std::string>());
            }
        }

        std::string id = uz::LogStreamer::subscribe(min_level, systems);
        return {{"ok", true}, {"subscription_id", id}};
    });

    ipcRegistry.add("unsubscribe_logs", [](const nlohmann::json& args) -> nlohmann::json {
        std::string id = args.value("id", "");
        if (id.empty()) {
            return {{"ok", false}, {"error", "missing 'id' parameter"}};
        }
        uz::LogStreamer::unsubscribe(id);
        return {{"ok", true}};
    });

    ipcRegistry.add("get_error_buffer", [](const nlohmann::json&) -> nlohmann::json {
        std::string buf = uz::LogStreamer::get_error_buffer_json();
        try {
            auto parsed = nlohmann::json::parse(buf);
            return {{"ok", true}, {"errors", parsed}};
        } catch (...) {
            return {{"ok", true}, {"errors", nlohmann::json::array()}};
        }
    });

    // §2: GPU Resource Introspection — query render target list.
    ipcRegistry.add("query_render_targets", [&renderer](const nlohmann::json&) -> nlohmann::json {
        // Return the known render targets from the deferred pipeline.
        nlohmann::json targets = nlohmann::json::array();
        const char* rt_names[] = {
            "GBuffer_Albedo", "GBuffer_Normal", "GBuffer_MetalRough",
            "GBuffer_Depth", "LightingResult", "FinalComposite"
        };
        const char* rt_formats[] = {
            "R8G8B8A8_UNORM", "R16G16B16A16_FLOAT", "R8G8B8A8_UNORM",
            "D32_FLOAT", "R16G16B16A16_FLOAT", "R8G8B8A8_UNORM"
        };
        for (int i = 0; i < 6; ++i) {
            targets.push_back({
                {"name", rt_names[i]},
                {"format", rt_formats[i]},
                {"width", renderer.getWidth()},
                {"height", renderer.getHeight()},
            });
        }
        return {{"ok", true}, {"render_targets", targets}};
    });

    // §2: GPU Resource Introspection — request texture readback (stub).
    ipcRegistry.add("request_texture", [](const nlohmann::json& args) -> nlohmann::json {
        std::string name = args.value("name", "");
        if (name.empty()) {
            return {{"ok", false}, {"error", "missing 'name' parameter"}};
        }
        // TODO: Implement actual RHI readback via RHITexture::readback().
        // For now, return a stub acknowledging the request.
        return {{"ok", false},
                {"error", "texture readback not yet implemented (requires RHI readback support)"},
                {"requested", name}};
    });

    // §6: RenderGraph Introspection — query render graph structure.
    ipcRegistry.add("query_render_graph", [&renderer](const nlohmann::json&) -> nlohmann::json {
        // Return the known render passes from the deferred pipeline.
        nlohmann::json passes = nlohmann::json::array();

        struct PassInfo { const char* name; const char* inputs; const char* outputs; };
        PassInfo graph[] = {
            {"GBufferPass",    "Scene Geometry",        "GBuffer_Albedo, GBuffer_Normal, GBuffer_MetalRough, GBuffer_Depth"},
            {"LightingPass",   "GBuffer_*, Shadow Maps", "LightingResult"},
            {"SkyboxPass",     "GBuffer_Depth",          "LightingResult (blend)"},
            {"GizmoPass",      "GBuffer_Depth",          "FinalComposite"},
            {"CompositePass",  "LightingResult",         "FinalComposite"},
        };

        for (const auto& p : graph) {
            passes.push_back({
                {"name", p.name},
                {"inputs", p.inputs},
                {"outputs", p.outputs},
                {"resolution", std::to_string(renderer.getWidth()) + "x" + std::to_string(renderer.getHeight())},
            });
        }

        return {{"ok", true},
                {"passes", passes},
                {"pass_count", passes.size()}};
    });

    // §6.3: GPU Memory query.
    ipcRegistry.add("query_gpu_memory", [](const nlohmann::json&) -> nlohmann::json {
        uint64_t total = 0, used = 0, budget = 0;
        if (rhi()->gpu_memory_info && rhi()->gpu_memory_info(&total, &used, &budget)) {
            return {{"ok", true},
                    {"total_bytes", total},
                    {"used_bytes", used},
                    {"budget_bytes", budget},
                    {"used_mb", used / (1024 * 1024)},
                    {"budget_mb", budget / (1024 * 1024)}};
        }
        return {{"ok", false}, {"error", "GPU memory info not supported"}};
    });
}

