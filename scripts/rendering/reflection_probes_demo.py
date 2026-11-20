"""Reflection probes demo (phase3a 3.37.0, item 1.9): an open-fronted room with coloured walls and two
reflection probes, metallic spheres whose reflections depend on where they stand, and a chrome strip
that crosses both probes' fade zones.

Geometry (cm, Z up; the room is open to the south, y = -400):
  ROOM  interior x in [-400, 400], y in [-400, 400], z in [0, 300]; walls 20 cm thick:
        west (x = -400) red, east (x = 400) blue, north (y = 400) green, ceiling (z = 300) white.
  GROUND [-3000, 3000]^2 x [-20, 0], grey dielectric: the room floor and the outdoors.
  Probe ROOM     at (0, 0, 150): influence 650, blend 200, box half-extents (400, 400, 150) = the
                 interior (parallax-corrected), priority 1.
  Probe OUTDOOR  at (0, -1300, 150): influence 900, blend 300, no box (an infinite environment),
                 priority 0. The two influence spheres overlap in the doorway, y in [-650, -400].
  Spheres (radius 60, metallic 1, base colour 0.95, centres 70 above the ground):
    room-mirror   (-250, 120, 70)  roughness 0.05  90 cm from the red wall: the parallax case.
    room-rough    (150, 60, 70)    roughness 0.5   the blurred probe.
    overlap       (-230, -520, 70) roughness 0.05  both probes and a sky share (off the strip).
    outdoor       (250, -1300, 70) roughness 0.05  the outdoor probe only.
    far           (0, -2600, 70)   roughness 0.05  outside every probe: the sky IBL.
  STRIP chrome slab x in [-150, 150], y in [-2800, 300], z in [0, 10]: a continuous surface from the
        room through both fade zones to no probe at all (the blend profile).

Writes sample/content/meshes/reference/reflection-probes-*.uzmesh, the demo scene
sample/content/scenes/reflection_probes.uzscene (camera `room-south`) and
scripts/rendering/reflection-probes.uzproject. With `--validation-out DIR` it also writes a content root
under DIR with one scene per camera, with the probes on and with them disabled (`enabled: false`), the
VARIANTS (probes on), and the matching projects (`reflection-probes-<camera>[-off].uzproject`) for
scripts/rendering/test_reflection_probes.py.

Usage: python scripts/rendering/reflection_probes_demo.py [--validation-out DIR]
"""
import argparse
import json
import math
import os
import shutil
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import box_mesh_bytes  # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CONTENT = os.path.join(REPO, 'sample', 'content')
SCENE = 'reflection_probes'
PROJECT = os.path.join(REPO, 'scripts', 'rendering', 'reflection-probes.uzproject')
MESH_PREFIX = 'meshes/reference/reflection-probes-'
W, H = 1280, 720
FOV = 58.51
SUN_ELEVATION = 40.0

WALL = 20.0
ROOM_LO, ROOM_HI = (-400.0, -400.0, 0.0), (400.0, 400.0, 300.0)
BOXES = {
    # name: ((lo, hi), colour, roughness, metallic)
    'ground': (((-3000.0, -3000.0, -20.0), (3000.0, 3000.0, 0.0)), (0.5, 0.5, 0.5), 1.0, 0.0),
    'wall-west': (((-420.0, -420.0, 0.0), (-400.0, 420.0, 320.0)), (0.8, 0.15, 0.1), 0.8, 0.0),
    'wall-east': (((400.0, -420.0, 0.0), (420.0, 420.0, 320.0)), (0.15, 0.25, 0.85), 0.8, 0.0),
    'wall-north': (((-420.0, 400.0, 0.0), (420.0, 420.0, 320.0)), (0.15, 0.7, 0.15), 0.8, 0.0),
    'ceiling': (((-420.0, -420.0, 300.0), (420.0, 420.0, 320.0)), (0.8, 0.8, 0.8), 0.8, 0.0),
    'strip': (((-150.0, -2800.0, 0.0), (150.0, 300.0, 10.0)), (0.95, 0.95, 0.95), 0.05, 1.0),
}
SPHERE_RADIUS = 60.0
SPHERES = {
    # name: (centre, roughness)
    'room-mirror': ((-250.0, 120.0, 70.0), 0.05),
    'room-rough': ((150.0, 60.0, 70.0), 0.5),
    'overlap': ((-230.0, -520.0, 70.0), 0.05),
    'outdoor': ((250.0, -1300.0, 70.0), 0.05),
    'far': ((0.0, -2600.0, 70.0), 0.05),
}
SPHERE_COLOUR = (0.95, 0.95, 0.95)
PROBES = {
    'ProbeRoom': {'position': [0.0, 0.0, 150.0], 'influence_radius': 650.0, 'blend_distance': 200.0,
                  'box_extents': [400.0, 400.0, 150.0], 'resolution': 128, 'priority': 1},
    'ProbeOutdoor': {'position': [0.0, -1300.0, 150.0], 'influence_radius': 900.0, 'blend_distance': 300.0,
                     'resolution': 128, 'priority': 0},
}
CAMERAS = {
    # name: (eye, target)
    'room-south': ((40.0, -360.0, 170.0), (-50.0, 60.0, 80.0)),
    'room-east': ((330.0, 300.0, 200.0), (-150.0, 100.0, 80.0)),
    'overlap': ((200.0, -1000.0, 230.0), (-230.0, -520.0, 80.0)),
    'outdoor': ((650.0, -1900.0, 250.0), (0.0, -1300.0, 80.0)),
    'far': ((300.0, -2300.0, 200.0), (0.0, -2600.0, 80.0)),
    'strip': ((1800.0, -1450.0, 1800.0), (0.0, -1450.0, 0.0)),
}
DEMO_CAMERA = 'room-south'
# Validation-only variants (probes on): name -> (camera, sphere roughness overrides). room-south-smooth
# puts the rough sphere at the mirror's roughness, so rough vs smooth is compared at one position.
VARIANTS = {'room-south-smooth': ('room-south', {'room-rough': 0.05})}
# The split-sum IBL layer is what the probes replace: the pinned cvars keep the other reflection
# layers out of the measurement (r.GI.Reflections, r.SSR, and the ray-traced reflections, which on DXR
# hardware take the smooth surfaces by default with r.SSR off) and switch off the environment MIS,
# which replaces the split-sum specular on smooth metals with a sky-only estimate (see docs/engine/
# 12-rendering/17-lighting.md, Reflection Probes).
SCENE_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0}
PROJECT_CVARS = {**SCENE_CVARS, 'r.RadianceCache': 0, 'r.GI.FinalGather': 0, 'r.GI.Reflections': 0,
                 'r.SSGI': 0, 'r.SSR': 0, 'r.RayTracing.Reflections': 0, 'r.EnvMIS': 0, 'r.TAA': 1,
                 'r.Fog': 0, 'r.TechPanel.Show': 0}


def mesh_asset(name):
    return f'{MESH_PREFIX}{name}.uzmesh'


def sphere_mesh_bytes(centre, radius, rings=24, segments=48):
    """A UV sphere in world coordinates, the same vertex layout and header as box_mesh_bytes
    (position, normal, uv, tangent; 48-byte stride; uint32 indices) and the same winding (the
    cross product of a front face's edges points inward, as MeshBuilder::CreateCube)."""
    cx, cy, cz = centre
    verts = []
    for i in range(rings + 1):
        theta = math.pi * i / rings            # 0 at +Z (the top)
        st, ct = math.sin(theta), math.cos(theta)
        for j in range(segments + 1):
            phi = 2.0 * math.pi * j / segments
            sp, cp = math.sin(phi), math.cos(phi)
            n = (st * cp, st * sp, ct)
            p = (cx + radius * n[0], cy + radius * n[1], cz + radius * n[2])
            t = (-sp, cp, 0.0, 1.0)
            verts.append(p + n + (j / segments, i / rings) + t)
    idx = []

    def emit(a, b, c):
        pa, pb, pc = (verts[k][:3] for k in (a, b, c))
        e1 = tuple(pb[k] - pa[k] for k in range(3))
        e2 = tuple(pc[k] - pa[k] for k in range(3))
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        outward = tuple((pa[k] + pb[k] + pc[k]) / 3.0 - centre[k] for k in range(3))
        if sum(cross[k] * outward[k] for k in range(3)) > 0.0:
            b, c = c, b
        idx.extend((a, b, c))

    for i in range(rings):
        for j in range(segments):
            p00 = i * (segments + 1) + j
            p01 = p00 + 1
            p10 = p00 + segments + 1
            p11 = p10 + 1
            if i > 0:
                emit(p00, p10, p11)
            if i < rings - 1:
                emit(p00, p11, p01)
    vdata = b''.join(struct.pack('<3f3f2f4f', *v) for v in verts)
    idata = struct.pack(f'<{len(idx)}I', *idx)
    header = struct.pack('<8I6f2QI6I28x', 0x484D5A55, 2, 0, 48, len(verts), 1, len(idx), 1,
                         cx - radius, cy - radius, cz - radius, cx + radius, cy + radius, cz + radius,
                         128, 128 + len(vdata), 1, 0, 0, 0, 0, 0, 0)
    assert len(header) == 128
    return header + vdata + idata


def _entity(name, components):
    return {'name': name, 'components': components}


def _mesh_entity(name, asset, colour, roughness, metallic):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': asset},
        'material': {'baseColorR': colour[0], 'baseColorG': colour[1], 'baseColorB': colour[2],
                     'roughnessMultiplier': roughness, 'metallicMultiplier': metallic},
    })


def _probe_entity(name, probe, enabled):
    params = {k: v for k, v in probe.items() if k != 'position'}
    if not enabled:
        params['enabled'] = False
    return _entity(name, {'transform': {'position': list(probe['position'])}, 'reflection_probe': params})


def scene_json(camera, probes_enabled=True, title=None, roughness_overrides=None):
    eye, target = CAMERAS[camera]
    roughness_overrides = roughness_overrides or {}
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 10, 'cast_shadows': True,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(eye), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(target)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 1.0, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                           'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
    ]
    for name, (_, colour, roughness, metallic) in BOXES.items():
        ents.append(_mesh_entity(name, mesh_asset(name), colour, roughness, metallic))
    for name, (_, roughness) in SPHERES.items():
        ents.append(_mesh_entity(f'sphere-{name}', mesh_asset(f'sphere-{name}'), SPHERE_COLOUR,
                                 roughness_overrides.get(name, roughness), 1.0))
    for name, probe in PROBES.items():
        ents.append(_probe_entity(name, probe, probes_enabled))
    return {'name': title or f'Reflection probes ({camera})', 'version': '1.0', 'cvars': dict(SCENE_CVARS), 'entities': ents}


def project_json(scene, title, content_root='../../sample/content', probes_on=False):
    """`probes_on` pins r.ReflectionProbes=1 (the shipped demo: the CVar defaults to 0, probes off). The
    validation projects leave it at the default; test_reflection_probes.py sets it per capture, so the
    same project serves the probes-on gates and the OFF byte-identity set."""
    return {'name': title, 'version': '1.0', 'content_root': content_root,
            'default_scene': f'scenes/{scene}.uzscene',
            'window': {'title': 'UzEngine - reflection probes', 'width': W, 'height': H},
            'cvars': {**({'r.ReflectionProbes': 1} if probes_on else {}), **PROJECT_CVARS}}


def mesh_blobs():
    blobs = {name: box_mesh_bytes(lo, hi) for name, ((lo, hi), _, _, _) in BOXES.items()}
    for name, (centre, _) in SPHERES.items():
        blobs[f'sphere-{name}'] = sphere_mesh_bytes(centre, SPHERE_RADIUS)
    return blobs


def validation_scene(camera, probes_enabled=True):
    return f'reflection-probes-{camera}' + ('' if probes_enabled else '-off')


def generate(validation_out=None):
    written = []
    blobs = mesh_blobs()
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    for name, blob in blobs.items():
        path = os.path.join(CONTENT, *mesh_asset(name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(blob)
        written.append(path)
    path = os.path.join(CONTENT, 'scenes', f'{SCENE}.uzscene')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(scene_json(DEMO_CAMERA, True, 'Reflection probes'), fh, indent=2)
    written.append(path)
    with open(PROJECT, 'w', encoding='utf-8') as fh:
        json.dump(project_json(SCENE, 'Reflection probes', probes_on=True), fh, indent=2)
    written.append(PROJECT)

    if validation_out:
        content = os.path.join(validation_out, 'content')
        os.makedirs(os.path.join(content, 'scenes'), exist_ok=True)
        os.makedirs(os.path.join(content, 'meshes', 'reference'), exist_ok=True)
        for name in blobs:
            shutil.copyfile(os.path.join(CONTENT, *mesh_asset(name).split('/')),
                            os.path.join(content, *mesh_asset(name).split('/')))
        for camera in CAMERAS:
            for enabled in (True, False):
                scene = validation_scene(camera, enabled)
                path = os.path.join(content, 'scenes', f'{scene}.uzscene')
                with open(path, 'w', encoding='utf-8') as fh:
                    json.dump(scene_json(camera, enabled), fh, indent=2)
                written.append(path)
                path = os.path.join(validation_out, f'{scene}.uzproject')
                with open(path, 'w', encoding='utf-8') as fh:
                    json.dump(project_json(scene, f'Reflection probes ({camera}{"" if enabled else ", probes off"})',
                                           content_root='content'), fh, indent=2)
                written.append(path)
        for variant, (camera, overrides) in VARIANTS.items():
            scene = validation_scene(variant)
            path = os.path.join(content, 'scenes', f'{scene}.uzscene')
            with open(path, 'w', encoding='utf-8') as fh:
                json.dump(scene_json(camera, True, f'Reflection probes ({variant})', overrides), fh, indent=2)
            written.append(path)
            path = os.path.join(validation_out, f'{scene}.uzproject')
            with open(path, 'w', encoding='utf-8') as fh:
                json.dump(project_json(scene, f'Reflection probes ({variant})', content_root='content'), fh, indent=2)
            written.append(path)
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--validation-out', default=None, help='also write the per-camera on/off scenes and projects here')
    args = ap.parse_args()
    for p in generate(args.validation_out):
        print('wrote', os.path.relpath(p, REPO) if p.startswith(REPO) else p)


if __name__ == '__main__':
    main()
