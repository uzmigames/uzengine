// uz_viewport.cpp — Standalone DX12 material preview viewport.
// Depends ONLY on system headers. No UzEngine abstractions.
// Features: procedural sky, ground grid, PBR sphere, interactive fly/orbit camera.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <vector>

using Microsoft::WRL::ComPtr;

static constexpr UINT        k_Width      = 1280;
static constexpr UINT        k_Height     = 720;
static constexpr UINT        k_Frames     = 2;
static constexpr DXGI_FORMAT k_RtvFormat  = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT k_DsvFormat  = DXGI_FORMAT_D32_FLOAT;
static constexpr DXGI_FORMAT k_HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static constexpr wchar_t     k_Title[]    = L"UzEngine VFX Viewport";
static constexpr wchar_t     k_Class[]    = L"UzViewportClass";

// Sphere mesh generation parameters
static constexpr UINT k_SphereSlices = 32;
static constexpr UINT k_SphereStacks = 16;

// ─── minimal math ─────────────────────────────────────────────────────────────

struct Vec3
{
    float x, y, z;

    Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s)       const { return {x * s, y * s, z * s}; }
};

static float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static Vec3 Normalize(const Vec3& v)
{
    float len = std::sqrt(Dot(v, v));
    if (len < 1e-8f) return {0.0f, 0.0f, 0.0f};
    float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv};
}

static Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// Column-major 4x4 matrix (matches HLSL float4x4 row_major storage when
// transposed, but we store column-major and mark HLSL as column_major).
struct Mat4
{
    float m[4][4]; // m[col][row]

    static Mat4 Identity()
    {
        Mat4 r = {};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
};

static Mat4 Mul(const Mat4& a, const Mat4& b)
{
    Mat4 r = {};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            for (int k = 0; k < 4; ++k)
                r.m[c][rr] += a.m[k][rr] * b.m[c][k];
    return r;
}

static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    Vec3 f = Normalize(target - eye);
    Vec3 r = Normalize(Cross(f, up));
    Vec3 u = Cross(r, f);

    Mat4 m = Mat4::Identity();
    m.m[0][0] =  r.x;  m.m[1][0] =  r.y;  m.m[2][0] =  r.z;
    m.m[0][1] =  u.x;  m.m[1][1] =  u.y;  m.m[2][1] =  u.z;
    m.m[0][2] = -f.x;  m.m[1][2] = -f.y;  m.m[2][2] = -f.z;
    m.m[3][0] = -Dot(r, eye);
    m.m[3][1] = -Dot(u, eye);
    m.m[3][2] =  Dot(f, eye);
    return m;
}

// Reverse-Z infinite-far perspective projection.
// Maps near plane to depth=1.0, far plane to depth=0.0.
static Mat4 PerspectiveReverseZ(float fovY, float aspect, float nearZ, float farZ)
{
    float tanHalf = std::tan(fovY * 0.5f);
    Mat4 m = {};
    m.m[0][0] = 1.0f / (aspect * tanHalf);
    m.m[1][1] = -1.0f / tanHalf;  // negative to flip Y (DX clip space Y-up vs screen Y-down)
    // Reverse-Z: remaps [near, far] to [1, 0]
    m.m[2][2] = nearZ / (farZ - nearZ);
    m.m[3][2] = (farZ * nearZ) / (farZ - nearZ);
    m.m[2][3] = -1.0f;
    return m;
}

// Simple matrix inversion for view-projection (general 4x4 via cofactors).
// Used to reconstruct world-space rays in the sky shader.
static Mat4 InvertMatrix(const Mat4& in)
{
    // Flatten column-major to a linear array for cleaner cofactor indexing.
    const float* s = &in.m[0][0]; // 16 floats, column-major
    float inv[16];

    inv[0]  =  s[5]*s[10]*s[15] - s[5]*s[11]*s[14] - s[9]*s[6]*s[15] + s[9]*s[7]*s[14] + s[13]*s[6]*s[11] - s[13]*s[7]*s[10];
    inv[4]  = -s[4]*s[10]*s[15] + s[4]*s[11]*s[14] + s[8]*s[6]*s[15] - s[8]*s[7]*s[14] - s[12]*s[6]*s[11] + s[12]*s[7]*s[10];
    inv[8]  =  s[4]*s[9]*s[15]  - s[4]*s[11]*s[13] - s[8]*s[5]*s[15] + s[8]*s[7]*s[13] + s[12]*s[5]*s[11] - s[12]*s[7]*s[9];
    inv[12] = -s[4]*s[9]*s[14]  + s[4]*s[10]*s[13] + s[8]*s[5]*s[14] - s[8]*s[6]*s[13] - s[12]*s[5]*s[10] + s[12]*s[6]*s[9];
    inv[1]  = -s[1]*s[10]*s[15] + s[1]*s[11]*s[14] + s[9]*s[2]*s[15] - s[9]*s[3]*s[14] - s[13]*s[2]*s[11] + s[13]*s[3]*s[10];
    inv[5]  =  s[0]*s[10]*s[15] - s[0]*s[11]*s[14] - s[8]*s[2]*s[15] + s[8]*s[3]*s[14] + s[12]*s[2]*s[11] - s[12]*s[3]*s[10];
    inv[9]  = -s[0]*s[9]*s[15]  + s[0]*s[11]*s[13] + s[8]*s[1]*s[15] - s[8]*s[3]*s[13] - s[12]*s[1]*s[11] + s[12]*s[3]*s[9];
    inv[13] =  s[0]*s[9]*s[14]  - s[0]*s[10]*s[13] - s[8]*s[1]*s[14] + s[8]*s[2]*s[13] + s[12]*s[1]*s[10] - s[12]*s[2]*s[9];
    inv[2]  =  s[1]*s[6]*s[15]  - s[1]*s[7]*s[14]  - s[5]*s[2]*s[15] + s[5]*s[3]*s[14] + s[13]*s[2]*s[7]  - s[13]*s[3]*s[6];
    inv[6]  = -s[0]*s[6]*s[15]  + s[0]*s[7]*s[14]  + s[4]*s[2]*s[15] - s[4]*s[3]*s[14] - s[12]*s[2]*s[7]  + s[12]*s[3]*s[6];
    inv[10] =  s[0]*s[5]*s[15]  - s[0]*s[7]*s[13]  - s[4]*s[1]*s[15] + s[4]*s[3]*s[13] + s[12]*s[1]*s[7]  - s[12]*s[3]*s[5];
    inv[14] = -s[0]*s[5]*s[14]  + s[0]*s[6]*s[13]  + s[4]*s[1]*s[14] - s[4]*s[2]*s[13] - s[12]*s[1]*s[6]  + s[12]*s[2]*s[5];
    inv[3]  = -s[1]*s[6]*s[11]  + s[1]*s[7]*s[10]  + s[5]*s[2]*s[11] - s[5]*s[3]*s[10] - s[9]*s[2]*s[7]   + s[9]*s[3]*s[6];
    inv[7]  =  s[0]*s[6]*s[11]  - s[0]*s[7]*s[10]  - s[4]*s[2]*s[11] + s[4]*s[3]*s[10] + s[8]*s[2]*s[7]   - s[8]*s[3]*s[6];
    inv[11] = -s[0]*s[5]*s[11]  + s[0]*s[7]*s[9]   + s[4]*s[1]*s[11] - s[4]*s[3]*s[9]  - s[8]*s[1]*s[7]   + s[8]*s[3]*s[5];
    inv[15] =  s[0]*s[5]*s[10]  - s[0]*s[6]*s[9]   - s[4]*s[1]*s[10] + s[4]*s[2]*s[9]  + s[8]*s[1]*s[6]   - s[8]*s[2]*s[5];

    float det = s[0]*inv[0] + s[1]*inv[4] + s[2]*inv[8] + s[3]*inv[12];
    if (std::fabs(det) < 1e-12f)
        return Mat4::Identity();

    float invDet = 1.0f / det;
    Mat4 result;
    for (int i = 0; i < 16; ++i)
        reinterpret_cast<float*>(&result.m[0][0])[i] = inv[i] * invDet;
    return result;
}

// ─── input state ─────────────────────────────────────────────────────────────

struct InputState {
    bool keys[256] = {};
    bool prevKeys[256] = {};
    bool mouseButtons[5] = {};   // L=0, R=1, Middle=2, X1=3, X2=4
    bool prevMouseButtons[5] = {};
    int  mouseX = 0, mouseY = 0;
    int  prevMouseX = 0, prevMouseY = 0;
    int  rawDeltaX = 0, rawDeltaY = 0;  // accumulated WM_INPUT deltas per frame
    float scrollDelta = 0.0f;

    bool IsKeyDown(int vk)      const { return keys[vk & 0xFF]; }
    bool JustPressed(int vk)    const { return keys[vk & 0xFF] && !prevKeys[vk & 0xFF]; }
    bool JustReleased(int vk)   const { return !keys[vk & 0xFF] && prevKeys[vk & 0xFF]; }
    bool IsMouseDown(int btn)   const { return mouseButtons[btn]; }
    bool JustClicked(int btn)   const { return mouseButtons[btn] && !prevMouseButtons[btn]; }

    void BeginFrame() {
        std::memcpy(prevKeys, keys, sizeof(keys));
        std::memcpy(prevMouseButtons, mouseButtons, sizeof(mouseButtons));
        prevMouseX = mouseX;
        prevMouseY = mouseY;
        rawDeltaX = 0;
        rawDeltaY = 0;
        scrollDelta = 0.0f;
    }
};

// ─── camera ──────────────────────────────────────────────────────────────────

static constexpr float k_Pi = 3.14159265358979323846f;

struct Camera {
    Vec3  position = {3.0f, 2.0f, 3.0f};
    float yaw   = -135.0f;   // degrees — initial: looking toward origin
    float pitch  = -20.0f;   // degrees
    float fovDeg = 60.0f;
    float nearZ  = 0.1f;
    float farZ   = 1000.0f;
    float moveSpeed = 5.0f;
    float lookSensitivity = 0.15f;
    float sprintMultiplier = 3.0f;

    // Orbit mode
    Vec3  orbitTarget = {0.0f, 0.0f, 0.0f};
    float orbitDistance = 5.0f;
    bool  orbiting = false;

    // Velocity for inertia
    Vec3  velocity = {0.0f, 0.0f, 0.0f};
    float damping = 10.0f;  // exponential decay rate

    Vec3 GetForward() const {
        float yr = yaw * k_Pi / 180.0f;
        float pr = pitch * k_Pi / 180.0f;
        return {
            std::cos(pr) * std::sin(yr),
            std::sin(pr),
            std::cos(pr) * std::cos(yr)
        };
    }

    Vec3 GetRight() const {
        float yr = yaw * k_Pi / 180.0f;
        return { std::cos(yr), 0.0f, -std::sin(yr) };
    }

    void Update(const InputState& input, float dt) {
        // Clamp dt to avoid huge jumps after breakpoints or stalls
        dt = std::fmin(dt, 0.1f);

        // Mouse look (RMB held)
        if (input.IsMouseDown(1)) {
            yaw   += input.rawDeltaX * lookSensitivity;
            pitch -= input.rawDeltaY * lookSensitivity;
            pitch = std::fmax(-89.0f, std::fmin(89.0f, pitch));
        }

        // Orbit mode (MMB held)
        if (input.IsMouseDown(2)) {
            constexpr float orbitSens = 0.3f;
            Vec3 offset = position - orbitTarget;
            float dist = std::sqrt(Dot(offset, offset));
            if (dist < 0.01f) dist = 0.01f;

            float orbitYaw   = std::atan2(offset.x, offset.z);
            float orbitPitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, offset.y / dist)));

            orbitYaw   += input.rawDeltaX * orbitSens * k_Pi / 180.0f;
            orbitPitch -= input.rawDeltaY * orbitSens * k_Pi / 180.0f;
            orbitPitch  = std::fmax(-1.4f, std::fmin(1.4f, orbitPitch)); // ~80 deg limit

            position.x = orbitTarget.x + dist * std::cos(orbitPitch) * std::sin(orbitYaw);
            position.y = orbitTarget.y + dist * std::sin(orbitPitch);
            position.z = orbitTarget.z + dist * std::cos(orbitPitch) * std::cos(orbitYaw);

            // Recompute camera angles to face orbit target
            Vec3 toTarget = Normalize(orbitTarget - position);
            yaw   = std::atan2(toTarget.x, toTarget.z) * 180.0f / k_Pi;
            pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, toTarget.y))) * 180.0f / k_Pi;
        }

        // Scroll: Ctrl+scroll = FOV, plain scroll = dolly forward/back
        if (input.scrollDelta != 0.0f) {
            if (input.IsKeyDown(VK_CONTROL)) {
                fovDeg -= input.scrollDelta * 2.0f;
                fovDeg = std::fmax(30.0f, std::fmin(120.0f, fovDeg));
            } else {
                Vec3 fwd = GetForward();
                position = position + fwd * (input.scrollDelta * 0.5f);
            }
        }

        // WASD fly (only when RMB held — FPS mode)
        Vec3 moveDir = {0.0f, 0.0f, 0.0f};
        if (input.IsMouseDown(1)) {
            Vec3 fwd   = GetForward();
            Vec3 right = GetRight();

            if (input.IsKeyDown('W')) moveDir = moveDir + fwd;
            if (input.IsKeyDown('S')) moveDir = moveDir - fwd;
            if (input.IsKeyDown('D')) moveDir = moveDir + right;
            if (input.IsKeyDown('A')) moveDir = moveDir - right;
            if (input.IsKeyDown(VK_SPACE))   moveDir.y += 1.0f;
            if (input.IsKeyDown(VK_CONTROL)) moveDir.y -= 1.0f;
        }

        float speed = moveSpeed;
        if (input.IsKeyDown(VK_SHIFT)) speed *= sprintMultiplier;

        float moveMag = std::sqrt(Dot(moveDir, moveDir));
        if (moveMag > 0.001f) {
            moveDir = moveDir * (1.0f / moveMag);
            velocity = moveDir * speed;
        }

        // Apply velocity with exponential damping
        position = position + velocity * dt;
        float dampFactor = std::exp(-damping * dt);
        velocity = velocity * dampFactor;
    }

    Mat4 GetViewMatrix() const {
        Vec3 fwd = GetForward();
        Vec3 target = position + fwd;
        return LookAt(position, target, {0.0f, 1.0f, 0.0f});
    }

    Mat4 GetProjectionMatrix(float aspect) const {
        float fovRad = fovDeg * k_Pi / 180.0f;
        return PerspectiveReverseZ(fovRad, aspect, nearZ, farZ);
    }
};

static InputState g_input;
static Camera     g_camera;

// Time-of-day: sun elevation in degrees, controllable with T/Shift+T
static float g_sunElevation = 45.0f;

// Shadow mapping: bias controllable with [ / ]
static float g_shadowBias = 0.002f;

// Post-processing: exposure controllable with +/-, vignette intensity
static float g_exposure          = 1.0f;
static float g_vignetteIntensity = 0.4f;
static bool  g_postProcessEnabled = true; // P key toggles (ACES vs linear pass-through)

// Shadow map dimensions
static constexpr UINT k_ShadowMapSize = 2048;

// ─── debug view modes (F1-F8) ────────────────────────────────────────────────

enum class DebugView : int {
    Lit = 0, Albedo, WorldNormal, Metallic, Roughness, Depth, Wireframe, Unlit, COUNT
};
static DebugView g_debugView = DebugView::Lit;
static const char* k_DebugViewNames[] = {
    "Lit", "Albedo", "Normal", "Metallic", "Roughness", "Depth", "Wireframe", "Unlit"
};

// ─── stats overlay ───────────────────────────────────────────────────────────

static bool g_showStats = true; // F12 toggles

// ─── screenshot & headless mode (Phase 8) ────────────────────────────────────

static bool g_headless          = false;
static int  g_maxFrames         = 0;      // 0 = unlimited
static int  g_screenshotFrame   = -1;     // -1 = no auto-capture
static bool g_screenshotStdout  = false;
static bool g_screenshotRequest = false;  // set true by F9 or auto-capture
static char g_screenshotPath[512] = {};

// ─── material presets ─────────────────────────────────────────────────────────

struct MaterialPreset {
    const char* name;
    float baseColor[3];
    float metallic;
    float roughness;
    float ao;
    float emissiveColor[3];
    float emissiveIntensity;
};

static const MaterialPreset k_Materials[] = {
    {"Red Plastic",     {0.8f, 0.1f, 0.1f},  0.0f, 0.4f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Gold Metal",      {1.0f, 0.76f, 0.33f}, 1.0f, 0.3f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Rough Stone",     {0.5f, 0.45f, 0.4f},  0.0f, 0.85f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Polished Chrome", {0.9f, 0.9f, 0.9f},   1.0f, 0.05f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Rubber",          {0.15f, 0.15f, 0.15f}, 0.0f, 0.9f,  1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Copper",          {0.97f, 0.74f, 0.62f}, 1.0f, 0.45f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Ceramic White",   {0.95f, 0.93f, 0.88f}, 0.0f, 0.15f, 1.0f, {0.0f, 0.0f, 0.0f}, 0.0f},
    {"Emissive Blue",   {0.1f, 0.1f, 0.3f},   0.0f, 0.5f,  1.0f, {0.2f, 0.4f, 1.0f}, 5.0f},
};
static constexpr int k_MaterialCount = 8;
static int g_currentMaterial = 0;

// ─── mesh registry ─────────────────────────────────────────────────────────────

struct MeshData {
    ComPtr<ID3D12Resource>    vb;
    ComPtr<ID3D12Resource>    ib;
    D3D12_VERTEX_BUFFER_VIEW  vbv = {};
    D3D12_INDEX_BUFFER_VIEW   ibv = {};
    UINT indexCount = 0;
    const char* name = "";
};

static constexpr int k_MeshCount = 5;
static MeshData g_meshes[k_MeshCount];
static int g_currentMesh = 0; // 0=sphere, 1=cube, 2=plane, 3=cylinder, 4=torus

// Frame timing
static LARGE_INTEGER g_freq     = {};
static LARGE_INTEGER g_lastTime = {};
static float         g_deltaTime = 0.016f;
static UINT          g_totalFrames = 0;

// ─── error handling ───────────────────────────────────────────────────────────

// Forward declaration so UZ_CHK can reference g_device before struct definition.
static ComPtr<ID3D12Device> g_device;

[[noreturn]] static void FatalHR(const char* ctx, HRESULT hr)
{
    char msg[512];
    std::snprintf(msg, sizeof(msg),
        "[uz_viewport] FATAL %s — HRESULT 0x%08X\n", ctx, static_cast<unsigned>(hr));
    OutputDebugStringA(msg);
    MessageBoxA(nullptr, msg, "uz_viewport fatal error", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static void ReportDeviceRemoved(ID3D12Device* dev, const char* ctx)
{
    HRESULT reason = dev->GetDeviceRemovedReason();
    char msg[512];
    std::snprintf(msg, sizeof(msg),
        "[uz_viewport] DEVICE REMOVED during %s\n"
        "Reason HRESULT: 0x%08X\n\n"
        "Common causes:\n"
        "- GPU driver crash/timeout (TDR)\n"
        "- Incompatible debug layer\n"
        "- Driver bug\n\n"
        "Try updating your GPU driver.",
        ctx, static_cast<unsigned>(reason));
    OutputDebugStringA(msg);
    MessageBoxA(nullptr, msg, "uz_viewport — Device Removed", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

#define UZ_CHK(ctx, expr) do {                              \
    HRESULT _hr = (expr);                                   \
    if (_hr == DXGI_ERROR_DEVICE_REMOVED && g_device)       \
        ReportDeviceRemoved(g_device.Get(), ctx);           \
    if (FAILED(_hr)) FatalHR(ctx, _hr);                     \
} while(false)

// ─── per-frame constant buffer (must match HLSL) ─────────────────────────────

struct PerFrameCB
{
    float viewProj[16];                                        // 64 bytes  (offset 0)
    float invViewProj[16];                                     // 64 bytes  (offset 64)
    float cameraPos[3];    float _pad0;                        // 16 bytes  (offset 128)
    float lightDir[3];     float _pad1;                        // 16 bytes  (offset 144)
    float lightColor[3];   float _pad2;                        // 16 bytes  (offset 160)
    float time;
    float nearPlane;
    float farPlane;
    float _pad3;                                               // 16 bytes  (offset 176)
    // Material params (Phase 4)
    float matBaseColor[3]; float matMetallic;                   // 16 bytes  (offset 192)
    float matRoughness;    float matAO;
    float matEmissiveIntensity; float _matPad;                  // 16 bytes  (offset 208)
    float matEmissiveColor[3];  float _matPad2;                 // 16 bytes  (offset 224)
    float sunElevation;    float _cbPad[3];                     // 16 bytes  (offset 240)
    // Phase 5: Shadow mapping
    float shadowViewProj[16];                                  // 64 bytes  (offset 256)
    float shadowBias;      float exposure;
    float vignetteIntensity; float postProcessMode;             // 16 bytes  (offset 320)
    float debugViewMode;   float _cbPad2[11];                   // 48 bytes  (offset 336) pad to 384
};
static_assert(sizeof(PerFrameCB) == 384, "CB must be 384 bytes (24 x float4)");

// ─── VFX particle system constants and structures ────────────────────────────

static constexpr UINT k_VFXMaxParticles = 50000;

// Spawn constants passed to the spawn compute shader (cbuffer b0).
// Laid out as 16-byte aligned rows for HLSL packing rules.
struct VFXSpawnCB
{
    float emitterPos[3];      float spawnRadius;          // 16 bytes
    float spawnVelocity[3];   float velocityVariation;    // 16 bytes
    float spreadAngle;        float minLifetime;
    float maxLifetime;        float initialSize;          // 16 bytes
    float initialColor[4];                                // 16 bytes
    float dt;                 uint32_t frameNumber;
    uint32_t spawnCount;      uint32_t maxParticles;      // 16 bytes
};
static_assert(sizeof(VFXSpawnCB) == 80, "VFXSpawnCB must be 80 bytes");

// Update constants passed to the update compute shader (cbuffer b0).
struct VFXUpdateCB
{
    float gravity[3];         float dt;                   // 16 bytes
    float damping;            uint32_t aliveCount;
    uint32_t frameNumber;     uint32_t maxParticles;      // 16 bytes
};
static_assert(sizeof(VFXUpdateCB) == 32, "VFXUpdateCB must be 32 bytes");

// CPU-side emitter configuration (simple fountain preset).
struct VFXEmitterConfig
{
    float position[3]      = {0.0f, 1.5f, 0.0f};   // above center
    float spawnRadius      = 0.1f;
    float spawnVelocity[3] = {0.0f, 3.0f, 0.0f};   // upward
    float velocityVariation = 0.5f;
    float spreadAngle      = 0.3f;                  // ~17 degrees cone
    float minLifetime      = 1.0f;
    float maxLifetime      = 3.0f;
    float initialSize      = 0.05f;
    float initialColor[4]  = {1.0f, 0.6f, 0.2f, 0.8f}; // warm orange
    float gravity[3]       = {0.0f, -9.81f, 0.0f};
    float damping          = 0.1f;
};

// ─── VFX emitter presets (Task 9.12) ──────────────────────────────────────────
struct VFXPreset {
    const char*     name;
    VFXEmitterConfig config;
    float           spawnRate;
};

static const VFXPreset k_VFXPresets[] = {
    { "Fire Fountain", {
        /* position */        {0.0f, 1.5f, 0.0f},
        /* spawnRadius */     0.1f,
        /* spawnVelocity */   {0.0f, 3.0f, 0.0f},
        /* velocityVariation */ 0.5f,
        /* spreadAngle */     0.3f,
        /* minLifetime */     1.0f,
        /* maxLifetime */     3.0f,
        /* initialSize */     0.05f,
        /* initialColor */    {1.0f, 0.6f, 0.2f, 0.8f},
        /* gravity */         {0.0f, -9.81f, 0.0f},
        /* damping */         0.1f
    }, 500.0f },
    { "Smoke Plume", {
        /* position */        {0.0f, 0.5f, 0.0f},
        /* spawnRadius */     0.15f,
        /* spawnVelocity */   {0.0f, 1.5f, 0.0f},
        /* velocityVariation */ 0.3f,
        /* spreadAngle */     0.5f,
        /* minLifetime */     3.0f,
        /* maxLifetime */     6.0f,
        /* initialSize */     0.15f,
        /* initialColor */    {0.4f, 0.4f, 0.4f, 0.6f},
        /* gravity */         {0.0f, -1.0f, 0.0f},
        /* damping */         0.2f
    }, 200.0f },
    { "Sparks", {
        /* position */        {0.0f, 2.0f, 0.0f},
        /* spawnRadius */     0.05f,
        /* spawnVelocity */   {0.0f, 8.0f, 0.0f},
        /* velocityVariation */ 0.8f,
        /* spreadAngle */     0.15f,
        /* minLifetime */     0.3f,
        /* maxLifetime */     1.0f,
        /* initialSize */     0.02f,
        /* initialColor */    {1.0f, 0.9f, 0.5f, 1.0f},
        /* gravity */         {0.0f, -15.0f, 0.0f},
        /* damping */         0.05f
    }, 1000.0f },
    { "Magic Orb", {
        /* position */        {0.0f, 1.5f, 0.0f},
        /* spawnRadius */     0.05f,
        /* spawnVelocity */   {0.0f, 0.5f, 0.0f},
        /* velocityVariation */ 0.6f,
        /* spreadAngle */     3.14159f,
        /* minLifetime */     1.0f,
        /* maxLifetime */     2.0f,
        /* initialSize */     0.03f,
        /* initialColor */    {0.2f, 0.6f, 1.0f, 0.9f},
        /* gravity */         {0.0f, 0.0f, 0.0f},
        /* damping */         0.3f
    }, 300.0f },
    { "Explosion Debris", {
        /* position */        {0.0f, 1.5f, 0.0f},
        /* spawnRadius */     0.1f,
        /* spawnVelocity */   {0.0f, 5.0f, 0.0f},
        /* velocityVariation */ 1.0f,
        /* spreadAngle */     1.5f,
        /* minLifetime */     0.5f,
        /* maxLifetime */     2.0f,
        /* initialSize */     0.08f,
        /* initialColor */    {1.0f, 0.4f, 0.1f, 0.9f},
        /* gravity */         {0.0f, -20.0f, 0.0f},
        /* damping */         0.08f
    }, 2000.0f },
};
static constexpr int k_VFXPresetCount = static_cast<int>(sizeof(k_VFXPresets) / sizeof(k_VFXPresets[0]));

static bool            g_vfxEnabled     = true;   // Tab toggles
static float           g_vfxSpawnRate   = 500.0f;  // particles/second
static float           g_vfxSpawnAccum  = 0.0f;    // fractional spawn accumulator
static int             g_vfxPingPong    = 0;        // 0 or 1
static int             g_vfxPresetIndex = 0;        // current preset (Task 9.13)
static VFXEmitterConfig g_vfxEmitter;
// Transient display for parameter tweaking (Task 9.14)
static const char*     g_vfxTweakName   = nullptr;  // parameter name being tweaked
static float           g_vfxTweakValue  = 0.0f;     // current value of tweaked param
static int             g_vfxTweakTimer  = 0;         // frames remaining to show tweak info

// SRV heap layout for VFX (offsets from start of expanded srvHeap):
//   [0]    = shadow map SRV         (existing)
//   [1]    = HDR texture SRV        (existing)
//   [2-7]  = read set A SRVs       (positions, velocities, ages, lifespans, sizes, colors)
//   [8-13] = read set B SRVs
//   [14-19]= write set A UAVs
//   [20-25]= write set B UAVs
//   [26]   = aliveCount UAV
//   [27]   = freeList UAV
//   [28]   = freeListHead UAV
//   [29]   = indirectArgs UAV
//   [30]   = aliveCount SRV
//   [31]   = freeList SRV
//   [32]   = freeListHead SRV
//   [33]   = indirectArgs SRV
static constexpr UINT k_VFXSrvHeapSize       = 64;
static constexpr UINT k_VFXReadSetA_SRV      = 2;   // slots 2..7
static constexpr UINT k_VFXReadSetB_SRV      = 8;   // slots 8..13
static constexpr UINT k_VFXWriteSetA_UAV     = 14;  // slots 14..19
static constexpr UINT k_VFXWriteSetB_UAV     = 20;  // slots 20..25
static constexpr UINT k_VFXBookkeepingUAV    = 26;  // slots 26..29
static constexpr UINT k_VFXBookkeepingSRV    = 30;  // slots 30..33
// Number of particle SoA channels
static constexpr UINT k_VFXChannelCount      = 6;   // positions, velocities, ages, lifespans, sizes, colors
// Depth copy SRV for soft particles
static constexpr UINT k_VFXDepthCopySRV      = 34;
// Sort buffer descriptors
static constexpr UINT k_VFXSortKeysSRV       = 35;
static constexpr UINT k_VFXSortIndicesSRV    = 36;
static constexpr UINT k_VFXSortKeysUAV       = 37;
static constexpr UINT k_VFXSortIndicesUAV    = 38;
// Scratch descriptor ranges for compute dispatch staging (overwritten each dispatch)
static constexpr UINT k_VFXScratch0          = 39;  // spawn pass, indirect args, sort: 39..47
static constexpr UINT k_VFXScratch1          = 48;  // update pass: 48..63

// ─── embedded HLSL shaders ───────────────────────────────────────────────────

static const char k_SkyVS[] = R"HLSL(
// Sky vertex shader — fullscreen triangle from SV_VertexID.
// Generates a triangle that covers the entire screen.

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    // Fullscreen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = uv;
    return o;
}
)HLSL";

static const char k_SkyPS[] = R"HLSL(
// Sky pixel shader — Rayleigh + Mie single-scattering atmosphere approximation.
// @ref Nishita 1993 "Display of The Earth Taking into Account Atmospheric Scattering"
// @ref Preetham 1999 "A Practical Analytic Model for Daylight"
// Reconstructs world-space ray, computes optical depth, inscattering, sun disc.
// Outputs LINEAR HDR color — tonemap pass handles gamma and tone mapping.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

static const float PI = 3.14159265358979323846;

struct PSIn {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

// Rayleigh scattering coefficients (proportional to 1/wavelength^4)
// @ref Nishita 1993 — sea-level Rayleigh coefficients for RGB
static const float3 betaR = float3(5.8e-3, 13.5e-3, 33.1e-3);
// Mie scattering coefficient (wavelength-independent)
static const float3 betaM = float3(21e-3, 21e-3, 21e-3);

float3 ComputeSkyColor(float3 rayDir, float3 sunDir)
{
    // View-zenith angle
    float cosTheta = rayDir.y;
    float zenithAngle = acos(saturate(cosTheta));

    // Optical depth approximation (thicker at horizon)
    // @ref Preetham 1999 — Kasten & Young 1989 airmass formula
    float opticalDepth = 1.0 / (cosTheta + 0.15 * pow(max(93.885 - degrees(zenithAngle), 0.1), -1.253));
    opticalDepth = max(opticalDepth, 0.0);

    // Rayleigh extinction
    float3 rayleigh = betaR * opticalDepth;
    float3 extinction = exp(-rayleigh);

    // Sun-view angle for phase functions
    float cosGamma = dot(rayDir, sunDir);

    // Rayleigh phase function
    float phaseR = 0.75 * (1.0 + cosGamma * cosGamma);

    // Mie phase function (Henyey-Greenstein, g=0.76)
    // @ref Henyey & Greenstein 1941
    float g = 0.76;
    float phaseM = (1.0 - g*g) / (4.0 * PI * pow(1.0 + g*g - 2.0*g*cosGamma, 1.5));

    // Sun transmittance (orange/red at low sun angles)
    float sunAltitude = sunDir.y;
    float sunZenith = acos(saturate(sunAltitude));
    float sunOpticalDepth = 1.0 / (sunAltitude + 0.15 * pow(max(93.885 - degrees(sunZenith), 0.1), -1.253));
    sunOpticalDepth = max(sunOpticalDepth, 0.0);
    float3 sunTransmittance = exp(-betaR * sunOpticalDepth);

    // Inscattering: combined Rayleigh + Mie contribution
    float3 inscatter = (betaR * phaseR + betaM * phaseM * 0.1) * sunTransmittance;

    // Sky luminance with intensity scale
    float3 skyColor = inscatter * 20.0;

    // Below horizon: blend to warm ground color
    if (rayDir.y < 0.0) {
        float3 groundCol = float3(0.25, 0.22, 0.20);
        float t = saturate(-rayDir.y * 5.0);
        skyColor = lerp(skyColor, groundCol, t);
    }

    return skyColor;
}

PSOut main(PSIn input)
{
    // Reconstruct clip-space position: UV to NDC
    float2 ndc = input.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    float4 nearClip = float4(ndc, 1.0, 1.0); // reverse-Z: near = 1.0
    float4 farClip  = float4(ndc, 0.0, 1.0); // reverse-Z: far  = 0.0

    float4 nearWorld = mul(invViewProj, nearClip);
    float4 farWorld  = mul(invViewProj, farClip);
    nearWorld.xyz /= nearWorld.w;
    farWorld.xyz  /= farWorld.w;

    float3 rayDir = normalize(farWorld.xyz - nearWorld.xyz);
    float3 skyColor = ComputeSkyColor(rayDir, lightDir);

    // Sun disc with limb darkening
    float sunAngularRadius = 0.00935; // ~0.535 degrees in radians
    float sunCos = dot(rayDir, lightDir);
    float sunAngle = acos(saturate(sunCos));
    if (sunAngle < sunAngularRadius * 3.0) {
        float limbDarkening = 1.0 - 0.6 * pow(sunAngle / (sunAngularRadius * 3.0), 2.0);
        float sunMask = smoothstep(sunAngularRadius * 3.0, sunAngularRadius, sunAngle);
        skyColor += float3(50.0, 45.0, 40.0) * sunMask * limbDarkening;
    }
    // Sun glow halo
    float glowFactor = exp(-sunAngle * 150.0) * 2.0;
    skyColor += float3(1.0, 0.7, 0.3) * glowFactor;

    // Output linear HDR — tonemap pass handles tone mapping + gamma
    PSOut o;
    o.color = float4(max(skyColor, 0.0), 1.0);
    o.depth = 0.0; // reverse-Z far plane
    return o;
}
)HLSL";

static const char k_SceneVS[] = R"HLSL(
// Scene vertex shader — transforms position and normal for scene geometry.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
};

struct VSOut {
    float4 clipPos : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : TEXCOORD1;
};

VSOut main(VSIn input)
{
    VSOut o;
    o.worldPos = input.pos;
    o.worldNrm = input.normal;
    o.clipPos  = mul(viewProj, float4(input.pos, 1.0));
    return o;
}
)HLSL";

static const char k_GridPS[] = R"HLSL(
// Infinite grid pixel shader — ray/XZ-plane intersection, anti-aliased lines.
// @ref Ben Golus 2022 "The Best Darn Grid Shader (Yet)"
// Uses fullscreen triangle (sky VS), reconstructs world ray, intersects Y=-0.5 plane.
// Outputs LINEAR HDR color — tonemap pass handles gamma and tone mapping.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

struct PSIn {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

PSOut main(PSIn input)
{
    // Reconstruct world-space ray from fullscreen UV
    float2 ndc = input.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    float4 nearW = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float4 farW  = mul(invViewProj, float4(ndc, 0.0, 1.0));
    nearW.xyz /= nearW.w;
    farW.xyz  /= farW.w;

    float3 ro = nearW.xyz;
    float3 rd = normalize(farW.xyz - nearW.xyz);

    // Ray/XZ-plane intersection at Y = -0.5 (sphere radius 1, centered at origin)
    float groundY = -0.5;
    float t = (groundY - ro.y) / rd.y;

    PSOut o;

    if (t < 0.0 || rd.y >= 0.0) {
        discard;
    }

    float3 worldPos = ro + rd * t;

    // Compute depth for this world position (reverse-Z)
    float4 clipPos = mul(viewProj, float4(worldPos, 1.0));
    o.depth = clipPos.z / clipPos.w;

    // Distance fade to prevent aliasing at far distances
    float dist = length(worldPos - cameraPos);
    float fadeStart = 50.0;
    float fadeEnd   = 100.0;
    float fadeFactor = 1.0 - saturate((dist - fadeStart) / (fadeEnd - fadeStart));

    if (fadeFactor <= 0.001) discard;

    // Grid line computation
    float2 coord = worldPos.xz;

    // Minor grid (1 unit spacing)
    float2 grid = abs(frac(coord - 0.5) - 0.5);
    float2 dGrid = fwidth(coord);
    float lineWidth = 1.0;
    float2 gridAA = 1.0 - smoothstep(0.0, dGrid * lineWidth, grid);
    float minorLine = max(gridAA.x, gridAA.y);

    // Major grid (10 unit spacing)
    float2 grid10 = abs(frac(coord * 0.1 - 0.5) - 0.5);
    float2 dGrid10 = fwidth(coord * 0.1);
    float2 gridAA10 = 1.0 - smoothstep(0.0, dGrid10 * lineWidth, grid10);
    float majorLine = max(gridAA10.x, gridAA10.y);

    // Axis lines (X-axis = red line along Z, Z-axis = blue line along X)
    float axisWidth = 2.0;
    float xAxis = 1.0 - smoothstep(0.0, dGrid.y * axisWidth, abs(coord.y));
    float zAxis = 1.0 - smoothstep(0.0, dGrid.x * axisWidth, abs(coord.x));

    // Compose grid colors
    float3 color = float3(0.0, 0.0, 0.0);
    float alpha = 0.0;

    // Minor grid: thin gray
    color = lerp(color, float3(0.3, 0.3, 0.3), minorLine * 0.3);
    alpha = max(alpha, minorLine * 0.3);

    // Major grid: thicker white
    color = lerp(color, float3(0.5, 0.5, 0.5), majorLine * 0.5);
    alpha = max(alpha, majorLine * 0.5);

    // X-axis = red
    color = lerp(color, float3(0.8, 0.2, 0.2), zAxis * 0.8);
    alpha = max(alpha, zAxis * 0.8);

    // Z-axis = blue
    color = lerp(color, float3(0.2, 0.2, 0.8), xAxis * 0.8);
    alpha = max(alpha, xAxis * 0.8);

    // Apply distance fade
    alpha *= fadeFactor;

    // Lit ground underneath the grid
    float3 groundColor = float3(0.08, 0.08, 0.08);
    float NdotL = saturate(lightDir.y);
    float3 ambient = float3(0.05, 0.05, 0.06);
    float3 groundLit = groundColor * (ambient + lightColor * NdotL * 0.3);

    // Composite grid lines over lit ground
    float3 finalColor = lerp(groundLit, color, alpha);

    // Debug view overrides — in non-Lit modes, show flat ground color
    if (debugViewMode > 0.5) {
        int mode = (int)debugViewMode;
        if (mode == 1) { o.color = float4(groundColor, 1.0); return o; }        // Albedo: flat ground
        if (mode == 2) { o.color = float4(0.5, 1.0, 0.5, 1.0); return o; }     // Normal: Y-up (0,1,0) -> (0.5,1,0.5)
        if (mode == 3) { o.color = float4(0.0, 0.0, 0.0, 1.0); return o; }     // Metallic: 0
        if (mode == 4) { o.color = float4(1.0, 1.0, 1.0, 1.0); return o; }     // Roughness: 1
        if (mode == 5) {
            float linearD = nearPlane / (farPlane - o.depth * (farPlane - nearPlane));
            linearD = saturate(linearD * 5.0);
            o.color = float4(linearD, linearD, linearD, 1.0); return o;
        }
        if (mode == 7) { o.color = float4(groundColor, 1.0); return o; }        // Unlit: flat ground
    }

    // Output linear HDR — tonemap pass handles gamma
    o.color = float4(max(finalColor, 0.0), 1.0);
    return o;
}
)HLSL";

static const char k_SpherePS[] = R"HLSL(
// Sphere pixel shader — Cook-Torrance PBR BRDF with GGX, Smith-Joint, Schlick Fresnel.
// @ref Walter et al. 2007 "Microfacet Models for Refraction" — GGX/Trowbridge-Reitz NDF
// @ref Heitz 2014 "Understanding the Masking-Shadowing Function" — Smith Joint visibility
// @ref Schlick 1994 "An Inexpensive BRDF Model" — Fresnel approximation
// @ref Karis 2013 "Real Shading in Unreal Engine 4" — PBR formulation overview
// Outputs LINEAR HDR with shadow attenuation — tonemap pass handles gamma and tone mapping.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

// Shadow map (bound at t0)
SamplerComparisonState shadowSampler : register(s0);
Texture2D<float> shadowMap : register(t0);

struct PSIn {
    float4 clipPos  : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : TEXCOORD1;
};

static const float PI = 3.14159265358979323846;

// GGX/Trowbridge-Reitz Normal Distribution Function
// @ref Walter et al. 2007 eq. 33
float DistributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith-Joint Geometry function using GGX (height-correlated)
// @ref Heitz 2014 eq. 99 — height-correlated Smith G2
float GeometrySmithGGX(float NdotV, float NdotL, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-7);
}

// Schlick Fresnel approximation
// @ref Schlick 1994
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    float t5 = t2 * t2 * t;
    return F0 + (1.0 - F0) * t5;
}

// 3x3 PCF shadow sampling
// @ref Engel 2004 "Cascaded Shadow Maps" — PCF filtering
float SampleShadowPCF(float3 worldPos)
{
    float4 lsPos = mul(shadowViewProj, float4(worldPos, 1.0));
    float3 proj = lsPos.xyz / lsPos.w;
    float2 uv = proj.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    float depth = proj.z - shadowBias;

    // Outside shadow map bounds -> fully lit
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;

    // 3x3 PCF
    float shadow = 0.0;
    float texelSize = 1.0 / 2048.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) * texelSize, depth);
        }
    }
    shadow /= 9.0;

    // Fade shadow at edges of shadow map to avoid hard cutoff
    float2 fade = smoothstep(0.0, 0.05, uv) * smoothstep(0.0, 0.05, 1.0 - uv);
    shadow = lerp(1.0, shadow, fade.x * fade.y);

    return shadow;
}

float4 main(PSIn input) : SV_Target
{
    // Material parameters from constant buffer
    float3 baseColor = matBaseColor;
    float  metallic  = matMetallic;
    float  roughness = max(matRoughness, 0.04); // clamp to avoid singularity at 0

    float3 N = normalize(input.worldNrm);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = lightDir;
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Dielectric F0 = 0.04, metallic blends toward baseColor
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    // Cook-Torrance specular BRDF
    float  D = DistributionGGX(NdotH, roughness);
    float  G = GeometrySmithGGX(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 specular = D * G * F;  // G already includes the 1/(4*NdotV*NdotL) denominator

    // Energy conservation: diffuse is attenuated by Fresnel (metallic surfaces have no diffuse)
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * baseColor / PI;

    // Shadow
    float shadow = SampleShadowPCF(input.worldPos);

    // Direct lighting with shadow
    float3 Lo = (diffuse + specular) * lightColor * NdotL * shadow;

    // Hemisphere ambient: sky color adapts to sun elevation
    float sunHeight = saturate(lightDir.y);
    float3 skyAmbient    = lerp(float3(0.08, 0.06, 0.04), float3(0.15, 0.20, 0.35), sunHeight);
    float3 groundAmbient = float3(0.08, 0.06, 0.05);
    float  upBlend = N.y * 0.5 + 0.5;
    float3 ambient = lerp(groundAmbient, skyAmbient, upBlend) * 0.15;
    float3 ambientContrib = ambient * baseColor * (1.0 - metallic) + ambient * F0 * metallic;

    // Emissive contribution
    float3 emissive = matEmissiveColor * matEmissiveIntensity;

    float3 color = Lo + ambientContrib + emissive;

    // Debug view overrides — selected via F1-F8 keys, passed through CB
    if (debugViewMode > 0.5) {
        int mode = (int)debugViewMode;
        if (mode == 1) return float4(baseColor, 1.0);                          // Albedo
        if (mode == 2) return float4(N * 0.5 + 0.5, 1.0);                     // World Normal
        if (mode == 3) return float4(metallic, metallic, metallic, 1.0);       // Metallic
        if (mode == 4) return float4(roughness, roughness, roughness, 1.0);    // Roughness
        if (mode == 5) {
            // Linearize depth from clip-space position
            float4 cp = mul(viewProj, float4(input.worldPos, 1.0));
            float d = cp.z / cp.w;
            // Reverse-Z: near=1, far=0. Linearize for visualization.
            float linearD = nearPlane / (farPlane - d * (farPlane - nearPlane));
            linearD = saturate(linearD * 5.0); // scale for visibility
            return float4(linearD, linearD, linearD, 1.0);
        }
        // mode 6 = Wireframe — handled by PSO swap on CPU, solid pass uses normal lighting
        if (mode == 7) return float4(baseColor, 1.0);                          // Unlit (albedo, no lighting)
    }

    // Output linear HDR — tonemap pass handles tone mapping + gamma
    return float4(max(color, 0.0), 1.0);
}
)HLSL";

// ─── shadow vertex shader (depth-only, uses shadowViewProj) ─────────────────

static const char k_ShadowVS[] = R"HLSL(
// Shadow depth-only vertex shader — transforms to light space for shadow map.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 pos : SV_Position; };

VSOut main(VSIn input)
{
    VSOut o;
    o.pos = mul(shadowViewProj, float4(input.pos, 1.0));
    return o;
}
)HLSL";

// ─── tonemap pixel shader (fullscreen post-process: HDR -> LDR) ─────────────

static const char k_TonemapPS[] = R"HLSL(
// Tonemap pixel shader — ACES filmic + exposure + vignette + gamma.
// @ref Hill 2016 "ACES Filmic Tone Mapping Curve" — Stephen Hill's fit
// Reads HDR texture at t1, outputs LDR to backbuffer.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

Texture2D<float4> hdrTexture : register(t1);
SamplerState pointSampler : register(s1);

struct PSIn {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ACES filmic tonemap — Stephen Hill's fit
// @ref Hill 2016 — attempt to approximate ACES RRT+ODT
// @constants a=2.51, b=0.03, c=2.43, d=0.59, e=0.14
float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(PSIn input) : SV_Target
{
    float3 hdr = hdrTexture.Sample(pointSampler, input.uv).rgb;

    // Exposure
    hdr *= exposure;

    // Vignette: darken edges based on distance from center
    float2 uvc = input.uv - 0.5;
    float vignette = 1.0 - dot(uvc, uvc) * vignetteIntensity;
    vignette = saturate(vignette);
    hdr *= vignette;

    float3 ldr;
    if (postProcessMode > 0.5) {
        // ACES filmic tonemap
        ldr = ACESFilm(hdr);
    } else {
        // Linear pass-through (clamped)
        ldr = saturate(hdr);
    }

    // Gamma correction (linear -> sRGB approximation)
    ldr = pow(max(ldr, 0.0), 1.0 / 2.2);

    return float4(ldr, 1.0);
}
)HLSL";

// ─── wireframe pixel shader (solid green overlay for wireframe debug mode) ───

static const char k_WireframePS[] = R"HLSL(
struct PSIn {
    float4 clipPos  : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 worldNrm : TEXCOORD1;
};

float4 main(PSIn input) : SV_Target
{
    return float4(0.0, 1.0, 0.0, 1.0); // bright green wireframe lines
}
)HLSL";

// ─── VFX compute shaders ─────────────────────────────────────────────────────

static const char k_ParticleSpawnCS[] = R"HLSL(
// Particle spawn compute shader — pops free indices and initializes new particles.
// Uses PCG hash for deterministic per-thread randomness.

cbuffer SpawnConstants : register(b0) {
    float3 emitterPos;     float spawnRadius;
    float3 spawnVelocity;  float velocityVariation;
    float  spreadAngle;    float minLifetime;
    float  maxLifetime;    float initialSize;
    float4 initialColor;
    float  dt;             uint  frameNumber;
    uint   spawnCount;     uint  maxParticles;
};

// Write-set UAVs (the set we are writing new particles into)
RWStructuredBuffer<float3> outPositions  : register(u0);
RWStructuredBuffer<float3> outVelocities : register(u1);
RWStructuredBuffer<float>  outAges       : register(u2);
RWStructuredBuffer<float>  outLifespans  : register(u3);
RWStructuredBuffer<float>  outSizes      : register(u4);
RWStructuredBuffer<float4> outColors     : register(u5);

// Bookkeeping UAVs
RWByteAddressBuffer        aliveCountBuf : register(u6);
RWStructuredBuffer<uint>   freeList      : register(u7);
RWByteAddressBuffer        freeListHead  : register(u8);

// PCG hash — fast, high-quality pseudo-random number generator
// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms"
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randFloat(uint seed)
{
    return float(pcgHash(seed)) / 4294967295.0;
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= spawnCount)
        return;

    // Pop a free index from the free list stack (atomic decrement)
    uint prevHead;
    freeListHead.InterlockedAdd(0, -4, prevHead); // subtract 4 bytes = 1 uint
    uint stackIdx = prevHead / 4; // convert byte offset to element index

    // If the stack is empty (underflow), bail out
    if (stackIdx == 0)
    {
        // Undo the decrement to keep the stack consistent
        freeListHead.InterlockedAdd(0, 4, prevHead);
        return;
    }

    // Read the free particle index from the stack
    uint particleIdx = freeList[stackIdx - 1];

    // Generate random seeds from thread ID and frame number
    uint seed0 = pcgHash(particleIdx * 6781u + DTid.x + frameNumber * 31337u);
    uint seed1 = pcgHash(seed0);
    uint seed2 = pcgHash(seed1);
    uint seed3 = pcgHash(seed2);
    uint seed4 = pcgHash(seed3);
    uint seed5 = pcgHash(seed4);

    // Random position offset (uniform sphere shell)
    float theta = randFloat(seed0) * 6.28318530718;
    float phi   = acos(1.0 - 2.0 * randFloat(seed1));
    float r     = spawnRadius * pow(randFloat(seed2), 0.333333);
    float3 offset = float3(
        r * sin(phi) * cos(theta),
        r * sin(phi) * sin(theta),
        r * cos(phi)
    );

    // Random velocity with cone spread
    float spreadX = (randFloat(seed3) - 0.5) * 2.0 * spreadAngle;
    float spreadZ = (randFloat(seed4) - 0.5) * 2.0 * spreadAngle;
    float velScale = 1.0 + (randFloat(seed5) - 0.5) * velocityVariation;
    float3 vel = spawnVelocity * velScale + float3(spreadX, 0.0, spreadZ) * length(spawnVelocity);

    // Random lifetime in [minLifetime, maxLifetime]
    float life = lerp(minLifetime, maxLifetime, randFloat(pcgHash(seed5 + 1u)));

    // Write particle data to the write set
    outPositions[particleIdx]  = emitterPos + offset;
    outVelocities[particleIdx] = vel;
    outAges[particleIdx]       = 0.0;
    outLifespans[particleIdx]  = life;
    outSizes[particleIdx]      = initialSize;
    outColors[particleIdx]     = initialColor;

    // Increment alive count
    uint dummy;
    aliveCountBuf.InterlockedAdd(0, 1, dummy);
}
)HLSL";

static const char k_ParticleUpdateCS[] = R"HLSL(
// Particle update compute shader — advances physics, kills expired particles.
// Reads from the READ set, writes surviving particles to the WRITE set.
// Uses DispatchIndirect so aliveCount is read from a GPU buffer, not the CB.

cbuffer UpdateConstants : register(b0) {
    float3 gravity;    float dt;
    float  damping;    uint  _unused_aliveCount;
    uint   frameNumber; uint maxParticles;
};

// Read-set (via RWStructuredBuffer — all buffers stay in UAV state during compute)
RWStructuredBuffer<float3> inPositions   : register(u0);
RWStructuredBuffer<float3> inVelocities  : register(u1);
RWStructuredBuffer<float>  inAges        : register(u2);
RWStructuredBuffer<float>  inLifespans   : register(u3);
RWStructuredBuffer<float>  inSizes       : register(u4);
RWStructuredBuffer<float4> inColors      : register(u5);

// Write-set UAVs
RWStructuredBuffer<float3> outPositions  : register(u6);
RWStructuredBuffer<float3> outVelocities : register(u7);
RWStructuredBuffer<float>  outAges       : register(u8);
RWStructuredBuffer<float>  outLifespans  : register(u9);

// Bookkeeping
RWByteAddressBuffer        newAliveCount : register(u10);
RWStructuredBuffer<uint>   freeList      : register(u11);
RWByteAddressBuffer        freeListHead  : register(u12);

// Additional write-set channels (compiled with cs_5_1 for >8 UAV support)
RWStructuredBuffer<float>  outSizes      : register(u13);
RWStructuredBuffer<float4> outColors     : register(u14);

// Previous frame's indirect args buffer, used to read the alive count
// (the dispatch was sized correctly via DispatchIndirect, but we still
// need to bounds-check threads in the last group)
RWByteAddressBuffer        prevIndirectArgs : register(u15);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Read alive count from the draw args written last frame:
    // indirectArgs[0] = vertexCount = aliveCount * 6, so aliveCount = vertexCount / 6
    uint vertexCount = prevIndirectArgs.Load(0);
    uint aliveCount = vertexCount / 6;

    if (DTid.x >= aliveCount)
        return;

    uint idx = DTid.x;

    // Read current particle state from READ set
    float3 pos      = inPositions[idx];
    float3 vel      = inVelocities[idx];
    float  age      = inAges[idx];
    float  lifespan = inLifespans[idx];
    float  sz       = inSizes[idx];
    float4 col      = inColors[idx];

    // Advance age
    age += dt;

    // Kill expired particles — push back to free list
    if (age >= lifespan)
    {
        uint prevHead;
        freeListHead.InterlockedAdd(0, 4, prevHead);
        uint stackIdx = prevHead / 4;
        if (stackIdx < maxParticles)
        {
            freeList[stackIdx] = idx;
        }
        return; // don't write to output — particle is dead
    }

    // Physics integration
    vel += gravity * dt;
    vel *= saturate(1.0 - damping * dt);
    pos += vel * dt;

    // Fade alpha over lifetime (last 30% of life)
    float lifeRatio = age / lifespan;
    float fadeAlpha = 1.0 - saturate((lifeRatio - 0.7) / 0.3);
    col.a = col.a * fadeAlpha;

    // Shrink particles toward end of life
    float sizeFade = 1.0 - saturate((lifeRatio - 0.5) / 0.5);
    sz *= sizeFade;

    // Write updated state to WRITE set
    outPositions[idx]  = pos;
    outVelocities[idx] = vel;
    outAges[idx]       = age;
    outLifespans[idx]  = lifespan;
    outSizes[idx]      = sz;
    outColors[idx]     = col;

    // Increment new alive count
    uint dummy;
    newAliveCount.InterlockedAdd(0, 1, dummy);
}
)HLSL";

static const char k_ParticleIndirectArgsCS[] = R"HLSL(
// Indirect args compute shader — reads aliveCount, writes DrawInstanced args.
// Single-thread dispatch: [1,1,1].

RWByteAddressBuffer aliveCountBuf  : register(u0);
RWByteAddressBuffer indirectArgs   : register(u1);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint alive = aliveCountBuf.Load(0);

    // DrawInstanced args: {VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation}
    // Each particle = 6 vertices (2 triangles = 1 quad)
    indirectArgs.Store(0,  alive * 6);   // VertexCountPerInstance
    indirectArgs.Store(4,  1);           // InstanceCount
    indirectArgs.Store(8,  0);           // StartVertexLocation
    indirectArgs.Store(12, 0);           // StartInstanceLocation

    // Also store dispatch args for next frame's update pass at byte offset 16:
    // DispatchIndirect args: {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ}
    uint groupCount = (alive + 255) / 256;
    indirectArgs.Store(16, groupCount);
    indirectArgs.Store(20, 1);
    indirectArgs.Store(24, 1);
}
)HLSL";

// ─── VFX sort compute shaders (Task 9.7: Bitonic merge sort) ─────────────────
//
// @ref Batcher 1968 "Sorting Networks and Their Applications"
// Two-phase GPU bitonic sort:
//   Phase 0 (local): each thread group of 256 threads loads particles, generates
//     view-distance sort keys, and performs a complete bitonic sort in groupshared
//     memory. Outputs sorted (key, index) pairs.
//   Phase 1 (global): CPU dispatches multiple passes with increasing block/sub-block
//     sizes to merge the sorted 256-element blocks into a fully sorted array.
//
// Sort key: view-space distance, encoded as sortable uint via sign-bit flip for
// descending order (back-to-front for correct alpha blending).

struct VFXSortCB {
    uint32_t particleCount;
    uint32_t sortPhase;         // 0 = local + keygen, 1 = global merge
    uint32_t paddedCount;       // next power of 2 >= particleCount
    uint32_t _pad0;
    float    cameraPos[3];
    float    _pad1;
    float    cameraForward[3];
    float    _pad2;
    uint32_t bitonicBlockSize;
    uint32_t bitonicSubBlockSize;
    uint32_t _pad3;
    uint32_t _pad4;
};

static const char k_ParticleSortLocalCS[] = R"HLSL(
// Phase 0: local bitonic sort + sort key generation.
// Each thread group sorts 256 elements in groupshared memory.

cbuffer SortConstants : register(b0) {
    uint   particleCount;
    uint   sortPhase;
    uint   paddedCount;
    uint   _pad0;
    float3 camPos;     float _pad1;
    float3 camForward; float _pad2;
    uint   bitonicBlockSize;
    uint   bitonicSubBlockSize;
    uint   _pad3, _pad4;
};

// Particle positions (SRV — read only during sort)
StructuredBuffer<float3> particlePositions : register(t0);

// Sort output buffers
RWStructuredBuffer<uint> sortKeys    : register(u0);
RWStructuredBuffer<uint> sortIndices : register(u1);

#define BLOCK_SIZE 256

groupshared uint gs_keys[BLOCK_SIZE];
groupshared uint gs_indices[BLOCK_SIZE];

// Convert float distance to sortable uint, flipped for descending order (back-to-front).
// IEEE 754 float: if positive, XOR with 0x80000000 makes it sortable ascending.
// For descending, invert all bits instead.
uint FloatToSortableDescending(float f)
{
    uint bits = asuint(f);
    // For positive floats: flip sign bit + invert = descending sort
    // For negative floats (shouldn't happen for distance): handle correctly
    uint mask = (bits & 0x80000000) ? 0xFFFFFFFF : 0x7FFFFFFF;
    return ~(bits ^ mask);
}

[numthreads(BLOCK_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    uint globalIdx = DTid.x;

    // Load sort key: view-space distance for alive particles, max distance for padding
    if (globalIdx < particleCount)
    {
        float3 pos = particlePositions[globalIdx];
        float viewDist = dot(pos - camPos, camForward);
        gs_keys[GI] = FloatToSortableDescending(viewDist);
        gs_indices[GI] = globalIdx;
    }
    else
    {
        // Padding: sort to front (will not be drawn, but must not disrupt sort)
        gs_keys[GI] = 0; // descending: 0 = farthest = drawn first = correct for padding
        gs_indices[GI] = globalIdx;
    }

    GroupMemoryBarrierWithGroupSync();

    // Bitonic sort within the 256-element block
    [unroll]
    for (uint blockSize = 2; blockSize <= BLOCK_SIZE; blockSize <<= 1)
    {
        [unroll]
        for (uint subBlockSize = blockSize >> 1; subBlockSize > 0; subBlockSize >>= 1)
        {
            uint idx = GI;
            uint partner = idx ^ subBlockSize;

            if (partner > idx)
            {
                // Determine sort direction: ascending within each block of size blockSize
                bool ascending = ((idx & blockSize) == 0);

                uint keyA = gs_keys[idx];
                uint keyB = gs_keys[partner];
                uint idxA = gs_indices[idx];
                uint idxB = gs_indices[partner];

                bool shouldSwap = ascending ? (keyA > keyB) : (keyA < keyB);

                if (shouldSwap)
                {
                    gs_keys[idx]       = keyB;
                    gs_keys[partner]   = keyA;
                    gs_indices[idx]    = idxB;
                    gs_indices[partner] = idxA;
                }
            }

            GroupMemoryBarrierWithGroupSync();
        }
    }

    // Write sorted results to global memory
    if (globalIdx < paddedCount)
    {
        sortKeys[globalIdx]    = gs_keys[GI];
        sortIndices[globalIdx] = gs_indices[GI];
    }
}
)HLSL";

static const char k_ParticleSortGlobalCS[] = R"HLSL(
// Phase 1: global bitonic merge pass.
// Each dispatch handles one (blockSize, subBlockSize) pair.
// CPU loops over all pairs and dispatches with UAV barriers between each.

cbuffer SortConstants : register(b0) {
    uint   particleCount;
    uint   sortPhase;
    uint   paddedCount;
    uint   _pad0;
    float3 camPos;     float _pad1;
    float3 camForward; float _pad2;
    uint   bitonicBlockSize;
    uint   bitonicSubBlockSize;
    uint   _pad3, _pad4;
};

RWStructuredBuffer<uint> sortKeys    : register(u0);
RWStructuredBuffer<uint> sortIndices : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= paddedCount)
        return;

    uint partner = idx ^ bitonicSubBlockSize;

    if (partner > idx && partner < paddedCount)
    {
        // Determine sort direction based on block position
        bool ascending = ((idx & bitonicBlockSize) == 0);

        uint keyA = sortKeys[idx];
        uint keyB = sortKeys[partner];

        bool shouldSwap = ascending ? (keyA > keyB) : (keyA < keyB);

        if (shouldSwap)
        {
            sortKeys[idx]    = keyB;
            sortKeys[partner] = keyA;

            uint idxA = sortIndices[idx];
            uint idxB = sortIndices[partner];
            sortIndices[idx]    = idxB;
            sortIndices[partner] = idxA;
        }
    }
}
)HLSL";

// ─── VFX billboard vertex/pixel shaders ──────────────────────────────────────

static const char k_ParticleBillboardVS[] = R"HLSL(
// Billboard vertex shader — expands point particles into camera-facing quads.
// Uses SV_VertexID only (no vertex buffer).
// Reads particle data from StructuredBuffer SRVs.

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

StructuredBuffer<float3> particlePositions : register(t0);
StructuredBuffer<float>  particleSizes     : register(t1);
StructuredBuffer<float4> particleColors    : register(t2);
// t3 = sceneDepth (used by PS only)
StructuredBuffer<uint>   sortIndices       : register(t4);

struct VSOut {
    float4 clipPos : SV_Position;
    float2 uv      : TEXCOORD0;
    float4 color   : COLOR0;
};

VSOut main(uint vertexID : SV_VertexID)
{
    VSOut o;

    // Use sorted index for back-to-front draw order (bitonic sort, Task 9.7)
    uint rawParticleID = vertexID / 6;
    uint particleID = sortIndices[rawParticleID];
    uint cornerIdx  = vertexID % 6;

    // Quad corner offsets (two triangles: 0-1-2 and 3-4-5)
    // Triangle 1: (-1,-1), (1,-1), (-1,1)
    // Triangle 2: (1,-1), (1,1), (-1,1)
    static const float2 corners[6] = {
        float2(-1, -1), float2( 1, -1), float2(-1,  1),
        float2( 1, -1), float2( 1,  1), float2(-1,  1)
    };

    float3 center = particlePositions[particleID];
    float  halfSize = particleSizes[particleID] * 0.5;
    float4 col = particleColors[particleID];

    // Extract camera right and up from invViewProj
    // Camera right = normalize(invViewProj[0].xyz)
    // Camera up    = normalize(invViewProj[1].xyz)
    float3 camRight = normalize(float3(invViewProj[0].x, invViewProj[1].x, invViewProj[2].x));
    float3 camUp    = normalize(float3(invViewProj[0].y, invViewProj[1].y, invViewProj[2].y));

    float2 corner = corners[cornerIdx];
    float3 worldPos = center + (camRight * corner.x + camUp * corner.y) * halfSize;

    o.clipPos = mul(viewProj, float4(worldPos, 1.0));
    o.uv = corner * 0.5 + 0.5;
    o.color = col;

    return o;
}
)HLSL";

static const char k_ParticleBillboardPS[] = R"HLSL(
// Billboard pixel shader — outputs interpolated particle color with soft circle.
// Samples scene depth for soft particle depth fade (prevents hard intersection artifacts).

cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float3   cameraPos;    float _pad0;
    float3   lightDir;     float _pad1;
    float3   lightColor;   float _pad2;
    float    time;
    float    nearPlane;
    float    farPlane;
    float    _pad3;
    float3   matBaseColor;  float matMetallic;
    float    matRoughness;  float matAO;
    float    matEmissiveIntensity; float _matPad;
    float3   matEmissiveColor; float _matPad2;
    float    sunElevation;  float3 _cbPadEnd;
    float4x4 shadowViewProj;
    float    shadowBias;    float exposure;
    float    vignetteIntensity; float postProcessMode;
    float    debugViewMode; float3 _dbgPad;
    float4   _cbPad3[2];
};

Texture2D<float> sceneDepth : register(t3);

// Linearize reverse-Z depth to world-space distance.
// Reverse-Z: near plane maps to 1.0, far plane maps to 0.0.
float LinearizeDepth(float d, float zNear, float zFar)
{
    return zNear * zFar / (zFar + d * (zNear - zFar));
}

struct PSIn {
    float4 clipPos : SV_Position;
    float2 uv      : TEXCOORD0;
    float4 color   : COLOR0;
};

float4 main(PSIn input) : SV_Target
{
    // Soft circular particle shape
    float2 center = input.uv - 0.5;
    float dist = length(center) * 2.0; // 0 at center, 1 at edge
    float alpha = 1.0 - smoothstep(0.7, 1.0, dist);

    float4 col = input.color;
    col.a *= alpha;

    // Soft particle depth fade — fade alpha near scene surfaces to prevent hard intersections.
    // Both depths are in reverse-Z ([1,0] range), linearize to world-space distance.
    float rawSceneDepth    = sceneDepth.Load(int3(input.clipPos.xy, 0)).r;
    float rawParticleDepth = input.clipPos.z;

    float linearScene    = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float linearParticle = LinearizeDepth(rawParticleDepth, nearPlane, farPlane);

    // Fade distance: 30cm in world units (centimeter space)
    float softFadeDistance = 30.0;
    float depthDiff = linearScene - linearParticle;
    float softFade = saturate(depthDiff / softFadeDistance);
    col.a *= softFade;

    // Discard fully transparent fragments
    if (col.a < 0.001)
        discard;

    return col;
}
)HLSL";

// ─── BMP writer (no external dependencies) ──────────────────────────────────

static void WriteBMP(const char* path, const uint8_t* pixels, int w, int h)
{
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return;

    const int rowBytes  = w * 3;
    const int paddedRow = (rowBytes + 3) & ~3;
    const int dataSize  = paddedRow * h;
    const int fileSize  = 54 + dataSize;

    // BMP file header (14 bytes) + DIB header (BITMAPINFOHEADER, 40 bytes)
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    std::memcpy(&header[2],  &fileSize, 4);
    int offset = 54;
    std::memcpy(&header[10], &offset, 4);
    int dibSize = 40;
    std::memcpy(&header[14], &dibSize, 4);
    std::memcpy(&header[18], &w, 4);
    int negH = -h; // top-down DIB (negative height = origin at top-left)
    std::memcpy(&header[22], &negH, 4);
    header[26] = 1;   // planes
    header[28] = 24;  // bits per pixel
    std::memcpy(&header[34], &dataSize, 4);

    fwrite(header, 1, 54, f);

    // Write pixel data (convert RGBA to BGR, row by row with padding)
    uint8_t pad[3] = {};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = pixels + (static_cast<size_t>(y) * w + x) * 4;
            uint8_t bgr[3] = { p[2], p[1], p[0] };
            fwrite(bgr, 1, 3, f);
        }
        if (paddedRow - rowBytes > 0)
            fwrite(pad, 1, static_cast<size_t>(paddedRow - rowBytes), f);
    }

    fclose(f);
}

// ─── BMP to memory (for base64 encoding) ────────────────────────────────────

static std::vector<uint8_t> EncodeBMP(const uint8_t* pixels, int w, int h)
{
    const int rowBytes  = w * 3;
    const int paddedRow = (rowBytes + 3) & ~3;
    const int dataSize  = paddedRow * h;
    const int fileSize  = 54 + dataSize;

    std::vector<uint8_t> bmp(static_cast<size_t>(fileSize));
    uint8_t* d = bmp.data();

    // Header
    d[0] = 'B'; d[1] = 'M';
    std::memcpy(&d[2],  &fileSize, 4);
    int offset = 54;
    std::memcpy(&d[10], &offset, 4);
    int dibSize = 40;
    std::memcpy(&d[14], &dibSize, 4);
    std::memcpy(&d[18], &w, 4);
    int negH = -h;
    std::memcpy(&d[22], &negH, 4);
    d[26] = 1;
    d[28] = 24;
    std::memcpy(&d[34], &dataSize, 4);

    // Pixel data
    uint8_t* dst = d + 54;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = pixels + (static_cast<size_t>(y) * w + x) * 4;
            *dst++ = p[2]; // B
            *dst++ = p[1]; // G
            *dst++ = p[0]; // R
        }
        for (int pad = 0; pad < paddedRow - rowBytes; ++pad)
            *dst++ = 0;
    }

    return bmp;
}

// ─── base64 encoder ─────────────────────────────────────────────────────────

static const char k_Base64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<char> Base64Encode(const uint8_t* data, size_t len)
{
    size_t outLen = 4 * ((len + 2) / 3);
    std::vector<char> out(outLen + 1); // +1 for null terminator
    size_t j = 0;

    for (size_t i = 0; i < len; ) {
        uint32_t a = (i < len) ? data[i++] : 0;
        uint32_t b = (i < len) ? data[i++] : 0;
        uint32_t c = (i < len) ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = k_Base64Chars[(triple >> 18) & 0x3F];
        out[j++] = k_Base64Chars[(triple >> 12) & 0x3F];
        out[j++] = k_Base64Chars[(triple >> 6)  & 0x3F];
        out[j++] = k_Base64Chars[triple         & 0x3F];
    }

    // Padding
    size_t mod = len % 3;
    if (mod == 1) { out[j - 1] = '='; out[j - 2] = '='; }
    else if (mod == 2) { out[j - 1] = '='; }
    out[j] = '\0';

    return out;
}

// ─── shader compilation helper ───────────────────────────────────────────────

static ComPtr<ID3DBlob> CompileShader(const char* source, UINT sourceLen,
                                       const char* target, const char* name)
{
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        source, sourceLen, name,
        nullptr, nullptr,
        "main", target,
        flags, 0,
        &blob, &errors);

    if (FAILED(hr))
    {
        const char* errMsg = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "Unknown compilation error";
        char buf[2048];
        std::snprintf(buf, sizeof(buf),
            "[uz_viewport] Shader compilation failed: %s\n%s", name, errMsg);
        OutputDebugStringA(buf);
        MessageBoxA(nullptr, buf, "Shader Error", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    return blob;
}

// ─── mesh vertex ─────────────────────────────────────────────────────────────

struct Vertex
{
    float pos[3];
    float nrm[3];
};

// ─── application state ───────────────────────────────────────────────────────

struct App
{
    // Core DX12
    ComPtr<ID3D12CommandQueue>        cmdQueue;
    ComPtr<IDXGISwapChain3>           swapchain;
    ComPtr<ID3D12DescriptorHeap>      rtvHeap;
    ComPtr<ID3D12DescriptorHeap>      dsvHeap;
    ComPtr<ID3D12Resource>            backbuffers[k_Frames];
    ComPtr<ID3D12Resource>            depthBuffer;
    ComPtr<ID3D12CommandAllocator>    cmdAllocs[k_Frames];
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    ComPtr<ID3D12Fence>               fence;
    HANDLE   fenceEvent      = nullptr;
    UINT64   fenceValues[k_Frames] = {};
    UINT     rtvStride       = 0;
    UINT     frameIndex      = 0;

    // Root signature + PSOs
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> skyPSO;
    ComPtr<ID3D12PipelineState> gridPSO;    // infinite grid (fullscreen, ray/plane intersection)
    ComPtr<ID3D12PipelineState> spherePSO;  // scene geometry with PBR
    ComPtr<ID3D12PipelineState> shadowPSO;  // depth-only shadow pass
    ComPtr<ID3D12PipelineState> tonemapPSO; // fullscreen post-process (HDR -> LDR)
    ComPtr<ID3D12PipelineState> wireframePSO; // wireframe overlay (Phase 7)

    // Shadow mapping (Phase 5)
    ComPtr<ID3D12Resource>       shadowMap;       // 2048x2048 D32_FLOAT
    ComPtr<ID3D12DescriptorHeap> shadowDsvHeap;   // DSV for shadow map
    // Shader-visible SRV heap: slot 0 = shadow map SRV, slot 1 = HDR SRV
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvStride = 0;

    // HDR render target (Phase 6)
    ComPtr<ID3D12Resource>       hdrTarget;       // RGBA16F at screen resolution
    ComPtr<ID3D12DescriptorHeap> hdrRtvHeap;      // RTV for HDR target

    // Per-frame constant buffer (upload heap, persistently mapped)
    ComPtr<ID3D12Resource>  cbUpload[k_Frames];
    PerFrameCB*             cbMapped[k_Frames] = {};

    // Timing
    LARGE_INTEGER perfFreq  = {};
    LARGE_INTEGER startTime = {};

    HWND hwnd = nullptr;

    // ── VFX Particle System (Phase 9) ──
    // Particle SoA buffers: 2 sets for ping-pong (A=0, B=1)
    // Channels: [0]=positions(float3), [1]=velocities(float3), [2]=ages(float),
    //           [3]=lifespans(float), [4]=sizes(float), [5]=colors(float4)
    ComPtr<ID3D12Resource> vfxParticleBuffers[2][k_VFXChannelCount]; // [set][channel]

    // Bookkeeping buffers (not double-buffered)
    ComPtr<ID3D12Resource> vfxAliveCount;      // RWByteAddressBuffer (4 bytes)
    ComPtr<ID3D12Resource> vfxFreeList;         // RWStructuredBuffer<uint> (capacity)
    ComPtr<ID3D12Resource> vfxFreeListHead;     // RWByteAddressBuffer (4 bytes)
    ComPtr<ID3D12Resource> vfxIndirectArgs;     // RWByteAddressBuffer (28 bytes: draw + dispatch args)

    // Compute root signature + PSOs
    ComPtr<ID3D12RootSignature> vfxComputeRootSig;
    ComPtr<ID3D12PipelineState> vfxSpawnPSO;
    ComPtr<ID3D12PipelineState> vfxUpdatePSO;
    ComPtr<ID3D12PipelineState> vfxIndirectArgsPSO;

    // Billboard graphics root signature + PSO
    ComPtr<ID3D12RootSignature> vfxGraphicsRootSig;
    ComPtr<ID3D12PipelineState> vfxBillboardPSO;

    // Command signature for ExecuteIndirect (DrawInstanced)
    ComPtr<ID3D12CommandSignature> vfxDrawCmdSig;

    // VFX constant buffer (upload heap, per-frame)
    ComPtr<ID3D12Resource> vfxCbUpload[k_Frames];
    uint8_t*               vfxCbMapped[k_Frames] = {};

    // Depth copy for soft particles (R32_FLOAT copy of scene depth, readable as SRV)
    ComPtr<ID3D12Resource> vfxDepthCopy;

    // Sort buffers for back-to-front alpha blending (padded to next power of 2)
    ComPtr<ID3D12Resource> vfxSortKeys;      // RWStructuredBuffer<uint>
    ComPtr<ID3D12Resource> vfxSortIndices;   // RWStructuredBuffer<uint>
    ComPtr<ID3D12RootSignature> vfxSortRootSig;
    ComPtr<ID3D12PipelineState> vfxSortLocalPSO;   // phase 0: local sort + key gen
    ComPtr<ID3D12PipelineState> vfxSortGlobalPSO;  // phase 1: global merge
    uint32_t vfxSortPaddedCount = 0;                // next power of 2 >= capacity
};

static App g_app;

// ─── helpers ──────────────────────────────────────────────────────────────────

static D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT i)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_app.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(i) * g_app.rtvStride;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle()
{
    return g_app.dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

static D3D12_CPU_DESCRIPTOR_HANDLE ShadowDsvHandle()
{
    return g_app.shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

static D3D12_CPU_DESCRIPTOR_HANDLE HdrRtvHandle()
{
    return g_app.hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
}

static D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(UINT index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = g_app.srvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * g_app.srvStride;
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle(UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_app.srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(index) * g_app.srvStride;
    return h;
}

static void WaitForFrame(UINT i)
{
    const UINT64 target = g_app.fenceValues[i];
    if (g_app.fence->GetCompletedValue() < target)
    {
        g_app.fence->SetEventOnCompletion(target, g_app.fenceEvent);
        WaitForSingleObjectEx(g_app.fenceEvent, 2000, FALSE);
    }
}

static void WaitForAllFrames()
{
    for (UINT i = 0; i < k_Frames; ++i)
        WaitForFrame(i);
}

static double GetElapsedSeconds()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - g_app.startTime.QuadPart)
         / static_cast<double>(g_app.perfFreq.QuadPart);
}

// ─── shadow view-projection (orthographic from light direction) ──────────────

static Mat4 ComputeShadowViewProj(const Vec3& lightDir)
{
    // Light is positioned along its direction, looking toward the origin.
    Vec3 lightPos = lightDir * 20.0f;
    Vec3 target = {0.0f, 0.0f, 0.0f};

    // Choose an up vector that isn't parallel to lightDir
    Vec3 up = {0.0f, 1.0f, 0.0f};
    if (std::fabs(Dot(lightDir, up)) > 0.99f)
        up = {0.0f, 0.0f, 1.0f};

    Mat4 view = LookAt(lightPos, target, up);

    // Orthographic projection covering the scene (-10 to 10 in X/Y, 0 to 40 in Z)
    // Uses standard [0,1] depth (NOT reverse-Z) for the shadow map.
    Mat4 proj = {};
    constexpr float size = 10.0f;
    proj.m[0][0] =  1.0f / size;
    proj.m[1][1] = -1.0f / size; // flip Y (DX convention)
    proj.m[2][2] =  1.0f / 40.0f; // depth range 0..40 -> 0..1
    proj.m[3][3] =  1.0f;

    return Mul(proj, view);
}

// ─── create upload buffer helper ─────────────────────────────────────────────

static ComPtr<ID3D12Resource> CreateUploadBuffer(UINT64 size)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buf;
    UZ_CHK("CreateUploadBuffer",
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf)));
    return buf;
}

static ComPtr<ID3D12Resource> CreateDefaultBuffer(UINT64 size)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buf;
    UZ_CHK("CreateDefaultBuffer",
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buf)));
    return buf;
}

// ─── mesh generation ─────────────────────────────────────────────────────────

static void GenerateSphereMesh(std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    // UV sphere: slices (longitude) x stacks (latitude)
    for (UINT stack = 0; stack <= k_SphereStacks; ++stack)
    {
        float phi = 3.14159265f * static_cast<float>(stack) / static_cast<float>(k_SphereStacks);
        float sinP = std::sin(phi);
        float cosP = std::cos(phi);

        for (UINT slice = 0; slice <= k_SphereSlices; ++slice)
        {
            float theta = 2.0f * 3.14159265f * static_cast<float>(slice) / static_cast<float>(k_SphereSlices);
            float sinT = std::sin(theta);
            float cosT = std::cos(theta);

            Vertex v;
            v.pos[0] = sinP * cosT;  // x
            v.pos[1] = cosP;         // y (up)
            v.pos[2] = sinP * sinT;  // z
            v.nrm[0] = v.pos[0];
            v.nrm[1] = v.pos[1];
            v.nrm[2] = v.pos[2];
            verts.push_back(v);
        }
    }

    // Triangulate: two triangles per quad
    for (UINT stack = 0; stack < k_SphereStacks; ++stack)
    {
        for (UINT slice = 0; slice < k_SphereSlices; ++slice)
        {
            uint16_t i0 = static_cast<uint16_t>(stack * (k_SphereSlices + 1) + slice);
            uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            uint16_t i2 = static_cast<uint16_t>((stack + 1) * (k_SphereSlices + 1) + slice);
            uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            // Two triangles, wound CCW when viewed from outside
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
}

static void GenerateCubeMesh(std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    // Helper to add a face with 4 vertices sharing the same normal
    auto addFace = [&](float nx, float ny, float nz,
                       float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float x2, float y2, float z2,
                       float x3, float y3, float z3)
    {
        auto base = static_cast<uint16_t>(verts.size());
        Vertex v0 = {{x0,y0,z0}, {nx,ny,nz}};
        Vertex v1 = {{x1,y1,z1}, {nx,ny,nz}};
        Vertex v2 = {{x2,y2,z2}, {nx,ny,nz}};
        Vertex v3 = {{x3,y3,z3}, {nx,ny,nz}};
        verts.push_back(v0); verts.push_back(v1);
        verts.push_back(v2); verts.push_back(v3);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
    };

    const float h = 0.5f;
    // +Y (top)
    addFace( 0, 1, 0,  -h, h,-h,  h, h,-h,  h, h, h, -h, h, h);
    // -Y (bottom)
    addFace( 0,-1, 0,  -h,-h, h,  h,-h, h,  h,-h,-h, -h,-h,-h);
    // +X (right)
    addFace( 1, 0, 0,   h,-h,-h,  h, h,-h,  h, h, h,  h,-h, h);
    // -X (left)
    addFace(-1, 0, 0,  -h,-h, h, -h, h, h, -h, h,-h, -h,-h,-h);
    // +Z (front)
    addFace( 0, 0, 1,  -h,-h, h,  h,-h, h,  h, h, h, -h, h, h);
    // -Z (back)
    addFace( 0, 0,-1,   h,-h,-h, -h,-h,-h, -h, h,-h,  h, h,-h);
}

static void GeneratePlaneMesh(std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    // Subdivided 10x10 XZ plane at Y=0, from -1 to +1
    constexpr int kDiv = 10;
    constexpr int kVerts = kDiv + 1;

    for (int z = 0; z <= kDiv; ++z) {
        for (int x = 0; x <= kDiv; ++x) {
            Vertex v;
            v.pos[0] = -1.0f + 2.0f * static_cast<float>(x) / static_cast<float>(kDiv);
            v.pos[1] = 0.0f;
            v.pos[2] = -1.0f + 2.0f * static_cast<float>(z) / static_cast<float>(kDiv);
            v.nrm[0] = 0.0f;
            v.nrm[1] = 1.0f;
            v.nrm[2] = 0.0f;
            verts.push_back(v);
        }
    }

    for (int z = 0; z < kDiv; ++z) {
        for (int x = 0; x < kDiv; ++x) {
            auto i0 = static_cast<uint16_t>(z * kVerts + x);
            auto i1 = static_cast<uint16_t>(i0 + 1);
            auto i2 = static_cast<uint16_t>((z + 1) * kVerts + x);
            auto i3 = static_cast<uint16_t>(i2 + 1);
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
}

static void GenerateCylinderMesh(std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    constexpr int kSegs = 24;
    const float radius = 0.5f;
    const float halfH  = 0.5f;

    // Side vertices: two rings (bottom and top), each with kSegs+1 verts
    for (int ring = 0; ring < 2; ++ring) {
        float y = (ring == 0) ? -halfH : halfH;
        for (int s = 0; s <= kSegs; ++s) {
            float theta = 2.0f * k_Pi * static_cast<float>(s) / static_cast<float>(kSegs);
            float cx = std::cos(theta);
            float cz = std::sin(theta);
            Vertex v;
            v.pos[0] = cx * radius;
            v.pos[1] = y;
            v.pos[2] = cz * radius;
            v.nrm[0] = cx;
            v.nrm[1] = 0.0f;
            v.nrm[2] = cz;
            verts.push_back(v);
        }
    }

    // Side indices
    for (int s = 0; s < kSegs; ++s) {
        auto b0 = static_cast<uint16_t>(s);
        auto b1 = static_cast<uint16_t>(s + 1);
        auto t0 = static_cast<uint16_t>(kSegs + 1 + s);
        auto t1 = static_cast<uint16_t>(kSegs + 1 + s + 1);
        indices.push_back(b0); indices.push_back(t0); indices.push_back(b1);
        indices.push_back(b1); indices.push_back(t0); indices.push_back(t1);
    }

    // Top cap
    {
        auto center = static_cast<uint16_t>(verts.size());
        Vertex cv;
        cv.pos[0] = 0.0f; cv.pos[1] = halfH; cv.pos[2] = 0.0f;
        cv.nrm[0] = 0.0f; cv.nrm[1] = 1.0f;  cv.nrm[2] = 0.0f;
        verts.push_back(cv);
        auto ringStart = static_cast<uint16_t>(verts.size());
        for (int s = 0; s <= kSegs; ++s) {
            float theta = 2.0f * k_Pi * static_cast<float>(s) / static_cast<float>(kSegs);
            Vertex v;
            v.pos[0] = std::cos(theta) * radius;
            v.pos[1] = halfH;
            v.pos[2] = std::sin(theta) * radius;
            v.nrm[0] = 0.0f; v.nrm[1] = 1.0f; v.nrm[2] = 0.0f;
            verts.push_back(v);
        }
        for (int s = 0; s < kSegs; ++s) {
            indices.push_back(center);
            indices.push_back(static_cast<uint16_t>(ringStart + s + 1));
            indices.push_back(static_cast<uint16_t>(ringStart + s));
        }
    }

    // Bottom cap
    {
        auto center = static_cast<uint16_t>(verts.size());
        Vertex cv;
        cv.pos[0] = 0.0f; cv.pos[1] = -halfH; cv.pos[2] = 0.0f;
        cv.nrm[0] = 0.0f; cv.nrm[1] = -1.0f;  cv.nrm[2] = 0.0f;
        verts.push_back(cv);
        auto ringStart = static_cast<uint16_t>(verts.size());
        for (int s = 0; s <= kSegs; ++s) {
            float theta = 2.0f * k_Pi * static_cast<float>(s) / static_cast<float>(kSegs);
            Vertex v;
            v.pos[0] = std::cos(theta) * radius;
            v.pos[1] = -halfH;
            v.pos[2] = std::sin(theta) * radius;
            v.nrm[0] = 0.0f; v.nrm[1] = -1.0f; v.nrm[2] = 0.0f;
            verts.push_back(v);
        }
        for (int s = 0; s < kSegs; ++s) {
            indices.push_back(center);
            indices.push_back(static_cast<uint16_t>(ringStart + s));
            indices.push_back(static_cast<uint16_t>(ringStart + s + 1));
        }
    }
}

static void GenerateTorusMesh(std::vector<Vertex>& verts, std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    constexpr int kMajorSegs = 24;
    constexpr int kMinorSegs = 12;
    const float majorR = 0.7f;
    const float minorR = 0.3f;

    for (int i = 0; i <= kMajorSegs; ++i) {
        float phi = 2.0f * k_Pi * static_cast<float>(i) / static_cast<float>(kMajorSegs);
        float cp = std::cos(phi);
        float sp = std::sin(phi);

        for (int j = 0; j <= kMinorSegs; ++j) {
            float theta = 2.0f * k_Pi * static_cast<float>(j) / static_cast<float>(kMinorSegs);
            float ct = std::cos(theta);
            float st = std::sin(theta);

            // Position on the torus surface
            float r = majorR + minorR * ct;
            Vertex v;
            v.pos[0] = r * cp;
            v.pos[1] = minorR * st;
            v.pos[2] = r * sp;

            // Normal = normalized (pos - center of tube ring)
            // Center of tube ring at this phi: (majorR*cp, 0, majorR*sp)
            v.nrm[0] = ct * cp;
            v.nrm[1] = st;
            v.nrm[2] = ct * sp;
            verts.push_back(v);
        }
    }

    for (int i = 0; i < kMajorSegs; ++i) {
        for (int j = 0; j < kMinorSegs; ++j) {
            auto i0 = static_cast<uint16_t>(i * (kMinorSegs + 1) + j);
            auto i1 = static_cast<uint16_t>(i0 + 1);
            auto i2 = static_cast<uint16_t>((i + 1) * (kMinorSegs + 1) + j);
            auto i3 = static_cast<uint16_t>(i2 + 1);
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
}

// ─── upload mesh to GPU ──────────────────────────────────────────────────────

// Uploads vertex + index data through an upload buffer, then copies to default heap.
// Uses a temporary command list + fence wait for simplicity (startup only).
static void UploadMesh(const std::vector<Vertex>& verts, const std::vector<uint16_t>& indices,
                       ComPtr<ID3D12Resource>& outVB, D3D12_VERTEX_BUFFER_VIEW& outVBV,
                       ComPtr<ID3D12Resource>& outIB, D3D12_INDEX_BUFFER_VIEW& outIBV,
                       UINT& outIndexCount)
{
    const UINT64 vbSize = verts.size() * sizeof(Vertex);
    const UINT64 ibSize = indices.size() * sizeof(uint16_t);

    // Create default-heap buffers
    outVB = CreateDefaultBuffer(vbSize);
    outIB = CreateDefaultBuffer(ibSize);

    // Create upload buffers
    auto uploadVB = CreateUploadBuffer(vbSize);
    auto uploadIB = CreateUploadBuffer(ibSize);

    // Map and copy data to upload buffers
    void* mappedVB = nullptr;
    void* mappedIB = nullptr;
    UZ_CHK("Map VB upload", uploadVB->Map(0, nullptr, &mappedVB));
    std::memcpy(mappedVB, verts.data(), static_cast<size_t>(vbSize));
    uploadVB->Unmap(0, nullptr);

    UZ_CHK("Map IB upload", uploadIB->Map(0, nullptr, &mappedIB));
    std::memcpy(mappedIB, indices.data(), static_cast<size_t>(ibSize));
    uploadIB->Unmap(0, nullptr);

    // Use a temporary allocator + list for the upload copy
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    UZ_CHK("tmp allocator",
        g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc)));

    ComPtr<ID3D12GraphicsCommandList> tmpList;
    UZ_CHK("tmp cmd list",
        g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList)));

    tmpList->CopyBufferRegion(outVB.Get(), 0, uploadVB.Get(), 0, vbSize);
    tmpList->CopyBufferRegion(outIB.Get(), 0, uploadIB.Get(), 0, ibSize);

    // Transition default buffers from COPY_DEST to appropriate read states
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource   = outVB.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource   = outIB.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    tmpList->ResourceBarrier(2, barriers);
    UZ_CHK("tmp close", tmpList->Close());

    ID3D12CommandList* lists[] = { tmpList.Get() };
    g_app.cmdQueue->ExecuteCommandLists(1, lists);

    // Wait for copy to complete
    ComPtr<ID3D12Fence> tmpFence;
    UZ_CHK("tmp fence", g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence)));
    HANDLE tmpEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UZ_CHK("tmp signal", g_app.cmdQueue->Signal(tmpFence.Get(), 1));
    if (tmpFence->GetCompletedValue() < 1)
    {
        tmpFence->SetEventOnCompletion(1, tmpEvent);
        WaitForSingleObjectEx(tmpEvent, 2000, FALSE);
    }
    CloseHandle(tmpEvent);

    // Set up views
    outVBV.BufferLocation = outVB->GetGPUVirtualAddress();
    outVBV.SizeInBytes    = static_cast<UINT>(vbSize);
    outVBV.StrideInBytes  = sizeof(Vertex);

    outIBV.BufferLocation = outIB->GetGPUVirtualAddress();
    outIBV.SizeInBytes    = static_cast<UINT>(ibSize);
    outIBV.Format         = DXGI_FORMAT_R16_UINT;

    outIndexCount = static_cast<UINT>(indices.size());
}

// ─── pipeline creation ───────────────────────────────────────────────────────

static void CreateRootSignature()
{
    // Root param 0: CBV at b0 (visibility = all stages)
    D3D12_ROOT_PARAMETER1 params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    // Root param 1: Descriptor table with 2 SRVs (t0 = shadow map, t1 = HDR texture)
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 2;
    srvRange.BaseShaderRegister                = 0; // t0
    srvRange.RegisterSpace                     = 0;
    srvRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler s0: shadow comparison sampler (PCF)
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
    staticSamplers[0].Filter           = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[0].ComparisonFunc   = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[0].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[0].ShaderRegister   = 0; // s0
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler s1: point sampler for tonemap (HDR texture read)
    staticSamplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    staticSamplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSamplers[1].ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[1].ShaderRegister   = 1; // s1
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
    desc.Version                    = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters     = _countof(params);
    desc.Desc_1_1.pParameters       = params;
    desc.Desc_1_1.NumStaticSamplers = _countof(staticSamplers);
    desc.Desc_1_1.pStaticSamplers   = staticSamplers;
    desc.Desc_1_1.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    UZ_CHK("SerializeRootSignature",
        D3D12SerializeVersionedRootSignature(&desc, &blob, &errors));
    UZ_CHK("CreateRootSignature",
        g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&g_app.rootSig)));
}

static void CreatePipelines()
{
    // Compile all shaders
    auto skyVSBlob      = CompileShader(k_SkyVS,      sizeof(k_SkyVS) - 1,      "vs_5_0", "SkyVS");
    auto skyPSBlob      = CompileShader(k_SkyPS,      sizeof(k_SkyPS) - 1,      "ps_5_0", "SkyPS");
    auto sceneVSBlob    = CompileShader(k_SceneVS,    sizeof(k_SceneVS) - 1,    "vs_5_0", "SceneVS");
    auto gridPSBlob     = CompileShader(k_GridPS,     sizeof(k_GridPS) - 1,     "ps_5_0", "GridPS");
    auto spherePSBlob   = CompileShader(k_SpherePS,   sizeof(k_SpherePS) - 1,   "ps_5_0", "SpherePBR");
    auto shadowVSBlob   = CompileShader(k_ShadowVS,   sizeof(k_ShadowVS) - 1,   "vs_5_0", "ShadowVS");
    auto tonemapPSBlob  = CompileShader(k_TonemapPS,  sizeof(k_TonemapPS) - 1,  "ps_5_0", "TonemapPS");
    auto wireframePSBlob = CompileShader(k_WireframePS, sizeof(k_WireframePS) - 1, "ps_5_0", "WireframePS");

    // Input layout for scene geometry (position + normal)
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // ── Sky PSO: fullscreen triangle, no input layout, no depth write ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {skyVSBlob->GetBufferPointer(), skyVSBlob->GetBufferSize()};
        pd.PS = {skyPSBlob->GetBufferPointer(), skyPSBlob->GetBufferSize()};

        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = FALSE; // sky writes depth via SV_Depth

        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Sky writes depth = 0.0 (reverse-Z far), depth test passes with GREATER_EQUAL
        // so all geometry (depth > 0.0) will be in front.
        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

        pd.SampleMask          = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets    = 1;
        pd.RTVFormats[0]       = k_HdrFormat; // renders to HDR target, not backbuffer
        pd.DSVFormat           = k_DsvFormat;
        pd.SampleDesc.Count    = 1;

        UZ_CHK("CreateSkyPSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.skyPSO)));
    }

    // ── Grid PSO: fullscreen triangle (sky VS), ray/plane intersection, writes depth ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {skyVSBlob->GetBufferPointer(), skyVSBlob->GetBufferSize()};
        pd.PS = {gridPSBlob->GetBufferPointer(), gridPSBlob->GetBufferSize()};

        // No input layout — fullscreen triangle from SV_VertexID

        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = FALSE; // grid writes depth via SV_Depth

        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // reverse-Z

        pd.SampleMask          = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets    = 1;
        pd.RTVFormats[0]       = k_HdrFormat; // renders to HDR target, not backbuffer
        pd.DSVFormat           = k_DsvFormat;
        pd.SampleDesc.Count    = 1;

        UZ_CHK("CreateGridPSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.gridPSO)));
    }

    // ── Sphere PSO: same as ground but with PBR pixel shader ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {sceneVSBlob->GetBufferPointer(), sceneVSBlob->GetBufferSize()};
        pd.PS = {spherePSBlob->GetBufferPointer(), spherePSBlob->GetBufferSize()};

        pd.InputLayout.pInputElementDescs = inputLayout;
        pd.InputLayout.NumElements        = _countof(inputLayout);

        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_BACK;
        pd.RasterizerState.FrontCounterClockwise = TRUE;
        pd.RasterizerState.DepthClipEnable = TRUE;

        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // reverse-Z

        pd.SampleMask          = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets    = 1;
        pd.RTVFormats[0]       = k_HdrFormat; // renders to HDR target, not backbuffer
        pd.DSVFormat           = k_DsvFormat;
        pd.SampleDesc.Count    = 1;

        UZ_CHK("CreateSpherePSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.spherePSO)));
    }

    // ── Shadow PSO: depth-only, front-face cull to reduce acne, standard Z ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {shadowVSBlob->GetBufferPointer(), shadowVSBlob->GetBufferSize()};
        // No pixel shader — depth-only pass

        pd.InputLayout.pInputElementDescs = inputLayout;
        pd.InputLayout.NumElements        = _countof(inputLayout);

        pd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode              = D3D12_CULL_MODE_FRONT; // cull front faces to reduce shadow acne
        pd.RasterizerState.FrontCounterClockwise = TRUE;
        pd.RasterizerState.DepthClipEnable       = TRUE;
        pd.RasterizerState.DepthBias             = 100;
        pd.RasterizerState.DepthBiasClamp        = 0.0f;
        pd.RasterizerState.SlopeScaledDepthBias  = 1.5f;

        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL; // standard Z for shadow map

        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 0; // no color output
        pd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        pd.SampleDesc.Count      = 1;

        UZ_CHK("CreateShadowPSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.shadowPSO)));
    }

    // ── Tonemap PSO: fullscreen triangle, no depth, writes to R8G8B8A8 backbuffer ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {skyVSBlob->GetBufferPointer(), skyVSBlob->GetBufferSize()}; // reuse fullscreen triangle VS
        pd.PS = {tonemapPSBlob->GetBufferPointer(), tonemapPSBlob->GetBufferSize()};

        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = FALSE;

        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pd.DepthStencilState.DepthEnable = FALSE; // no depth test for fullscreen post-process

        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = k_RtvFormat; // R8G8B8A8 backbuffer
        pd.SampleDesc.Count      = 1;

        UZ_CHK("CreateTonemapPSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.tonemapPSO)));
    }

    // ── Wireframe PSO: same as sphere but wireframe fill, no backface culling, green overlay ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.rootSig.Get();
        pd.VS = {sceneVSBlob->GetBufferPointer(), sceneVSBlob->GetBufferSize()};
        pd.PS = {wireframePSBlob->GetBufferPointer(), wireframePSBlob->GetBufferSize()};

        pd.InputLayout.pInputElementDescs = inputLayout;
        pd.InputLayout.NumElements        = _countof(inputLayout);

        pd.RasterizerState.FillMode              = D3D12_FILL_MODE_WIREFRAME;
        pd.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.FrontCounterClockwise = TRUE;
        pd.RasterizerState.DepthClipEnable       = TRUE;

        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // don't write depth — overlay on top
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL; // reverse-Z

        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = k_HdrFormat;
        pd.DSVFormat             = k_DsvFormat;
        pd.SampleDesc.Count      = 1;

        UZ_CHK("CreateWireframePSO", g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.wireframePSO)));
    }
}

// ─── VFX buffer creation helper ──────────────────────────────────────────────

static ComPtr<ID3D12Resource> CreateUAVBuffer(UINT64 size, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> buf;
    UZ_CHK("CreateUAVBuffer",
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            initialState, nullptr, IID_PPV_ARGS(&buf)));
    return buf;
}

// ─── VFX resource creation ──────────────────────────────────────────────────

static void CreateVFXResources()
{
    // Per-particle element sizes for each SoA channel
    static constexpr UINT k_ChannelStrides[k_VFXChannelCount] = {
        12, // positions:  float3
        12, // velocities: float3
        4,  // ages:       float
        4,  // lifespans:  float
        4,  // sizes:      float
        16, // colors:     float4
    };

    // Create 2 sets of particle SoA buffers for ping-pong
    for (UINT set = 0; set < 2; ++set)
    {
        for (UINT ch = 0; ch < k_VFXChannelCount; ++ch)
        {
            UINT64 bufSize = static_cast<UINT64>(k_VFXMaxParticles) * k_ChannelStrides[ch];
            g_app.vfxParticleBuffers[set][ch] = CreateUAVBuffer(bufSize);
        }
    }

    // Bookkeeping buffers
    g_app.vfxAliveCount   = CreateUAVBuffer(4);
    g_app.vfxFreeList     = CreateUAVBuffer(static_cast<UINT64>(k_VFXMaxParticles) * 4);
    g_app.vfxFreeListHead = CreateUAVBuffer(4);
    // Draw args (16 bytes) + Dispatch args (12 bytes) = 28 bytes
    g_app.vfxIndirectArgs = CreateUAVBuffer(28);

    // ── Initialize freeList with [0, 1, 2, ..., capacity-1] via upload buffer ──
    {
        UINT64 freeListSize = static_cast<UINT64>(k_VFXMaxParticles) * 4;
        auto uploadFreeList = CreateUploadBuffer(freeListSize);

        uint32_t* mapped = nullptr;
        UZ_CHK("Map freeList upload", uploadFreeList->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
        for (uint32_t i = 0; i < k_VFXMaxParticles; ++i)
            mapped[i] = i;
        uploadFreeList->Unmap(0, nullptr);

        // freeListHead = capacity * 4 (byte offset, since InterlockedAdd operates on bytes)
        auto uploadHead = CreateUploadBuffer(4);
        uint32_t* headMapped = nullptr;
        UZ_CHK("Map freeListHead upload", uploadHead->Map(0, nullptr, reinterpret_cast<void**>(&headMapped)));
        headMapped[0] = k_VFXMaxParticles * 4; // byte offset = capacity * sizeof(uint)
        uploadHead->Unmap(0, nullptr);

        // aliveCount = 0
        auto uploadAlive = CreateUploadBuffer(4);
        uint32_t* aliveMapped = nullptr;
        UZ_CHK("Map aliveCount upload", uploadAlive->Map(0, nullptr, reinterpret_cast<void**>(&aliveMapped)));
        aliveMapped[0] = 0;
        uploadAlive->Unmap(0, nullptr);

        // indirectArgs = {0, 1, 0, 0,  0, 1, 1} (draw: 0 verts, dispatch: 0 groups)
        auto uploadArgs = CreateUploadBuffer(28);
        uint32_t* argsMapped = nullptr;
        UZ_CHK("Map indirectArgs upload", uploadArgs->Map(0, nullptr, reinterpret_cast<void**>(&argsMapped)));
        argsMapped[0] = 0;  // VertexCountPerInstance
        argsMapped[1] = 1;  // InstanceCount
        argsMapped[2] = 0;  // StartVertexLocation
        argsMapped[3] = 0;  // StartInstanceLocation
        argsMapped[4] = 0;  // ThreadGroupCountX (dispatch)
        argsMapped[5] = 1;  // ThreadGroupCountY
        argsMapped[6] = 1;  // ThreadGroupCountZ
        uploadArgs->Unmap(0, nullptr);

        // Execute copies
        ComPtr<ID3D12CommandAllocator> tmpAlloc;
        UZ_CHK("VFX tmpAlloc",
            g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc)));

        ComPtr<ID3D12GraphicsCommandList> tmpList;
        UZ_CHK("VFX tmpList",
            g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList)));

        // Transition targets from UAV to COPY_DEST
        D3D12_RESOURCE_BARRIER preCopy[4] = {};
        for (int i = 0; i < 4; ++i) {
            preCopy[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopy[i].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            preCopy[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            preCopy[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        preCopy[0].Transition.pResource = g_app.vfxFreeList.Get();
        preCopy[1].Transition.pResource = g_app.vfxFreeListHead.Get();
        preCopy[2].Transition.pResource = g_app.vfxAliveCount.Get();
        preCopy[3].Transition.pResource = g_app.vfxIndirectArgs.Get();
        tmpList->ResourceBarrier(4, preCopy);

        tmpList->CopyBufferRegion(g_app.vfxFreeList.Get(), 0, uploadFreeList.Get(), 0, freeListSize);
        tmpList->CopyBufferRegion(g_app.vfxFreeListHead.Get(), 0, uploadHead.Get(), 0, 4);
        tmpList->CopyBufferRegion(g_app.vfxAliveCount.Get(), 0, uploadAlive.Get(), 0, 4);
        tmpList->CopyBufferRegion(g_app.vfxIndirectArgs.Get(), 0, uploadArgs.Get(), 0, 28);

        // Transition targets back to UAV
        D3D12_RESOURCE_BARRIER postCopy[4] = {};
        for (int i = 0; i < 4; ++i) {
            postCopy[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            postCopy[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            postCopy[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            postCopy[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        postCopy[0].Transition.pResource = g_app.vfxFreeList.Get();
        postCopy[1].Transition.pResource = g_app.vfxFreeListHead.Get();
        postCopy[2].Transition.pResource = g_app.vfxAliveCount.Get();
        postCopy[3].Transition.pResource = g_app.vfxIndirectArgs.Get();
        tmpList->ResourceBarrier(4, postCopy);

        UZ_CHK("VFX init close", tmpList->Close());

        ID3D12CommandList* lists[] = { tmpList.Get() };
        g_app.cmdQueue->ExecuteCommandLists(1, lists);

        ComPtr<ID3D12Fence> tmpFence;
        UZ_CHK("VFX init fence", g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence)));
        HANDLE tmpEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        UZ_CHK("VFX init signal", g_app.cmdQueue->Signal(tmpFence.Get(), 1));
        if (tmpFence->GetCompletedValue() < 1) {
            tmpFence->SetEventOnCompletion(1, tmpEvent);
            WaitForSingleObjectEx(tmpEvent, 2000, FALSE);
        }
        CloseHandle(tmpEvent);
    }

    // ── Create SRV/UAV descriptors in the expanded srvHeap ──
    // Particle buffer SRVs and UAVs
    static constexpr DXGI_FORMAT k_ChannelFormats[k_VFXChannelCount] = {
        DXGI_FORMAT_UNKNOWN, // positions:  struct stride 12
        DXGI_FORMAT_UNKNOWN, // velocities: struct stride 12
        DXGI_FORMAT_UNKNOWN, // ages:       struct stride 4
        DXGI_FORMAT_UNKNOWN, // lifespans:  struct stride 4
        DXGI_FORMAT_UNKNOWN, // sizes:      struct stride 4
        DXGI_FORMAT_UNKNOWN, // colors:     struct stride 16
    };

    for (UINT set = 0; set < 2; ++set)
    {
        UINT srvBase = (set == 0) ? k_VFXReadSetA_SRV : k_VFXReadSetB_SRV;
        UINT uavBase = (set == 0) ? k_VFXWriteSetA_UAV : k_VFXWriteSetB_UAV;

        for (UINT ch = 0; ch < k_VFXChannelCount; ++ch)
        {
            UINT stride = (ch == 0 || ch == 1) ? 12 : (ch == 5 ? 16 : 4);

            // SRV (StructuredBuffer)
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                  = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement     = 0;
            srvDesc.Buffer.NumElements      = k_VFXMaxParticles;
            srvDesc.Buffer.StructureByteStride = stride;
            g_device->CreateShaderResourceView(
                g_app.vfxParticleBuffers[set][ch].Get(), &srvDesc,
                SrvCpuHandle(srvBase + ch));

            // UAV (RWStructuredBuffer)
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format               = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension        = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.FirstElement  = 0;
            uavDesc.Buffer.NumElements   = k_VFXMaxParticles;
            uavDesc.Buffer.StructureByteStride = stride;
            g_device->CreateUnorderedAccessView(
                g_app.vfxParticleBuffers[set][ch].Get(), nullptr, &uavDesc,
                SrvCpuHandle(uavBase + ch));
        }
    }

    // Bookkeeping buffer descriptors
    // aliveCount (RWByteAddressBuffer — raw buffer)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format              = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements  = 1;
        uavDesc.Buffer.Flags        = D3D12_BUFFER_UAV_FLAG_RAW;
        g_device->CreateUnorderedAccessView(
            g_app.vfxAliveCount.Get(), nullptr, &uavDesc,
            SrvCpuHandle(k_VFXBookkeepingUAV + 0));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement     = 0;
        srvDesc.Buffer.NumElements      = 1;
        srvDesc.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_RAW;
        g_device->CreateShaderResourceView(
            g_app.vfxAliveCount.Get(), &srvDesc,
            SrvCpuHandle(k_VFXBookkeepingSRV + 0));
    }

    // freeList (RWStructuredBuffer<uint>)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format               = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension        = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement  = 0;
        uavDesc.Buffer.NumElements   = k_VFXMaxParticles;
        uavDesc.Buffer.StructureByteStride = 4;
        g_device->CreateUnorderedAccessView(
            g_app.vfxFreeList.Get(), nullptr, &uavDesc,
            SrvCpuHandle(k_VFXBookkeepingUAV + 1));
    }

    // freeListHead (RWByteAddressBuffer)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format              = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements  = 1;
        uavDesc.Buffer.Flags        = D3D12_BUFFER_UAV_FLAG_RAW;
        g_device->CreateUnorderedAccessView(
            g_app.vfxFreeListHead.Get(), nullptr, &uavDesc,
            SrvCpuHandle(k_VFXBookkeepingUAV + 2));
    }

    // indirectArgs (RWByteAddressBuffer — 28 bytes = 7 uints)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format              = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements  = 7;
        uavDesc.Buffer.Flags        = D3D12_BUFFER_UAV_FLAG_RAW;
        g_device->CreateUnorderedAccessView(
            g_app.vfxIndirectArgs.Get(), nullptr, &uavDesc,
            SrvCpuHandle(k_VFXBookkeepingUAV + 3));
    }

    // ── Per-frame VFX constant buffers (large enough for SpawnCB, UpdateCB, or SortCB) ──
    for (UINT i = 0; i < k_Frames; ++i)
    {
        // 256-byte aligned (enough for the larger SortCB at 64 bytes)
        g_app.vfxCbUpload[i] = CreateUploadBuffer(256);
        D3D12_RANGE readRange = {0, 0};
        UZ_CHK("Map VFX CB",
            g_app.vfxCbUpload[i]->Map(0, &readRange, reinterpret_cast<void**>(&g_app.vfxCbMapped[i])));
    }

    // ── Depth copy resource for soft particles (Task 9.6) ──
    // We cannot read D32_FLOAT as SRV while it's bound as DSV, so we create an R32_FLOAT
    // copy that receives CopyResource from the depth buffer between scene and particle passes.
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = k_Width;
        rd.Height             = k_Height;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = DXGI_FORMAT_R32_FLOAT;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

        UZ_CHK("CreateVFXDepthCopy",
            g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&g_app.vfxDepthCopy)));

        // Create SRV at the dedicated slot
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        g_device->CreateShaderResourceView(
            g_app.vfxDepthCopy.Get(), &srvDesc, SrvCpuHandle(k_VFXDepthCopySRV));
    }

    // ── Sort buffers for bitonic merge sort (Task 9.7) ──
    // Padded to next power of 2 >= max particles for bitonic sort correctness.
    {
        uint32_t n = k_VFXMaxParticles;
        uint32_t padded = 1;
        while (padded < n) padded <<= 1;
        g_app.vfxSortPaddedCount = padded;

        UINT64 sortBufSize = static_cast<UINT64>(padded) * sizeof(uint32_t);
        g_app.vfxSortKeys    = CreateUAVBuffer(sortBufSize);
        g_app.vfxSortIndices = CreateUAVBuffer(sortBufSize);

        // SRV for sort indices (read by billboard VS)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements      = padded;
        srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        g_device->CreateShaderResourceView(
            g_app.vfxSortIndices.Get(), &srvDesc, SrvCpuHandle(k_VFXSortIndicesSRV));

        // SRV for sort keys (read by billboard VS — not strictly needed but good for debug)
        g_device->CreateShaderResourceView(
            g_app.vfxSortKeys.Get(), &srvDesc, SrvCpuHandle(k_VFXSortKeysSRV));

        // UAV for sort keys
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension         = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements    = padded;
        uavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        g_device->CreateUnorderedAccessView(
            g_app.vfxSortKeys.Get(), nullptr, &uavDesc, SrvCpuHandle(k_VFXSortKeysUAV));

        // UAV for sort indices
        g_device->CreateUnorderedAccessView(
            g_app.vfxSortIndices.Get(), nullptr, &uavDesc, SrvCpuHandle(k_VFXSortIndicesUAV));

        // Initialize sort indices to identity (0, 1, 2, ... N-1) via upload buffer
        ComPtr<ID3D12Resource> uploadSortIdx = CreateUploadBuffer(sortBufSize);
        {
            uint32_t* mapped = nullptr;
            D3D12_RANGE readRange = {0, 0};
            UZ_CHK("Map sort idx init",
                uploadSortIdx->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
            for (uint32_t i = 0; i < padded; ++i)
                mapped[i] = i;
            uploadSortIdx->Unmap(0, nullptr);
        }

        // Copy via temporary command list
        ComPtr<ID3D12CommandAllocator> tmpAlloc;
        ComPtr<ID3D12GraphicsCommandList> tmpList;
        UZ_CHK("Sort init alloc",
            g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc)));
        UZ_CHK("Sort init list",
            g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList)));

        D3D12_RESOURCE_BARRIER pre = {};
        pre.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        pre.Transition.pResource   = g_app.vfxSortIndices.Get();
        pre.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        pre.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        pre.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        tmpList->ResourceBarrier(1, &pre);

        tmpList->CopyBufferRegion(g_app.vfxSortIndices.Get(), 0, uploadSortIdx.Get(), 0, sortBufSize);

        D3D12_RESOURCE_BARRIER post = {};
        post.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        post.Transition.pResource   = g_app.vfxSortIndices.Get();
        post.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        post.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        post.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        tmpList->ResourceBarrier(1, &post);

        UZ_CHK("Sort init close", tmpList->Close());

        ID3D12CommandList* lists[] = { tmpList.Get() };
        g_app.cmdQueue->ExecuteCommandLists(1, lists);

        ComPtr<ID3D12Fence> tmpFence;
        UZ_CHK("Sort init fence", g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence)));
        UZ_CHK("Sort init signal", g_app.cmdQueue->Signal(tmpFence.Get(), 1));
        if (tmpFence->GetCompletedValue() < 1) {
            HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            tmpFence->SetEventOnCompletion(1, evt);
            WaitForSingleObject(evt, INFINITE);
            CloseHandle(evt);
        }
    }
}

// ─── VFX compute + graphics pipeline creation ────────────────────────────────

static void CreateVFXPipelines()
{
    // ── Compute root signature ──
    // Param 0: CBV at b0
    // Param 1: Descriptor table — UAVs (variable count, bound at u0+)
    {
        D3D12_ROOT_PARAMETER1 params[2] = {};

        // CBV
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        // UAV descriptor table — large range to cover all compute UAVs
        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors                    = 16; // enough for any compute pass
        uavRange.BaseShaderRegister                = 0;  // u0
        uavRange.RegisterSpace                     = 0;
        uavRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &uavRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
        desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = _countof(params);
        desc.Desc_1_1.pParameters   = params;
        desc.Desc_1_1.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> blob, errors;
        UZ_CHK("VFX compute root sig serialize",
            D3D12SerializeVersionedRootSignature(&desc, &blob, &errors));
        UZ_CHK("VFX compute root sig create",
            g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                IID_PPV_ARGS(&g_app.vfxComputeRootSig)));
    }

    // ── Billboard graphics root signature ──
    // Param 0: CBV at b0 (PerFrameCB)
    // Param 1: Descriptor table — SRVs (t0-t2: positions, sizes, colors)
    {
        D3D12_ROOT_PARAMETER1 params[2] = {};

        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE1 srvRange = {};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 5; // t0=positions, t1=sizes, t2=colors, t3=depthCopy, t4=sortIndices
        srvRange.BaseShaderRegister                = 0; // t0
        srvRange.RegisterSpace                     = 0;
        srvRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
        desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = _countof(params);
        desc.Desc_1_1.pParameters   = params;
        desc.Desc_1_1.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> blob, errors;
        UZ_CHK("VFX gfx root sig serialize",
            D3D12SerializeVersionedRootSignature(&desc, &blob, &errors));
        UZ_CHK("VFX gfx root sig create",
            g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                IID_PPV_ARGS(&g_app.vfxGraphicsRootSig)));
    }

    // ── Compile compute shaders (cs_5_1 for >8 UAV support) ──
    auto spawnCSBlob = CompileShader(k_ParticleSpawnCS,
        sizeof(k_ParticleSpawnCS) - 1, "cs_5_1", "ParticleSpawnCS");
    auto updateCSBlob = CompileShader(k_ParticleUpdateCS,
        sizeof(k_ParticleUpdateCS) - 1, "cs_5_1", "ParticleUpdateCS");
    auto indirectCSBlob = CompileShader(k_ParticleIndirectArgsCS,
        sizeof(k_ParticleIndirectArgsCS) - 1, "cs_5_1", "ParticleIndirectArgsCS");

    // ── Compile billboard shaders ──
    auto billboardVSBlob = CompileShader(k_ParticleBillboardVS,
        sizeof(k_ParticleBillboardVS) - 1, "vs_5_1", "ParticleBillboardVS");
    auto billboardPSBlob = CompileShader(k_ParticleBillboardPS,
        sizeof(k_ParticleBillboardPS) - 1, "ps_5_1", "ParticleBillboardPS");

    // ── Spawn compute PSO ──
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxComputeRootSig.Get();
        pd.CS = {spawnCSBlob->GetBufferPointer(), spawnCSBlob->GetBufferSize()};
        UZ_CHK("CreateVFXSpawnPSO",
            g_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_app.vfxSpawnPSO)));
    }

    // ── Update compute PSO ──
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxComputeRootSig.Get();
        pd.CS = {updateCSBlob->GetBufferPointer(), updateCSBlob->GetBufferSize()};
        UZ_CHK("CreateVFXUpdatePSO",
            g_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_app.vfxUpdatePSO)));
    }

    // ── Indirect args compute PSO ──
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxComputeRootSig.Get();
        pd.CS = {indirectCSBlob->GetBufferPointer(), indirectCSBlob->GetBufferSize()};
        UZ_CHK("CreateVFXIndirectArgsPSO",
            g_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_app.vfxIndirectArgsPSO)));
    }

    // ── Billboard graphics PSO ──
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxGraphicsRootSig.Get();
        pd.VS = {billboardVSBlob->GetBufferPointer(), billboardVSBlob->GetBufferSize()};
        pd.PS = {billboardPSBlob->GetBufferPointer(), billboardPSBlob->GetBufferSize()};

        // No input layout — vertex-less rendering from SV_VertexID

        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;

        // Alpha blending: SrcAlpha + InvSrcAlpha (pre-multiplied would be SrcAlpha, One)
        pd.BlendState.RenderTarget[0].BlendEnable    = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Depth test enabled (reverse-Z), depth write disabled (transparent)
        pd.DepthStencilState.DepthEnable    = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_GREATER_EQUAL;

        pd.SampleMask            = UINT_MAX;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = k_HdrFormat;
        pd.DSVFormat             = k_DsvFormat;
        pd.SampleDesc.Count      = 1;

        UZ_CHK("CreateVFXBillboardPSO",
            g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_app.vfxBillboardPSO)));
    }

    // ── Command signature for ExecuteIndirect (DrawInstanced) ──
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC csDesc = {};
        csDesc.ByteStride       = 16; // 4 uints: VertexCountPerInstance, InstanceCount, StartVertex, StartInstance
        csDesc.NumArgumentDescs = 1;
        csDesc.pArgumentDescs   = &argDesc;

        UZ_CHK("CreateVFXDrawCmdSig",
            g_device->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&g_app.vfxDrawCmdSig)));
    }

    // ── Sort compute root signature + PSOs (Task 9.7) ──
    // Param 0: CBV at b0 (SortCB)
    // Param 1: Descriptor table — SRVs (t0: positions, used by local sort for key gen)
    // Param 2: Descriptor table — UAVs (u0: sortKeys, u1: sortIndices)
    {
        D3D12_ROOT_PARAMETER1 params[3] = {};

        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace  = 0;
        params[0].Descriptor.Flags          = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE1 srvRange = {};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1; // t0: positions
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors                    = 2; // u0: sortKeys, u1: sortIndices
        uavRange.BaseShaderRegister                = 0;
        uavRange.RegisterSpace                     = 0;
        uavRange.Flags                             = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges   = &uavRange;
        params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
        desc.Version                = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = _countof(params);
        desc.Desc_1_1.pParameters   = params;
        desc.Desc_1_1.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> blob, errors;
        UZ_CHK("VFX sort root sig serialize",
            D3D12SerializeVersionedRootSignature(&desc, &blob, &errors));
        UZ_CHK("VFX sort root sig create",
            g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                IID_PPV_ARGS(&g_app.vfxSortRootSig)));
    }

    // Compile sort shaders
    auto sortLocalBlob = CompileShader(k_ParticleSortLocalCS,
        sizeof(k_ParticleSortLocalCS) - 1, "cs_5_1", "ParticleSortLocalCS");
    auto sortGlobalBlob = CompileShader(k_ParticleSortGlobalCS,
        sizeof(k_ParticleSortGlobalCS) - 1, "cs_5_1", "ParticleSortGlobalCS");

    // Sort local PSO
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxSortRootSig.Get();
        pd.CS = {sortLocalBlob->GetBufferPointer(), sortLocalBlob->GetBufferSize()};
        UZ_CHK("CreateVFXSortLocalPSO",
            g_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_app.vfxSortLocalPSO)));
    }

    // Sort global PSO
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_app.vfxSortRootSig.Get();
        pd.CS = {sortGlobalBlob->GetBufferPointer(), sortGlobalBlob->GetBufferSize()};
        UZ_CHK("CreateVFXSortGlobalPSO",
            g_device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_app.vfxSortGlobalPSO)));
    }
}

// ─── initialization ──────────────────────────────────────────────────────────

static void InitDX12()
{
    ComPtr<IDXGIFactory6> factory;
    UZ_CHK("CreateDXGIFactory2", CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

    // Try hardware adapter, fall back to WARP
    ComPtr<IDXGIAdapter1> adapter;
    HRESULT hrEnum = factory->EnumAdapterByGpuPreference(
        0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));

    if (SUCCEEDED(hrEnum))
    {
        HRESULT hrDev = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device));
        if (FAILED(hrDev))
        {
            adapter.Reset();
            g_device.Reset();
        }
    }

    if (!g_device)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        UZ_CHK("EnumWarpAdapter", factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
        UZ_CHK("D3D12CreateDevice(WARP)",
            D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));
    }

    // Log adapter info
    {
        DXGI_ADAPTER_DESC1 desc = {};
        if (adapter && SUCCEEDED(adapter->GetDesc1(&desc)))
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[uz_viewport] GPU: %ls\n", desc.Description);
            OutputDebugStringA(buf);
        }
    }

    // Command queue
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    UZ_CHK("CreateCommandQueue", g_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_app.cmdQueue)));

    // Swapchain
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount      = k_Frames;
    scd.Width            = k_Width;
    scd.Height           = k_Height;
    scd.Format           = k_RtvFormat;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    UZ_CHK("CreateSwapChainForHwnd",
        factory->CreateSwapChainForHwnd(g_app.cmdQueue.Get(), g_app.hwnd, &scd, nullptr, nullptr, &sc1));
    factory->MakeWindowAssociation(g_app.hwnd, DXGI_MWA_NO_ALT_ENTER);
    UZ_CHK("QueryInterface IDXGISwapChain3", sc1.As(&g_app.swapchain));
    g_app.frameIndex = g_app.swapchain->GetCurrentBackBufferIndex();

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHD = {};
    rtvHD.NumDescriptors = k_Frames;
    rtvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    UZ_CHK("CreateRTVHeap", g_device->CreateDescriptorHeap(&rtvHD, IID_PPV_ARGS(&g_app.rtvHeap)));
    g_app.rtvStride = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHD = {};
    dsvHD.NumDescriptors = 1;
    dsvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    UZ_CHK("CreateDSVHeap", g_device->CreateDescriptorHeap(&dsvHD, IID_PPV_ARGS(&g_app.dsvHeap)));

    // Create RTVs and command allocators per frame
    for (UINT i = 0; i < k_Frames; ++i)
    {
        UZ_CHK("GetBuffer", g_app.swapchain->GetBuffer(i, IID_PPV_ARGS(&g_app.backbuffers[i])));
        g_device->CreateRenderTargetView(g_app.backbuffers[i].Get(), nullptr, RtvHandle(i));
        UZ_CHK("CreateCommandAllocator",
            g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_app.cmdAllocs[i])));
    }

    // Depth buffer (D32_FLOAT, reverse-Z clear = 0.0)
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = k_Width;
        rd.Height             = k_Height;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = k_DsvFormat;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format               = k_DsvFormat;
        cv.DepthStencil.Depth   = 0.0f; // reverse-Z: far = 0.0
        cv.DepthStencil.Stencil = 0;

        UZ_CHK("CreateDepthBuffer",
            g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&g_app.depthBuffer)));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = k_DsvFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        g_device->CreateDepthStencilView(g_app.depthBuffer.Get(), &dsvDesc, DsvHandle());
    }

    // ── Shadow map (2048x2048 D32_FLOAT) ──
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = k_ShadowMapSize;
        rd.Height             = k_ShadowMapSize;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = DXGI_FORMAT_R32_TYPELESS; // typeless for DSV (D32) + SRV (R32)
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format               = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth   = 1.0f; // standard Z: far = 1.0
        cv.DepthStencil.Stencil = 0;

        UZ_CHK("CreateShadowMap",
            g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&g_app.shadowMap)));

        // Shadow DSV heap
        D3D12_DESCRIPTOR_HEAP_DESC shadowDsvHD = {};
        shadowDsvHD.NumDescriptors = 1;
        shadowDsvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        UZ_CHK("CreateShadowDSVHeap", g_device->CreateDescriptorHeap(&shadowDsvHD, IID_PPV_ARGS(&g_app.shadowDsvHeap)));

        D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc = {};
        shadowDsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        shadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        g_device->CreateDepthStencilView(g_app.shadowMap.Get(), &shadowDsvDesc, ShadowDsvHandle());
    }

    // ── HDR render target (RGBA16F at screen resolution) ──
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = k_Width;
        rd.Height             = k_Height;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cv.Color[0] = 0.0f; cv.Color[1] = 0.0f; cv.Color[2] = 0.0f; cv.Color[3] = 1.0f;

        UZ_CHK("CreateHDRTarget",
            g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&g_app.hdrTarget)));

        // HDR RTV heap (1 descriptor)
        D3D12_DESCRIPTOR_HEAP_DESC hdrRtvHD = {};
        hdrRtvHD.NumDescriptors = 1;
        hdrRtvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        UZ_CHK("CreateHDRRTVHeap", g_device->CreateDescriptorHeap(&hdrRtvHD, IID_PPV_ARGS(&g_app.hdrRtvHeap)));

        D3D12_RENDER_TARGET_VIEW_DESC hdrRtvDesc = {};
        hdrRtvDesc.Format        = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g_device->CreateRenderTargetView(g_app.hdrTarget.Get(), &hdrRtvDesc, HdrRtvHandle());
    }

    // ── Shader-visible SRV heap (slot 0 = shadow map, slot 1 = HDR texture) ──
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHD = {};
        srvHD.NumDescriptors = k_VFXSrvHeapSize; // expanded for VFX (slots 0-1 existing, 2+ VFX)
        srvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHD.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        UZ_CHK("CreateSRVHeap", g_device->CreateDescriptorHeap(&srvHD, IID_PPV_ARGS(&g_app.srvHeap)));
        g_app.srvStride = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Slot 0: Shadow map SRV (R32_FLOAT view of the typeless D32 texture)
        D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
        shadowSrvDesc.Format                  = DXGI_FORMAT_R32_FLOAT;
        shadowSrvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadowSrvDesc.Texture2D.MipLevels     = 1;
        g_device->CreateShaderResourceView(g_app.shadowMap.Get(), &shadowSrvDesc, SrvCpuHandle(0));

        // Slot 1: HDR texture SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrvDesc = {};
        hdrSrvDesc.Format                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
        hdrSrvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        hdrSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        hdrSrvDesc.Texture2D.MipLevels     = 1;
        g_device->CreateShaderResourceView(g_app.hdrTarget.Get(), &hdrSrvDesc, SrvCpuHandle(1));
    }

    // Command list
    UZ_CHK("CreateCommandList",
        g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_app.cmdAllocs[g_app.frameIndex].Get(), nullptr, IID_PPV_ARGS(&g_app.cmdList)));
    g_app.cmdList->Close();

    // Fence
    UZ_CHK("CreateFence", g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_app.fence)));
    g_app.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_app.fenceEvent) FatalHR("CreateEvent", HRESULT_FROM_WIN32(GetLastError()));

    // Per-frame constant buffers (upload heap, persistently mapped)
    for (UINT i = 0; i < k_Frames; ++i)
    {
        // CB size must be 256-byte aligned
        const UINT64 cbAligned = (sizeof(PerFrameCB) + 255) & ~255ULL;
        g_app.cbUpload[i] = CreateUploadBuffer(cbAligned);

        D3D12_RANGE readRange = {0, 0}; // we never read from GPU
        UZ_CHK("Map CB",
            g_app.cbUpload[i]->Map(0, &readRange, reinterpret_cast<void**>(&g_app.cbMapped[i])));
    }
}

static void InitAssets()
{
    CreateRootSignature();
    CreatePipelines();

    // VFX system: create buffers first (needs srvHeap), then pipelines
    CreateVFXResources();
    CreateVFXPipelines();

    // Generate and upload all meshes into the mesh registry
    using GenFunc = void(*)(std::vector<Vertex>&, std::vector<uint16_t>&);
    struct MeshGen { GenFunc fn; const char* name; };
    const MeshGen generators[k_MeshCount] = {
        {GenerateSphereMesh,   "Sphere"},
        {GenerateCubeMesh,     "Cube"},
        {GeneratePlaneMesh,    "Plane"},
        {GenerateCylinderMesh, "Cylinder"},
        {GenerateTorusMesh,    "Torus"},
    };

    for (int i = 0; i < k_MeshCount; ++i) {
        std::vector<Vertex> verts;
        std::vector<uint16_t> indices;
        generators[i].fn(verts, indices);
        UploadMesh(verts, indices,
            g_meshes[i].vb, g_meshes[i].vbv,
            g_meshes[i].ib, g_meshes[i].ibv,
            g_meshes[i].indexCount);
        g_meshes[i].name = generators[i].name;
    }
}

// ─── render frame ─────────────────────────────────────────────────────────────

static void RenderFrame()
{
    const UINT fi = g_app.frameIndex;
    WaitForFrame(fi);

    // Update constant buffer with camera, light, and timing data
    const float t = static_cast<float>(GetElapsedSeconds());
    const float aspect = static_cast<float>(k_Width) / static_cast<float>(k_Height);

    // Camera matrices from interactive camera
    Mat4 view = g_camera.GetViewMatrix();
    Mat4 proj = g_camera.GetProjectionMatrix(aspect);
    Mat4 viewProj = Mul(proj, view);
    Mat4 invViewProj = InvertMatrix(viewProj);

    // Light direction computed from sun elevation (time-of-day)
    float sunElRad = g_sunElevation * k_Pi / 180.0f;
    float sunAzRad = 0.3f; // fixed azimuth offset
    Vec3 lightDir = Normalize({
        std::cos(sunElRad) * std::sin(sunAzRad),
        std::sin(sunElRad),
        std::cos(sunElRad) * std::cos(sunAzRad)
    });

    // Compute shadow view-projection (orthographic from light direction)
    Mat4 shadowVP = ComputeShadowViewProj(lightDir);

    // Current material preset
    const MaterialPreset& mat = k_Materials[g_currentMaterial];

    // Fill constant buffer
    PerFrameCB cb = {};
    std::memcpy(cb.viewProj,       &viewProj.m[0][0],    sizeof(float) * 16);
    std::memcpy(cb.invViewProj,    &invViewProj.m[0][0],  sizeof(float) * 16);
    std::memcpy(cb.shadowViewProj, &shadowVP.m[0][0],     sizeof(float) * 16);
    cb.cameraPos[0] = g_camera.position.x;
    cb.cameraPos[1] = g_camera.position.y;
    cb.cameraPos[2] = g_camera.position.z;
    cb.lightDir[0]  = lightDir.x;
    cb.lightDir[1]  = lightDir.y;
    cb.lightDir[2]  = lightDir.z;
    cb.lightColor[0] = 3.0f;
    cb.lightColor[1] = 2.85f;
    cb.lightColor[2] = 2.7f;
    cb.time      = t;
    cb.nearPlane = g_camera.nearZ;
    cb.farPlane  = g_camera.farZ;
    // Material params
    cb.matBaseColor[0]     = mat.baseColor[0];
    cb.matBaseColor[1]     = mat.baseColor[1];
    cb.matBaseColor[2]     = mat.baseColor[2];
    cb.matMetallic         = mat.metallic;
    cb.matRoughness        = mat.roughness;
    cb.matAO               = mat.ao;
    cb.matEmissiveIntensity = mat.emissiveIntensity;
    cb.matEmissiveColor[0] = mat.emissiveColor[0];
    cb.matEmissiveColor[1] = mat.emissiveColor[1];
    cb.matEmissiveColor[2] = mat.emissiveColor[2];
    cb.sunElevation        = g_sunElevation;
    // Phase 5+6 params
    cb.shadowBias        = g_shadowBias;
    cb.exposure          = g_exposure;
    cb.vignetteIntensity = g_vignetteIntensity;
    cb.postProcessMode   = g_postProcessEnabled ? 1.0f : 0.0f;
    cb.debugViewMode     = static_cast<float>(static_cast<int>(g_debugView));
    std::memcpy(g_app.cbMapped[fi], &cb, sizeof(PerFrameCB));

    // Begin command recording
    UZ_CHK("Allocator::Reset", g_app.cmdAllocs[fi]->Reset());
    UZ_CHK("CmdList::Reset",   g_app.cmdList->Reset(g_app.cmdAllocs[fi].Get(), nullptr));

    auto* cl = g_app.cmdList.Get();

    // Bind root signature and CB (shared across all passes)
    cl->SetGraphicsRootSignature(g_app.rootSig.Get());
    cl->SetGraphicsRootConstantBufferView(0, g_app.cbUpload[fi]->GetGPUVirtualAddress());

    // Set shader-visible SRV heap (shared between scene and VFX passes)
    ID3D12DescriptorHeap* heaps[] = { g_app.srvHeap.Get() };
    cl->SetDescriptorHeaps(_countof(heaps), heaps);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(0));

    // ══════════════════════════════════════════════════════════════════════
    // VFX Compute Passes (before graphics — spawn, update, indirect args)
    // ══════════════════════════════════════════════════════════════════════
    if (g_vfxEnabled)
    {
        // Determine read/write sets based on ping-pong index
        const UINT readSetUAV  = (g_vfxPingPong == 0) ? k_VFXWriteSetA_UAV : k_VFXWriteSetB_UAV;
        const UINT writeSetUAV = (g_vfxPingPong == 0) ? k_VFXWriteSetB_UAV : k_VFXWriteSetA_UAV;

        // Accumulate spawn count from frame delta time
        g_vfxSpawnAccum += g_vfxSpawnRate * g_deltaTime;
        uint32_t spawnCount = static_cast<uint32_t>(g_vfxSpawnAccum);
        g_vfxSpawnAccum -= static_cast<float>(spawnCount);
        if (spawnCount > k_VFXMaxParticles)
            spawnCount = k_VFXMaxParticles;

        // Switch to compute root signature
        cl->SetComputeRootSignature(g_app.vfxComputeRootSig.Get());

        // ── Reset alive count to 0 before update pass ──
        // We need to clear aliveCount so the update shader can re-count living particles.
        // Use a copy from a zero-initialized upload buffer. But we can also just use
        // a single-thread compute shader... or simply rely on the spawn/update atomics.
        //
        // Actually: the architecture is:
        //   1. Spawn: adds newly spawned particles to WRITE set, increments aliveCount
        //   2. Update: reads from READ set (previous frame's live particles), writes survivors to WRITE set,
        //      increments newAliveCount, pushes dead to freeList
        //   3. IndirectArgs: reads aliveCount (from update), writes draw/dispatch args
        //
        // Problem: spawn writes to WRITE set, update ALSO writes to WRITE set.
        // They both atomically increment aliveCount.
        // So aliveCount after spawn+update = spawnedThisFrame + survivorsFromLastFrame. Correct!
        //
        // But we need to CLEAR aliveCount to 0 at the start of each frame.
        // Use a UAV clear (ClearUnorderedAccessViewUint).

        // Clear aliveCount to 0
        {
            UINT clearValues[4] = {0, 0, 0, 0};
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SrvGpuHandle(k_VFXBookkeepingUAV + 0);
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = SrvCpuHandle(k_VFXBookkeepingUAV + 0);
            cl->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle,
                g_app.vfxAliveCount.Get(), clearValues, 0, nullptr);

            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = g_app.vfxAliveCount.Get();
            cl->ResourceBarrier(1, &uavBarrier);
        }

        // ── Spawn pass ──
        if (spawnCount > 0)
        {
            VFXSpawnCB spawnCB = {};
            std::memcpy(spawnCB.emitterPos, g_vfxEmitter.position, 12);
            spawnCB.spawnRadius = g_vfxEmitter.spawnRadius;
            std::memcpy(spawnCB.spawnVelocity, g_vfxEmitter.spawnVelocity, 12);
            spawnCB.velocityVariation = g_vfxEmitter.velocityVariation;
            spawnCB.spreadAngle  = g_vfxEmitter.spreadAngle;
            spawnCB.minLifetime  = g_vfxEmitter.minLifetime;
            spawnCB.maxLifetime  = g_vfxEmitter.maxLifetime;
            spawnCB.initialSize  = g_vfxEmitter.initialSize;
            std::memcpy(spawnCB.initialColor, g_vfxEmitter.initialColor, 16);
            spawnCB.dt           = g_deltaTime;
            spawnCB.frameNumber  = g_totalFrames;
            spawnCB.spawnCount   = spawnCount;
            spawnCB.maxParticles = k_VFXMaxParticles;

            std::memcpy(g_app.vfxCbMapped[fi], &spawnCB, sizeof(VFXSpawnCB));

            cl->SetPipelineState(g_app.vfxSpawnPSO.Get());
            cl->SetComputeRootConstantBufferView(0, g_app.vfxCbUpload[fi]->GetGPUVirtualAddress());

            // Spawn shader UAV layout: u0-u5 = write set particle channels,
            // u6 = aliveCount, u7 = freeList, u8 = freeListHead
            // We bind the write set UAVs starting at the writeSetUAV offset in the heap,
            // but the shader expects u0-u8 contiguously. So we need a contiguous descriptor range.
            // Since our heap layout doesn't have them contiguous, we need to create
            // a staging area or use individual root descriptors.
            //
            // SIMPLER APPROACH: copy the needed descriptors to a contiguous range in the heap.
            // Use slots 34+ as scratch space for compute dispatch descriptor tables.

            // Copy descriptors for spawn: u0-u5 = write set, u6-u8 = bookkeeping
            for (UINT ch = 0; ch < k_VFXChannelCount; ++ch) {
                g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + ch),
                    SrvCpuHandle(writeSetUAV + ch), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            // u6 = aliveCount UAV
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 6),
                SrvCpuHandle(k_VFXBookkeepingUAV + 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u7 = freeList UAV
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 7),
                SrvCpuHandle(k_VFXBookkeepingUAV + 1), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u8 = freeListHead UAV
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 8),
                SrvCpuHandle(k_VFXBookkeepingUAV + 2), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            cl->SetComputeRootDescriptorTable(1, SrvGpuHandle(k_VFXScratch0));
            cl->Dispatch((spawnCount + 255) / 256, 1, 1);

            // UAV barrier between spawn and update
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = nullptr; // all UAVs
            cl->ResourceBarrier(1, &uavBarrier);
        }

        // ── Update pass ──
        // Reads from READ set, writes survivors to WRITE set.
        // Uses DispatchIndirect from the indirect args buffer (bytes 16-27 = dispatch args
        // from PREVIOUS frame's indirect args pass).
        {
            VFXUpdateCB updateCB = {};
            std::memcpy(updateCB.gravity, g_vfxEmitter.gravity, 12);
            updateCB.dt           = g_deltaTime;
            updateCB.damping      = g_vfxEmitter.damping;
            updateCB.aliveCount   = 0; // not used when dispatching indirectly, shader uses aliveCount from buffer
            updateCB.frameNumber  = g_totalFrames;
            updateCB.maxParticles = k_VFXMaxParticles;

            std::memcpy(g_app.vfxCbMapped[fi], &updateCB, sizeof(VFXUpdateCB));

            cl->SetPipelineState(g_app.vfxUpdatePSO.Get());
            cl->SetComputeRootConstantBufferView(0, g_app.vfxCbUpload[fi]->GetGPUVirtualAddress());

            // Copy descriptors for update pass at scratch1 slots:
            // u0-u5 = read set (RW for reading), u6-u14 = write set + bookkeeping
            for (UINT ch = 0; ch < k_VFXChannelCount; ++ch) {
                // u0-u5: read set UAVs
                g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + ch),
                    SrvCpuHandle(readSetUAV + ch), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            for (UINT ch = 0; ch < k_VFXChannelCount; ++ch) {
                // u6-u11: write set UAVs
                g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 6 + ch),
                    SrvCpuHandle(writeSetUAV + ch), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            // u10 = newAliveCount = aliveCount buffer
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 10),
                SrvCpuHandle(k_VFXBookkeepingUAV + 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u11 = freeList
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 11),
                SrvCpuHandle(k_VFXBookkeepingUAV + 1), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u12 = freeListHead
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 12),
                SrvCpuHandle(k_VFXBookkeepingUAV + 2), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u13 = outSizes (write set channel 4)
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 13),
                SrvCpuHandle(writeSetUAV + 4), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u14 = outColors (write set channel 5)
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 14),
                SrvCpuHandle(writeSetUAV + 5), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // u15 = indirectArgs (for reading alive count from previous frame's draw args)
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 15),
                SrvCpuHandle(k_VFXBookkeepingUAV + 3), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            cl->SetComputeRootDescriptorTable(1, SrvGpuHandle(k_VFXScratch1));

            // Dispatch enough groups to cover all possible alive particles.
            // The shader reads the actual alive count from the indirect args buffer (u15)
            // and early-exits threads beyond that count.
            // This avoids the complexity of DispatchIndirect + state conflicts.
            cl->Dispatch((k_VFXMaxParticles + 255) / 256, 1, 1);

            // UAV barrier after update
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = nullptr;
            cl->ResourceBarrier(1, &uavBarrier);
        }

        // ── Indirect args pass — single-thread dispatch ──
        {
            cl->SetPipelineState(g_app.vfxIndirectArgsPSO.Get());

            // u0 = aliveCount, u1 = indirectArgs
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 0),
                SrvCpuHandle(k_VFXBookkeepingUAV + 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 1),
                SrvCpuHandle(k_VFXBookkeepingUAV + 3), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            cl->SetComputeRootDescriptorTable(1, SrvGpuHandle(k_VFXScratch0));
            cl->Dispatch(1, 1, 1);

            // UAV barrier after indirect args
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = nullptr;
            cl->ResourceBarrier(1, &uavBarrier);
        }

        // Swap ping-pong index for next frame
        g_vfxPingPong = 1 - g_vfxPingPong;
    }

    // Restore graphics root signature for scene passes
    cl->SetGraphicsRootSignature(g_app.rootSig.Get());
    cl->SetGraphicsRootConstantBufferView(0, g_app.cbUpload[fi]->GetGPUVirtualAddress());
    cl->SetDescriptorHeaps(_countof(heaps), heaps);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(0));

    // ══════════════════════════════════════════════════════════════════════
    // Pass 0: Shadow depth pass
    // ══════════════════════════════════════════════════════════════════════

    // Transition shadow map: PIXEL_SHADER_RESOURCE -> DEPTH_WRITE
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.shadowMap.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        cl->ResourceBarrier(1, &barrier);
    }

    // Set shadow viewport (2048x2048) and clear shadow depth to 1.0 (standard Z)
    {
        D3D12_VIEWPORT shadowVp = {};
        shadowVp.Width    = static_cast<float>(k_ShadowMapSize);
        shadowVp.Height   = static_cast<float>(k_ShadowMapSize);
        shadowVp.MinDepth = 0.0f;
        shadowVp.MaxDepth = 1.0f;
        cl->RSSetViewports(1, &shadowVp);

        D3D12_RECT shadowScissor = {0, 0, static_cast<LONG>(k_ShadowMapSize), static_cast<LONG>(k_ShadowMapSize)};
        cl->RSSetScissorRects(1, &shadowScissor);

        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = ShadowDsvHandle();
        cl->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
        cl->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Draw scene mesh into shadow map
        cl->SetPipelineState(g_app.shadowPSO.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const MeshData& mesh = g_meshes[g_currentMesh];
        cl->IASetVertexBuffers(0, 1, &mesh.vbv);
        cl->IASetIndexBuffer(&mesh.ibv);
        cl->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
    }

    // Transition shadow map: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.shadowMap.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cl->ResourceBarrier(1, &barrier);
    }

    // ══════════════════════════════════════════════════════════════════════
    // Passes 1-3: Scene rendering to HDR target
    // ══════════════════════════════════════════════════════════════════════

    // Transition HDR target: PIXEL_SHADER_RESOURCE -> RENDER_TARGET
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.hdrTarget.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &barrier);
    }

    // Set HDR render target + main depth buffer
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = HdrRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv    = DsvHandle();
    cl->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);

    // Clear HDR target and depth
    const FLOAT clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    cl->ClearRenderTargetView(hdrRtv, clearColor, 0, nullptr);
    cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr); // reverse-Z

    // Set main viewport and scissor
    D3D12_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(k_Width);
    vp.Height   = static_cast<float>(k_Height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cl->RSSetViewports(1, &vp);

    D3D12_RECT scissor = {0, 0, static_cast<LONG>(k_Width), static_cast<LONG>(k_Height)};
    cl->RSSetScissorRects(1, &scissor);

    // Re-bind SRV heap and descriptor table (shadow map at t0 for sphere PS)
    cl->SetDescriptorHeaps(_countof(heaps), heaps);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(0));

    // ── Pass 1: Sky (fullscreen triangle) — outputs linear HDR ──
    cl->SetPipelineState(g_app.skyPSO.Get());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(3, 1, 0, 0);

    // ── Pass 2: Infinite Grid (fullscreen triangle) — outputs linear HDR ──
    cl->SetPipelineState(g_app.gridPSO.Get());
    cl->DrawInstanced(3, 1, 0, 0);

    // ── Pass 3: Current mesh with PBR + shadow — outputs linear HDR ──
    {
        const MeshData& mesh = g_meshes[g_currentMesh];
        cl->SetPipelineState(g_app.spherePSO.Get());
        cl->IASetVertexBuffers(0, 1, &mesh.vbv);
        cl->IASetIndexBuffer(&mesh.ibv);
        cl->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

        // ── Pass 3b: Wireframe overlay (only in wireframe debug mode) ──
        if (g_debugView == DebugView::Wireframe) {
            cl->SetPipelineState(g_app.wireframePSO.Get());
            cl->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        }
    }

    // ── Pass 3c-prep: Copy scene depth for soft particles (Task 9.6) ──
    // We copy the main depth buffer to a separate R32_FLOAT texture that the
    // billboard PS can sample as an SRV. This is needed because D32_FLOAT cannot
    // be read as SRV while bound as DSV in the same pass.
    if (g_vfxEnabled)
    {
        // Transition depth buffer: DEPTH_WRITE → COPY_SOURCE
        D3D12_RESOURCE_BARRIER depthCopyBarriers[2] = {};
        depthCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthCopyBarriers[0].Transition.pResource   = g_app.depthBuffer.Get();
        depthCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthCopyBarriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        depthCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        // Transition depth copy: PIXEL_SHADER_RESOURCE → COPY_DEST
        depthCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthCopyBarriers[1].Transition.pResource   = g_app.vfxDepthCopy.Get();
        depthCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        depthCopyBarriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        depthCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(2, depthCopyBarriers);

        cl->CopyResource(g_app.vfxDepthCopy.Get(), g_app.depthBuffer.Get());

        // Transition depth copy: COPY_DEST → PIXEL_SHADER_RESOURCE
        depthCopyBarriers[0].Transition.pResource   = g_app.vfxDepthCopy.Get();
        depthCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        depthCopyBarriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        // Transition depth buffer: COPY_SOURCE → DEPTH_WRITE
        depthCopyBarriers[1].Transition.pResource   = g_app.depthBuffer.Get();
        depthCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        depthCopyBarriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        cl->ResourceBarrier(2, depthCopyBarriers);
    }

    // ── Pass 3c-sort: GPU bitonic sort for back-to-front alpha blending (Task 9.7) ──
    if (g_vfxEnabled)
    {
        // Determine which set has the current frame's live particle data
        const UINT renderSetIdx = (g_vfxPingPong == 0) ? 0u : 1u;
        const UINT renderSetSRV = (g_vfxPingPong == 0) ? k_VFXReadSetA_SRV : k_VFXReadSetB_SRV;

        // Transition positions to SRV for sort key generation
        D3D12_RESOURCE_BARRIER posSrvBarrier = {};
        posSrvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        posSrvBarrier.Transition.pResource   = g_app.vfxParticleBuffers[renderSetIdx][0].Get();
        posSrvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        posSrvBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        posSrvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &posSrvBarrier);

        // Switch to sort root signature
        cl->SetComputeRootSignature(g_app.vfxSortRootSig.Get());

        // Prepare sort CB
        Vec3 camFwd = g_camera.GetForward();
        VFXSortCB sortCB = {};
        // Read alive count from indirect args: vertexCount = alive * 6
        // We don't have CPU-side alive count, so use paddedCount and let the shader handle padding.
        // The sort shader checks particleCount and fills padding with sentinel keys.
        sortCB.particleCount = k_VFXMaxParticles; // upper bound; local sort handles out-of-range
        sortCB.sortPhase     = 0;
        sortCB.paddedCount   = g_app.vfxSortPaddedCount;
        sortCB.cameraPos[0]  = g_camera.position.x;
        sortCB.cameraPos[1]  = g_camera.position.y;
        sortCB.cameraPos[2]  = g_camera.position.z;
        sortCB.cameraForward[0] = camFwd.x;
        sortCB.cameraForward[1] = camFwd.y;
        sortCB.cameraForward[2] = camFwd.z;
        sortCB.bitonicBlockSize    = 0;
        sortCB.bitonicSubBlockSize = 0;

        std::memcpy(g_app.vfxCbMapped[fi], &sortCB, sizeof(VFXSortCB));
        cl->SetComputeRootConstantBufferView(0, g_app.vfxCbUpload[fi]->GetGPUVirtualAddress());

        // Bind SRV table (param 1): t0 = positions from render set
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 0),
            SrvCpuHandle(renderSetSRV + 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cl->SetComputeRootDescriptorTable(1, SrvGpuHandle(k_VFXScratch1));

        // Bind UAV table (param 2): u0 = sortKeys, u1 = sortIndices
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 2),
            SrvCpuHandle(k_VFXSortKeysUAV), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch1 + 3),
            SrvCpuHandle(k_VFXSortIndicesUAV), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cl->SetComputeRootDescriptorTable(2, SrvGpuHandle(k_VFXScratch1 + 2));

        // Phase 0: local sort + key generation (256-element blocks)
        cl->SetPipelineState(g_app.vfxSortLocalPSO.Get());
        cl->Dispatch((g_app.vfxSortPaddedCount + 255) / 256, 1, 1);

        // UAV barrier between local and global passes
        {
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = nullptr;
            cl->ResourceBarrier(1, &uavBarrier);
        }

        // Phase 1: global merge passes — iterate over blockSize > 256 (local already sorted 256)
        // For each block size, iterate sub-blocks from blockSize/2 down to 1
        cl->SetPipelineState(g_app.vfxSortGlobalPSO.Get());

        for (uint32_t blockSize = 512; blockSize <= g_app.vfxSortPaddedCount; blockSize <<= 1)
        {
            for (uint32_t subBlock = blockSize >> 1; subBlock > 0; subBlock >>= 1)
            {
                // Update sort CB with current block/sub-block sizes
                sortCB.sortPhase          = 1;
                sortCB.bitonicBlockSize    = blockSize;
                sortCB.bitonicSubBlockSize = subBlock;
                std::memcpy(g_app.vfxCbMapped[fi], &sortCB, sizeof(VFXSortCB));

                cl->SetComputeRootConstantBufferView(0, g_app.vfxCbUpload[fi]->GetGPUVirtualAddress());
                cl->Dispatch((g_app.vfxSortPaddedCount + 255) / 256, 1, 1);

                // UAV barrier between each merge pass
                D3D12_RESOURCE_BARRIER uavBarrier = {};
                uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                uavBarrier.UAV.pResource = nullptr;
                cl->ResourceBarrier(1, &uavBarrier);
            }
        }

        // Transition sort indices to SRV for billboard VS
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = g_app.vfxSortIndices.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
        }

        // Transition positions back to UAV (we'll transition them to SRV again for billboard)
        // Actually, positions need to be SRV for billboard too, so leave them as SRV.
        // But sizes and colors still need UAV→SRV transition. Let's handle all of that in the billboard section.
    }

    // ── Pass 3c: VFX Particles (billboard quads, alpha-blended) ──
    if (g_vfxEnabled)
    {
        const UINT renderSetSRV = (g_vfxPingPong == 0) ? k_VFXReadSetA_SRV : k_VFXReadSetB_SRV;
        UINT renderSetIdx = (g_vfxPingPong == 0) ? 0 : 1;

        // Positions were already transitioned to SRV by the sort pass above.
        // Only sizes (ch 4) and colors (ch 5) need UAV → SRV transition.
        D3D12_RESOURCE_BARRIER srvBarriers[2] = {};
        ID3D12Resource* renderBufs[2] = {
            g_app.vfxParticleBuffers[renderSetIdx][4].Get(), // sizes
            g_app.vfxParticleBuffers[renderSetIdx][5].Get(), // colors
        };
        for (int i = 0; i < 2; ++i) {
            srvBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            srvBarriers[i].Transition.pResource   = renderBufs[i];
            srvBarriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            srvBarriers[i].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            srvBarriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        cl->ResourceBarrier(2, srvBarriers);

        // Switch to VFX graphics root signature
        cl->SetGraphicsRootSignature(g_app.vfxGraphicsRootSig.Get());
        cl->SetGraphicsRootConstantBufferView(0, g_app.cbUpload[fi]->GetGPUVirtualAddress());

        // Bind SRV descriptor table: t0=positions, t1=sizes, t2=colors, t3=depthCopy, t4=sortIndices
        // Copy needed SRVs to a contiguous range at scratch0 (compute is done by now)
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 0),
            SrvCpuHandle(renderSetSRV + 0), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // positions
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 1),
            SrvCpuHandle(renderSetSRV + 4), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // sizes
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 2),
            SrvCpuHandle(renderSetSRV + 5), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // colors
        // t3 = scene depth copy for soft particles
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 3),
            SrvCpuHandle(k_VFXDepthCopySRV), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // t4 = sort indices (for sorted draw order)
        g_device->CopyDescriptorsSimple(1, SrvCpuHandle(k_VFXScratch0 + 4),
            SrvCpuHandle(k_VFXSortIndicesSRV), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(k_VFXScratch0));

        cl->SetPipelineState(g_app.vfxBillboardPSO.Get());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Transition indirect args for ExecuteIndirect
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = g_app.vfxIndirectArgs.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
        }

        // Draw particles via ExecuteIndirect (DrawInstanced)
        cl->ExecuteIndirect(g_app.vfxDrawCmdSig.Get(), 1,
            g_app.vfxIndirectArgs.Get(), 0, nullptr, 0);

        // Transition indirect args back to UAV
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = g_app.vfxIndirectArgs.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cl->ResourceBarrier(1, &barrier);
        }

        // Transition particle buffers (positions, sizes, colors) and sort indices back to UAV
        D3D12_RESOURCE_BARRIER backBarriers[4] = {};
        // positions
        backBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        backBarriers[0].Transition.pResource   = g_app.vfxParticleBuffers[renderSetIdx][0].Get();
        backBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        backBarriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        backBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        // sizes
        backBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        backBarriers[1].Transition.pResource   = g_app.vfxParticleBuffers[renderSetIdx][4].Get();
        backBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        backBarriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        backBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        // colors
        backBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        backBarriers[2].Transition.pResource   = g_app.vfxParticleBuffers[renderSetIdx][5].Get();
        backBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        backBarriers[2].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        backBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        // sort indices
        backBarriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        backBarriers[3].Transition.pResource   = g_app.vfxSortIndices.Get();
        backBarriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        backBarriers[3].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        backBarriers[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(4, backBarriers);

        // Restore scene graphics root signature for tonemap pass
        cl->SetGraphicsRootSignature(g_app.rootSig.Get());
        cl->SetGraphicsRootConstantBufferView(0, g_app.cbUpload[fi]->GetGPUVirtualAddress());
        cl->SetDescriptorHeaps(_countof(heaps), heaps);
        cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(0));
    }

    // ══════════════════════════════════════════════════════════════════════
    // Pass 4: Tonemap (HDR -> LDR, fullscreen triangle to backbuffer)
    // ══════════════════════════════════════════════════════════════════════

    // Transition HDR target: RENDER_TARGET -> PIXEL_SHADER_RESOURCE
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.hdrTarget.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cl->ResourceBarrier(1, &barrier);
    }

    // Transition backbuffer: PRESENT -> RENDER_TARGET
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.backbuffers[fi].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &barrier);
    }

    // Set backbuffer as render target (no depth for fullscreen post-process)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHandle(fi);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &scissor);

    // Re-bind SRV heap — tonemap reads HDR at t1
    cl->SetDescriptorHeaps(_countof(heaps), heaps);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpuHandle(0));

    cl->SetPipelineState(g_app.tonemapPSO.Get());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(3, 1, 0, 0);

    // Transition backbuffer: RENDER_TARGET -> PRESENT
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = g_app.backbuffers[fi].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        cl->ResourceBarrier(1, &barrier);
    }

    UZ_CHK("CmdList::Close", cl->Close());

    // Submit
    ID3D12CommandList* lists[] = { cl };
    g_app.cmdQueue->ExecuteCommandLists(1, lists);

    // Present with VSync (SyncInterval=1)
    UZ_CHK("Present", g_app.swapchain->Present(1, 0));

    // Signal fence
    const UINT64 val = ++g_app.fenceValues[fi];
    UZ_CHK("Signal", g_app.cmdQueue->Signal(g_app.fence.Get(), val));

    g_app.frameIndex = g_app.swapchain->GetCurrentBackBufferIndex();
}

// ─── cleanup ──────────────────────────────────────────────────────────────────

static void Cleanup()
{
    WaitForAllFrames();

    // Unmap constant buffers
    for (UINT i = 0; i < k_Frames; ++i)
    {
        if (g_app.cbUpload[i])
            g_app.cbUpload[i]->Unmap(0, nullptr);
        g_app.cbMapped[i] = nullptr;
    }

    if (g_app.fenceEvent)
    {
        CloseHandle(g_app.fenceEvent);
        g_app.fenceEvent = nullptr;
    }

    // Unmap VFX constant buffers
    for (UINT i = 0; i < k_Frames; ++i)
    {
        if (g_app.vfxCbUpload[i])
            g_app.vfxCbUpload[i]->Unmap(0, nullptr);
        g_app.vfxCbMapped[i] = nullptr;
    }

    // Release VFX resources
    g_app.vfxSortGlobalPSO.Reset();
    g_app.vfxSortLocalPSO.Reset();
    g_app.vfxSortRootSig.Reset();
    g_app.vfxSortKeys.Reset();
    g_app.vfxSortIndices.Reset();
    g_app.vfxDepthCopy.Reset();
    g_app.vfxDrawCmdSig.Reset();
    g_app.vfxBillboardPSO.Reset();
    g_app.vfxIndirectArgsPSO.Reset();
    g_app.vfxUpdatePSO.Reset();
    g_app.vfxSpawnPSO.Reset();
    g_app.vfxGraphicsRootSig.Reset();
    g_app.vfxComputeRootSig.Reset();
    g_app.vfxIndirectArgs.Reset();
    g_app.vfxFreeListHead.Reset();
    g_app.vfxFreeList.Reset();
    g_app.vfxAliveCount.Reset();
    for (UINT set = 0; set < 2; ++set)
        for (UINT ch = 0; ch < k_VFXChannelCount; ++ch)
            g_app.vfxParticleBuffers[set][ch].Reset();
    for (UINT i = 0; i < k_Frames; ++i)
        g_app.vfxCbUpload[i].Reset();

    // Release everything (ComPtr handles it, but explicit ordering for clarity)
    g_app.wireframePSO.Reset();
    g_app.tonemapPSO.Reset();
    g_app.shadowPSO.Reset();
    g_app.spherePSO.Reset();
    g_app.gridPSO.Reset();
    g_app.skyPSO.Reset();
    g_app.rootSig.Reset();
    g_app.srvHeap.Reset();
    g_app.hdrRtvHeap.Reset();
    g_app.hdrTarget.Reset();
    g_app.shadowDsvHeap.Reset();
    g_app.shadowMap.Reset();
    for (int i = 0; i < k_MeshCount; ++i) {
        g_meshes[i].vb.Reset();
        g_meshes[i].ib.Reset();
    }
    for (UINT i = 0; i < k_Frames; ++i)
    {
        g_app.cbUpload[i].Reset();
        g_app.cmdAllocs[i].Reset();
        g_app.backbuffers[i].Reset();
    }
    g_app.cmdList.Reset();
    g_app.depthBuffer.Reset();
    g_app.dsvHeap.Reset();
    g_app.rtvHeap.Reset();
    g_app.fence.Reset();
    g_app.swapchain.Reset();
    g_app.cmdQueue.Reset();
    g_device.Reset();
}

// ─── screenshot capture (readback backbuffer to CPU, save as BMP) ────────────

static void CaptureScreenshot()
{
    WaitForAllFrames();

    const UINT fi = g_app.swapchain->GetCurrentBackBufferIndex();
    ID3D12Resource* backbuffer = g_app.backbuffers[fi].Get();

    // Get backbuffer description
    D3D12_RESOURCE_DESC bbDesc = backbuffer->GetDesc();
    UINT64 totalSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    g_device->GetCopyableFootprints(&bbDesc, 0, 1, 0, &layout, nullptr, nullptr, &totalSize);

    // Create readback buffer
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = totalSize;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> readback;
    UZ_CHK("CreateReadbackBuffer",
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

    // Use a temporary command allocator + list
    ComPtr<ID3D12CommandAllocator> tmpAlloc;
    UZ_CHK("Screenshot tmpAlloc",
        g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc)));

    ComPtr<ID3D12GraphicsCommandList> tmpList;
    UZ_CHK("Screenshot tmpList",
        g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            tmpAlloc.Get(), nullptr, IID_PPV_ARGS(&tmpList)));

    // Transition backbuffer: PRESENT -> COPY_SOURCE
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = backbuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
        tmpList->ResourceBarrier(1, &barrier);
    }

    // Copy backbuffer texture to readback buffer
    {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource        = backbuffer;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource       = readback.Get();
        dst.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = layout;

        tmpList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    // Transition backbuffer: COPY_SOURCE -> PRESENT
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = backbuffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        tmpList->ResourceBarrier(1, &barrier);
    }

    UZ_CHK("Screenshot close", tmpList->Close());

    ID3D12CommandList* lists[] = { tmpList.Get() };
    g_app.cmdQueue->ExecuteCommandLists(1, lists);

    // Wait for copy to complete
    ComPtr<ID3D12Fence> tmpFence;
    UZ_CHK("Screenshot fence", g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&tmpFence)));
    HANDLE tmpEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UZ_CHK("Screenshot signal", g_app.cmdQueue->Signal(tmpFence.Get(), 1));
    if (tmpFence->GetCompletedValue() < 1) {
        tmpFence->SetEventOnCompletion(1, tmpEvent);
        WaitForSingleObjectEx(tmpEvent, 5000, FALSE);
    }
    CloseHandle(tmpEvent);

    // Map readback buffer and read pixel data
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(totalSize) };
    UZ_CHK("Map readback", readback->Map(0, &readRange, &mapped));

    const int w = static_cast<int>(bbDesc.Width);
    const int h = static_cast<int>(bbDesc.Height);
    const UINT rowPitch = layout.Footprint.RowPitch;

    // Convert from GPU layout (with row pitch alignment) to tightly-packed RGBA
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    const uint8_t* srcData = static_cast<const uint8_t*>(mapped);
    for (int y = 0; y < h; ++y) {
        std::memcpy(&pixels[static_cast<size_t>(y) * w * 4],
                     srcData + static_cast<size_t>(y) * rowPitch,
                     static_cast<size_t>(w) * 4);
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readback->Unmap(0, &writeRange);

    // Output: either file, stdout base64, or both
    if (g_screenshotStdout) {
        auto bmp = EncodeBMP(pixels.data(), w, h);
        auto b64 = Base64Encode(bmp.data(), bmp.size());
        // Attach to parent console for stdout output (WinMain apps have no console)
        AttachConsole(ATTACH_PARENT_PROCESS);
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        const char prefix[] = "SCREENSHOT_BASE64:";
        DWORD written = 0;
        WriteFile(hStdout, prefix, sizeof(prefix) - 1, &written, nullptr);
        WriteFile(hStdout, b64.data(), static_cast<DWORD>(std::strlen(b64.data())), &written, nullptr);
        const char newline[] = "\n";
        WriteFile(hStdout, newline, 1, &written, nullptr);
    }

    if (g_screenshotPath[0] != '\0') {
        WriteBMP(g_screenshotPath, pixels.data(), w, h);
        char buf[600];
        std::snprintf(buf, sizeof(buf), "[uz_viewport] Screenshot saved: %s\n", g_screenshotPath);
        OutputDebugStringA(buf);
    }

    // If neither path nor stdout was set (manual F9), save to default path
    if (!g_screenshotStdout && g_screenshotPath[0] == '\0') {
        WriteBMP("uz_viewport_screenshot.bmp", pixels.data(), w, h);
        OutputDebugStringA("[uz_viewport] Screenshot saved: uz_viewport_screenshot.bmp\n");
    }
}

// ─── command-line parsing ────────────────────────────────────────────────────

static void ParseCommandLine(const char* cmdLine)
{
    if (!cmdLine || !cmdLine[0]) return;

    // Simple token-based parser — handles --flag and --flag value
    const char* p = cmdLine;
    while (*p) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        // Extract token
        char token[256] = {};
        int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && ti < 255)
            token[ti++] = *p++;
        token[ti] = '\0';

        if (std::strcmp(token, "--headless") == 0) {
            g_headless = true;
        }
        else if (std::strcmp(token, "--screenshot-stdout") == 0) {
            g_screenshotStdout = true;
            g_headless = true; // --screenshot-stdout implies headless
        }
        else if (std::strcmp(token, "--frames") == 0) {
            // Next token is the frame count
            while (*p == ' ' || *p == '\t') ++p;
            char val[64] = {};
            int vi = 0;
            while (*p && *p != ' ' && *p != '\t' && vi < 63)
                val[vi++] = *p++;
            val[vi] = '\0';
            g_maxFrames = std::atoi(val);
        }
        else if (std::strcmp(token, "--screenshot") == 0) {
            // Next token is the file path
            while (*p == ' ' || *p == '\t') ++p;
            int si = 0;
            while (*p && *p != ' ' && *p != '\t' && si < 511)
                g_screenshotPath[si++] = *p++;
            g_screenshotPath[si] = '\0';
        }
        else if (std::strcmp(token, "--screenshot-frame") == 0) {
            // Next token is the frame number
            while (*p == ' ' || *p == '\t') ++p;
            char val[64] = {};
            int vi = 0;
            while (*p && *p != ' ' && *p != '\t' && vi < 63)
                val[vi++] = *p++;
            val[vi] = '\0';
            g_screenshotFrame = std::atoi(val);
        }
    }
}

// ─── Win32 window proc ───────────────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    // ── Keyboard ──
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        // Time-of-day: T = raise sun, Shift+T = lower sun
        if (wp == 'T') {
            if (GetKeyState(VK_SHIFT) & 0x8000)
                g_sunElevation = std::fmax(-10.0f, g_sunElevation - 5.0f);
            else
                g_sunElevation = std::fmin(90.0f, g_sunElevation + 5.0f);
            return 0;
        }
        // Material cycling: Left/Right arrows
        if (wp == VK_LEFT)  { g_currentMaterial = (g_currentMaterial - 1 + k_MaterialCount) % k_MaterialCount; return 0; }
        if (wp == VK_RIGHT) { g_currentMaterial = (g_currentMaterial + 1) % k_MaterialCount; return 0; }
        // Mesh cycling: Up/Down arrows
        if (wp == VK_UP)    { g_currentMesh = (g_currentMesh - 1 + k_MeshCount) % k_MeshCount; return 0; }
        if (wp == VK_DOWN)  { g_currentMesh = (g_currentMesh + 1) % k_MeshCount; return 0; }
        // Exposure control: +/= to increase, - to decrease
        if (wp == VK_OEM_PLUS || wp == VK_ADD) { g_exposure *= 1.2f; return 0; }
        if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) { g_exposure /= 1.2f; g_exposure = std::fmax(0.01f, g_exposure); return 0; }
        // Shadow bias: [ to decrease, ] to increase
        if (wp == VK_OEM_4) { g_shadowBias = std::fmax(0.0f, g_shadowBias - 0.0005f); return 0; } // [
        if (wp == VK_OEM_6) { g_shadowBias += 0.0005f; return 0; } // ]
        // Post-process toggle: P
        if (wp == 'P') { g_postProcessEnabled = !g_postProcessEnabled; return 0; }
        // Vignette: V to toggle (0 or 0.4)
        if (wp == 'V') { g_vignetteIntensity = (g_vignetteIntensity > 0.01f) ? 0.0f : 0.4f; return 0; }
        // Debug view modes: F1-F8
        if (wp == VK_F1) { g_debugView = DebugView::Lit;         return 0; }
        if (wp == VK_F2) { g_debugView = DebugView::Albedo;      return 0; }
        if (wp == VK_F3) { g_debugView = DebugView::WorldNormal; return 0; }
        if (wp == VK_F4) { g_debugView = DebugView::Metallic;    return 0; }
        if (wp == VK_F5) { g_debugView = DebugView::Roughness;   return 0; }
        if (wp == VK_F6) { g_debugView = DebugView::Depth;       return 0; }
        if (wp == VK_F7) { g_debugView = DebugView::Wireframe;   return 0; }
        if (wp == VK_F8) { g_debugView = DebugView::Unlit;       return 0; }
        // Screenshot: F9
        if (wp == VK_F9) { g_screenshotRequest = true; return 0; }
        // VFX preset cycling: Tab (Shift+Tab = toggle on/off)
        if (wp == VK_TAB) {
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                g_vfxEnabled = !g_vfxEnabled;
            } else {
                g_vfxPresetIndex = (g_vfxPresetIndex + 1) % k_VFXPresetCount;
                g_vfxEmitter   = k_VFXPresets[g_vfxPresetIndex].config;
                g_vfxSpawnRate = k_VFXPresets[g_vfxPresetIndex].spawnRate;
                g_vfxEnabled   = true;
            }
            return 0;
        }
        // VFX parameter tweaking: number keys 1-9 (plain=increase, Shift=decrease)
        if (wp >= '1' && wp <= '9' && g_vfxEnabled) {
            bool decrease = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            float sign = decrease ? -1.0f : 1.0f;
            switch (wp) {
            case '1': g_vfxSpawnRate += sign * 100.0f;
                      if (g_vfxSpawnRate < 10.0f) g_vfxSpawnRate = 10.0f;
                      g_vfxTweakName = "Spawn Rate"; g_vfxTweakValue = g_vfxSpawnRate; break;
            case '2': g_vfxEmitter.initialSize += sign * 0.01f;
                      if (g_vfxEmitter.initialSize < 0.005f) g_vfxEmitter.initialSize = 0.005f;
                      g_vfxTweakName = "Size"; g_vfxTweakValue = g_vfxEmitter.initialSize; break;
            case '3': g_vfxEmitter.gravity[1] += sign * 1.0f;
                      g_vfxTweakName = "Gravity Y"; g_vfxTweakValue = g_vfxEmitter.gravity[1]; break;
            case '4': g_vfxEmitter.spreadAngle += sign * 0.1f;
                      if (g_vfxEmitter.spreadAngle < 0.0f) g_vfxEmitter.spreadAngle = 0.0f;
                      g_vfxTweakName = "Spread"; g_vfxTweakValue = g_vfxEmitter.spreadAngle; break;
            case '5': g_vfxEmitter.minLifetime += sign * 0.2f;
                      if (g_vfxEmitter.minLifetime < 0.1f) g_vfxEmitter.minLifetime = 0.1f;
                      g_vfxTweakName = "Min Life"; g_vfxTweakValue = g_vfxEmitter.minLifetime; break;
            case '6': g_vfxEmitter.maxLifetime += sign * 0.5f;
                      if (g_vfxEmitter.maxLifetime < g_vfxEmitter.minLifetime + 0.1f)
                          g_vfxEmitter.maxLifetime = g_vfxEmitter.minLifetime + 0.1f;
                      g_vfxTweakName = "Max Life"; g_vfxTweakValue = g_vfxEmitter.maxLifetime; break;
            case '7': g_vfxEmitter.spawnVelocity[1] += sign * 0.5f;
                      g_vfxTweakName = "Velocity Y"; g_vfxTweakValue = g_vfxEmitter.spawnVelocity[1]; break;
            case '8': g_vfxEmitter.damping += sign * 0.05f;
                      if (g_vfxEmitter.damping < 0.0f) g_vfxEmitter.damping = 0.0f;
                      g_vfxTweakName = "Damping"; g_vfxTweakValue = g_vfxEmitter.damping; break;
            case '9': g_vfxEmitter.spawnRadius += sign * 0.05f;
                      if (g_vfxEmitter.spawnRadius < 0.0f) g_vfxEmitter.spawnRadius = 0.0f;
                      g_vfxTweakName = "Spawn Radius"; g_vfxTweakValue = g_vfxEmitter.spawnRadius; break;
            }
            g_vfxTweakTimer = 90; // show for ~90 frames
            return 0;
        }
        // Stats toggle: F12
        if (wp == VK_F12) { g_showStats = !g_showStats; return 0; }
        g_input.keys[wp & 0xFF] = true;
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        g_input.keys[wp & 0xFF] = false;
        return 0;

    // ── Mouse buttons ──
    case WM_LBUTTONDOWN:
        g_input.mouseButtons[0] = true;
        return 0;
    case WM_LBUTTONUP:
        g_input.mouseButtons[0] = false;
        return 0;
    case WM_RBUTTONDOWN:
        g_input.mouseButtons[1] = true;
        SetCapture(hwnd);
        ShowCursor(FALSE);
        return 0;
    case WM_RBUTTONUP:
        g_input.mouseButtons[1] = false;
        ReleaseCapture();
        ShowCursor(TRUE);
        return 0;
    case WM_MBUTTONDOWN:
        g_input.mouseButtons[2] = true;
        SetCapture(hwnd);
        ShowCursor(FALSE);
        return 0;
    case WM_MBUTTONUP:
        g_input.mouseButtons[2] = false;
        ReleaseCapture();
        ShowCursor(TRUE);
        return 0;

    // ── Mouse position ──
    case WM_MOUSEMOVE:
        g_input.mouseX = static_cast<int>(static_cast<short>(LOWORD(lp)));
        g_input.mouseY = static_cast<int>(static_cast<short>(HIWORD(lp)));
        return 0;

    // ── Mouse wheel ──
    case WM_MOUSEWHEEL:
        g_input.scrollDelta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / 120.0f;
        return 0;

    // ── Raw input (high-precision mouse delta, no cursor acceleration) ──
    case WM_INPUT:
    {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size == 0) return 0;

        // Stack buffer for typical RAWINPUT size (avoid heap allocation per event)
        alignas(8) BYTE buf[64];
        BYTE* data = (size <= sizeof(buf)) ? buf : static_cast<BYTE*>(_malloca(size));
        if (!data) return 0;

        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, data, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1))
        {
            const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(data);
            if (raw->header.dwType == RIM_TYPEMOUSE &&
                (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
            {
                g_input.rawDeltaX += raw->data.mouse.lLastX;
                g_input.rawDeltaY += raw->data.mouse.lLastY;
            }
        }

        if (data != buf) _freea(data);
        return 0;
    }

    case WM_CLOSE:   DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── entry point ─────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int nShow)
{
    // Parse command-line arguments before anything else
    ParseCommandLine(lpCmdLine);

    QueryPerformanceFrequency(&g_app.perfFreq);
    QueryPerformanceCounter(&g_app.startTime);

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.style        = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = k_Class;
    if (!RegisterClassExW(&wc))
        FatalHR("RegisterClassExW", HRESULT_FROM_WIN32(GetLastError()));

    RECT wr = {0, 0, static_cast<LONG>(k_Width), static_cast<LONG>(k_Height)};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    g_app.hwnd = CreateWindowExW(0, k_Class, k_Title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInst, nullptr);
    if (!g_app.hwnd)
        FatalHR("CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));

    InitDX12();
    InitAssets();

    // Register raw input for high-precision mouse deltas (no cursor acceleration)
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rid.usUsage     = 0x02;  // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags     = 0;
    rid.hwndTarget  = g_app.hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    // Initialize frame timing
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_lastTime);

    // Show window unless headless mode is active
    if (!g_headless) {
        ShowWindow(g_app.hwnd, nShow);
        UpdateWindow(g_app.hwnd);
    }

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Pump all pending messages before rendering
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (msg.message == WM_QUIT) break;

        // Frame timing via QPC
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_deltaTime = static_cast<float>(now.QuadPart - g_lastTime.QuadPart)
                    / static_cast<float>(g_freq.QuadPart);
        g_deltaTime = std::fmin(g_deltaTime, 0.1f); // clamp to avoid huge jumps
        g_lastTime = now;

        // Update camera from accumulated input
        g_camera.Update(g_input, g_deltaTime);

        // End input frame (swap current -> previous, reset deltas)
        g_input.BeginFrame();

        RenderFrame();
        ++g_totalFrames;

        // Auto-screenshot at specified frame
        if (g_screenshotFrame >= 0 &&
            static_cast<int>(g_totalFrames) == g_screenshotFrame)
        {
            CaptureScreenshot();
        }

        // Manual screenshot request (F9 key)
        if (g_screenshotRequest) {
            g_screenshotRequest = false;
            CaptureScreenshot();
        }

        // Frame limit: exit after rendering the specified number of frames
        if (g_maxFrames > 0 && static_cast<int>(g_totalFrames) >= g_maxFrames) {
            PostQuitMessage(0);
        }

        // Update title bar with mesh, material, sun, camera, debug view, and FPS info
        if (g_showStats && g_totalFrames % 30 == 0) {
            const char* viewName = k_DebugViewNames[static_cast<int>(g_debugView)];
            // Decay tweak display timer
            if (g_vfxTweakTimer > 0) g_vfxTweakTimer--;

            char title[512];
            char tweakBuf[64] = "";
            if (g_vfxTweakTimer > 0 && g_vfxTweakName)
                std::snprintf(tweakBuf, sizeof(tweakBuf), " | %s: %.3f", g_vfxTweakName, g_vfxTweakValue);

            std::snprintf(title, sizeof(title),
                "UzEngine VFX | %s | %s | Sun: %.0f%c | View: %s | VFX: %s [%s] | %.0f fps%s",
                k_Materials[g_currentMaterial].name,
                g_meshes[g_currentMesh].name,
                g_sunElevation, '\xB0',
                viewName,
                g_vfxEnabled ? "ON" : "OFF",
                k_VFXPresets[g_vfxPresetIndex].name,
                (g_deltaTime > 0.0001f) ? (1.0f / g_deltaTime) : 0.0f,
                tweakBuf);
            SetWindowTextA(g_app.hwnd, title);
        }
    }

    Cleanup();
    return static_cast<int>(msg.wParam);
}
