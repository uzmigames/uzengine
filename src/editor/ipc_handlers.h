// ---------------------------------------------------------------------------
// ipc_handlers.h -- IPC handler registration for the editor.
// ---------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>

namespace uz {
    class IpcRegistry;
    class WsServer;
    class Renderer;
    struct SceneParams;
    struct EngineMetrics;
    struct Scene;
}

/// Register all IPC command handlers with the registry.
void registerIpcHandlers(uz::IpcRegistry& registry,
                         uz::SceneParams& sceneParams,
                         uz::EngineMetrics& metrics,
                         uz::Renderer& renderer,
                         std::unique_ptr<uz::Scene>& sceneData,
                         uz::WsServer& wsServer,
                         const std::string& ipcBaseDir);
