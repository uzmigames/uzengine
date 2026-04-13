#pragma once

/// CPU-side mirror structs for VFX compute shader constant buffers.
///
/// These MUST match the HLSL cbuffer layouts EXACTLY (byte-for-byte).
/// Any mismatch causes the GPU to read garbage data for bindless
/// descriptor indices, leading to device removed / crash.

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VFXSpawnCB — matches VFXSpawnConstants in particle_spawn_cs.hlsl (register b1)
// Total: 192 bytes (12 x float4) in bindless mode
// ─────────────────────────────────────────────────────────────────────────────

struct VFXSpawnCB {
    // ── Emitter Parameters (bytes 0-63) ─────────────────────────────────
    float emitterPosX = 0.0f;           // 0
    float emitterPosY = 0.0f;           // 4
    float emitterPosZ = 0.0f;           // 8
    float spawnRadius = 0.0f;           // 12

    float spawnVelocityX = 0.0f;        // 16
    float spawnVelocityY = 0.0f;        // 20
    float spawnVelocityZ = 0.0f;        // 24
    float velocityVariation = 0.0f;     // 28

    float spreadAngle = 0.0f;           // 32
    float minLifetime = 1.0f;           // 36
    float maxLifetime = 2.0f;           // 40
    float initialSize = 1.0f;           // 44

    float initialColorR = 1.0f;         // 48
    float initialColorG = 1.0f;         // 52
    float initialColorB = 1.0f;         // 56
    float initialColorA = 1.0f;         // 60

    float dt = 0.016f;                  // 64
    uint32_t frameNumber = 0;           // 68
    uint32_t spawnCount = 0;            // 72
    uint32_t maxParticles = 0;          // 76

    // ── Bindless Descriptor Indices (bytes 80-171) ──────────────────────
    // UINT32_MAX = no descriptor bound. Shaders MUST guard with != 0xFFFFFFFF.
    // Default 0 is WRONG — slot 0 is a valid descriptor (m_dbufferA SRV).
    uint32_t freeListSrvIndex = UINT32_MAX;      // 80
    uint32_t positionsUavIndex = UINT32_MAX;     // 84
    uint32_t velocitiesUavIndex = UINT32_MAX;    // 88
    uint32_t agesUavIndex = UINT32_MAX;          // 92

    uint32_t lifespansUavIndex = UINT32_MAX;     // 96
    uint32_t sizesUavIndex = UINT32_MAX;         // 100
    uint32_t colorsUavIndex = UINT32_MAX;        // 104
    uint32_t rotationsUavIndex = UINT32_MAX;     // 108

    uint32_t freeListHeadUavIndex = UINT32_MAX;  // 112
    uint32_t aliveCountUavIndex = UINT32_MAX;    // 116

    // ── Spawn shape (bytes 120-143) ─────────────────────────────────────
    uint32_t spawnShape = 0;            // 120 (0=Point)
    uint32_t spawnOnSurface = 0;        // 124
    float shapeParam1 = 0.0f;          // 128
    float shapeParam2 = 0.0f;          // 132
    float shapeParam3 = 0.0f;          // 136
    uint32_t _pad0 = 0;                // 140

    // ── Rotation + velocity inheritance (bytes 144-171) ─────────────────
    float initialRotationMin = 0.0f;    // 144
    float initialRotationMax = 6.28f;   // 148
    float emitterVelocityX = 0.0f;      // 152
    float emitterVelocityY = 0.0f;      // 156

    float emitterVelocityZ = 0.0f;      // 160
    float velocityInheritance = 0.0f;   // 164
    uint32_t dynamicParamsUavIndex = UINT32_MAX; // 168
    uint32_t _pad1 = 0;                // 172

    // ── SubUV + padding to 192 bytes (12 x float4) ────────────────────
    uint32_t subImageIndicesUavIndex = 0;          // 176 (HLSL not yet updated)
    uint32_t flipbookTotalFrames = 1;              // 180
    float _pad2[2] = {};                           // 184-191
};
static_assert(sizeof(VFXSpawnCB) == 192, "VFXSpawnCB must be 192 bytes to match HLSL layout");

// ─────────────────────────────────────────────────────────────────────────────
// VFXUpdateCB — matches VFXUpdateConstants in particle_update_cs.hlsl (register b1)
// Total: 320 bytes (20 x float4)
// ─────────────────────────────────────────────────────────────────────────────

/// MUST match VFXUpdateConstants in particle_update_cs.hlsl BYTE-FOR-BYTE.
/// The HLSL cbuffer is the ground truth. If you add a field to HLSL, add it
/// here at the SAME offset. Use UINT32_MAX for unbound descriptor indices.
struct VFXUpdateCB {
    // ── Simulation Parameters (bytes 0-31) ──────────────────────────────
    float gravityX = 0.0f;              // 0
    float gravityY = 0.0f;              // 4
    float gravityZ = -981.0f;           // 8
    float dt = 0.016f;                  // 12

    float damping = 0.98f;              // 16
    uint32_t aliveCount = 0;            // 20
    uint32_t frameNumber = 0;           // 24
    uint32_t maxParticles = 0;          // 28

    // ── Read-set SRV indices (bytes 32-63) ──────────────────────────────
    uint32_t positionsSrvIndex = UINT32_MAX;    // 32
    uint32_t velocitiesSrvIndex = UINT32_MAX;   // 36
    uint32_t agesSrvIndex = UINT32_MAX;         // 40
    uint32_t lifespansSrvIndex = UINT32_MAX;    // 44

    uint32_t sizesSrvIndex = UINT32_MAX;        // 48
    uint32_t colorsSrvIndex = UINT32_MAX;       // 52
    uint32_t rotationsSrvIndex = UINT32_MAX;    // 56
    uint32_t subImageIndicesSrvIndex = 0;      // 60 (replaces _padSrv0; HLSL not yet updated)

    // ── Write-set UAV indices (bytes 64-95) ─────────────────────────────
    uint32_t positionsUavIndex = UINT32_MAX;    // 64
    uint32_t velocitiesUavIndex = UINT32_MAX;   // 68
    uint32_t agesUavIndex = UINT32_MAX;         // 72
    uint32_t lifespansUavIndex = UINT32_MAX;    // 76

    uint32_t sizesUavIndex = UINT32_MAX;        // 80
    uint32_t colorsUavIndex = UINT32_MAX;       // 84
    uint32_t rotationsUavIndex = UINT32_MAX;    // 88
    uint32_t subImageIndicesUavIndex = 0;      // 92 (replaces _padUav0; HLSL not yet updated)

    // ── Shared buffer UAV indices (bytes 96-111) ────────────────────────
    uint32_t freeListUavIndex = UINT32_MAX;     // 96
    uint32_t freeListHeadUavIndex = UINT32_MAX; // 100
    uint32_t aliveCountNewUavIndex = UINT32_MAX; // 104
    uint32_t _padShared0 = 0;                   // 108

    // ── Curl noise (bytes 112-127) ──────────────────────────────────────
    float curlStrength = 0.0f;          // 112
    float curlFrequency = 1.0f;         // 116
    float curlSpeed = 0.0f;             // 120  (was curlOctaves — HLSL has curlSpeed)
    float totalTime = 0.0f;             // 124  (was curlTime — HLSL has totalTime)

    // ── Vortex force (bytes 128-163) ────────────────────────────────────
    float vortexAxisOriginX = 0.0f;     // 128
    float vortexAxisOriginY = 0.0f;     // 132
    float vortexAxisOriginZ = 0.0f;     // 136
    float vortexTangentialStrength = 0.0f; // 140

    float vortexAxisDirX = 0.0f;        // 144
    float vortexAxisDirY = 0.0f;        // 148
    float vortexAxisDirZ = 1.0f;        // 152
    float vortexRadialStrength = 0.0f;  // 156

    float vortexMaxRadius = 0.0f;       // 160
    uint32_t orbitEnabled = 0;          // 164
    float orbitRadius = 0.0f;           // 168
    float orbitSpeed = 0.0f;            // 172

    // ── Orbit axis (bytes 176-191) ──────────────────────────────────────
    float orbitAxisX = 0.0f;            // 176
    float orbitAxisY = 0.0f;            // 180
    float orbitAxisZ = 1.0f;            // 184
    float _orbitAxisPad = 0.0f;         // 188

    // ── Wind zone (bytes 192-207) ───────────────────────────────────────
    uint32_t windZoneSrvIndex = UINT32_MAX; // 192
    uint32_t activeWindZoneCount = 0;   // 196
    uint32_t _padWind0 = 0;             // 200
    uint32_t _padWind1 = 0;             // 204

    // ── Kill volume (bytes 208-223) ─────────────────────────────────────
    uint32_t killVolumeSrvIndex = UINT32_MAX; // 208
    uint32_t activeKillVolumeCount = 0; // 212
    uint32_t groundCollision = 0;       // 216
    float restitution = 0.6f;           // 220

    // ── Drag + death spawn (bytes 224-255) ──────────────────────────────
    float dragCoefficient = 0.0f;       // 224
    uint32_t deathSpawnEnabled = 0;     // 228
    uint32_t maxDeathEvents = 0;        // 232
    float deathSpawnSpread = 0.0f;      // 236

    float deathSpawnSpeedScale = 1.0f;  // 240
    uint32_t deathSpawnCount = 1;       // 244
    uint32_t deathEventBufferUavIndex = UINT32_MAX; // 248
    uint32_t deathEventCountUavIndex = UINT32_MAX;  // 252

    // ── Rotation + acceleration + collision (bytes 256-287) ─────────────
    float rotationRate = 0.0f;          // 256
    float rotationOverLifeStart = 0.0f; // 260
    float rotationOverLifeEnd = 0.0f;   // 264
    float accelX = 0.0f;                // 268

    float accelY = 0.0f;                // 272
    float accelZ = 0.0f;                // 276
    uint32_t collisionResponse = 0;     // 280
    float collisionFriction = 0.5f;     // 284

    // ── Dynamic material params (bytes 288-295) ─────────────────────────
    uint32_t readDynParamsSrv = UINT32_MAX;  // 288
    uint32_t writeDynParamsUav = UINT32_MAX; // 292

    // ── Vector field (bytes 296-319) ────────────────────────────────────
    uint32_t vectorFieldEnabled = 0;    // 296
    float vectorFieldStrength = 0.0f;   // 300

    float vectorFieldTightness = 0.0f;  // 304
    float vectorFieldScale = 1.0f;      // 308
    uint32_t flipbookTotalFrames = 1;  // 312  SubUV total frames for subImageIndex computation
    uint32_t _padVF1 = 0;              // 316
};
static_assert(sizeof(VFXUpdateCB) == 320, "VFXUpdateCB must be 320 bytes to match HLSL layout");

// ─────────────────────────────────────────────────────────────────────────────
// VFXIndirectArgsCB — matches VFXIndirectArgsConstants at register(b0)
// ─────────────────────────────────────────────────────────────────────────────

struct VFXIndirectArgsCB {
    uint32_t aliveCountSrvIndex = 0;    // 0
    uint32_t indirectArgsUavIndex = 0;  // 4
    uint32_t verticesPerParticle = 6;   // 8
    uint32_t updateGroupSize = 256;     // 12
};
static_assert(sizeof(VFXIndirectArgsCB) == 16, "VFXIndirectArgsCB must be 16 bytes");

} // namespace uz
