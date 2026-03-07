"""Generate a plane.gltf — flat quad on XZ plane, centered at origin.
   Size: 20x20 world units, subdivided 20x20 for smooth shadow reception.
   UVs: tiled 10x so checker texture repeats nicely."""
import struct, json, base64

SIZE    = 10.0   # half-extent: plane goes from -SIZE to +SIZE
DIVS    = 20     # subdivisions per axis
UV_TILE = 10.0   # UV tiling factor

vertices = []
indices  = []

# Generate vertices (XZ plane, Y=0)
for iz in range(DIVS + 1):
    for ix in range(DIVS + 1):
        x = -SIZE + (2.0 * SIZE * ix / DIVS)
        z = -SIZE + (2.0 * SIZE * iz / DIVS)
        y = 0.0
        # Normal up
        nx, ny, nz = 0.0, 1.0, 0.0
        # UV tiled
        u = (ix / DIVS) * UV_TILE
        v = (iz / DIVS) * UV_TILE
        # Tangent along +X
        tx, ty, tz, tw = 1.0, 0.0, 0.0, 1.0
        vertices.append((x, y, z, nx, ny, nz, u, v, tx, ty, tz, tw))

# Indices (two triangles per quad, CCW winding)
stride = DIVS + 1
for iz in range(DIVS):
    for ix in range(DIVS):
        tl = iz * stride + ix
        tr = tl + 1
        bl = tl + stride
        br = bl + 1
        # Triangle 1: TL, BL, TR
        indices.extend([tl, bl, tr])
        # Triangle 2: TR, BL, BR
        indices.extend([tr, bl, br])

# Pack binary
pos_data = b''
nrm_data = b''
uv_data  = b''
tan_data = b''
for v in vertices:
    pos_data += struct.pack('<3f', v[0], v[1], v[2])
    nrm_data += struct.pack('<3f', v[3], v[4], v[5])
    uv_data  += struct.pack('<2f', v[6], v[7])
    tan_data += struct.pack('<4f', v[8], v[9], v[10], v[11])

idx_data = b''
for i in indices:
    idx_data += struct.pack('<I', i)

# Merge into one buffer, track offsets
def aligned(offset, align=4):
    return (offset + align - 1) & ~(align - 1)

off0 = 0
off1 = aligned(off0 + len(pos_data))
off2 = aligned(off1 + len(nrm_data))
off3 = aligned(off2 + len(uv_data))
off4 = aligned(off3 + len(tan_data))
total = off4 + len(idx_data)

buf = bytearray(total)
buf[off0:off0+len(pos_data)] = pos_data
buf[off1:off1+len(nrm_data)] = nrm_data
buf[off2:off2+len(uv_data)]  = uv_data
buf[off3:off3+len(tan_data)] = tan_data
buf[off4:off4+len(idx_data)] = idx_data

b64 = base64.b64encode(bytes(buf)).decode()
nv  = len(vertices)
ni  = len(indices)

# Bounding box for positions
xs = [v[0] for v in vertices]
ys = [v[1] for v in vertices]
zs = [v[2] for v in vertices]

gltf = {
    "asset": {"version": "2.0", "generator": "gen_plane.py"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0}],
    "meshes": [{
        "primitives": [{
            "attributes": {
                "POSITION":  0,
                "NORMAL":    1,
                "TEXCOORD_0":2,
                "TANGENT":   3
            },
            "indices": 4,
            "mode": 4
        }]
    }],
    "accessors": [
        # 0: POSITION
        {"bufferView": 0, "componentType": 5126, "count": nv, "type": "VEC3",
         "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]},
        # 1: NORMAL
        {"bufferView": 1, "componentType": 5126, "count": nv, "type": "VEC3"},
        # 2: TEXCOORD_0
        {"bufferView": 2, "componentType": 5126, "count": nv, "type": "VEC2"},
        # 3: TANGENT
        {"bufferView": 3, "componentType": 5126, "count": nv, "type": "VEC4"},
        # 4: INDICES
        {"bufferView": 4, "componentType": 5125, "count": ni, "type": "SCALAR"}
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": off0, "byteLength": len(pos_data), "byteStride": 12, "target": 34962},
        {"buffer": 0, "byteOffset": off1, "byteLength": len(nrm_data), "byteStride": 12, "target": 34962},
        {"buffer": 0, "byteOffset": off2, "byteLength": len(uv_data),  "byteStride":  8, "target": 34962},
        {"buffer": 0, "byteOffset": off3, "byteLength": len(tan_data), "byteStride": 16, "target": 34962},
        {"buffer": 0, "byteOffset": off4, "byteLength": len(idx_data),                  "target": 34963},
    ],
    "buffers": [{"byteLength": total, "uri": f"data:application/octet-stream;base64,{b64}"}]
}

out = "e:/UzEngine/assets/meshes/plane.gltf"
with open(out, "w") as f:
    json.dump(gltf, f, indent=2)

print(f"Written {out}: {nv} vertices, {ni} indices")
