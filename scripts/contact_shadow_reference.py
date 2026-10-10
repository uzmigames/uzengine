"""Contact shadow reference: the fog shaft scene's floor and wall plus a small box resting on the
floor in the sun, seen from close by. Lighting block B, item 4b: the control for the screen-space
contact shadows (the cascaded shadow maps alone leave a bias gap at the base of the box and blur its
shadow; the contact march should close the gap without darkening open lit floor).

Geometry (cm, Z up): floor top at z = 0 (fog shaft slab), wall at x in [0, 100] (fog shaft wall),
box BOX = [-1000, -960] x [-1420, -1380] x [0, 12] (40 x 40 x 12 cm) on the lit floor south of the
wall's shadow. Sun at 40 degrees towards +X (rotation [-40, 0, 0]): the box's shadow falls towards
-X over 12 / tan(40) = 14 cm. Camera 2.2 m south-west of the box, 90 cm high, looking at it.

`sun_visible(points)` is the analytic shadow test (wall and box); `box_base_distance(points)` the
distance from a floor point to the box's footprint (0 inside).

Writes sample/content/meshes/reference/contact_box.uzmesh, sample/content/scenes/contact-shadow-reference.uzscene
and scripts/contact-shadow-reference.uzproject. Usage: python scripts/contact_shadow_reference.py
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
SCENE = 'contact-shadow-reference'
PROJECT = os.path.join(REPO, 'scripts', f'{SCENE}.uzproject')
SUN_ELEVATION = shaft.SUN_ELEVATION
SUN_DIR = shaft.SUN_DIR
BOX = ((-1000.0, -1420.0, 0.0), (-960.0, -1380.0, 12.0))
CAMERA_EYE = (-1150.0, -1560.0, 90.0)
CAMERA_TARGET = (-980.0, -1400.0, 6.0)
FOV = 58.51
ALBEDO = shaft.ALBEDO
BOX_MESH = 'meshes/reference/contact_box.uzmesh'


def _slab_hit(p, box):
    (x0, y0, z0), (x1, y1, z1) = box
    sx, _, sz = SUN_DIR
    tx0 = (x0 - p[..., 0]) / sx
    tx1 = (x1 - p[..., 0]) / sx
    tz0 = (z0 - p[..., 2]) / sz
    tz1 = (z1 - p[..., 2]) / sz
    t_in = np.maximum(np.maximum(tx0, tz0), 0.0)
    t_out = np.minimum(tx1, tz1)
    in_y = (p[..., 1] >= y0) & (p[..., 1] <= y1)
    return in_y & (t_out > t_in)


def sun_visible(points):
    """1.0 where the sun ray from the point misses the wall and the box, 0.0 where it hits one."""
    p = np.asarray(points, dtype=np.float64)
    hit = _slab_hit(p, shaft.WALL) | _slab_hit(p, BOX)
    return np.where(hit, 0.0, 1.0)


def box_base_distance(points):
    """Horizontal distance (cm) from a point to the box's footprint; 0 inside it."""
    p = np.asarray(points, dtype=np.float64)
    (x0, y0, _), (x1, y1, _) = BOX
    dx = np.maximum(np.maximum(x0 - p[..., 0], p[..., 0] - x1), 0.0)
    dy = np.maximum(np.maximum(y0 - p[..., 1], p[..., 1] - y1), 0.0)
    return np.sqrt(dx * dx + dy * dy)


def _entity(name, components):
    return {'name': name, 'components': components}


def _mesh_entity(name, asset):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': asset},
        'material': {'baseColorR': ALBEDO, 'baseColorG': ALBEDO, 'baseColorB': ALBEDO,
                     'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0},
    })


def scene_json():
    return {
        'name': 'Contact shadow reference', 'version': '1.0', 'cvars': dict(shaft.SCENE_CVARS),
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
            _mesh_entity('Floor', shaft.mesh_asset('floor')),
            _mesh_entity('Wall', shaft.mesh_asset('wall')),
            _mesh_entity('Box', BOX_MESH),
        ],
    }


def project_json():
    return {
        'name': 'Contact shadow reference', 'version': '1.0', 'content_root': '../sample/content',
        'default_scene': f'scenes/{SCENE}.uzscene',
        'window': {'width': 1920, 'height': 1080},
        'cvars': {**shaft.SCENE_CVARS, 'r.Fog': 0, 'r.TechPanel.Show': 0},
    }


def generate():
    written = []
    for name, (lo, hi) in shaft.MESHES.items():      # the shaft floor and wall, in case they are missing
        path = os.path.join(CONTENT, *shaft.mesh_asset(name).split('/'))
        if not os.path.isfile(path):
            with open(path, 'wb') as fh:
                fh.write(box_mesh_bytes(lo, hi))
            written.append(path)
    path = os.path.join(CONTENT, *BOX_MESH.split('/'))
    with open(path, 'wb') as fh:
        fh.write(box_mesh_bytes(*BOX))
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
