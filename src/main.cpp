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

namespace {

constexpr const wchar_t* kWindowClassName = L"UzEngineWndClass";
constexpr const wchar_t* kWindowTitle     = L"UzEngine PoC";
constexpr uint16_t       kWsPort          = 9002;
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
    _In_     LPSTR     /*lpCmdLine*/,
    _In_     int       nCmdShow)
{
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

    if (!wsServer.start(kWsPort)) {
        OutputDebugStringA("[UzEngine] WARNING: WebSocket server failed to start\n");
        // Continue rendering even without streaming.
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

    // -- IPC setup -----------------------------------------------------------
    // Compute the IPC base directory (build/preview relative to the exe).
    std::string ipcBaseDir;
    {
        char exePath[MAX_PATH]{};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        // Strip the exe filename to get the directory.
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

        std::vector<uz::DX12Renderer::EditorNode> nodes;
        nodes.reserve(args["nodes"].size());

        for (const auto& n : args["nodes"]) {
            uz::DX12Renderer::EditorNode node;

            if (n.contains("position") && n["position"].is_array() && n["position"].size() >= 3) {
                node.position = {
                    n["position"][0].get<float>(),
                    n["position"][1].get<float>(),
                    n["position"][2].get<float>()
                };
            }
            if (n.contains("rotation") && n["rotation"].is_array() && n["rotation"].size() >= 3) {
                node.rotation = {
                    n["rotation"][0].get<float>(),
                    n["rotation"][1].get<float>(),
                    n["rotation"][2].get<float>()
                };
            }
            if (n.contains("scale") && n["scale"].is_array() && n["scale"].size() >= 3) {
                node.scale = {
                    n["scale"][0].get<float>(),
                    n["scale"][1].get<float>(),
                    n["scale"][2].get<float>()
                };
            }

            // Assign wireframe color based on node type.
            const std::string type = n.value("type", "Node3D");
            if (type == "StaticMesh") {
                node.color = {0.2f, 0.8f, 0.9f};    // cyan
            } else if (type == "Camera") {
                node.color = {0.9f, 0.9f, 0.2f};    // yellow
            } else if (type == "DirectionalLight" || type == "PointLight") {
                node.color = {0.9f, 0.7f, 0.2f};    // amber
            } else {
                node.color = {0.8f, 0.8f, 0.8f};    // default gray
            }

            nodes.push_back(node);
        }

        const size_t count = renderer.loadEditorScene(std::move(nodes));
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
