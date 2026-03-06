"""Generate a proper sphere.gltf with correct outward-facing normals, UVs, and tangents."""
import struct, json, base64, math, sys

SLICES = 64
STACKS = 32
RADIUS = 1.0

vertices = []
indices = []

# Generate vertices
for stack in range(STACKS + 1):
    phi = math.pi * stack / STACKS
    sin_phi = math.sin(phi)
    cos_phi = math.cos(phi)
    for sl in range(SLICES + 1):
        theta = 2.0 * math.pi * sl / SLICES
        sin_t = math.sin(theta)
        cos_t = math.cos(theta)
        nx = sin_phi * cos_t
        ny = cos_phi
        nz = sin_phi * sin_t
        px, py, pz = nx * RADIUS, ny * RADIUS, nz * RADIUS
        u = sl / SLICES
        v = stack / STACKS
        # Tangent along theta direction
        tx, ty, tz = -sin_t, 0.0, cos_t
        tw = 1.0  # handedness
        vertices.append((px, py, pz, nx, ny, nz, u, v, tx, ty, tz, tw))

# Generate indices (CCW winding for glTF)
for stack in range(STACKS):
    for sl in range(SLICES):
        first = stack * (SLICES + 1) + sl
        second = first + SLICES + 1
        # Triangle 1
        indices.extend([first, second, first + 1])
        # Triangle 2
        indices.extend([second, second + 1, first + 1])

# Pack binary data
pos_data = b''
nrm_data = b''
uv_data = b''
tan_data = b''
for v in vertices:
    pos_data += struct.pack('<3f', v[0], v[1], v[2])
    nrm_data += struct.pack('<3f', v[3], v[4], v[5])
    uv_data += struct.pack('<2f', v[6], v[7])
    tan_data += struct.pack('<4f', v[8], v[9], v[10], v[11])

idx_data = struct.pack(f'<{len(indices)}I', *indices)

# Compute min/max for positions
pos_min = [min(v[i] for v in vertices) for i in range(3)]
pos_max = [max(v[i] for v in vertices) for i in range(3)]

# Build buffer
buf = idx_data + pos_data + nrm_data + uv_data + tan_data

# Offsets
idx_offset = 0
idx_len = len(idx_data)
pos_offset = idx_len
pos_len = len(pos_data)
nrm_offset = pos_offset + pos_len
nrm_len = len(nrm_data)
uv_offset = nrm_offset + nrm_len
uv_len = len(uv_data)
tan_offset = uv_offset + uv_len
tan_len = len(tan_data)

vc = len(vertices)
ic = len(indices)

gltf = {
    "asset": {"version": "2.0", "generator": "UzEngine sphere generator"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "Sphere"}],
    "meshes": [{
        "primitives": [{
            "attributes": {
                "POSITION": 1,
                "NORMAL": 2,
                "TEXCOORD_0": 3,
                "TANGENT": 4
            },
            "indices": 0,
            "mode": 4
        }],
        "name": "Sphere"
    }],
    "accessors": [
        {"bufferView": 0, "componentType": 5125, "count": ic, "type": "SCALAR"},
        {"bufferView": 1, "componentType": 5126, "count": vc, "type": "VEC3",
         "min": pos_min, "max": pos_max},
        {"bufferView": 2, "componentType": 5126, "count": vc, "type": "VEC3"},
        {"bufferView": 3, "componentType": 5126, "count": vc, "type": "VEC2"},
        {"bufferView": 4, "componentType": 5126, "count": vc, "type": "VEC4"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": idx_offset, "byteLength": idx_len, "target": 34963},
        {"buffer": 0, "byteOffset": pos_offset, "byteLength": pos_len, "target": 34962},
        {"buffer": 0, "byteOffset": nrm_offset, "byteLength": nrm_len, "target": 34962},
        {"buffer": 0, "byteOffset": uv_offset, "byteLength": uv_len, "target": 34962},
        {"buffer": 0, "byteOffset": tan_offset, "byteLength": tan_len, "target": 34962},
    ],
    "buffers": [{"uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode(), "byteLength": len(buf)}]
}

out_path = r"E:\UzEngine\assets\meshes\sphere.gltf"
with open(out_path, 'w') as f:
    json.dump(gltf, f)
print(f"Generated sphere.gltf: {vc} verts, {ic//3} tris, {len(buf)} bytes")
print("DONE")
