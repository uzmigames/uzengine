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
    uint32_t freeListSrvIndex = 0;      // 80
    uint32_t positionsUavIndex = 0;     // 84
    uint32_t velocitiesUavIndex = 0;    // 88
    uint32_t agesUavIndex = 0;          // 92

    uint32_t lifespansUavIndex = 0;     // 96
    uint32_t sizesUavIndex = 0;         // 100
    uint32_t colorsUavIndex = 0;        // 104
    uint32_t rotationsUavIndex = 0;     // 108

    uint32_t freeListHeadUavIndex = 0;  // 112
    uint32_t aliveCountUavIndex = 0;    // 116

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
    uint32_t dynamicParamsUavIndex = 0; // 168
    uint32_t _pad1 = 0;                // 172

    // ── Padding to 192 bytes (12 x float4) ──────────────────────────────
    float _pad2[4] = {};                // 176-191
};
static_assert(sizeof(VFXSpawnCB) == 192, "VFXSpawnCB must be 192 bytes to match HLSL layout");

// ─────────────────────────────────────────────────────────────────────────────
// VFXUpdateCB — matches VFXUpdateConstants in particle_update_cs.hlsl (register b1)
// Total: 320 bytes (20 x float4)
// ─────────────────────────────────────────────────────────────────────────────

struct VFXUpdateCB {
    // ── Physics (bytes 0-31) ────────────────────────────────────────────
    float gravityX = 0.0f;              // 0
    float gravityY = 0.0f;              // 4
    float gravityZ = -981.0f;           // 8
    float dt = 0.016f;                  // 12

    float damping = 0.98f;              // 16
    uint32_t aliveCount = 0;            // 20
    uint32_t frameNumber = 0;           // 24
    uint32_t maxParticles = 0;          // 28

    // ── Read-set SRV indices (bytes 32-59) ──────────────────────────────
    uint32_t rdPositionsSrvIndex = 0;   // 32
    uint32_t rdVelocitiesSrvIndex = 0;  // 36
    uint32_t rdAgesSrvIndex = 0;        // 40
    uint32_t rdLifespansSrvIndex = 0;   // 44

    uint32_t rdSizesSrvIndex = 0;       // 48
    uint32_t rdColorsSrvIndex = 0;      // 52
    uint32_t rdRotationsSrvIndex = 0;   // 56
    uint32_t rdDynamicParamsSrvIndex = 0; // 60

    // ── Write-set UAV indices (bytes 64-95) ─────────────────────────────
    uint32_t wrPositionsUavIndex = 0;   // 64
    uint32_t wrVelocitiesUavIndex = 0;  // 68
    uint32_t wrAgesUavIndex = 0;        // 72
    uint32_t wrLifespansUavIndex = 0;   // 76

    uint32_t wrSizesUavIndex = 0;       // 80
    uint32_t wrColorsUavIndex = 0;      // 84
    uint32_t wrRotationsUavIndex = 0;   // 88
    uint32_t wrDynamicParamsUavIndex = 0; // 92

    // ── Shared buffer UAV indices (bytes 96-111) ────────────────────────
    uint32_t aliveCountUavIndex = 0;    // 96
    uint32_t freeListUavIndex = 0;      // 100
    uint32_t freeListHeadUavIndex = 0;  // 104
    uint32_t idToIndexUavIndex = 0;     // 108

    // ── Curl noise params (bytes 112-127) ───────────────────────────────
    float curlStrength = 0.0f;          // 112
    float curlFrequency = 1.0f;         // 116
    uint32_t curlOctaves = 1;           // 120
    float curlTime = 0.0f;             // 124

    // ── Vortex params (bytes 128-159) ───────────────────────────────────
    float vortexCenterX = 0.0f;         // 128
    float vortexCenterY = 0.0f;         // 132
    float vortexCenterZ = 0.0f;         // 136
    float vortexStrength = 0.0f;        // 140

    float vortexAxisX = 0.0f;           // 144
    float vortexAxisY = 0.0f;           // 148
    float vortexAxisZ = 1.0f;           // 152
    float vortexRadialPull = 0.0f;      // 156

    // ── Wind + kill volume (bytes 160-191) ──────────────────────────────
    float windDirX = 0.0f;              // 160
    float windDirY = 0.0f;              // 164
    float windDirZ = 0.0f;              // 168
    float windStrength = 0.0f;          // 172

    float killMinX = -1e10f;            // 176
    float killMinY = -1e10f;            // 180
    float killMinZ = -1e10f;            // 184
    float killMaxX = 1e10f;             // 188

    // ── More params (bytes 192-255) ─────────────────────────────────────
    float killMaxY = 1e10f;             // 192
    float killMaxZ = 1e10f;             // 196
    uint32_t killVolumeEnabled = 0;     // 200
    uint32_t collisionEnabled = 0;      // 204

    float collisionBounceFactor = 0.6f; // 208
    float collisionOffset = 1.0f;       // 212
    float drag = 0.0f;                  // 216
    uint32_t deathSpawnEnabled = 0;     // 220

    float rotationRate = 0.0f;          // 224
    float accelerationX = 0.0f;         // 228
    float accelerationY = 0.0f;         // 232
    float accelerationZ = 0.0f;         // 236

    // ── Dynamic params + vector field (bytes 240-319) ───────────────────
    uint32_t rdDynParamsSrvIdx2 = 0;    // 240
    uint32_t wrDynParamsUavIdx2 = 0;    // 244
    float vectorFieldStrength = 0.0f;   // 248
    uint32_t vectorFieldSrvIndex = UINT32_MAX; // 252

    float vectorFieldScaleX = 1.0f;     // 256
    float vectorFieldScaleY = 1.0f;     // 260
    float vectorFieldScaleZ = 1.0f;     // 264
    float vectorFieldOffsetX = 0.0f;    // 268

    float vectorFieldOffsetY = 0.0f;    // 272
    float vectorFieldOffsetZ = 0.0f;    // 276
    uint32_t sizeOverLifeEnabled = 0;   // 280
    float sizeOverLifeStart = 1.0f;     // 284

    float sizeOverLifePeak = 1.0f;      // 288
    float sizeOverLifeEnd = 0.0f;       // 292
    float sizeOverLifePeakTime = 0.3f;  // 296
    uint32_t colorOverLifeEnabled = 0;  // 300

    float colorLifeStartR = 1.0f;       // 304
    float colorLifeStartG = 1.0f;       // 308
    float colorLifeStartB = 1.0f;       // 312
    float colorLifeStartA = 1.0f;       // 316
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
