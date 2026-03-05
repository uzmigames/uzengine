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
#include "engine/renderer/renderer.h"
#include "engine/renderer/scene_params.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
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

        // Create / reset the real Scene.
        if (!g_sceneData) {
            g_sceneData = std::make_unique<uz::Scene>();
            uz::scene_init(g_sceneData.get(), "IPCScene");
        } else {
            uz::scene_clear(g_sceneData.get());
        }

        std::vector<uz::Renderer::EditorNode> editorNodes;
        editorNodes.reserve(args["nodes"].size());

        for (const auto& n : args["nodes"]) {
            const std::string typeStr = n.value("type", "Node3D");
            uz::NodeType nodeType = uz::node_type_from_string(typeStr.c_str());
            const std::string nodeName = n.value("name", "Unnamed");

            uz::NodeId id = uz::scene_create_node(
                g_sceneData.get(), nodeName.c_str(), nodeType);
            if (id == uz::kNodeIdInvalid) continue;

            uz::SceneNode* node = uz::scene_get_node(g_sceneData.get(), id);
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

            // Component data.
            if (nodeType == uz::NodeType::Camera) {
                uz::CameraData* cd = uz::component_add<uz::CameraData>(
                    &g_sceneData->components, id);
                if (cd) {
                    cd->fov_y      = n.value("fov",  45.0f);
                    cd->near_plane = n.value("near", 0.1f);
                    cd->far_plane  = n.value("far",  500.0f);
                }
            }
            if (nodeType == uz::NodeType::DirectionalLight) {
                uz::DirectionalLightData* ld = uz::component_add<uz::DirectionalLightData>(
                    &g_sceneData->components, id);
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
                    ld->intensity = n.value("light_intensity", 1.0f);
                }
            }

            // Build EditorNode for wireframe rendering.
            uz::Renderer::EditorNode edNode;
            edNode.position = {px, py, pz};
            edNode.rotation = {rx, ry, rz};
            edNode.rotationQuat = {
                node->local_transform.rotation.x,
                node->local_transform.rotation.y,
                node->local_transform.rotation.z,
                node->local_transform.rotation.w
            };
            edNode.scale    = {sx, sy, sz};
            edNode.nodeType = static_cast<uint16_t>(nodeType);
            edNode.nodeId   = static_cast<uint32_t>(id);

            if (typeStr == "StaticMesh") {
                edNode.color = {0.2f, 0.8f, 0.9f};
            } else if (typeStr == "Camera") {
                edNode.color = {0.9f, 0.9f, 0.2f};
            } else if (typeStr == "DirectionalLight" || typeStr == "PointLight") {
                edNode.color = {0.9f, 0.7f, 0.2f};
            } else {
                edNode.color = {0.8f, 0.8f, 0.8f};
            }
            editorNodes.push_back(edNode);
        }

        uz::scene_update_world_transforms(g_sceneData.get());
        renderer.setScene(g_sceneData.get());

        const size_t count = renderer.loadEditorScene(std::move(editorNodes));
        return {{"ok", true}, {"count", static_cast<int>(count)}};
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
}
