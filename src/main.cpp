// ---------------------------------------------------------------------------
// main.cpp -- Win32 entry point for UzEngine PoC.
//
// Architecture:
//   Main thread   : Win32 message loop + DX12 render + GPU readback
//   Encode thread : RGBA -> H.264 (NVENC or MF) -> fMP4 mux -> WS broadcast
//
// Frames travel render->encode via a lock-free ring buffer (drop-oldest).
// The WebSocket server runs IXWebSocket's internal threads.
// ---------------------------------------------------------------------------

#include "renderer/renderer.h"
#include "renderer/scene_params.h"
#include "encoder/jpeg_encoder.h"
#include "network/ws_server.h"
#include "common/timer.h"
#include "common/types.h"
#include "common/ring_buffer.h"
#include "common/ipc_registry.h"
#include "common/ipc_poller.h"

#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_saver.h"

#include <windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

#include <ixwebsocket/IXNetSystem.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <stb_image_write.h>
#include <io.h>
#include <fcntl.h>

namespace {

constexpr const wchar_t* kWindowClassName = L"UzEngineWndClass";
constexpr const wchar_t* kWindowTitle     = L"UzEngine PoC";
constexpr uint16_t       kDefaultWsPort   = 9002;

// ---------------------------------------------------------------------------
// Command-line argument helpers
// ---------------------------------------------------------------------------

/// Parses the raw lpCmdLine string into a vector of argument strings.
std::vector<std::string> parseCmdLine(const char* cmdLine) {
    std::vector<std::string> args;
    if (!cmdLine || !*cmdLine) return args;
    std::string current;
    bool inQuote = false;
    for (const char* p = cmdLine; *p; ++p) {
        if (*p == '"') {
            inQuote = !inQuote;
        } else if (*p == ' ' && !inQuote) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += *p;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

/// Returns the value of a named argument (e.g. --ws-port 9003), or defaultVal.
std::string getCmdArg(const std::vector<std::string>& args,
                      const std::string& name,
                      const std::string& defaultVal) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) return args[i + 1];
    }
    return defaultVal;
}

uint16_t getCmdArgU16(const std::vector<std::string>& args,
                     const std::string& name,
                     uint16_t defaultVal) {
    std::string val = getCmdArg(args, name, "");
    if (val.empty()) return defaultVal;
    try { return static_cast<uint16_t>(std::stoi(val)); }
    catch (...) { return defaultVal; }
}
constexpr std::size_t    kRingSize        = 8; // power-of-2, absorbs encode jitter

// Two SPSC ring buffers -- one per encoder thread.  The render thread dispatches
// frames in round-robin so both encoder threads work in parallel.
uz::RingBuffer<uz::FrameData, kRingSize> g_frameRingA;
uz::RingBuffer<uz::FrameData, kRingSize> g_frameRingB;

// Round-robin dispatch toggle (written only by the render thread, no sync needed).
bool g_dispatchToggle = false;

// Signal encode threads to stop.
std::atomic<bool> g_encodeRunning{false};

// IPC-controlled scene parameters and live engine metrics.
uz::SceneParams  g_scene;
uz::EngineMetrics g_metrics;

// Heap-allocated Scene for real scene data with ComponentStore.
// Scene is ~8 MB, too large for the stack.
std::unique_ptr<uz::Scene> g_sceneData;

// ---------------------------------------------------------------------------
// Encode thread body
// Pops RGBA frames from the given SPSC ring buffer, encodes each to JPEG via
// libjpeg-turbo, and broadcasts the raw JPEG blob over WebSocket.
//
// Two instances of this function run on separate threads (encodeThreadA and
// encodeThreadB), each draining its own ring buffer.  The render thread
// dispatches frames in round-robin so both encoders work in parallel,
// doubling encode throughput from ~78 fps to ~144 fps.
//
// Each WebSocket message is one self-contained, independently decodable JPEG.
// Frame ordering is not guaranteed between threads (fine for JPEG canvas mode:
// the webview always draws the most recently received frame).
// ---------------------------------------------------------------------------
void encodeThreadFn(uz::WsServer* server,
                    uz::RingBuffer<uz::FrameData, kRingSize>* ring,
                    const char* threadName)
{
    uz::JpegEncoder encoder;
    std::vector<uint8_t> jpegBlob;

    OutputDebugStringA(("[UzEngine] " + std::string(threadName) + " started (JPEG mode)\n").c_str());

    while (g_encodeRunning.load(std::memory_order_relaxed)) {
        auto frameOpt = ring->pop();
        if (!frameOpt) {
            // No frame in this ring yet — yield to avoid busy-waiting.
            std::this_thread::yield();
            continue;
        }

        const uz::FrameData& frame = *frameOpt;
        if (frame.pixels.empty()) {
            continue;
        }

        jpegBlob.clear();
        if (!encoder.encode(frame.pixels.data(),
                             static_cast<int>(frame.width),
                             static_cast<int>(frame.height),
                             85,
                             jpegBlob)) {
            OutputDebugStringA("[UzEngine] JPEG encode failed — dropping frame\n");
            continue;
        }

        // Prepend 4-byte little-endian viewport_id (1 = scene viewport)
        // so the extension can demux multiple viewports in the future.
        constexpr uint32_t kViewportId = 1;
        std::vector<uint8_t> framedData(4 + jpegBlob.size());
        std::memcpy(framedData.data(), &kViewportId, 4);
        std::memcpy(framedData.data() + 4, jpegBlob.data(), jpegBlob.size());
        server->broadcast(framedData);
    }

    OutputDebugStringA(("[UzEngine] " + std::string(threadName) + " stopped\n").c_str());
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

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
    // -- Redirect stdout/stderr to inherited pipe handles ---------------------
    // When spawned by Node.js cp.spawn(), the child process inherits pipe
    // handles for stdin/stdout/stderr.  GUI apps (WinMain) don't automatically
    // connect the C runtime FILE streams to these handles.  We re-open them so
    // fprintf(stderr, ...) and printf() work — the extension can then read the
    // engine's diagnostic output.
    {
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
        if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
            int fdOut = _open_osfhandle(reinterpret_cast<intptr_t>(hStdOut), 0);
            if (fdOut >= 0) {
                FILE* fOut = _fdopen(fdOut, "w");
                if (fOut) { *stdout = *fOut; setvbuf(stdout, nullptr, _IONBF, 0); }
            }
        }
        if (hStdErr && hStdErr != INVALID_HANDLE_VALUE) {
            int fdErr = _open_osfhandle(reinterpret_cast<intptr_t>(hStdErr), 0);
            if (fdErr >= 0) {
                FILE* fErr = _fdopen(fdErr, "w");
                if (fErr) { *stderr = *fErr; setvbuf(stderr, nullptr, _IONBF, 0); }
            }
        }
    }

    // -- Parse command-line arguments -----------------------------------------
    const auto cmdArgs = parseCmdLine(lpCmdLine);
    const uint16_t wsPort = getCmdArgU16(cmdArgs, "--ws-port", kDefaultWsPort);
    const std::string cmdIpcDir  = getCmdArg(cmdArgs, "--ipc-dir", "");
    const std::string cmdScene   = getCmdArg(cmdArgs, "--scene",   "");

    // -- Log parsed arguments for diagnostics ---------------------------------
    fprintf(stderr, "[UzEngine] lpCmdLine: \"%s\"\n", lpCmdLine ? lpCmdLine : "(null)");
    fprintf(stderr, "[UzEngine] Parsed %zu args:", cmdArgs.size());
    for (size_t i = 0; i < cmdArgs.size(); ++i) {
        fprintf(stderr, " [%zu]=\"%s\"", i, cmdArgs[i].c_str());
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "[UzEngine] wsPort=%u ipcDir=\"%s\" scene=\"%s\"\n",
            static_cast<unsigned>(wsPort), cmdIpcDir.c_str(), cmdScene.c_str());
    fflush(stderr);

    // -- Raise Windows timer resolution to 1 ms (default ~15 ms is too coarse
    //    for Sleep()-based frame limiting at 60 fps / 16.67 ms target).
    timeBeginPeriod(1);

    // -- Initialize Winsock (required by IXWebSocket on Windows) --------------
    ix::initNetSystem();

    // -- Register window class -----------------------------------------------

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    // -- Detect headless / windowed-debug mode --------------------------------
    // UZENGINE_HEADLESS=1  → hide window (normal VSCode extension mode)
    // UZENGINE_WINDOWED=1  → force visible window even when headless (debug)
    wchar_t headlessEnv[4]{};
    wchar_t windowedEnv[4]{};
    const bool headless  = GetEnvironmentVariableW(L"UZENGINE_HEADLESS", headlessEnv, 4) > 0;
    const bool forceWin  = GetEnvironmentVariableW(L"UZENGINE_WINDOWED", windowedEnv, 4) > 0;
    const bool showWindow = !headless || forceWin;

    // -- Create the window ---------------------------------------------------
    // In headless mode use WS_EX_TOOLWINDOW + WS_EX_NOACTIVATE so the window
    // stays off the taskbar/Alt+Tab. WS_POPUP avoids the title bar chrome.
    // A valid HWND is always required by CreateSwapChainForHwnd.
    DWORD exStyle = (!showWindow)
        ? (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
        : 0;
    DWORD style = (!showWindow)
        ? WS_POPUP
        : WS_OVERLAPPEDWINDOW;

    RECT windowRect = { 0, 0,
        static_cast<LONG>(uz::kDefaultWidth),
        static_cast<LONG>(uz::kDefaultHeight) };
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

    const int winX = showWindow ? CW_USEDEFAULT : -32000;
    const int winY = showWindow ? CW_USEDEFAULT : -32000;

    HWND hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        kWindowTitle,
        style,
        winX, winY,
        windowRect.right  - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return 1;
    }

    if (showWindow) {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }
    // In headless mode we intentionally never call ShowWindow — the HWND
    // exists solely for the DX12 swap chain.

    // -- WebSocket server ----------------------------------------------------

    uz::WsServer wsServer;
    wsServer.setLogCallback([](const std::string& msg) {
        OutputDebugStringA(("[UzEngine/WS] " + msg + "\n").c_str());
    });

    // -- Camera input via WebSocket text messages ------------------------------
    // The VSCode extension sends JSON text frames with camera input state.
    // Mouse deltas and scroll are additive (accumulated until consumed by the
    // render thread via atomic exchange).
    wsServer.setTextMessageCallback([&](const std::string& text) {
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
    });

    if (!wsServer.start(wsPort)) {
        char dbgWs[128];
        snprintf(dbgWs, sizeof(dbgWs),
                 "[UzEngine] WARNING: WebSocket server failed to start on port %u\n",
                 static_cast<unsigned>(wsPort));
        OutputDebugStringA(dbgWs);
        fprintf(stderr, "%s", dbgWs);
        fflush(stderr);
        // Continue rendering even without streaming.
    } else {
        char dbgWs[128];
        snprintf(dbgWs, sizeof(dbgWs),
                 "[UzEngine] WebSocket server started on port %u\n",
                 static_cast<unsigned>(wsPort));
        OutputDebugStringA(dbgWs);
        fprintf(stderr, "%s", dbgWs);
        fflush(stderr);
    }

    // -- Encode thread -------------------------------------------------------

    g_encodeRunning.store(true, std::memory_order_relaxed);
    std::thread encodeThreadA(encodeThreadFn, &wsServer, &g_frameRingA, "EncodeThreadA");
    std::thread encodeThreadB(encodeThreadFn, &wsServer, &g_frameRingB, "EncodeThreadB");

    // -- DX12 Renderer -------------------------------------------------------

    uz::DX12Renderer renderer;
    uz::Timer timer;

    renderer.init(hwnd, uz::kDefaultWidth, uz::kDefaultHeight);
    renderer.setSceneParams(&g_scene);

    // -- Auto-load the default scene from disk --------------------------------
    // Resolve the scene path relative to the executable directory.
    {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        auto lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        // Use --scene arg if provided, otherwise default.
        const std::string scenePath = cmdScene.empty()
            ? (exeDir + "/../../assets/scenes/default.uzscene")
            : cmdScene;

        g_sceneData = std::make_unique<uz::Scene>();
        uz::scene_init(g_sceneData.get(), "DefaultScene");

        auto loadResult = uz::scene_load_file(g_sceneData.get(), scenePath.c_str());
        if (loadResult.is_ok()) {
            // Update world transforms so light/camera components can read them.
            uz::scene_update_world_transforms(g_sceneData.get());

            // Convert Scene nodes into EditorNodes for the editor rendering.
            std::vector<uz::DX12Renderer::EditorNode> editorNodes;
            for (uint32_t i = 1; i < g_sceneData->next_id; ++i) {
                const uz::SceneNode& sn = g_sceneData->nodes[i];
                if (!sn.alive) continue;

                uz::DX12Renderer::EditorNode edNode;
                edNode.position = {
                    sn.local_transform.position.x,
                    sn.local_transform.position.y,
                    sn.local_transform.position.z
                };
                edNode.rotation = {0.0f, 0.0f, 0.0f}; // Euler approximation not needed for gizmos
                edNode.scale = {
                    sn.local_transform.scale.x,
                    sn.local_transform.scale.y,
                    sn.local_transform.scale.z
                };
                edNode.nodeType = static_cast<uint16_t>(sn.type);

                // Assign gizmo colors by node type.
                switch (sn.type) {
                    case uz::NodeType::Camera:
                        edNode.color = {0.9f, 0.9f, 0.2f}; break;
                    case uz::NodeType::DirectionalLight:
                    case uz::NodeType::PointLight:
                        edNode.color = {0.9f, 0.7f, 0.2f}; break;
                    case uz::NodeType::StaticMesh:
                        edNode.color = {0.2f, 0.8f, 0.9f}; break;
                    default:
                        edNode.color = {0.8f, 0.8f, 0.8f}; break;
                }
                editorNodes.push_back(edNode);
            }

            renderer.setScene(g_sceneData.get());  // scene integration ENABLED
            renderer.loadEditorScene(std::move(editorNodes));

            // Restore editor camera from the [editor] section (if present).
            if (g_sceneData->editor_state.valid) {
                g_scene.camPosX  = g_sceneData->editor_state.cam_pos_x;
                g_scene.camPosY  = g_sceneData->editor_state.cam_pos_y;
                g_scene.camPosZ  = g_sceneData->editor_state.cam_pos_z;
                g_scene.camYaw   = g_sceneData->editor_state.cam_yaw;
                g_scene.camPitch = g_sceneData->editor_state.cam_pitch;
                g_scene.camSpeed = g_sceneData->editor_state.cam_speed;
            }

            char dbg[256];
            snprintf(dbg, sizeof(dbg),
                     "[UzEngine] Auto-loaded default scene: %s (%u nodes)\n",
                     scenePath.c_str(),
                     static_cast<unsigned>(g_sceneData->next_id - 1));
            OutputDebugStringA(dbg);
        } else {
            char dbg[256];
            snprintf(dbg, sizeof(dbg),
                     "[UzEngine] WARNING: Could not load default scene: %s\n",
                     scenePath.c_str());
            OutputDebugStringA(dbg);
        }
    }

    // -- IPC setup -----------------------------------------------------------
    // Use --ipc-dir arg if provided, otherwise compute from exe directory.
    std::string ipcBaseDir;
    if (!cmdIpcDir.empty()) {
        ipcBaseDir = cmdIpcDir;
    } else {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir(exePath);
        auto lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        ipcBaseDir = exeDir + "/preview";
    }

    g_metrics.encoderName = "JPEG";
    g_metrics.resolution  = std::to_string(uz::kDefaultWidth) + "x"
                          + std::to_string(uz::kDefaultHeight);

    uz::IpcRegistry ipcRegistry;

    // -- Register IPC handlers -----------------------------------------------

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

        // Readback pixels are RGBA from the R8G8B8A8_UNORM render target, so
        // they are already in the correct channel order for stbi_write_png.
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

        // ------------------------------------------------------------------
        // Auto-save the current scene before loading the new one.
        // ------------------------------------------------------------------
        if (g_sceneData && g_sceneData->source_path[0] != '\0') {
            // Snapshot editor camera into scene before saving.
            g_sceneData->editor_state.cam_pos_x = g_scene.camPosX;
            g_sceneData->editor_state.cam_pos_y = g_scene.camPosY;
            g_sceneData->editor_state.cam_pos_z = g_scene.camPosZ;
            g_sceneData->editor_state.cam_yaw   = g_scene.camYaw;
            g_sceneData->editor_state.cam_pitch = g_scene.camPitch;
            g_sceneData->editor_state.cam_speed = g_scene.camSpeed;
            g_sceneData->editor_state.valid     = true;
            (void)uz::scene_save_file(g_sceneData.get(), g_sceneData->source_path);
        }

        // ------------------------------------------------------------------
        // Create / reset the real Scene with ComponentStore.
        // ------------------------------------------------------------------
        if (!g_sceneData) {
            g_sceneData = std::make_unique<uz::Scene>();
            uz::scene_init(g_sceneData.get(), "IPCScene");
        } else {
            uz::scene_clear(g_sceneData.get());
        }

        std::vector<uz::DX12Renderer::EditorNode> editorNodes;
        editorNodes.reserve(args["nodes"].size());

        for (const auto& n : args["nodes"]) {
            // Determine NodeType.
            const std::string typeStr = n.value("type", "Node3D");
            uz::NodeType nodeType = uz::node_type_from_string(typeStr.c_str());
            const std::string nodeName = n.value("name", "Unnamed");

            // Create the SceneNode.
            uz::NodeId id = uz::scene_create_node(
                g_sceneData.get(), nodeName.c_str(), nodeType);

            if (id == uz::kNodeIdInvalid) continue;

            uz::SceneNode* node = uz::scene_get_node(g_sceneData.get(), id);
            if (!node) continue;

            // Parse transform.
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
                // Store as euler angles in the quaternion fields for now.
                // Build a rotation quaternion from euler angles (Y * X * Z).
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

            // -- Add component data to ComponentStore. ---------------------
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

            // -- Build EditorNode for wireframe rendering. ------------------
            uz::DX12Renderer::EditorNode edNode;
            edNode.position = {px, py, pz};
            edNode.rotation = {rx, ry, rz};
            edNode.scale    = {sx, sy, sz};
            edNode.nodeType = static_cast<uint16_t>(nodeType);

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

        // Update world transforms so light/camera components can read them.
        uz::scene_update_world_transforms(g_sceneData.get());

        // Pass the real Scene to the renderer (for camera + lighting).
        renderer.setScene(g_sceneData.get());  // scene integration ENABLED

        // Load editor wireframe nodes.
        const size_t count = renderer.loadEditorScene(std::move(editorNodes));
        return {{"ok", true}, {"count", static_cast<int>(count)}};
    });

    ipcRegistry.add("scene.setParam", [&](const nlohmann::json& args) -> nlohmann::json {
        const std::string key = args.value("key", "");
        if (key.starts_with("rotation.")) {
            const std::string axis = key.substr(9); // after "rotation."
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
        // Sandbox: reject path traversal attempts.
        if (relPath.find("..") != std::string::npos) {
            return {{"ok", false}, {"error", "path traversal not allowed"}};
        }
        // Resolve relative to the build directory (parent of preview/).
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

        // Also clear any pending camera input to prevent a stale movement
        // impulse from being applied on the next frame.
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

    uz::IpcPoller ipcPoller(ipcBaseDir, ipcRegistry, g_scene, g_metrics);
    ipcPoller.start();

    // -- FPS tracking --------------------------------------------------------
    LARGE_INTEGER fpsQpcFreq;
    QueryPerformanceFrequency(&fpsQpcFreq);
    LARGE_INTEGER fpsLastTime;
    QueryPerformanceCounter(&fpsLastTime);
    uint32_t fpsFrameCounter = 0;

    // -- Main loop -----------------------------------------------------------
    // Target 30 fps for the encode/stream pipeline. The webview MSE player
    // is capped at ~30 fps anyway; rendering faster wastes CPU and adds latency
    // by producing frames faster than they can be consumed and displayed.
    constexpr double kTargetFrameTime = 1.0 / static_cast<double>(uz::kTargetFPS);
    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    LARGE_INTEGER qpcLast;
    QueryPerformanceCounter(&qpcLast);

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            // Frame-rate limiter: spin-wait until the target frame time has elapsed.
            LARGE_INTEGER qpcNow;
            QueryPerformanceCounter(&qpcNow);
            double elapsed = static_cast<double>(qpcNow.QuadPart - qpcLast.QuadPart)
                           / static_cast<double>(qpcFreq.QuadPart);
            if (elapsed < kTargetFrameTime) {
                // Sleep most of the remaining time, then spin for precision.
                double sleepMs = (kTargetFrameTime - elapsed) * 1000.0 - 1.0;
                if (sleepMs > 1.0) {
                    ::Sleep(static_cast<DWORD>(sleepMs));
                }
                // Spin the rest.
                do {
                    QueryPerformanceCounter(&qpcNow);
                    elapsed = static_cast<double>(qpcNow.QuadPart - qpcLast.QuadPart)
                            / static_cast<double>(qpcFreq.QuadPart);
                } while (elapsed < kTargetFrameTime);
            }
            qpcLast = qpcNow;

            const double delta = timer.tick();
            renderer.update(timer.elapsed(), delta);

            // Only perform the expensive GPU readback (GPU copy + fence wait +
            // 3 MB memcpy) when at least one encoder has room for a new frame.
            // On skipped frames the render still runs at full speed for smooth
            // camera movement — we just don't copy pixels to CPU.
            const bool needReadback = g_frameRingA.size() < 2 || g_frameRingB.size() < 2;
            renderer.render(needReadback);

            // Update the resolution metric if it changed (e.g. after a
            // viewport resize handled inside renderer.update()).
            {
                const std::string currentRes =
                    std::to_string(renderer.getWidth()) + "x"
                    + std::to_string(renderer.getHeight());
                if (currentRes != g_metrics.resolution) {
                    g_metrics.resolution = currentRes;
                }
            }

            // -- Update IPC metrics -------------------------------------------
            ++fpsFrameCounter;
            g_metrics.frameCount.fetch_add(1, std::memory_order_relaxed);

            {
                LARGE_INTEGER fpsNow;
                QueryPerformanceCounter(&fpsNow);
                const double fpsElapsed =
                    static_cast<double>(fpsNow.QuadPart - fpsLastTime.QuadPart)
                    / static_cast<double>(fpsQpcFreq.QuadPart);
                if (fpsElapsed >= 1.0) {
                    g_metrics.fps.store(
                        static_cast<float>(fpsFrameCounter) / static_cast<float>(fpsElapsed),
                        std::memory_order_relaxed);
                    fpsFrameCounter = 0;
                    fpsLastTime = fpsNow;
                }
            }

            // Dispatch the captured frame to one of the two encoder threads in
            // round-robin.  Only push to a ring that has room (size < 2) to
            // avoid stacking frames on a backed-up encoder; if the preferred
            // ring is full, try the other, then drop.
            if (needReadback) {
                uz::FrameData frame = renderer.getLatestFrame();
                if (!frame.pixels.empty()) {
                    auto* preferred = g_dispatchToggle ? &g_frameRingA : &g_frameRingB;
                    auto* fallback  = g_dispatchToggle ? &g_frameRingB : &g_frameRingA;
                    g_dispatchToggle = !g_dispatchToggle;

                    if (preferred->size() < 2) {
                        preferred->push(std::move(frame));
                    } else if (fallback->size() < 2) {
                        fallback->push(std::move(frame));
                    }
                    // else both rings are full — drop this frame (encode threads
                    // are temporarily behind, drop-oldest ensures freshness).
                }
            }
        }
    }

    // -- Cleanup -------------------------------------------------------------

    ipcPoller.stop();

    g_encodeRunning.store(false, std::memory_order_relaxed);
    if (encodeThreadA.joinable()) { encodeThreadA.join(); }
    if (encodeThreadB.joinable()) { encodeThreadB.join(); }

    wsServer.stop();
    renderer.shutdown();

    ix::uninitNetSystem();

    // Restore default timer resolution.
    timeEndPeriod(1);

    return static_cast<int>(msg.wParam);
}
