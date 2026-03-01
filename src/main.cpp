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
#include "encoder/encoder_factory.h"
#include "muxer/fmp4_muxer.h"
#include "network/ws_server.h"
#include "common/timer.h"
#include "common/types.h"
#include "common/ring_buffer.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

constexpr const wchar_t* kWindowClassName = L"UzEngineWndClass";
constexpr const wchar_t* kWindowTitle     = L"UzEngine PoC";
constexpr uint16_t       kWsPort          = 9002;
constexpr std::size_t    kRingSize        = 8; // power-of-2, hold ~8 frames

// Ring buffer shared between render thread (producer) and encode thread (consumer).
uz::RingBuffer<uz::FrameData, kRingSize> g_frameRing;

// Signal encode thread to stop.
std::atomic<bool> g_encodeRunning{false};

// ---------------------------------------------------------------------------
// Encode thread body
// ---------------------------------------------------------------------------
void encodeThreadFn(uz::WsServer* server) {
    // -- Create encoder ------------------------------------------------------
    uz::EncoderConfig encCfg;
    encCfg.width      = uz::kDefaultWidth;
    encCfg.height     = uz::kDefaultHeight;
    encCfg.fps        = uz::kTargetFPS;
    encCfg.bitrate    = 4'000'000;
    encCfg.preferNVENC = true;

    auto encoder = uz::createEncoder(encCfg);
    if (!encoder) {
        OutputDebugStringA("[UzEngine] FATAL: no encoder available\n");
        return;
    }

    // -- Muxer state (initialised on first IDR frame) -----------------------
    uz::FMP4Muxer muxer;
    bool muxerReady = false;
    uint32_t frameCount = 0;

    OutputDebugStringA("[UzEngine] Encode thread started\n");

    while (g_encodeRunning.load(std::memory_order_relaxed)) {
        auto frameOpt = g_frameRing.pop();
        if (!frameOpt) {
            // No frame available yet -- yield and retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uz::FrameData& frame = *frameOpt;
        if (frame.pixels.empty()) {
            continue;
        }

        // -- Encode frame ----------------------------------------------------
        std::vector<uint8_t> nalus;
        if (!encoder->encode(frame.pixels.data(),
                              frame.width, frame.height,
                              nalus)) {
            continue;
        }

        if (nalus.empty()) {
            continue;
        }

        // -- Initialize muxer on first IDR -----------------------------------
        if (!muxerReady) {
            const auto& cc = encoder->getCodecConfig();
            if (!cc.sps.empty() && !cc.pps.empty()) {
                if (muxer.initialize(cc.profileIdc, cc.levelIdc,
                                     cc.sps, cc.pps,
                                     frame.width, frame.height,
                                     90000,    // timescale
                                     3000)) {  // 90000/30fps = 3000 ticks
                    server->setInitSegment(muxer.getInitSegment());
                    muxerReady = true;
                    OutputDebugStringA("[UzEngine] fMP4 muxer initialized\n");
                }
            }
        }

        if (!muxerReady) {
            continue; // wait until we have SPS/PPS
        }

        // -- Mux + broadcast -------------------------------------------------
        const bool isKeyFrame = (frameCount % 30 == 0); // force key frame every 30
        auto segment = muxer.muxFrame(nalus, isKeyFrame);

        if (!segment.empty()) {
            server->broadcast(segment);
        }

        ++frameCount;
    }

    encoder->shutdown();
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

    // -- Create the window ---------------------------------------------------

    RECT windowRect = {
        0, 0,
        static_cast<LONG>(uz::kDefaultWidth),
        static_cast<LONG>(uz::kDefaultHeight)
    };
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right  - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

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

    // -- Main loop -----------------------------------------------------------

    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            const double delta = timer.tick();
            renderer.update(timer.elapsed());
            renderer.render();

            // Push the latest captured frame into the ring buffer.
            uz::FrameData frame = renderer.getLatestFrame();
            if (!frame.pixels.empty()) {
                g_frameRing.push(std::move(frame));
            }
        }
    }

    // -- Cleanup -------------------------------------------------------------

    g_encodeRunning.store(false, std::memory_order_relaxed);
    if (encodeThread.joinable()) {
        encodeThread.join();
    }

    wsServer.stop();
    renderer.shutdown();

    return static_cast<int>(msg.wParam);
}
