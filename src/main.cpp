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

#include "engine/renderer/core/renderer.h"
#include "engine/renderer/scene/scene_params.h"
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

#include "engine/core/diag.h"

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

// ---- Projection math helpers (uz:: engine math, left-handed DX convention) ----

uz::Mat4 buildLookAtLH(uz::Vec3 eye, uz::Vec3 target, uz::Vec3 up) {
    using namespace uz;
    const Vec3 diff = target - eye;
    const float diffLen = vec3_len(diff);
    if (diffLen < kEpsilon) return Mat4::identity();
    const Vec3 f = diff * (1.0f / diffLen);
    Vec3 crossUpF = vec3_cross(up, f);
    float crossLen = vec3_len(crossUpF);
    if (crossLen < kEpsilon) {
        // up parallel to f — use alternative up
        Vec3 altUp = (std::abs(f.y) > 0.99f) ? Vec3{1,0,0,0} : Vec3{0,1,0,0};
        crossUpF = vec3_cross(altUp, f);
        crossLen = vec3_len(crossUpF);
        if (crossLen < kEpsilon) return Mat4::identity();
    }
    const Vec3 r = crossUpF * (1.0f / crossLen);
    const Vec3 u = vec3_cross(f, r);
    Mat4 m = {};
    m.m[0][0]=r.x;  m.m[0][1]=u.x;  m.m[0][2]=f.x;  m.m[0][3]=0;
    m.m[1][0]=r.y;  m.m[1][1]=u.y;  m.m[1][2]=f.y;  m.m[1][3]=0;
    m.m[2][0]=r.z;  m.m[2][1]=u.z;  m.m[2][2]=f.z;  m.m[2][3]=0;
    m.m[3][0]=-vec3_dot(r,eye); m.m[3][1]=-vec3_dot(u,eye);
    m.m[3][2]=-vec3_dot(f,eye); m.m[3][3]=1;
    return m;
}

uz::Mat4 buildPerspLH(float fovY, float aspect, float nearZ, float farZ) {
    float h = 1.0f / std::tan(fovY * 0.5f);
    float w = h / aspect;
    float q = farZ / (farZ - nearZ);
    uz::Mat4 m = {};
    m.m[0][0]=w; m.m[1][1]=h;
    m.m[2][2]=q; m.m[2][3]=1;
    m.m[3][2]=-nearZ*q;
    return m;
}

/// Project a world position through a VP matrix. Returns false if behind camera.
bool projectWorldToScreen(const uz::Mat4& vp, float wx, float wy, float wz,
                          int vpW, int vpH, float& outSx, float& outSy) {
    // Mat4 * (wx,wy,wz,1) — manual 4-component transform
    float cx = vp.m[0][0]*wx + vp.m[1][0]*wy + vp.m[2][0]*wz + vp.m[3][0];
    float cy = vp.m[0][1]*wx + vp.m[1][1]*wy + vp.m[2][1]*wz + vp.m[3][1];
    float cz = vp.m[0][2]*wx + vp.m[1][2]*wy + vp.m[2][2]*wz + vp.m[3][2];  // unused
    float cw = vp.m[0][3]*wx + vp.m[1][3]*wy + vp.m[2][3]*wz + vp.m[3][3];
    (void)cz;
    if (cw <= 0.001f) return false;
    float ndcX = cx / cw;
    float ndcY = cy / cw;
    outSx = (ndcX * 0.5f + 0.5f) * vpW;
    outSy = (1.0f - (ndcY * 0.5f + 0.5f)) * vpH;
    return true;
}

/// Build a VP matrix from camera yaw/pitch/position (matching camera_component fly-cam).
uz::Mat4 buildCameraVP(float yaw, float pitch,
                       float camX, float camY, float camZ,
                       int vpW, int vpH) {
    float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
    float cosYaw   = cosf(yaw),   sinYaw   = cosf(yaw); // intentional: match camera_component
    // Correct: sinYaw for the forward direction
    uz::Vec3 camPos = {camX, camY, camZ, 0.0f};
    uz::Vec3 forward = {
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
        0.0f
    };
    uz::Vec3 target = camPos + forward;
    uz::Vec3 up = {0.0f, 1.0f, 0.0f, 0.0f};

    float aspect = (vpW > 0 && vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
    uz::Mat4 view = buildLookAtLH(camPos, target, up);
    uz::Mat4 proj = buildPerspLH(uz::kHalfPi * 0.5f, aspect, 0.1f, 500.0f);
    return proj * view;  // reversed: row-major data in col-major multiply (matches camera_component)
}

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

// Pointer to the renderer — set after init so the WS text callback can access it.
uz::Renderer* g_renderer = nullptr;

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
//
// Optimisations (2026-03-04):
//   - JPEG quality lowered to 65 (from 85) — ~45% smaller frames, faster encode
//   - Frame pacing via atomic flag — drops frames when WS broadcast can't keep up
//   - Viewport prefix written in-place — eliminates extra allocation + memcpy
// ---------------------------------------------------------------------------

// Per-thread flag: true while the thread is inside WsServer::broadcast().
// If a new encoded frame is ready while the previous broadcast hasn't finished
// on the OTHER thread, both threads would contend on the broadcast mutex.
// Instead, each thread skips the WS send if it's still broadcasting -- the
// client always overwrites the canvas with the newest frame anyway.
std::atomic<bool> g_broadcastBusyA{false};
std::atomic<bool> g_broadcastBusyB{false};

void encodeThreadFn(uz::WsServer* server,
                    uz::RingBuffer<uz::FrameData, kRingSize>* ring,
                    const char* threadName,
                    std::atomic<bool>* broadcastBusy)
{
    uz::JpegEncoder encoder;
    std::vector<uint8_t> jpegBlob;

    // Pre-allocate to reduce reallocs for typical frame sizes.
    jpegBlob.reserve(128 * 1024);

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

        // -- Reserve 4 bytes for the viewport_id prefix, encode JPEG after it --
        // This avoids allocating a second buffer + memcpy of the entire JPEG blob.
        jpegBlob.clear();
        constexpr uint32_t kViewportId = 1;
        jpegBlob.resize(4);  // placeholder for the prefix
        std::memcpy(jpegBlob.data(), &kViewportId, 4);

        // Encode JPEG into a temporary buffer, then append to jpegBlob.
        std::vector<uint8_t> encodedJpeg;
        if (!encoder.encode(frame.pixels.data(),
                             static_cast<int>(frame.width),
                             static_cast<int>(frame.height),
                             85,   // quality restored to 85 (65 caused ghosting)
                             encodedJpeg)) {
            OutputDebugStringA("[UzEngine] JPEG encode failed — dropping frame\n");
            continue;
        }

        jpegBlob.insert(jpegBlob.end(), encodedJpeg.begin(), encodedJpeg.end());

        // -- Frame pacing: skip WS broadcast if this thread is still sending --
        // This prevents the encode thread from blocking on the WS mutex when
        // the WebSocket can't keep up with the encode rate.
        bool expected = false;
        if (!broadcastBusy->compare_exchange_strong(expected, true,
                                                     std::memory_order_acquire)) {
            // Previous broadcast hasn't finished — drop this frame.
            continue;
        }

        server->broadcast(jpegBlob);
        broadcastBusy->store(false, std::memory_order_release);
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

    DIAG("=== UzEngine main START ===");
    DIAG_FMT("wsPort=%u scene=%s", (unsigned)wsPort, cmdScene.c_str());

    DIAG("Initializing Winsock...");
    ix::initNetSystem();
    DIAG("Winsock OK");

    // -- Register window class -----------------------------------------------

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    DIAG("Registering window class...");
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

    DIAG("Creating window...");
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

    DIAG("Window created OK");
    // -- WebSocket server ----------------------------------------------------
    DIAG("Creating WS server...");
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
                    uz::SceneNode* node = uz::scene_get_node(g_sceneData.get(), nodeId);
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

                const uz::SceneNode* node = uz::scene_get_node(g_sceneData.get(), nodeId);
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

                // Build separate view and projection matrices.
                uz::Vec3 camPos = {camX, camY, camZ, 0.0f};
                uz::Vec3 forward = {
                    cosf(g_scene.camPitch) * sinf(g_scene.camYaw),
                    sinf(g_scene.camPitch),
                    cosf(g_scene.camPitch) * cosf(g_scene.camYaw),
                    0.0f
                };
                uz::Vec3 target = camPos + forward;
                uz::Vec3 up = {0.0f, 1.0f, 0.0f, 0.0f};

                float aspect = (vpW > 0 && vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
                uz::Mat4 viewMat = buildLookAtLH(camPos, target, up);
                uz::Mat4 projMat = buildPerspLH(uz::kHalfPi * 0.5f, aspect, 0.1f, 500.0f);

                // Serialize as flat 16-float arrays (row-major layout: m[row][col]).
                // Mat4 memory layout matches DX row-major convention.
                auto mat4ToArr = [](const uz::Mat4& m) {
                    std::vector<float> arr(16);
                    std::memcpy(arr.data(), &m, 16 * sizeof(float));
                    return arr;
                };

                nlohmann::json resp;
                resp["type"]     = "cameraMatrices";
                resp["view"]     = mat4ToArr(viewMat);
                resp["proj"]     = mat4ToArr(projMat);
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
    std::thread encodeThreadA(encodeThreadFn, &wsServer, &g_frameRingA, "EncodeThreadA", &g_broadcastBusyA);
    std::thread encodeThreadB(encodeThreadFn, &wsServer, &g_frameRingB, "EncodeThreadB", &g_broadcastBusyB);

    // -- DX12 Renderer -------------------------------------------------------

    DIAG("Creating renderer...");
    uz::Renderer renderer;
    uz::Timer timer;

    DIAG("Calling renderer.init()...");
    if (!renderer.init(hwnd, uz::kDefaultWidth, uz::kDefaultHeight)) {
        DIAG("FATAL: renderer.init() returned false!");
        return 1;
    }
    DIAG("renderer.init() done OK");
    renderer.setSceneParams(&g_scene);
    g_renderer = &renderer;  // allow WS handler to access renderer

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

        DIAG_FMT("Loading scene: %s", scenePath.c_str());
        g_sceneData = std::make_unique<uz::Scene>();
        uz::scene_init(g_sceneData.get(), "DefaultScene");

        auto loadResult = uz::scene_load_file(g_sceneData.get(), scenePath.c_str());
        if (loadResult.is_ok()) {
            DIAG("Scene loaded OK, building editor nodes...");
            // Update world transforms so light/camera components can read them.
            uz::scene_update_world_transforms(g_sceneData.get());

            // Convert Scene nodes into EditorNodes for the editor rendering.
            std::vector<uz::Renderer::EditorNode> editorNodes;
            for (uint32_t i = 1; i < g_sceneData->next_id; ++i) {
                const uz::SceneNode& sn = g_sceneData->nodes[i];
                if (!sn.alive) continue;

                uz::Renderer::EditorNode edNode;
                edNode.position = {
                    sn.local_transform.position.x,
                    sn.local_transform.position.y,
                    sn.local_transform.position.z
                };
                edNode.rotation = {0.0f, 0.0f, 0.0f}; // legacy euler (unused)
                // Store quaternion from scene node (x,y,z,w)
                edNode.rotationQuat = {
                    sn.local_transform.rotation.x,
                    sn.local_transform.rotation.y,
                    sn.local_transform.rotation.z,
                    sn.local_transform.rotation.w
                };
                edNode.scale = {
                    sn.local_transform.scale.x,
                    sn.local_transform.scale.y,
                    sn.local_transform.scale.z
                };
                edNode.nodeType = 0; // legacy field
                edNode.nodeId   = i;

                // Assign gizmo colors by node type string.
                const char* tn = sn.type_name;
                if (std::strcmp(tn, "Camera") == 0) {
                    edNode.color = {0.9f, 0.9f, 0.2f};
                } else if (std::strcmp(tn, "DirectionalLight") == 0 ||
                           std::strcmp(tn, "PointLight") == 0) {
                    edNode.color = {0.9f, 0.7f, 0.2f};
                } else if (std::strcmp(tn, "StaticMesh") == 0) {
                    edNode.color = {0.2f, 0.8f, 0.9f};
                } else {
                    edNode.color = {0.8f, 0.8f, 0.8f};
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

        std::vector<uz::Renderer::EditorNode> editorNodes;
        editorNodes.reserve(args["nodes"].size());

        for (const auto& n : args["nodes"]) {
            // Determine node type (string-based, no enum).
            const std::string typeStr = n.value("type", "Node3D");
            const std::string nodeName = n.value("name", "Unnamed");

            // Create the SceneNode.
            uz::NodeId id = uz::scene_create_node(
                g_sceneData.get(), nodeName.c_str(), typeStr.c_str());

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
            if (typeStr == "Camera") {
                uz::CameraData* cd = uz::component_add<uz::CameraData>(
                    &g_sceneData->components, id);
                if (cd) {
                    cd->fov_y      = n.value("fov",  45.0f);
                    cd->near_plane = n.value("near", 0.1f);
                    cd->far_plane  = n.value("far",  500.0f);
                }
            }
            if (typeStr == "DirectionalLight") {
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
            uz::Renderer::EditorNode edNode;
            edNode.position = {px, py, pz};
            edNode.rotation = {rx, ry, rz}; // legacy euler
            // Store quaternion from the scene node (already converted from euler above)
            edNode.rotationQuat = {
                node->local_transform.rotation.x,
                node->local_transform.rotation.y,
                node->local_transform.rotation.z,
                node->local_transform.rotation.w
            };
            edNode.scale    = {sx, sy, sz};
            edNode.nodeType = 0; // legacy field
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
