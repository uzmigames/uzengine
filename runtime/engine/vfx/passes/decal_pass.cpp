/// VFX Decal Render Pass -- Implementation
///
/// Registers and executes a graphics pass that projects deferred decals
/// into the G-Buffer. Each decal is drawn as the back faces of an oriented
/// bounding box, with the pixel shader reconstructing world position from
/// depth and sampling the decal material within the projection volume.

#include "decal_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"
#include "engine/vfx/decals/decal_component.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root parameter slot for per-decal constant buffer (b1).
static constexpr uint32_t kSlotPerDecal = 1;

/// Root parameter slot for SRV descriptor table (t0..tN).
static constexpr uint32_t kSlotSRVTable = 5;

/// Number of indices in the unit cube (12 triangles * 3 vertices).
static constexpr uint32_t kCubeIndexCount = 36;

// ─────────────────────────────────────────────────────────────────────────────
// Unit Cube Mesh Data
// ─────────────────────────────────────────────────────────────────────────────

/// Unit cube vertices: 8 corners of a [-0.5, 0.5] cube.
///
/// The decal's world transform scales and orients this cube to match the
/// decal's position, rotation, and halfExtents.
static constexpr float kCubeVertices[kDecalCubeVertexCount * 3] = {
    // Front face (Z = +0.5)
    -0.5f, -0.5f,  0.5f,  // 0: left-bottom-front
     0.5f, -0.5f,  0.5f,  // 1: right-bottom-front
     0.5f,  0.5f,  0.5f,  // 2: right-top-front
    -0.5f,  0.5f,  0.5f,  // 3: left-top-front
    // Back face (Z = -0.5)
    -0.5f, -0.5f, -0.5f,  // 4: left-bottom-back
     0.5f, -0.5f, -0.5f,  // 5: right-bottom-back
     0.5f,  0.5f, -0.5f,  // 6: right-top-back
    -0.5f,  0.5f, -0.5f,  // 7: left-top-back
};

/// Unit cube indices: 12 triangles (2 per face), counter-clockwise winding.
///
/// Since the decal PSO uses front-face culling, the rasterizer draws back
/// faces (the box interior), which is visible when the camera is outside
/// or inside the decal volume.
static constexpr uint16_t kCubeIndices[kDecalCubeIndexCount] = {
    // Front  (+Z)
    0, 1, 2,  2, 3, 0,
    // Back   (-Z)
    5, 4, 7,  7, 6, 5,
    // Left   (-X)
    4, 0, 3,  3, 7, 4,
    // Right  (+X)
    1, 5, 6,  6, 2, 1,
    // Top    (+Y)
    3, 2, 6,  6, 7, 3,
    // Bottom (-Y)
    4, 5, 1,  1, 0, 4,
};

const float* GetDecalCubeVertices() noexcept
{
    return kCubeVertices;
}

const uint16_t* GetDecalCubeIndices() noexcept
{
    return kCubeIndices;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Draws a single decal's bounding box with the per-decal constant buffer
/// and texture bindings.
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `decalData` -- Per-decal rendering data (CB allocation, texture SRV indices).
/// * `cubeVB` -- Unit cube vertex buffer handle.
/// * `cubeIB` -- Unit cube index buffer handle.
static void DrawDecalBox(IRHICommandList* cmd,
                         const DecalRenderData& decalData,
                         RHIBufferHandle cubeVB,
                         RHIBufferHandle cubeIB)
{
    // Bind per-decal constant buffer (worldToDecal, invViewProj, params, screen size).
    cmd->SetRootCBV(kSlotPerDecal,
                    decalData.decalCB.bufferHandle,
                    decalData.decalCB.offsetBytes,
                    reinterpret_cast<const uint8_t*>(decalData.decalCB.cpuPtr),
                    decalData.decalCB.sizeBytes);

    // Bind SRV table: decal textures (albedo, normal, roughness) + scene depth.
    cmd->SetDescriptorTable(kSlotSRVTable, decalData.albedoSrvIndex);

    // Bind unit cube mesh.
    cmd->SetVertexBuffer(0, cubeVB);
    cmd->SetIndexBuffer(cubeIB, RHIIndexFormat::Uint16);

    // Draw the cube (36 indices = 12 triangles).
    cmd->DrawIndexed(kCubeIndexCount, 1, 0, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddDecalPass
// ─────────────────────────────────────────────────────────────────────────────

void AddDecalPass(rg::RgBuilder& builder, const DecalPassDesc& desc)
{
    UZ_CHECKF(desc.decalPipeline.IsValid(),
              "DecalPass: decal pipeline handle is invalid");
    UZ_CHECKF(desc.cubeVertexBuffer.IsValid(),
              "DecalPass: cube vertex buffer handle is invalid");
    UZ_CHECKF(desc.cubeIndexBuffer.IsValid(),
              "DecalPass: cube index buffer handle is invalid");

    for (size_t i = 0; i < desc.decals.size(); ++i) {
        UZ_CHECKF(desc.decals[i].decal != nullptr,
                  "DecalPass: decal[{}] has null DecalComponent pointer", i);
    }

    auto passDesc = desc;

    builder.AddPass("VFX_DecalDeferred", [passDesc](rg::RgPassBuilder& pb) mutable {
        // Declare render graph resource dependencies.
        // G-Buffer targets: we write to them (render target blend).
        if (passDesc.gbufferAlbedo.IsValid()) {
            pb.Write(passDesc.gbufferAlbedo, RHIResourceState::RenderTarget);
        }
        if (passDesc.gbufferNormal.IsValid()) {
            pb.Write(passDesc.gbufferNormal, RHIResourceState::RenderTarget);
        }
        if (passDesc.gbufferRoughness.IsValid()) {
            pb.Write(passDesc.gbufferRoughness, RHIResourceState::RenderTarget);
        }

        // Depth target: we read for depth testing (no depth write).
        if (passDesc.depthTarget.IsValid()) {
            pb.Read(passDesc.depthTarget, RHIResourceState::DepthStencilRead);
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::DecalPass");

            if (passDesc.decals.empty()) return;

            // Bind the decal deferred pipeline once for all decals.
            cmd->SetPipeline(passDesc.decalPipeline);

            // Draw each decal in the order provided (pre-sorted by caller).
            uint32_t drawnCount = 0;
            for (const auto& decalData : passDesc.decals) {
                // Skip inactive or fully faded decals.
                if (!decalData.decal->isActive) continue;
                if (decalData.decal->fadeAlpha <= 0.0f) continue;

                DrawDecalBox(cmd, decalData,
                             passDesc.cubeVertexBuffer,
                             passDesc.cubeIndexBuffer);
                ++drawnCount;
            }

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "DecalPass: first execution -- {} decals submitted, {} drawn",
                       passDesc.decals.size(), drawnCount);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
