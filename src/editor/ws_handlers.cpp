// ---------------------------------------------------------------------------
// ws_handlers.cpp -- WebSocket text message dispatch for editor IPC.
//
// Extracted from main.cpp to meet CS-21 (1500-line limit).
// ---------------------------------------------------------------------------

#include "ws_handlers.h"
#include "projection.h"

#include "engine/renderer/core/renderer.h"
#include "engine/renderer/scene/scene_params.h"
#include "network/ws_server.h"

#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

void handleWsTextMessage(const std::string& text,
                         uz::SceneParams& g_scene,
                         uz::Renderer* g_renderer,
                         uz::Scene* g_sceneData,
                         uz::WsServer& wsServer) {
    try {
    const auto j = nlohmann::json::parse(text);
    const std::string type = j.value("type", "");

    // -- Viewport resize request from webview -------------------------
    if (type == "viewport_resize") {
        const int w = j.value("width",  0);
        const int h = j.value("height", 0);
        if (w >= 64 && w <= 7680 && h >= 64 && h <= 4320) {
            g_scene.requestResize(w, h);
            char dbg[128];
            snprintf(dbg, sizeof(dbg),
                     "[UzEngine] viewport_resize received: %dx%d\n", w, h);
            OutputDebugStringA(dbg);
        }
        return;
    }

    // -- Node picking via screen-space distance -------------------------
    if (type == "editor.pickNode") {
        const float sx = j.value("screen_x", 0.0f);
        const float sy = j.value("screen_y", 0.0f);
        const int vpW  = g_renderer ? g_renderer->getWidth()  : 0;
        const int vpH  = g_renderer ? g_renderer->getHeight() : 0;

        // Get the current view-projection matrix from the renderer.
        auto nodes = g_renderer ? g_renderer->getEditorNodes()
            : std::vector<uz::Renderer::EditorNode>{};

        // Project each node to screen space, find nearest to click.
        int bestNodeId = -1;
        float bestDist = 50.0f * 50.0f;  // max pick radius = 50 px

        // We need the VP matrix — get it from scene params (camera).
        // Build the same VP matrix the renderer uses.
        const float yaw   = g_scene.camYaw;
        const float pitch = g_scene.camPitch;
        const uz::Mat4 vp = buildCameraVP(yaw, pitch,
            g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ,
            vpW, vpH);

        for (const auto& node : nodes) {
            float screenX, screenY;
            if (!projectWorldToScreen(vp, node.position.x, node.position.y,
                                     node.position.z, vpW, vpH, screenX, screenY))
                continue;

            float dx = screenX - sx;
            float dy = screenY - sy;
            float distSq = dx * dx + dy * dy;

            if (distSq < bestDist) {
                bestDist   = distSq;
                bestNodeId = static_cast<int>(node.nodeId);
            }
        }

        // Prefer GPU pick buffer (pixel-accurate) when available.
#ifdef UZ_EDITOR
        if (g_renderer) {
            uint32_t gpuId = g_renderer->pickNodeAtScreen(static_cast<int>(sx), static_cast<int>(sy));
            if (gpuId != 0) {
                bestNodeId = static_cast<int>(gpuId);
            }
        }
#endif

        // Send response back via WS text message.
        nlohmann::json resp;
        resp["type"] = "pickResult";
        if (bestNodeId >= 0) {
            resp["nodeId"] = bestNodeId;
        } else {
            resp["nodeId"] = nullptr;
        }
        resp["debug_nodeCount"] = (int)nodes.size();
        resp["debug_vpSize"] = std::to_string(vpW) + "x" + std::to_string(vpH);
        resp["debug_click"] = std::to_string((int)sx) + "," + std::to_string((int)sy);
        resp["debug_bestDist"] = bestDist < 50.0f * 50.0f ? std::sqrt(bestDist) : -1.0f;

        // Include projected positions of all nodes for debugging
        nlohmann::json projNodes = nlohmann::json::array();
        for (const auto& node : nodes) {
            float screenX, screenY;
            if (!projectWorldToScreen(vp, node.position.x, node.position.y,
                                     node.position.z, vpW, vpH, screenX, screenY))
                continue;
            projNodes.push_back({
                {"id", (int)node.nodeId},
                {"sx", (int)screenX}, {"sy", (int)screenY},
                {"worldPos", {node.position.x, node.position.y, node.position.z}}
            });
        }
        resp["debug_projNodes"] = projNodes;

        wsServer.broadcastText(resp.dump());

        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "[UzEngine] pickNode at (%.0f,%.0f) vp=%dx%d nodes=%d -> id=%d\n",
                 sx, sy, vpW, vpH, (int)nodes.size(), bestNodeId);
        OutputDebugStringA(dbg);

        // Auto-select the picked node
        if (bestNodeId >= 0) {
            g_scene.selectedNodeId.store(static_cast<uint32_t>(bestNodeId),
                                          std::memory_order_relaxed);
        } else {
            g_scene.selectedNodeId.store(0, std::memory_order_relaxed);
        }
        return;
    }

    // -- Task 2.5: box-pick — return all node IDs within a screen rect ----
    if (type == "editor.boxPick") {
        const int bx = j.value("x", 0);
        const int by = j.value("y", 0);
        const int bw = j.value("width", 0);
        const int bh = j.value("height", 0);

        nlohmann::json resp;
        resp["type"] = "boxPickResult";
#ifdef UZ_EDITOR
        if (g_renderer) {
            auto ids = g_renderer->boxPickNodes(bx, by, bw, bh);
            resp["nodeIds"] = ids;
        } else {
            resp["nodeIds"] = nlohmann::json::array();
        }
#else
        resp["nodeIds"] = nlohmann::json::array();
#endif
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Gizmo mode switching -----------------------------------------------
    if (type == "gizmo_mode") {
        const std::string mode = j.value("mode", "");
        if      (mode == "translate") g_scene.gizmoMode.store(0, std::memory_order_relaxed);
        else if (mode == "rotate")    g_scene.gizmoMode.store(1, std::memory_order_relaxed);
        else if (mode == "scale")     g_scene.gizmoMode.store(2, std::memory_order_relaxed);
        return;
    }

    if (type == "gizmo_space") {
        const std::string sp = j.value("space", "");
        if      (sp == "world") g_scene.gizmoSpace.store(0, std::memory_order_relaxed);
        else if (sp == "local") g_scene.gizmoSpace.store(1, std::memory_order_relaxed);
        return;
    }

    // -- Node selection via WS -----------------------------------------------
    if (type == "editor.selectNode") {
        uint32_t id = j.value("nodeId", 0u);
        g_scene.selectedNodeId.store(id, std::memory_order_relaxed);
        // Broadcast selection change
        nlohmann::json resp;
        resp["type"] = "selectionChanged";
        resp["nodeId"] = id > 0 ? nlohmann::json(id) : nlohmann::json(nullptr);
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Gizmo begin drag — hit test axis handles ---------------------------
    if (type == "editor.gizmoBeginDrag") {
        const float msx = j.value("screen_x", 0.0f);
        const float msy = j.value("screen_y", 0.0f);

        const uint32_t selId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
        if (selId == 0 || !g_renderer) {
            nlohmann::json resp;
            resp["type"] = "gizmoState";
            resp["axis"] = nullptr;
            wsServer.broadcastText(resp.dump());
            return;
        }

        // Find selected node's full transform
        auto eNodes = g_renderer->getEditorNodes();
        uz::Float3 selPos{0,0,0};
        uz::Float3 selRot{0,0,0};
        uz::Float3 selScale{1,1,1};
        bool found = false;
        for (const auto& n : eNodes) {
            if (n.nodeId == selId) {
                selPos = n.position;
                selRot = n.rotation;
                selScale = n.scale;
                found = true;
                break;
            }
        }
        if (!found) {
            nlohmann::json resp;
            resp["type"] = "gizmoState";
            resp["axis"] = nullptr;
            wsServer.broadcastText(resp.dump());
            return;
        }

        const int vpW = g_renderer->getWidth();
        const int vpH = g_renderer->getHeight();
        const uz::Mat4 vpMat = buildCameraVP(
            g_scene.camYaw, g_scene.camPitch,
            g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ,
            vpW, vpH);

        // Scale hit-test radii by camera distance (matches renderer task 1.5).
        // Base constants match gizmo_transform.cpp kAxisLen / kCircleRad.
        const float gizmoDistBD = sqrtf(
            (selPos.x-g_scene.camPosX)*(selPos.x-g_scene.camPosX) +
            (selPos.y-g_scene.camPosY)*(selPos.y-g_scene.camPosY) +
            (selPos.z-g_scene.camPosZ)*(selPos.z-g_scene.camPosZ));
        const float gizmoScaleBD = std::max(gizmoDistBD * 0.1f, 0.1f);
        const float kAxisLen    = 2.5f * gizmoScaleBD;
        const float kCircleRad  = 1.8f * gizmoScaleBD;
        constexpr int kCircleSamples = 16;

        int gMode = g_scene.gizmoMode.load(std::memory_order_relaxed);

        int bestAxis = -1;
        float bestDist = 50.0f * 50.0f; // 50px pick radius

        // Helper lambda: project world point to screen
        auto projectToScreen = [&](float wx, float wy, float wz, float& outSx, float& outSy) -> bool {
            return projectWorldToScreen(vpMat, wx, wy, wz, vpW, vpH, outSx, outSy);
        };

        // Helper: point-to-line-segment distance squared
        auto ptSegDistSq = [](float px, float py, float ax, float ay, float bx, float by) -> float {
            float dx = bx - ax, dy = by - ay;
            float lenSq = dx*dx + dy*dy;
            if (lenSq < 1.0f) {
                float ex = px - ax, ey = py - ay;
                return ex*ex + ey*ey;
            }
            float t = std::clamp(((px - ax)*dx + (py - ay)*dy) / lenSq, 0.0f, 1.0f);
            float cx = ax + t*dx, cy = ay + t*dy;
            float ex = px - cx, ey = py - cy;
            return ex*ex + ey*ey;
        };

        if (gMode == 1) {
            // --- ROTATION MODE: check distance to projected circle arcs ---
            constexpr float kPI2 = 6.28318530718f;
            for (int a = 0; a < 3; ++a) {
                float prevSx, prevSy;
                bool prevValid = false;
                for (int s = 0; s <= kCircleSamples; ++s) {
                    float angle = (float)s / kCircleSamples * kPI2;
                    float wx, wy, wz;
                    if (a == 0) { // X rotation: circle in YZ plane
                        wx = selPos.x;
                        wy = selPos.y + cosf(angle) * kCircleRad;
                        wz = selPos.z + sinf(angle) * kCircleRad;
                    } else if (a == 1) { // Y rotation: circle in XZ plane
                        wx = selPos.x + cosf(angle) * kCircleRad;
                        wy = selPos.y;
                        wz = selPos.z + sinf(angle) * kCircleRad;
                    } else { // Z rotation: circle in XY plane
                        wx = selPos.x + cosf(angle) * kCircleRad;
                        wy = selPos.y + sinf(angle) * kCircleRad;
                        wz = selPos.z;
                    }
                    float sx2, sy2;
                    if (projectToScreen(wx, wy, wz, sx2, sy2)) {
                        if (prevValid) {
                            float d = ptSegDistSq(msx, msy, prevSx, prevSy, sx2, sy2);
                            if (d < bestDist) {
                                bestDist = d;
                                bestAxis = a;
                            }
                        }
                        prevSx = sx2; prevSy = sy2;
                        prevValid = true;
                    } else {
                        prevValid = false;
                    }
                }
            }
        } else {
            // --- TRANSLATE / SCALE MODE: check distance to axis line segments ---
            uz::Float3 axisEndpoints[3] = {
                {selPos.x + kAxisLen, selPos.y, selPos.z},
                {selPos.x, selPos.y + kAxisLen, selPos.z},
                {selPos.x, selPos.y, selPos.z + kAxisLen}
            };

            float origSx, origSy;
            if (!projectToScreen(selPos.x, selPos.y, selPos.z, origSx, origSy)) {
                // Origin behind camera — can't test
            } else {
                for (int a = 0; a < 3; ++a) {
                    float endSx, endSy;
                    if (!projectToScreen(axisEndpoints[a].x, axisEndpoints[a].y, axisEndpoints[a].z, endSx, endSy)) continue;
                    float d = ptSegDistSq(msx, msy, origSx, origSy, endSx, endSy);
                    if (d < bestDist) {
                        bestDist = d;
                        bestAxis = a;
                    }
                }
            }
        }

        g_scene.gizmoActiveAxis.store(bestAxis, std::memory_order_relaxed);
        g_scene.gizmoDragging.store(bestAxis >= 0, std::memory_order_relaxed);
        g_scene.gizmoDragStartX.store(msx, std::memory_order_relaxed);
        g_scene.gizmoDragStartY.store(msy, std::memory_order_relaxed);

        // Save original transform (position + rotation + scale)
        g_scene.gizmoDragOrigPos[0] = selPos.x;
        g_scene.gizmoDragOrigPos[1] = selPos.y;
        g_scene.gizmoDragOrigPos[2] = selPos.z;
        g_scene.gizmoDragOrigRot[0] = selRot.x;
        g_scene.gizmoDragOrigRot[1] = selRot.y;
        g_scene.gizmoDragOrigRot[2] = selRot.z;
        g_scene.gizmoDragOrigScale[0] = selScale.x;
        g_scene.gizmoDragOrigScale[1] = selScale.y;
        g_scene.gizmoDragOrigScale[2] = selScale.z;

        nlohmann::json resp;
        resp["type"] = "gizmoState";
        if (bestAxis >= 0) {
            const char* axisNames[] = {"x", "y", "z"};
            resp["axis"] = axisNames[bestAxis];
        } else {
            resp["axis"] = nullptr;
        }
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Gizmo drag — apply continuous transform ----------------------------
    if (type == "editor.gizmoDrag") {
        if (!g_scene.gizmoDragging.load(std::memory_order_relaxed)) return;
        if (!g_renderer) return;

        const float msx = j.value("screen_x", 0.0f);
        const float msy = j.value("screen_y", 0.0f);
        const int axis = g_scene.gizmoActiveAxis.load(std::memory_order_relaxed);
        if (axis < 0 || axis > 2) return;

        const float startX = g_scene.gizmoDragStartX.load(std::memory_order_relaxed);
        const float startY = g_scene.gizmoDragStartY.load(std::memory_order_relaxed);
        const float deltaScreenX = msx - startX;
        const float deltaScreenY = msy - startY;

        const int vpW = g_renderer->getWidth();
        const int vpH = g_renderer->getHeight();
        const uz::Mat4 vpMat = buildCameraVP(
            g_scene.camYaw, g_scene.camPitch,
            g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ,
            vpW, vpH);

        // Project the axis direction to screen space to determine
        // how screen movement maps to world movement.
        uz::Float3 origPos = {g_scene.gizmoDragOrigPos[0],
                            g_scene.gizmoDragOrigPos[1],
                            g_scene.gizmoDragOrigPos[2]};

        uz::Float3 axisDirArr[3] = {{1,0,0},{0,1,0},{0,0,1}};
        uz::Float3 axisEnd = {
            origPos.x + axisDirArr[axis].x,
            origPos.y + axisDirArr[axis].y,
            origPos.z + axisDirArr[axis].z
        };

        // Project origin and axis endpoint
        float sOx, sOy, sEx, sEy;
        if (!projectWorldToScreen(vpMat, origPos.x, origPos.y, origPos.z, vpW, vpH, sOx, sOy)) return;
        if (!projectWorldToScreen(vpMat, axisEnd.x, axisEnd.y, axisEnd.z, vpW, vpH, sEx, sEy)) return;

        // Screen-space axis direction
        float sDx = sEx - sOx;
        float sDy = sEy - sOy;
        float sLen = sqrtf(sDx * sDx + sDy * sDy);
        if (sLen < 0.5f) return;

        // Project screen delta onto screen axis direction
        float projectedPx = (deltaScreenX * sDx + deltaScreenY * sDy) / sLen;

        int gMode = g_scene.gizmoMode.load(std::memory_order_relaxed);

        if (gMode == 0) {
            // Translate: convert pixel delta to world units
            // 1 world unit = sLen pixels on screen
            float worldDelta = projectedPx / sLen;

            float newPos[3] = {
                g_scene.gizmoDragOrigPos[0],
                g_scene.gizmoDragOrigPos[1],
                g_scene.gizmoDragOrigPos[2]
            };
            newPos[axis] += worldDelta;

            // Apply to scene node
            uint32_t selId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
            auto eNodes = g_renderer->getEditorNodes();
            for (size_t i = 0; i < eNodes.size(); ++i) {
                if (eNodes[i].nodeId == selId) {
                    eNodes[i].position = {newPos[0], newPos[1], newPos[2]};
                    break;
                }
            }
            g_renderer->loadEditorNodes(std::move(eNodes));

            // Also broadcast the position change
            nlohmann::json resp;
            resp["type"] = "transformUpdate";
            resp["nodeId"] = selId;
            resp["position"] = {newPos[0], newPos[1], newPos[2]};
            wsServer.broadcastText(resp.dump());

        } else if (gMode == 1) {
            // Rotate: pixel delta → angle
            float angleDelta = projectedPx * 0.01f; // radians per pixel

            float newRot[3] = {
                g_scene.gizmoDragOrigRot[0],
                g_scene.gizmoDragOrigRot[1],
                g_scene.gizmoDragOrigRot[2]
            };
            newRot[axis] += angleDelta;

            uint32_t selId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
            auto eNodes = g_renderer->getEditorNodes();
            for (size_t i = 0; i < eNodes.size(); ++i) {
                if (eNodes[i].nodeId == selId) {
                    eNodes[i].rotation = {newRot[0], newRot[1], newRot[2]};
                    // Also update quaternion from euler for consistent rendering
                    uz::Quat qx = uz::quat_from_axis_angle({1,0,0,0}, newRot[0]);
                    uz::Quat qy = uz::quat_from_axis_angle({0,1,0,0}, newRot[1]);
                    uz::Quat qz = uz::quat_from_axis_angle({0,0,1,0}, newRot[2]);
                    eNodes[i].rotationQuat = uz::quat_mul(uz::quat_mul(qz, qy), qx);
                    break;
                }
            }
            g_renderer->loadEditorNodes(std::move(eNodes));

            nlohmann::json resp;
            resp["type"] = "transformUpdate";
            resp["nodeId"] = selId;
            resp["rotation"] = {newRot[0], newRot[1], newRot[2]};
            wsServer.broadcastText(resp.dump());

        } else if (gMode == 2) {
            // Scale: pixel delta → scale factor
            float scaleFactor = 1.0f + projectedPx * 0.005f;
            if (scaleFactor < 0.01f) scaleFactor = 0.01f;

            float newScale[3] = {
                g_scene.gizmoDragOrigScale[0],
                g_scene.gizmoDragOrigScale[1],
                g_scene.gizmoDragOrigScale[2]
            };
            newScale[axis] *= scaleFactor;

            uint32_t selId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
            auto eNodes = g_renderer->getEditorNodes();
            for (size_t i = 0; i < eNodes.size(); ++i) {
                if (eNodes[i].nodeId == selId) {
                    eNodes[i].scale = {newScale[0], newScale[1], newScale[2]};
                    break;
                }
            }
            g_renderer->loadEditorNodes(std::move(eNodes));

            nlohmann::json resp;
            resp["type"] = "transformUpdate";
            resp["nodeId"] = selId;
            resp["scale"] = {newScale[0], newScale[1], newScale[2]};
            wsServer.broadcastText(resp.dump());
        }
        return;
    }

    // -- Gizmo end drag -------------------------------------------------------
    if (type == "editor.gizmoEndDrag") {
        g_scene.gizmoDragging.store(false, std::memory_order_relaxed);
        g_scene.gizmoActiveAxis.store(-1, std::memory_order_relaxed);

        nlohmann::json resp;
        resp["type"] = "gizmoState";
        resp["axis"] = nullptr;
        resp["dragging"] = false;
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Gizmo hover — hit test without drag for visual feedback -------
    if (type == "editor.gizmoHover") {
        if (!g_renderer) return;
        if (g_scene.gizmoDragging.load(std::memory_order_relaxed)) return;

        using namespace uz;
        const float msx = j.value("screen_x", 0.0f);
        const float msy = j.value("screen_y", 0.0f);

        uint32_t selId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
        if (selId == 0) {
            g_scene.gizmoHoverAxis.store(-1, std::memory_order_relaxed);
            return;
        }

        auto eNodes = g_renderer->getEditorNodes();
        Float3 selPos{0,0,0};
        bool found = false;
        for (const auto& n : eNodes) {
            if (n.nodeId == selId) { selPos = n.position; found = true; break; }
        }
        if (!found) {
            g_scene.gizmoHoverAxis.store(-1, std::memory_order_relaxed);
            return;
        }

        const int vpW = g_renderer->getWidth();
        const int vpH = g_renderer->getHeight();
        const Mat4 vpMat = buildCameraVP(
            g_scene.camYaw, g_scene.camPitch,
            g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ,
            vpW, vpH);

        // Scale hit-test radii by camera distance (matches renderer task 1.5).
        const float gizmoDistHV = sqrtf(
            (selPos.x-g_scene.camPosX)*(selPos.x-g_scene.camPosX) +
            (selPos.y-g_scene.camPosY)*(selPos.y-g_scene.camPosY) +
            (selPos.z-g_scene.camPosZ)*(selPos.z-g_scene.camPosZ));
        const float gizmoScaleHV = std::max(gizmoDistHV * 0.1f, 0.1f);
        const float kAxisLen    = 2.5f * gizmoScaleHV;
        const float kCircleRad  = 1.8f * gizmoScaleHV;
        constexpr int kCircleSamples = 16;
        constexpr float kPI2 = 6.28318530718f;
        int gMode = g_scene.gizmoMode.load(std::memory_order_relaxed);

        int bestAxis = -1;
        float bestDist = 50.0f * 50.0f;

        auto projectToScreen = [&](float wx, float wy, float wz, float& outSx, float& outSy) -> bool {
            return projectWorldToScreen(vpMat, wx, wy, wz, vpW, vpH, outSx, outSy);
        };
        auto ptSegDistSq = [](float px, float py, float ax, float ay, float bx, float by) -> float {
            float dx = bx-ax, dy = by-ay;
            float lenSq = dx*dx+dy*dy;
            if (lenSq < 1.0f) { float ex=px-ax,ey=py-ay; return ex*ex+ey*ey; }
            float t = std::clamp(((px-ax)*dx+(py-ay)*dy)/lenSq, 0.0f, 1.0f);
            float cx=ax+t*dx, cy=ay+t*dy;
            float ex=px-cx, ey=py-cy;
            return ex*ex+ey*ey;
        };

        if (gMode == 1) {
            for (int a = 0; a < 3; ++a) {
                float prevSx, prevSy; bool prevValid = false;
                for (int s = 0; s <= kCircleSamples; ++s) {
                    float angle = (float)s/kCircleSamples*kPI2;
                    float wx, wy, wz;
                    if (a==0) { wx=selPos.x; wy=selPos.y+cosf(angle)*kCircleRad; wz=selPos.z+sinf(angle)*kCircleRad; }
                    else if (a==1) { wx=selPos.x+cosf(angle)*kCircleRad; wy=selPos.y; wz=selPos.z+sinf(angle)*kCircleRad; }
                    else { wx=selPos.x+cosf(angle)*kCircleRad; wy=selPos.y+sinf(angle)*kCircleRad; wz=selPos.z; }
                    float sx2, sy2;
                    if (projectToScreen(wx,wy,wz,sx2,sy2)) {
                        if (prevValid) { float d=ptSegDistSq(msx,msy,prevSx,prevSy,sx2,sy2); if(d<bestDist){bestDist=d;bestAxis=a;} }
                        prevSx=sx2; prevSy=sy2; prevValid=true;
                    } else { prevValid=false; }
                }
            }
        } else {
            uz::Float3 axisEnd[3] = {
                {selPos.x+kAxisLen,selPos.y,selPos.z},
                {selPos.x,selPos.y+kAxisLen,selPos.z},
                {selPos.x,selPos.y,selPos.z+kAxisLen}
            };
            float osx, osy;
            if (projectToScreen(selPos.x,selPos.y,selPos.z,osx,osy)) {
                for (int a=0;a<3;++a) {
                    float esx,esy;
                    if (!projectToScreen(axisEnd[a].x,axisEnd[a].y,axisEnd[a].z,esx,esy)) continue;
                    float d=ptSegDistSq(msx,msy,osx,osy,esx,esy);
                    if(d<bestDist){bestDist=d;bestAxis=a;}
                }
            }
        }

        g_scene.gizmoHoverAxis.store(bestAxis, std::memory_order_relaxed);

        nlohmann::json hresp;
        hresp["type"] = "gizmoHover";
        if (bestAxis >= 0) {
            const char* axisNames[] = {"x","y","z"};
            hresp["axis"] = axisNames[bestAxis];
        } else {
            hresp["axis"] = nullptr;
        }
        wsServer.broadcastText(hresp.dump());
        return;
    }

    // -- Task 3.1: editor.setGizmo — set active node + gizmo mode ------------
    if (type == "editor.setGizmo") {
        uint32_t nodeId = j.value("nodeId", 0u);
        std::string mode = j.value("mode", "translate");
        g_scene.selectedNodeId.store(nodeId, std::memory_order_relaxed);
        int modeInt = 0;
        if      (mode == "rotate") modeInt = 1;
        else if (mode == "scale")  modeInt = 2;
        g_scene.gizmoMode.store(modeInt, std::memory_order_relaxed);

        // Task 7.2: accept optional pivot for multi-select centroid gizmo.
        if (j.contains("pivot") && j["pivot"].is_array() && j["pivot"].size() >= 3) {
            std::lock_guard<std::mutex> lk(g_scene.gizmoPivotMutex);
            g_scene.gizmoPivotX = j["pivot"][0].get<float>();
            g_scene.gizmoPivotY = j["pivot"][1].get<float>();
            g_scene.gizmoPivotZ = j["pivot"][2].get<float>();
            g_scene.useGizmoPivot.store(true, std::memory_order_relaxed);
        } else {
            g_scene.useGizmoPivot.store(false, std::memory_order_relaxed);
        }
        return;
    }

    // -- Task 3.2: editor.setGizmoHover — store pre-computed hovered axis ---
    if (type == "editor.setGizmoHover") {
        std::string axis = j.value("axis", "");
        int axisInt = -1;
        if      (axis == "x") axisInt = 0;
        else if (axis == "y") axisInt = 1;
        else if (axis == "z") axisInt = 2;
        g_scene.gizmoHoverAxis.store(axisInt, std::memory_order_relaxed);
        return;
    }

    // -- Task 3.3: editor.setGizmoSpace — local/world toggle ---------------
    if (type == "editor.setGizmoSpace") {
        std::string space = j.value("space", "world");
        g_scene.gizmoSpace.store(space == "local" ? 1 : 0, std::memory_order_relaxed);
        return;
    }

    // -- Task 3.4: editor.setNodeTransformLive — apply live transform -------
    if (type == "editor.setNodeTransformLive") {
        if (!g_renderer) return;
        uint32_t nodeId = j.value("nodeId", 0u);
        if (nodeId == 0) return;

        auto xform = j.value("transform", nlohmann::json::object());
        auto pos   = xform.value("position", nlohmann::json::object());
        auto rot   = xform.value("rotation", nlohmann::json::object());
        auto scl   = xform.value("scale",    nlohmann::json::object());

        const float px = pos.value("x", 0.0f);
        const float py = pos.value("y", 0.0f);
        const float pz = pos.value("z", 0.0f);
        const float rw = rot.value("w", 1.0f);
        const float rx = rot.value("x", 0.0f);
        const float ry = rot.value("y", 0.0f);
        const float rz = rot.value("z", 0.0f);
        const float sx = scl.value("x", 1.0f);
        const float sy = scl.value("y", 1.0f);
        const float sz = scl.value("z", 1.0f);

        // Update EditorNode (renderer visualization).
        // Store quaternion directly — no lossy euler conversion.
        auto eNodes = g_renderer->getEditorNodes();
        for (auto& n : eNodes) {
            if (n.nodeId == nodeId) {
                n.position = {px, py, pz};
                // XMMatrixRotationQuaternion expects XMFLOAT4 as (x,y,z,w)
                n.rotationQuat = {rx, ry, rz, rw};
                n.scale = {sx, sy, sz};
                break;
            }
        }
        g_renderer->loadEditorNodes(std::move(eNodes));

        // Update authoritative scene node.
        if (g_sceneData) {
            uz::SceneNode* node = uz::scene_get_node(g_sceneData, nodeId);
            if (node) {
                node->local_transform.position = {px, py, pz, 0.0f};
                node->local_transform.rotation = {rx, ry, rz, rw};  // uz Quat = (x,y,z,w)
                node->local_transform.scale    = {sx, sy, sz, 0.0f};
            }
        }
        return;
    }

    // -- Task 3.5: editor.commitTransform — finalize and return transform ---
    if (type == "editor.commitTransform") {
        uint32_t nodeId = j.value("nodeId", 0u);
        nlohmann::json resp;
        resp["type"]   = "commitTransformResult";
        resp["nodeId"] = nodeId;   // Task 7.1: multi-select transform tracking

        if (nodeId == 0 || !g_sceneData) {
            resp["transform"] = nullptr;
            wsServer.broadcastText(resp.dump());
            return;
        }

        const uz::SceneNode* node = uz::scene_get_node(g_sceneData, nodeId);
        if (!node) {
            resp["transform"] = nullptr;
            wsServer.broadcastText(resp.dump());
            return;
        }

        const auto& t = node->local_transform;
        resp["transform"] = {
            {"position", {{"x", t.position.x}, {"y", t.position.y}, {"z", t.position.z}}},
            {"rotation", {{"w", t.rotation.w}, {"x", t.rotation.x}, {"y", t.rotation.y}, {"z", t.rotation.z}}},
            {"scale",    {{"x", t.scale.x},    {"y", t.scale.y},    {"z", t.scale.z}}}
        };
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Task 3.6: editor.getCamera — return view/proj matrices + position --
    if (type == "editor.getCamera") {
        if (!g_renderer) return;

        const int vpW = g_renderer->getWidth();
        const int vpH = g_renderer->getHeight();
        const float camX  = g_scene.camPosX;
        const float camY  = g_scene.camPosY;
        const float camZ  = g_scene.camPosZ;

        // Build view and projection using the same helper as pickNode.
        const uz::Mat4 vpMat = buildCameraVP(
            g_scene.camYaw, g_scene.camPitch,
            camX, camY, camZ, vpW, vpH);

        // Send the combined VP as a flat 16-float array (row-major).
        std::vector<float> vpArr(16);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                vpArr[r * 4 + c] = vpMat.m[r][c];

        nlohmann::json resp;
        resp["type"]     = "cameraMatrices";
        resp["view"]     = vpArr;  // combined VP for now
        resp["proj"]     = vpArr;  // combined VP for now
        resp["position"] = {camX, camY, camZ};
        wsServer.broadcastText(resp.dump());
        return;
    }

    // -- Focus camera on node --------------------------------------------------
    if (type == "editor.focusNode") {
        uint32_t nodeId = j.value("nodeId", 0u);
        if (nodeId == 0) {
            nodeId = g_scene.selectedNodeId.load(std::memory_order_relaxed);
        }
        if (nodeId == 0 || !g_renderer) return;

        auto eNodes = g_renderer->getEditorNodes();
        uz::Float3 nodePos{0,0,0};
        bool found = false;
        for (const auto& n : eNodes) {
            if (n.nodeId == nodeId) {
                nodePos = n.position;
                found = true;
                break;
            }
        }
        if (!found) return;

        // Position camera at distance 8 from node, looking at it
        // Keep current yaw/pitch direction but reposition
        constexpr float kFocusDist = 8.0f;
        float yaw   = g_scene.camYaw;
        float pitch = g_scene.camPitch;

        // Camera offset = -forward * distance
        float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
        float cosYaw   = cosf(yaw),   sinYaw   = sinf(yaw);
        float fwdX = cosPitch * sinYaw;
        float fwdY = sinPitch;
        float fwdZ = cosPitch * cosYaw;

        g_scene.camPosX = nodePos.x - fwdX * kFocusDist;
        g_scene.camPosY = nodePos.y - fwdY * kFocusDist + 2.0f; // slight elevation
        g_scene.camPosZ = nodePos.z - fwdZ * kFocusDist;

        // Adjust pitch to look slightly down at the node
        float dx = nodePos.x - g_scene.camPosX;
        float dy = nodePos.y - g_scene.camPosY;
        float dz = nodePos.z - g_scene.camPosZ;
        float horizDist = sqrtf(dx*dx + dz*dz);
        g_scene.camPitch = atan2f(dy, horizDist);
        g_scene.camYaw   = atan2f(dx, dz);

        nlohmann::json resp;
        resp["type"] = "cameraFocused";
        resp["nodeId"] = nodeId;
        resp["position"] = {g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ};
        wsServer.broadcastText(resp.dump());

        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "[UzEngine] focusNode %u -> cam(%.1f,%.1f,%.1f)\n",
                 nodeId, g_scene.camPosX, g_scene.camPosY, g_scene.camPosZ);
        OutputDebugStringA(dbg);
        return;
    }

    if (type != "camera_input") {
        return;
    }

    auto& ci = g_scene.cameraInput;
    ci.moveForward.store(j.value("w",  0.0f), std::memory_order_relaxed);
    ci.moveBack.store(   j.value("s",  0.0f), std::memory_order_relaxed);
    ci.moveLeft.store(   j.value("a",  0.0f), std::memory_order_relaxed);
    ci.moveRight.store(  j.value("d",  0.0f), std::memory_order_relaxed);
    ci.moveUp.store(     j.value("e",  0.0f), std::memory_order_relaxed);
    ci.moveDown.store(   j.value("q",  0.0f), std::memory_order_relaxed);
    ci.rightMouseDown.store(j.value("rmb",   false), std::memory_order_relaxed);
    ci.shiftDown.store(     j.value("shift", false), std::memory_order_relaxed);

    // Mouse deltas and scroll are additive -- accumulate until the
    // render thread consumes them with exchange(0).
    const float dx     = j.value("dx",     0.0f);
    const float dy     = j.value("dy",     0.0f);
    const float scroll = j.value("scroll", 0.0f);

    if (dx != 0.0f) {
        float prev = ci.mouseDeltaX.load(std::memory_order_relaxed);
        while (!ci.mouseDeltaX.compare_exchange_weak(
            prev, prev + dx, std::memory_order_relaxed)) {}
    }
    if (dy != 0.0f) {
        float prev = ci.mouseDeltaY.load(std::memory_order_relaxed);
        while (!ci.mouseDeltaY.compare_exchange_weak(
            prev, prev + dy, std::memory_order_relaxed)) {}
    }
    if (scroll != 0.0f) {
        float prev = ci.scrollDelta.load(std::memory_order_relaxed);
        while (!ci.scrollDelta.compare_exchange_weak(
            prev, prev + scroll, std::memory_order_relaxed)) {}
    }
} catch (...) {
    // Malformed JSON -- silently ignore.
}
}
