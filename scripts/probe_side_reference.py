"""followup-01, review of 2026-09-21, item 1: a reference with known geometry and known sides for the
radiance cache's probe classification.

One source of truth for the geometry. `--generate` writes the scenes, projects and the single-sided
shell mesh from the constants below; the study (`run_probe_side_reference.py`) imports the same
constants to classify every probe and label every pixel, so the truth the measurements are compared
with is the geometry that was rendered, not a description of it.

Probe planes. With r.GI.RadianceCache.GridExtent = 2000 the cache has 16 probes per axis and a spacing
S = 4000 / 15 cm, and RadianceCache::Update snaps the grid centre to a multiple of S, so on every axis
the probes lie at (j + 1/2) S for integer j whatever the camera position; the camera only selects
which sixteen planes are in the window. Geometry placed against those planes therefore puts probes at
known distances from known faces, and a capture from any camera sees the same probes.

Two scenes, both on a solid ground slab, sun from +x (35 degrees above the horizon):

* SOLID — three sealed rooms built from closed boxes (outward normals, the renderer's front faces).
  Every boundary except each room's east wall is a 100 cm slab centred on a probe plane (the ground
  slab is 266.67 cm thick, centred on z = -S/2), so the probes of those planes are inside solid by
  construction and the east wall is the only boundary with free-space probes on both sides. The
  east walls differ only in where the plane x = 400 falls:
    CORE      east wall x in [350, 450]: the plane lies inside the wall, 50 cm from each face;
    NEAR_OUT  east wall x in [330, 390]: the plane lies 10 cm outside the lit face;
    NEAR_IN   east wall x in [410, 470]: the plane lies 10 cm inside the dark face.
  The rooms are sealed, so the true irradiance of every interior surface from outside light is zero.
* SHELL — one room made of a single-sided box whose faces point inward (Sponza's situation: the
  exterior sees back faces). Its faces lie 30 cm from probe planes: the planes x = 400, y = +-400 and
  z = 400 lie 30 cm BEHIND the east, side and roof faces; the plane x = -133.33 lies 30 cm IN FRONT of
  the west face. Sides are defined by the faces, the convention of the renderer and of Majercik 2021
  section 6.1: behind a single-sided face is not free space of the scene.

Configurations (the band the current rule discards is config-dependent — review 2026-09-21):
  QA       r.GI.GDF.Resolution 64,  r.GI.MeshSdf.Resolution 32  (sponza-lighting-camera-c.uzproject)
  PROJECT  r.GI.GDF.Resolution 256, r.GI.MeshSdf.Resolution 128 (sample/projects/sponza.uzproject)

Usage: python scripts/probe_side_reference.py --generate
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, 'sample', 'content')

GRID_EXTENT = 2000.0
S = 2.0 * GRID_EXTENT / 15.0            # probe spacing: RadianceCache kProbesPerAxis = 16


def plane(j):
    """World coordinate of the j-th probe plane on any axis."""
    return (j + 0.5) * S


SUN_ELEVATION = 35.0                    # degrees; rotation (pitch, yaw) = (-elevation, azimuth from +x)
ALBEDO = 0.5
ROOM_TOP = 350.0                        # interior height; the roof slab is centred on z = plane(1) = 400
SLAB = 100.0                            # thickness of every non-test boundary slab (centred on a plane)
GROUND = ((-700.0, -1800.0, -S), (2900.0, 1800.0, 0.0))    # 266.67 cm thick, centred on the plane z = -S/2
GROUND_TILES = 3                        # 3 x 3 tiles of 1200 cm: a mesh's SDF bias grows with its size

# Every box is its own mesh, written in world coordinates and placed with an identity transform. A
# scaled unit cube would give the field the engine's conservative bound for non-uniform scale
# (global_distance_field.cpp, Gershgorin): distances along a slab's long axes are underestimated by
# its aspect ratio, which discards free probes metres away - a different defect from the one under
# test. At true size the field differs from the geometry only by the two half-voxel biases.

CONFIGS = {
    'QA': {'r.GI.GDF.Resolution': 64, 'r.GI.MeshSdf.Resolution': 32},
    'PROJECT': {'r.GI.GDF.Resolution': 256, 'r.GI.MeshSdf.Resolution': 128},
}

# The solid rooms: name, y centre (a multiple of S so the side walls at y0 +- 1.5 S are centred on
# planes), east wall x range. The interior spans x in [-350, east[0]], y in [y0 - 350, y0 + 350],
# z in [0, 350]. Rooms are 4 S apart and share no wall, so every lit face is flush with its own
# side walls (no fin from a neighbour) and a 167 cm exterior slot separates the rooms.
X400 = plane(1)
ROOMS = (
    ('CORE', -4.0 * S, (X400 - 50.0, X400 + 50.0)),
    ('NEAR_OUT', 0.0, (X400 - 70.0, X400 - 10.0)),
    ('NEAR_IN', 4.0 * S, (X400 + 10.0, X400 + 70.0)),
)
WEST = (plane(-2) - SLAB / 2.0, plane(-2) + SLAB / 2.0)          # [-450, -350]
HALF_IN = 1.5 * S - SLAB / 2.0                                    # 350: interior half-width in y

# The shell room: a single-sided box, faces inward.
SHELL_BOX = ((plane(-1) - 30.0, -(plane(1) - 30.0), -10.0), (X400 - 30.0, plane(1) - 30.0, plane(1) - 30.0))


def ground_boxes():
    (x0, y0, z0), (x1, y1, z1) = GROUND
    xs, ys = np.linspace(x0, x1, GROUND_TILES + 1), np.linspace(y0, y1, GROUND_TILES + 1)
    return [(f'Ground{i}{j}', (float(xs[i]), float(ys[j]), z0), (float(xs[i + 1]), float(ys[j + 1]), z1))
            for i in range(GROUND_TILES) for j in range(GROUND_TILES)]


def solid_boxes():
    """Closed boxes of the SOLID scene as (name, min, max)."""
    boxes = ground_boxes()
    z0, z1 = GROUND[0][2], plane(1) + SLAB / 2.0                    # walls reach into the ground and the roof
    for name, y0, (e0, e1) in ROOMS:
        ys0, ys1 = y0 - 1.5 * S - SLAB / 2.0, y0 + 1.5 * S + SLAB / 2.0
        boxes.append((f'{name}_West', (WEST[0], ys0, z0), (WEST[1], ys1, z1)))
        boxes.append((f'{name}_East', (e0, ys0, z0), (e1, ys1, z1)))
        boxes.append((f'{name}_South', (WEST[0], ys0, z0), (e1, ys0 + SLAB, z1)))
        boxes.append((f'{name}_North', (WEST[0], ys1 - SLAB, z0), (e1, ys1, z1)))
        boxes.append((f'{name}_Roof', (WEST[0], ys0, plane(1) - SLAB / 2.0), (e1, ys1, z1)))
    return boxes


def shell_scene_boxes():
    return ground_boxes()


# ── truth ────────────────────────────────────────────────────────────────────────────────────────

def _box_signed(p, lo, hi):
    """Signed Euclidean distance from points p (..., 3) to an axis-aligned box: < 0 inside."""
    lo, hi = np.asarray(lo, float), np.asarray(hi, float)
    c, h = (lo + hi) / 2.0, (hi - lo) / 2.0
    q = np.abs(p - c) - h
    outside = np.linalg.norm(np.maximum(q, 0.0), axis=-1)
    inside = np.minimum(np.max(q, axis=-1), 0.0)
    return outside + inside


def truth(scene, p):
    """Known class of points p (..., 3) and the unsigned distance to the nearest surface.

    Classes: 'inside' (inside a closed box), 'behind' (outside the single-sided shell box, i.e. on
    the back side of its faces), 'free'. For the SHELL scene `shell_distance` is the distance to the
    shell's faces, reported separately because the ground may be nearer.
    """
    p = np.asarray(p, float)
    boxes = solid_boxes() if scene == 'SOLID' else shell_scene_boxes()
    d = np.stack([_box_signed(p, lo, hi) for _, lo, hi in boxes], axis=-1)
    inside = (d < 0.0).any(-1)
    dist = np.abs(d).min(-1)
    cls = np.where(inside, 'inside', 'free').astype(object)
    shell_d = None
    if scene == 'SHELL':
        sd = _box_signed(p, *SHELL_BOX)
        shell_d = np.abs(sd)
        dist = np.minimum(dist, shell_d)
        cls = np.where(~inside & (sd > 0.0), 'behind', cls)
    return cls, dist, shell_d


# ── pixel labels (world positions from the region dump) ─────────────────────────────────────────

def label_pixels(scene, camera, pos, tol=2.0):
    """Dict label -> boolean mask over pixels with world positions `pos` (h, w, 3)."""
    x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
    finite = np.isfinite(pos).all(-1)
    labels = {}
    if scene == 'SOLID':
        for name, y0, (e0, e1) in ROOMS:
            ym = np.abs(y - y0) <= HALF_IN - 100.0                    # keep 100 cm clear of the side walls
            if camera == 'EXT':
                labels[f'{name}:lit_face'] = finite & (np.abs(x - e1) < tol) & ym & (z > 30) & (z < ROOM_TOP - 30)
                labels[f'{name}:lit_ground'] = finite & (np.abs(z) < tol) & ym & (x > e1 + 5) & (x < e1 + S - 5)
            elif camera == name:
                inside = finite & (x > WEST[1] - tol) & (x < e0 + tol) & (np.abs(y - y0) < HALF_IN + tol) \
                    & (z > -tol) & (z < ROOM_TOP + tol)
                labels[f'{name}:dark_face'] = inside & (np.abs(x - e0) < tol) & ym & (z > 30) & (z < ROOM_TOP - 30)
                labels[f'{name}:dark_floor'] = inside & (np.abs(z) < tol) & ym & (x < e0 - 5) & (x > e0 - S + 5)
                labels[f'{name}:dark_all'] = inside
    else:
        (x0, y0_, z0), (x1, y1, z1) = SHELL_BOX
        inside = finite & (x > x0 - tol) & (x < x1 + tol) & (y > y0_ - tol) & (y < y1 + tol) & (z > -tol) & (z < z1 + tol)
        ym = np.abs(y) <= y1 - 100.0
        if camera == 'INT_E':
            labels['SHELL:east_face'] = inside & (np.abs(x - x1) < tol) & ym & (z > 30) & (z < z1 - 30)
            labels['SHELL:floor_east'] = inside & (np.abs(z) < tol) & ym & (x < x1 - 5) & (x > x1 - S + 5)
            labels['SHELL:all_e'] = inside
        else:
            labels['SHELL:west_face'] = inside & (np.abs(x - x0) < tol) & ym & (z > 30) & (z < z1 - 30)
            labels['SHELL:floor_west'] = inside & (np.abs(z) < tol) & ym & (x > x0 + 5) & (x < x0 + S - 5)
            labels['SHELL:all_w'] = inside
    return labels


# ── cameras ──────────────────────────────────────────────────────────────────────────────────────

CAMERAS = {
    ('SOLID', 'EXT'): ((1700.0, 0.0, 250.0), (400.0, 0.0, 150.0), 60.0),
    ('SOLID', 'CORE'): ((-300.0, ROOMS[0][1], 175.0), (400.0, ROOMS[0][1], 150.0), 50.0),
    ('SOLID', 'NEAR_OUT'): ((-300.0, ROOMS[1][1], 175.0), (400.0, ROOMS[1][1], 150.0), 50.0),
    ('SOLID', 'NEAR_IN'): ((-300.0, ROOMS[2][1], 175.0), (400.0, ROOMS[2][1], 150.0), 50.0),
    ('SHELL', 'INT_E'): ((-100.0, 0.0, 175.0), (370.0, 0.0, 150.0), 50.0),
    ('SHELL', 'INT_W'): ((300.0, 0.0, 175.0), (-163.33, 0.0, 150.0), 50.0),
}
# Rendered small on purpose: the labels are whole surfaces and the whole frame is dumped.
WIDTH, HEIGHT = 640, 360


def project_rect(camera_key, points, margin=8):
    """Screen rectangle (x, y, w, h) enclosing world points, pinhole model (vertical fov, z up)."""
    eye, target, fov = CAMERAS[camera_key]
    eye, target = np.asarray(eye, float), np.asarray(target, float)
    f = target - eye
    f /= np.linalg.norm(f)
    r = np.cross(f, (0.0, 0.0, 1.0))
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    t = math.tan(math.radians(fov) / 2.0)
    aspect = WIDTH / HEIGHT
    pts = np.asarray(points, float) - eye
    xc, yc, zc = pts @ r, pts @ u, pts @ f
    sx = (xc / zc / (t * aspect) * 0.5 + 0.5) * WIDTH
    sy = (0.5 - yc / zc / t * 0.5) * HEIGHT
    x0 = int(max(0, math.floor(sx.min()) - margin))
    y0 = int(max(0, math.floor(sy.min()) - margin))
    x1 = int(min(WIDTH, math.ceil(sx.max()) + margin))
    y1 = int(min(HEIGHT, math.ceil(sy.max()) + margin))
    return x0, y0, x1 - x0, y1 - y0


def _corners(lo, hi):
    return [(a, b, c) for a in (lo[0], hi[0]) for b in (lo[1], hi[1]) for c in (lo[2], hi[2])]


def regions(camera_key):
    """Region-dump rectangles: the whole (small) frame; pixels are labelled by world position."""
    return [('FRAME', 0, 0, WIDTH, HEIGHT)]


# ── generation ───────────────────────────────────────────────────────────────────────────────────

def _entity(name, components):
    return {'name': name, 'components': components}


def _box_entity(name, mesh):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': mesh},
        'material': {'baseColorR': ALBEDO, 'baseColorG': ALBEDO, 'baseColorB': ALBEDO,
                     'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0},
    })


def mesh_asset(scene, name):
    return f'meshes/reference/{scene.lower()}_{name.lower()}.uzmesh'


def scene_json(scene, camera):
    eye, target, fov = CAMERAS[(scene, camera)]
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 10, 'cast_shadows': True,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(eye), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': fov, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(target)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 1, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1,
                                           'min_occlusion': 0.01, 'lower_hemisphere_is_black': False,
                                           'lower_hemisphere_color': [0.12, 0.11, 0.1]}}),
    ]
    boxes = solid_boxes() if scene == 'SOLID' else shell_scene_boxes()
    ents += [_box_entity(n, mesh_asset(scene, n)) for n, _, _ in boxes]
    if scene == 'SHELL':
        ents.append(_box_entity('Shell', mesh_asset(scene, 'shell')))
    return {'name': f'Probe side reference ({scene}, {camera})', 'version': '1.0',
            'cvars': {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0}, 'entities': ents}


def scene_name(scene, camera):
    return f'probe-side-reference-{scene.lower()}-{camera.lower().replace("_", "")}'


def project_json(scene, camera):
    return {'name': f'Probe side reference ({scene}, {camera})', 'version': '1.0', 'content_root': '../sample/content',
            'default_scene': f'scenes/{scene_name(scene, camera)}.uzscene',
            'window': {'title': 'UzEngine - probe side reference', 'width': WIDTH, 'height': HEIGHT},
            'cvars': {'r.RadianceCache': 1, 'r.GI.MeshSdf.TwoSided': 1, 'r.GI.RadianceCache.GridExtent': GRID_EXTENT}}


def project_path(scene, camera):
    return os.path.join(REPO, 'scripts', f'{scene_name(scene, camera)}.uzproject')


def box_mesh_bytes(lo, hi, inward=False):
    """A closed box mesh in world coordinates, faces as MeshBuilder::CreateCube (mesh_builder.cpp).

    `inward` negates every normal and reverses every triangle's winding: the single-sided shell,
    whose inside is exactly what the outside of the outward box is.
    """
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    p = {'000': (x0, y0, z0), '100': (x1, y0, z0), '110': (x1, y1, z0), '010': (x0, y1, z0),
         '001': (x0, y0, z1), '101': (x1, y0, z1), '111': (x1, y1, z1), '011': (x0, y1, z1)}
    faces = [  # corners and outward normal/tangent exactly as CreateCube
        (('100', '110', '111', '101'), (1, 0, 0), (0, 1, 0, 1)),
        (('010', '000', '001', '011'), (-1, 0, 0), (0, -1, 0, 1)),
        (('110', '010', '011', '111'), (0, 1, 0), (1, 0, 0, 1)),
        (('000', '100', '101', '001'), (0, -1, 0), (-1, 0, 0, 1)),
        (('001', '101', '111', '011'), (0, 0, 1), (1, 0, 0, 1)),
        (('010', '110', '100', '000'), (0, 0, -1), (1, 0, 0, 1)),
    ]
    verts, idx = [], []
    uvs = ((0, 0), (1, 0), (1, 1), (0, 1))
    for corners, n, t in faces:
        base = len(verts)
        normal = tuple(-v for v in n) if inward else n
        for c, uv in zip(corners, uvs):
            verts.append(p[c] + normal + uv + t)
        # CreateCube at one subdivision: (c0, c2, c1) and (c0, c3, c2); reversed for the shell.
        for a, b, c in ((0, 2, 1), (0, 3, 2)):
            idx += [base + a, base + c, base + b] if inward else [base + a, base + b, base + c]
    vdata = b''.join(struct.pack('<3f3f2f4f', *v) for v in verts)
    idata = struct.pack(f'<{len(idx)}I', *idx)
    header = struct.pack('<8I6f2QI6I28x', 0x484D5A55, 2, 0, 48, len(verts), 1, len(idx), 1,
                         x0, y0, z0, x1, y1, z1, 128, 128 + len(vdata), 1, 0, 0, 0, 0, 0, 0)
    assert len(header) == 128
    return header + vdata + idata


def generate():
    written = []
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    meshes = [('SOLID', n, lo, hi, False) for n, lo, hi in solid_boxes()]
    meshes += [('SHELL', n, lo, hi, False) for n, lo, hi in shell_scene_boxes()]
    meshes.append(('SHELL', 'shell', SHELL_BOX[0], SHELL_BOX[1], True))
    for scene, name, lo, hi, inward in meshes:
        path = os.path.join(CONTENT, *mesh_asset(scene, name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi, inward))
        written.append(path)
    for scene, camera in CAMERAS:
        path = os.path.join(CONTENT, 'scenes', f'{scene_name(scene, camera)}.uzscene')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(scene_json(scene, camera), fh, indent=2)
        written.append(path)
        path = project_path(scene, camera)
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(project_json(scene, camera), fh, indent=2)
        written.append(path)
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--generate', action='store_true')
    args = ap.parse_args()
    if args.generate:
        for p in generate():
            print(os.path.relpath(p, REPO))
    for key in CAMERAS:
        print(key, regions(key))


if __name__ == '__main__':
    main()
