"""Mesh-decal reference: a grey wall facing the camera, a decal quad 3 cm in front of it whose texture carries
a constant dark colour and an opacity ramp (0 at the left edge, 1 at the right), a sun from the camera's side
so an opaque quad would cast a shadow band beside itself, and the same scene without the decal as the control.
Lighting block, Sponza mesh decals (2026-10-08).

Identity: the lit colour of a mesh decal blended into the G-buffer is lerp(wall, decal, alpha) at every pixel
(the lighting is linear in the albedo; the decal changes nothing else), the G-buffer positions stay on the wall
plane, and the decal casts no shadow.

Geometry (cm, Z up): WALL = [-200, 200] x [0, 20] x [0, 300] (front face y = 0, normal -Y, towards the camera);
DECAL = [-120, 120] x [-3, -2.5] x [60, 240] (its front face at y = -3; the box's -Y face maps u along +X and
v along +Z, so the opacity ramp runs along world x). Camera CAMERA_EYE = (0, -420, 150) looking at
(0, 0, 150). Sun: elevation 30 deg, yaw -60 deg (in front of the wall, from the camera's right).

Writes sample/content/meshes/reference/decal_*.uzmesh, sample/content/textures/reference/decal_ramp.png,
sample/content/materials/reference/decal_ramp.uzmat, sample/content/scenes/decal-reference{,-wall}.uzscene and
scripts/decal-reference{,-wall}.uzproject. Usage: python scripts/decal_reference.py
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
SCENE = 'decal-reference'
SCENE_WALL = 'decal-reference-wall'
PROJECT = os.path.join(REPO, 'scripts', f'{SCENE}.uzproject')
PROJECT_WALL = os.path.join(REPO, 'scripts', f'{SCENE_WALL}.uzproject')
WALL = ((-200.0, 0.0, 0.0), (200.0, 20.0, 300.0))
DECAL = ((-120.0, -3.0, 60.0), (120.0, -2.5, 240.0))
WALL_ALBEDO = 0.5
DECAL_RGB_SRGB = 0.15            # the texture's constant colour (sRGB-encoded value)
TEXTURE = 'textures/reference/decal_ramp.png'
MATERIAL = 'materials/reference/decal_ramp.uzmat'
SUN_ELEVATION, SUN_YAW = 30.0, -60.0
CAMERA_EYE = (0.0, -420.0, 150.0)
CAMERA_TARGET = (0.0, 0.0, 150.0)
FOV = 58.51
W, H = 1920, 1080
SCENE_CVARS = {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0, 'r.RadianceCache': 0, 'r.GI.FinalGather': 0,
               'r.GI.Reflections': 0, 'r.SSGI': 0, 'r.TAA': 1, 'r.SSR': 0}


def sun_dir():
    e, y = np.radians(SUN_ELEVATION), np.radians(SUN_YAW)
    return (float(np.cos(e) * np.cos(y)), float(np.cos(e) * np.sin(y)), float(np.sin(e)))


def alpha_of_x(x):
    """The opacity the ramp texture carries at world x on the decal's front face."""
    (x0, _, _), (x1, _, _) = DECAL
    return np.clip((np.asarray(x, dtype=np.float64) - x0) / (x1 - x0), 0.0, 1.0)


def _entity(name, components):
    return {'name': name, 'components': components}


def write_texture(path):
    from PIL import Image
    w, h = 512, 128
    a = np.zeros((h, w, 4), dtype=np.uint8)
    a[..., :3] = int(round(DECAL_RGB_SRGB * 255))
    a[..., 3] = np.round(np.linspace(0.0, 1.0, w) * 255).astype(np.uint8)[None, :]
    Image.fromarray(a, 'RGBA').save(path)


def material_json():
    return {
        'version': 1, 'name': 'decal_ramp', 'domain': 'Decal', 'shadingModel': 'DefaultLit', 'blendMode': 'Masked', 'twoSided': False,
        'expressions': [
            {'id': 1, 'type': 'PBROutput',
             'inputs': [{'dstPin': 'BaseColor', 'srcExpr': 2, 'srcPin': 'RGBA'}, {'dstPin': 'Opacity', 'srcExpr': 2, 'srcPin': 'A'}],
             'floatParams': {'alphaClipThreshold': 0}},
            {'id': 2, 'type': 'TextureSample', 'name': 'Albedo', 'stringParams': {'texturePath': TEXTURE}},
        ],
        'params': {'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0, 'baseColorTint': [1.0, 1.0, 1.0], 'normalStrength': 1.0,
                   'aoMultiplier': 1.0, 'emissiveMultiplier': 1.0, 'specularMultiplier': 0},
    }


def scene_json(with_decal, name):
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, SUN_YAW], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 10, 'cast_shadows': True,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(CAMERA_EYE), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(CAMERA_TARGET)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 1.0, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                           'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
        _entity('Wall', {'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                         'mesh': {'asset': 'meshes/reference/decal_wall.uzmesh'},
                         'material': {'baseColorR': WALL_ALBEDO, 'baseColorG': WALL_ALBEDO, 'baseColorB': WALL_ALBEDO,
                                      'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0}}),
    ]
    if with_decal:
        ents.append(_entity('Decal', {'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                                      'mesh': {'asset': 'meshes/reference/decal_quad.uzmesh'},
                                      'material': {'material_graph': MATERIAL}}))
    return {'name': name, 'version': '1.0', 'cvars': dict(SCENE_CVARS), 'entities': ents}


def project_json(scene, name):
    return {'name': name, 'version': '1.0', 'content_root': '../sample/content', 'default_scene': f'scenes/{scene}.uzscene',
            'window': {'width': W, 'height': H}, 'cvars': {**SCENE_CVARS, 'r.Fog': 0, 'r.TechPanel.Show': 0}}


def generate():
    written = []
    for d in ('meshes/reference', 'textures/reference', 'materials/reference', 'scenes'):
        os.makedirs(os.path.join(CONTENT, d), exist_ok=True)
    for key, (lo, hi) in (('wall', WALL), ('quad', DECAL)):
        path = os.path.join(CONTENT, 'meshes', 'reference', f'decal_{key}.uzmesh')
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    tex = os.path.join(CONTENT, *TEXTURE.split('/'))
    write_texture(tex)
    written.append(tex)
    mat = os.path.join(CONTENT, *MATERIAL.split('/'))
    with open(mat, 'w', encoding='utf-8') as fh:
        json.dump(material_json(), fh, indent=2)
    written.append(mat)
    for scene, with_decal, project, title in ((SCENE, True, PROJECT, 'Mesh-decal reference'), (SCENE_WALL, False, PROJECT_WALL, 'Mesh-decal reference (wall only)')):
        path = os.path.join(CONTENT, 'scenes', f'{scene}.uzscene')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(scene_json(with_decal, title), fh, indent=2)
        written.append(path)
        with open(project, 'w', encoding='utf-8') as fh:
            json.dump(project_json(scene, title), fh, indent=2)
        written.append(project)
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
