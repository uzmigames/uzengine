"""Reflection reference: a mirror floor, three rough boxes and a pillar under a sun at 40 degrees, and the
same scene without the floor seen from the mirrored camera. Lighting block B, item 5 (layered reflections).

Planar-reflection identity: the reflection a mirror floor shows at a pixel is what the camera mirrored
through the floor plane sees along the same ray, continued past the plane. The engine builds cameras with
the world up (0, 0, 1), so the mirrored camera's image is the true mirror image rotated by 180 degrees
(both the right and the up vectors flip); `mirror_pixel` maps a main-view pixel to the mirrored view's
pixel, and the harness verifies the mapping with the dumps' world positions (gate R0).

Geometry (cm, Z up): floor top at z = 0, FLOOR = [-1500, 1500]^2 x [-10, 0], chrome (metallic 1,
roughness 0.02, base colour 0.95); BOXES = red (-300, 150..250), green (0, 350..500, 150 tall),
blue (300, 200..300); PILLAR = (-650, 450) 60 x 60 x 450 cm; all rough dielectrics (roughness 0.7).
Camera CAMERA_EYE = (0, -900, 180) looking at (0, 250, 0); mirrored camera (0, -900, -180) looking at
the same point.

Writes sample/content/meshes/reference/reflection_*.uzmesh, sample/content/scenes/reflection-reference.uzscene,
sample/content/scenes/reflection-reference-mirror.uzscene, scripts/reflection-reference.uzproject and
scripts/reflection-reference-mirror.uzproject. Block B.5b adds the rough metal floors (FLOOR_VARIANTS: the
same scene with the floor at roughness 0.3 and 0.5, `reflection-reference-rough30` / `-rough50`), the
energy references of the environment-MIS specular. Usage: python scripts/reflection_reference.py
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import box_mesh_bytes  # noqa: E402
import fog_shaft_reference as shaft  # noqa: E402

REPO = shaft.REPO
CONTENT = shaft.CONTENT
SCENE = 'reflection-reference'
SCENE_MIRROR = 'reflection-reference-mirror'
PROJECT = os.path.join(REPO, 'scripts', f'{SCENE}.uzproject')
PROJECT_MIRROR = os.path.join(REPO, 'scripts', f'{SCENE_MIRROR}.uzproject')
SUN_ELEVATION = shaft.SUN_ELEVATION
SUN_DIR = shaft.SUN_DIR
FLOOR = ((-1500.0, -1500.0, -10.0), (1500.0, 1500.0, 0.0))
FLOOR_F0 = 0.95                  # chrome: base colour = F0
FLOOR_ROUGHNESS = 0.02
FLOOR_VARIANTS = {'rough30': 0.3, 'rough50': 0.5}   # block B.5b: rough metal floors (same F0)
HDRI_VARIANT = 'hdri'                                # block B.5b: the chrome floor under a PMREM environment
HDRI_CUBEMAP = 'textures/ballroom_2k.pmrem.ktx2'
WARM_VARIANT = 'warm'                                # block B.5c: a coloured sun (the sky capture per channel)
WARM_SUN_COLOUR = (1.0, 0.7, 0.4)
BOXES = {
    'red':   (((-350.0, 150.0, 0.0), (-250.0, 250.0, 100.0)), (0.8, 0.15, 0.1)),
    'green': (((-50.0, 350.0, 0.0), (50.0, 500.0, 150.0)), (0.15, 0.7, 0.15)),
    'blue':  (((250.0, 200.0, 0.0), (350.0, 300.0, 100.0)), (0.15, 0.25, 0.85)),
    'pillar': (((-680.0, 420.0, 0.0), (-620.0, 480.0, 450.0)), (0.7, 0.7, 0.7)),
}
BOX_ROUGHNESS = 0.7
CAMERA_EYE = (0.0, -900.0, 180.0)
CAMERA_TARGET = (0.0, 250.0, 0.0)
FOV = 58.51
W, H = 1920, 1080


def mirror_point(p):
    p = np.asarray(p, dtype=np.float64).copy()
    p[..., 2] = -p[..., 2]
    return p


def mirror_pixel(x, y):
    """Main-view pixel -> mirrored-view pixel: a vertical flip (measured on the dumps: 90 % of the
    geometry hits within 0.5 degrees of the reflected ray, median 0.08 degrees; the other candidate
    mappings give 0-3 %)."""
    return x, H - 1 - y


def mesh_asset(name):
    return f'meshes/reference/reflection_{name}.uzmesh'


def variant_scene(key):
    return f'{SCENE}-{key}'


def variant_project(key):
    return os.path.join(REPO, 'scripts', f'{variant_scene(key)}.uzproject')


def _entity(name, components):
    return {'name': name, 'components': components}


def _mesh_entity(name, asset, colour, roughness, metallic):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': asset},
        'material': {'baseColorR': colour[0], 'baseColorG': colour[1], 'baseColorB': colour[2],
                     'roughnessMultiplier': roughness, 'metallicMultiplier': metallic},
    })


SCENE_CVARS = dict(shaft.SCENE_CVARS)


def scene_json(with_floor: bool, eye, target, name, floor_roughness=FLOOR_ROUGHNESS, hdri=None, sun_colour=(1.0, 1.0, 1.0)):
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': list(sun_colour), 'intensity': 10, 'cast_shadows': True,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(eye), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(target)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 1.0, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                           'min_occlusion': 0.0, 'lower_hemisphere_is_black': True,
                                           **({'cubemap_path': hdri} if hdri else {})}}),
    ]
    if with_floor:
        ents.append(_mesh_entity('MirrorFloor', mesh_asset('floor'), (FLOOR_F0, FLOOR_F0, FLOOR_F0), floor_roughness, 1.0))
    for key, (_, colour) in BOXES.items():
        ents.append(_mesh_entity(key.capitalize(), mesh_asset(key), colour, BOX_ROUGHNESS, 0.0))
    return {'name': name, 'version': '1.0', 'cvars': dict(SCENE_CVARS), 'entities': ents}


def project_json(scene, name):
    return {
        'name': name, 'version': '1.0', 'content_root': '../sample/content',
        'default_scene': f'scenes/{scene}.uzscene',
        'window': {'width': W, 'height': H},
        'cvars': {**SCENE_CVARS, 'r.Fog': 0, 'r.TechPanel.Show': 0},
    }


def generate():
    written = []
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    meshes = {'floor': FLOOR, **{k: v[0] for k, v in BOXES.items()}}
    for key, (lo, hi) in meshes.items():
        path = os.path.join(CONTENT, *mesh_asset(key).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    for scene, with_floor, eye, project, title in (
            (SCENE, True, CAMERA_EYE, PROJECT, 'Reflection reference'),
            (SCENE_MIRROR, False, tuple(mirror_point(CAMERA_EYE).tolist()), PROJECT_MIRROR, 'Reflection reference (mirrored camera)')):
        path = os.path.join(CONTENT, 'scenes', f'{scene}.uzscene')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(scene_json(with_floor, eye, CAMERA_TARGET, title), fh, indent=2)
        written.append(path)
        with open(project, 'w', encoding='utf-8') as fh:
            json.dump(project_json(scene, title), fh, indent=2)
        written.append(project)
    variants = [(key, f'Reflection reference (metal floor, roughness {rough})', rough, None, (1.0, 1.0, 1.0)) for key, rough in FLOOR_VARIANTS.items()]
    variants.append((HDRI_VARIANT, 'Reflection reference (chrome floor, PMREM environment)', FLOOR_ROUGHNESS, HDRI_CUBEMAP, (1.0, 1.0, 1.0)))
    variants.append((WARM_VARIANT, 'Reflection reference (chrome floor, warm sun)', FLOOR_ROUGHNESS, None, WARM_SUN_COLOUR))
    for key, title, rough, hdri, sun_colour in variants:
        scene = variant_scene(key)
        path = os.path.join(CONTENT, 'scenes', f'{scene}.uzscene')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(scene_json(True, CAMERA_EYE, CAMERA_TARGET, title, rough, hdri, sun_colour), fh, indent=2)
        written.append(path)
        with open(variant_project(key), 'w', encoding='utf-8') as fh:
            json.dump(project_json(scene, title), fh, indent=2)
        written.append(variant_project(key))
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
