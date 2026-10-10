"""Fog shaft reference: a floor slab, one wall, the sun at 40 degrees elevation and a uniform
volumetric fog. Lighting block B, item 4: the control scene for the sun shadow in the froxel fog.

The wall (1 m thick, 12 m long, 8 m tall, long axis along Y) casts a slanted shadow prism towards
-X. The camera stands 2 m south of the wall's Y extent, 9 m west of the wall, 80 cm above the floor,
and looks north-east across the floor: the rays to the floor south of the wall (y < -600) never
enter the shadow, the rays to the footprint spend most of their length inside it (the first camera
position, 9 m south, gave only two pixels with more than half of the ray in shadow). `sun_visible` is the analytic shadow test scripts/rendering/test_froxel_fog.py uses to predict, per
pixel, which fraction of the fog in-scatter survives with the shadow on.

Sun convention (lighting block A, verified): `sunDir = rotation * Vec3::Forward()` (+X) and the sun's
rotation points towards the sun, so rotation [-e, 0, 0] is the sun at elevation e in the +X
direction: SUN_DIR = (cos e, 0, sin e) towards the sun, shadows fall towards -X.

Writes sample/content/meshes/reference/fogshaft_{floor,wall}.uzmesh,
sample/content/scenes/fog-shaft-reference.uzscene and scripts/rendering/fog-shaft-reference.uzproject.
Usage: python scripts/rendering/fog_shaft_reference.py
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import box_mesh_bytes  # noqa: E402

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CONTENT = os.path.join(REPO, 'sample', 'content')
SCENE = 'fog-shaft-reference'
PROJECT = os.path.join(REPO, 'scripts', 'rendering', f'{SCENE}.uzproject')
SUN_ELEVATION = 40.0
SUN_DIR = (math.cos(math.radians(SUN_ELEVATION)), 0.0, math.sin(math.radians(SUN_ELEVATION)))  # towards the sun
FLOOR = ((-5000.0, -5000.0, -20.0), (5000.0, 5000.0, 0.0))     # top face at z = 0
WALL = ((0.0, -600.0, 0.0), (100.0, 600.0, 800.0))
CAMERA_EYE = (-900.0, -800.0, 80.0)
CAMERA_TARGET = (-100.0, -400.0, 0.0)
FOV = 58.51
DENSITY = 0.0005          # per cm: 1/e at 20 m
ALBEDO = 0.5
MESHES = {'floor': FLOOR, 'wall': WALL}


def mesh_asset(name):
    return f'meshes/reference/fogshaft_{name}.uzmesh'


def sun_visible(points):
    """1.0 where the sun ray from the point misses the wall, 0.0 where it hits it (the floor cannot
    occlude a point above it). `points` is an array (..., 3) in world centimetres."""
    p = np.asarray(points, dtype=np.float64)
    (x0, y0, z0), (x1, y1, z1) = WALL
    sx, _, sz = SUN_DIR
    tx0 = (x0 - p[..., 0]) / sx
    tx1 = (x1 - p[..., 0]) / sx
    tz0 = (z0 - p[..., 2]) / sz
    tz1 = (z1 - p[..., 2]) / sz
    t_in = np.maximum(np.maximum(tx0, tz0), 0.0)
    t_out = np.minimum(tx1, tz1)
    in_y = (p[..., 1] >= y0) & (p[..., 1] <= y1)   # the sun ray keeps its y (SUN_DIR.y == 0)
    hit = in_y & (t_out > t_in)
    return np.where(hit, 0.0, 1.0)


def _entity(name, components):
    return {'name': name, 'components': components}


def _box_entity(name, key):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': mesh_asset(key)},
        'material': {'baseColorR': ALBEDO, 'baseColorG': ALBEDO, 'baseColorB': ALBEDO,
                     'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0},
    })


SCENE_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0, 'r.RadianceCache': 0, 'r.GI.FinalGather': 0,
               'r.GI.Reflections': 0, 'r.SSGI': 0, 'r.TAA': 1}


def scene_json():
    return {
        'name': 'Fog shaft reference', 'version': '1.0', 'cvars': dict(SCENE_CVARS),
        'entities': [
            _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, 0], 'scale': [1, 1, 1]},
                            'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 10, 'cast_shadows': True,
                                      'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
            _entity('Camera', {'transform': {'position': list(CAMERA_EYE), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                               'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(CAMERA_TARGET)}}),
            _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
            _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                                 'sky_light': {'intensity': 0.05, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                               'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
            _box_entity('Floor', 'floor'),
            _box_entity('Wall', 'wall'),
            _entity('VolumetricFog', {'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                                      'height_fog': {'enabled': True, 'enable_volumetric_fog': True, 'density': DENSITY,
                                                     'height_falloff': 0.0, 'base_height': 0.0, 'color': [1, 1, 1]}}),
        ],
    }


def project_json():
    return {
        'name': 'Fog shaft reference', 'version': '1.0', 'content_root': '../../sample/content',
        'default_scene': f'scenes/{SCENE}.uzscene',
        'window': {'width': 1920, 'height': 1080},
        'cvars': {**SCENE_CVARS, 'r.Fog': 0, 'r.TechPanel.Show': 0},
    }


def generate():
    written = []
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    for name, (lo, hi) in MESHES.items():
        path = os.path.join(CONTENT, *mesh_asset(name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    path = os.path.join(CONTENT, 'scenes', f'{SCENE}.uzscene')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(scene_json(), fh, indent=2)
    written.append(path)
    with open(PROJECT, 'w', encoding='utf-8') as fh:
        json.dump(project_json(), fh, indent=2)
    written.append(PROJECT)
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
