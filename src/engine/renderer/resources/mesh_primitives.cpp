// ---------------------------------------------------------------------------
// mesh_primitives.cpp -- Procedural mesh generation
// ---------------------------------------------------------------------------

#include "mesh_primitives.h"
#include <cmath>

namespace uz {

static constexpr float kPI = 3.14159265359f;

// ===========================================================================
// Sphere
// ===========================================================================

MeshData generate_sphere(uint32_t slices, uint32_t stacks, float radius) {
    MeshData data;
    data.bounds = Aabb::empty();

    for (uint32_t stack = 0; stack <= stacks; ++stack) {
        float phi    = kPI * static_cast<float>(stack) / static_cast<float>(stacks);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice) {
            float theta    = 2.0f * kPI * static_cast<float>(slice) / static_cast<float>(slices);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            float nx = sinPhi * cosTheta;
            float ny = cosPhi;
            float nz = sinPhi * sinTheta;

            MeshVertex v{};
            v.position = Float3{nx * radius, ny * radius, nz * radius};
            v.normal   = Float3{nx, ny, nz};
            v.texcoord[0] = static_cast<float>(slice) / static_cast<float>(slices);
            v.texcoord[1] = static_cast<float>(stack) / static_cast<float>(stacks);

            // Tangent along theta direction.
            v.tangent[0] = -sinTheta;
            v.tangent[1] =  0.0f;
            v.tangent[2] =  cosTheta;
            v.tangent[3] =  1.0f;

            data.vertices.push_back(v);
            data.bounds.merge(Vec3{v.position.x, v.position.y, v.position.z, 0.0f});
        }
    }

    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            uint32_t first  = stack * (slices + 1) + slice;
            uint32_t second = first + slices + 1;

            data.indices.push_back(first);
            data.indices.push_back(second);
            data.indices.push_back(first + 1);

            data.indices.push_back(second);
            data.indices.push_back(second + 1);
            data.indices.push_back(first + 1);
        }
    }

    data.submeshes.push_back(SubMesh{0, static_cast<uint32_t>(data.indices.size()), 0});
    return data;
}

// ===========================================================================
// Cube
// ===========================================================================

MeshData generate_cube(float h) {
    MeshData data;
    data.bounds = Aabb::empty();

    // 6 faces × 4 vertices = 24 vertices with per-face normals.
    struct FaceDef { float nx,ny,nz; float p[4][3]; float u[4][2]; };
    FaceDef faces[6] = {
        // +Z front
        {0,0,1,  {{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}},  {{0,1},{1,1},{1,0},{0,0}}},
        // -Z back
        {0,0,-1, {{h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}},  {{0,1},{1,1},{1,0},{0,0}}},
        // +X right
        {1,0,0,  {{h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}},  {{0,1},{1,1},{1,0},{0,0}}},
        // -X left
        {-1,0,0, {{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}},  {{0,1},{1,1},{1,0},{0,0}}},
        // +Y top
        {0,1,0,  {{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}},  {{0,1},{1,1},{1,0},{0,0}}},
        // -Y bottom
        {0,-1,0, {{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}},  {{0,1},{1,1},{1,0},{0,0}}},
    };

    for (auto& f : faces) {
        uint32_t base = static_cast<uint32_t>(data.vertices.size());
        for (int i = 0; i < 4; ++i) {
            MeshVertex v{};
            v.position = Float3{f.p[i][0], f.p[i][1], f.p[i][2]};
            v.normal   = Float3{f.nx, f.ny, f.nz};
            v.texcoord[0] = f.u[i][0];
            v.texcoord[1] = f.u[i][1];
            v.tangent[0] = 1; v.tangent[3] = 1;
            data.vertices.push_back(v);
            data.bounds.merge(Vec3{f.p[i][0], f.p[i][1], f.p[i][2], 0});
        }
        data.indices.push_back(base);
        data.indices.push_back(base+1);
        data.indices.push_back(base+2);
        data.indices.push_back(base);
        data.indices.push_back(base+2);
        data.indices.push_back(base+3);
    }

    data.submeshes.push_back(SubMesh{0, static_cast<uint32_t>(data.indices.size()), 0});
    return data;
}

// ===========================================================================
// Plane
// ===========================================================================

MeshData generate_plane(float width, float depth, uint32_t segments) {
    MeshData data;
    data.bounds = Aabb::empty();

    float hw = width * 0.5f;
    float hd = depth * 0.5f;

    for (uint32_t z = 0; z <= segments; ++z) {
        for (uint32_t x = 0; x <= segments; ++x) {
            float fx = static_cast<float>(x) / static_cast<float>(segments);
            float fz = static_cast<float>(z) / static_cast<float>(segments);

            MeshVertex v{};
            v.position = Float3{-hw + fx * width, 0.0f, -hd + fz * depth};
            v.normal   = Float3{0.0f, 1.0f, 0.0f};
            v.texcoord[0] = fx;
            v.texcoord[1] = fz;
            v.tangent[0] = 1; v.tangent[3] = 1;
            data.vertices.push_back(v);
            data.bounds.merge(Vec3{v.position.x, 0, v.position.z, 0});
        }
    }

    for (uint32_t z = 0; z < segments; ++z) {
        for (uint32_t x = 0; x < segments; ++x) {
            uint32_t i = z * (segments + 1) + x;
            data.indices.push_back(i);
            data.indices.push_back(i + segments + 1);
            data.indices.push_back(i + 1);
            data.indices.push_back(i + 1);
            data.indices.push_back(i + segments + 1);
            data.indices.push_back(i + segments + 2);
        }
    }

    data.submeshes.push_back(SubMesh{0, static_cast<uint32_t>(data.indices.size()), 0});
    return data;
}

} // namespace uz
