/// UzEngine MikkTSpace Tangent Generator — Implementation
///
/// @ref Mikkelsen 2010 "Simulation of Wrinkled Surfaces Revisited"

#include "mikktspace_tangents.h"
#include "engine/asset/formats/uzmesh_format.h"

#include <mikktspace.h>

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MikkTSpace Callback Context
// ─────────────────────────────────────────────────────────────────────────────

struct MikkContext {
    const std::vector<VertexPosNormUV>* vertices;
    const std::vector<uint32_t>* indices;
    std::vector<float>* tangentsOut; // 4 floats per vertex
};

// ─────────────────────────────────────────────────────────────────────────────
// SMikkTSpaceInterface Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static int GetNumFaces(const SMikkTSpaceContext* ctx) {
    auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
    return static_cast<int>(mc->indices->size() / 3);
}

static int GetNumVerticesOfFace(const SMikkTSpaceContext* /*ctx*/, int /*face*/) {
    return 3; // Always triangles
}

static void GetPosition(const SMikkTSpaceContext* ctx, float outPos[], int face, int vert) {
    auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
    uint32_t idx = (*mc->indices)[face * 3 + vert];
    const auto& v = (*mc->vertices)[idx];
    outPos[0] = v.position[0];
    outPos[1] = v.position[1];
    outPos[2] = v.position[2];
}

static void GetNormal(const SMikkTSpaceContext* ctx, float outNorm[], int face, int vert) {
    auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
    uint32_t idx = (*mc->indices)[face * 3 + vert];
    const auto& v = (*mc->vertices)[idx];
    outNorm[0] = v.normal[0];
    outNorm[1] = v.normal[1];
    outNorm[2] = v.normal[2];
}

static void GetTexCoord(const SMikkTSpaceContext* ctx, float outUV[], int face, int vert) {
    auto* mc = static_cast<const MikkContext*>(ctx->m_pUserData);
    uint32_t idx = (*mc->indices)[face * 3 + vert];
    const auto& v = (*mc->vertices)[idx];
    outUV[0] = v.uv[0];
    outUV[1] = v.uv[1];
}

static void SetTSpaceBasic(const SMikkTSpaceContext* ctx,
                            const float tangent[], float sign,
                            int face, int vert) {
    auto* mc = static_cast<MikkContext*>(ctx->m_pUserData);
    uint32_t idx = (*mc->indices)[face * 3 + vert];
    auto& out = *mc->tangentsOut;
    out[idx * 4 + 0] = tangent[0];
    out[idx * 4 + 1] = tangent[1];
    out[idx * 4 + 2] = tangent[2];
    out[idx * 4 + 3] = sign;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void ComputeMikkTSpaceTangents(
    const std::vector<VertexPosNormUV>& vertices,
    const std::vector<uint32_t>& indices,
    std::vector<float>& tangentsOut)
{
    tangentsOut.resize(vertices.size() * 4, 0.0f);

    MikkContext mc{};
    mc.vertices = &vertices;
    mc.indices = &indices;
    mc.tangentsOut = &tangentsOut;

    SMikkTSpaceInterface iface{};
    iface.m_getNumFaces = GetNumFaces;
    iface.m_getNumVerticesOfFace = GetNumVerticesOfFace;
    iface.m_getPosition = GetPosition;
    iface.m_getNormal = GetNormal;
    iface.m_getTexCoord = GetTexCoord;
    iface.m_setTSpaceBasic = SetTSpaceBasic;
    iface.m_setTSpace = nullptr;

    SMikkTSpaceContext ctx{};
    ctx.m_pInterface = &iface;
    ctx.m_pUserData = &mc;

    genTangSpaceDefault(&ctx);
}

} // namespace uz
