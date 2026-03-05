// ---------------------------------------------------------------------------
// ws_handlers.h -- WebSocket text message dispatch for editor IPC.
//
// Extracts the large message-handling lambda from main.cpp to meet CS-21
// (1500-line limit).  All editor IPC commands (pickNode, gizmoDrag, etc.)
// are dispatched here.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <memory>

// Forward declarations — avoid pulling in heavy headers.
namespace uz {
    struct SceneParams;
    class  Renderer;
    class  WsServer;
    struct Scene;
}

/// Process a single WebSocket text message from the VSCode extension.
/// Called from the wsServer text callback in main.cpp.
void handleWsTextMessage(const std::string& text,
                         uz::SceneParams& scene,
                         uz::Renderer* renderer,
                         uz::Scene* sceneData,
                         uz::WsServer& wsServer);
