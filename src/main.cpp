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

// Ring buffer shared between render thread (producer) and encode thread (consumer).
uz::RingBuffer<uz::FrameData, kRingSize> g_frameRing;

// Signal encode thread to stop.
std::atomic<bool> g_encodeRunning{false};

// IPC-controlled scene parameters and live engine metrics.
uz::SceneParams  g_scene;
uz::EngineMetrics g_metrics;

// ---------------------------------------------------------------------------
// Encode thread body
// Pops RGBA frames from the ring buffer, encodes each to JPEG via
// libjpeg-turbo, and broadcasts the raw JPEG blob over WebSocket.
// Each WebSocket message is one self-contained, independently decodable JPEG.
// ---------------------------------------------------------------------------
void encodeThreadFn(uz::WsServer* server) {
    uz::JpegEncoder encoder;
    std::vector<uint8_t> jpegBlob;

    OutputDebugStringA("[UzEngine] Encode thread started (JPEG mode)\n");

    while (g_encodeRunning.load(std::memory_order_relaxed)) {
        auto frameOpt = g_frameRing.pop();
        if (!frameOpt) {
            // No frame available yet — sleep briefly to avoid busy-waiting.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

        server->broadcast(jpegBlob);
    }

    OutputDebugStringA("[UzEngine] Encode thread stopped\n");
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

    if (!wsServer.start(kWsPort)) {
        OutputDebugStringA("[UzEngine] WARNING: WebSocket server failed to start\n");
        // Continue rendering even without streaming.
    }

    // -- Encode thread -------------------------------------------------------

    g_encodeRunning.store(true, std::memory_order_relaxed);
    std::thread encodeThread(encodeThreadFn, &wsServer);

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
            renderer.render();

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

            // Push the latest captured frame into the ring buffer.
            uz::FrameData frame = renderer.getLatestFrame();
            if (!frame.pixels.empty()) {
                g_frameRing.push(std::move(frame));
            }
        }
    }

    // -- Cleanup -------------------------------------------------------------

    ipcPoller.stop();

    g_encodeRunning.store(false, std::memory_order_relaxed);
    if (encodeThread.joinable()) {
        encodeThread.join();
    }

    wsServer.stop();
    renderer.shutdown();

    ix::uninitNetSystem();

    // Restore default timer resolution.
    timeEndPeriod(1);

    return static_cast<int>(msg.wParam);
}
