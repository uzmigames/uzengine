// ---------------------------------------------------------------------------
// main.cpp -- Win32 entry point for UzEngine PoC.
//
// This file is a thin orchestrator: it initializes subsystems and delegates
// all logic to dedicated modules in src/editor/.
// ---------------------------------------------------------------------------

#include "editor/cmd_args.h"
#include "editor/win32_app.h"
#include "editor/stream.h"
#include "editor/ws_handlers.h"
#include "editor/scene_bridge.h"
#include "editor/ipc_handlers.h"
#include "editor/engine_loop.h"

#include "engine/renderer/renderer.h"
#include "engine/renderer/scene_params.h"
#include "network/ws_server.h"
#include "common/timer.h"
#include "common/types.h"
#include "common/ipc_registry.h"
#include "common/ipc_poller.h"
#include "engine/scene/scene.h"
#include "engine/core/diag.h"

#include <windows.h>
#include <timeapi.h>
#include <ixwebsocket/IXNetSystem.h>

#include <cstdio>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Global state shared across editor modules.
// ---------------------------------------------------------------------------
namespace {

uz::SceneParams   g_scene;
uz::EngineMetrics g_metrics;
std::unique_ptr<uz::Scene> g_sceneData;
uz::DX12Renderer* g_renderer = nullptr;

constexpr uint16_t kDefaultWsPort = 9002;

} // anonymous namespace

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_     LPSTR     lpCmdLine,
    _In_     int       nCmdShow)
{
    // -- stdio & args --------------------------------------------------------
    setupStdio();
    const auto cmdArgs = parseCmdLine(lpCmdLine);
    const uint16_t wsPort    = getCmdArgU16(cmdArgs, "--ws-port", kDefaultWsPort);
    const std::string ipcDir = getCmdArg(cmdArgs, "--ipc-dir", "");
    const std::string scene  = getCmdArg(cmdArgs, "--scene",   "");

    fprintf(stderr, "[UzEngine] wsPort=%u scene=\"%s\"\n",
            static_cast<unsigned>(wsPort), scene.c_str());
    fflush(stderr);

    // -- Timer resolution & networking ---------------------------------------
    timeBeginPeriod(1);
    DIAG("=== UzEngine main START ===");
    ix::initNetSystem();

    // -- Window --------------------------------------------------------------
    HWND hwnd = createEngineWindow(hInstance, nCmdShow);
    if (!hwnd) return 1;

    // -- WebSocket -----------------------------------------------------------
    uz::WsServer wsServer;
    wsServer.setLogCallback([](const std::string& msg) {
        OutputDebugStringA(("[UzEngine/WS] " + msg + "\n").c_str());
    });
    wsServer.setTextMessageCallback([&](const std::string& text) {
        handleWsTextMessage(text, g_scene, g_renderer, g_sceneData.get(), wsServer);
    });
    wsServer.start(wsPort);

    // -- Stream threads ------------------------------------------------------
    startEncodeThreads(&wsServer);

    // -- Renderer ------------------------------------------------------------
    uz::DX12Renderer renderer;
    uz::Timer timer;
    if (!renderer.init(hwnd, uz::kDefaultWidth, uz::kDefaultHeight)) {
        DIAG("FATAL: renderer.init() failed!");
        return 1;
    }
    renderer.setSceneParams(&g_scene);
    g_renderer = &renderer;

    // -- Scene ---------------------------------------------------------------
    loadSceneFile(g_sceneData, renderer, g_scene, scene);

    // -- IPC -----------------------------------------------------------------
    std::string ipcBaseDir = ipcDir;
    if (ipcBaseDir.empty()) {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        auto ls = exeDir.find_last_of("\\/");
        if (ls != std::string::npos) exeDir = exeDir.substr(0, ls);
        ipcBaseDir = exeDir + "/preview";
    }

    g_metrics.encoderName = "JPEG";
    g_metrics.resolution  = std::to_string(uz::kDefaultWidth) + "x"
                          + std::to_string(uz::kDefaultHeight);

    uz::IpcRegistry ipcRegistry;
    registerIpcHandlers(ipcRegistry, g_scene, g_metrics, renderer,
                        g_sceneData, wsServer, ipcBaseDir);

    uz::IpcPoller ipcPoller(ipcBaseDir, ipcRegistry, g_scene, g_metrics);
    ipcPoller.start();

    // -- Main loop -----------------------------------------------------------
    int exitCode = runMainLoop(hwnd, renderer, timer, g_scene, g_metrics, ipcPoller);

    // -- Cleanup -------------------------------------------------------------
    stopEncodeThreads();
    wsServer.stop();
    renderer.shutdown();
    ix::uninitNetSystem();
    timeEndPeriod(1);

    return exitCode;
}
