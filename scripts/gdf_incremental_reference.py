"""Reference scene for the incremental global distance field (lighting block B, item 2).

A 30 x 30 m ground slab, two 100 cm walls and a 100 cm box that `scripts/gdf_box_move.lua` moves 400 cm
along +x between Lua frames 100 and 160 (then rests). The camera looks down on the box's path from
above and the sun comes from +x at 35 degrees, so the box casts a shadow and blocks sky light on the
ground it covers: `diffuse_indirect` under and beside the box tells whether the GI followed it.

Meshes are written at true size (one `.uzmesh` per box, identity transforms) with the writer of
`probe_side_reference.py`: a scaled unit cube would get the Gershgorin-conservative field.

Usage: python scripts/gdf_incremental_reference.py   (writes meshes, scene, project)
"""
from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from probe_side_reference import box_mesh_bytes  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(REPO, 'sample', 'content')
SCENE = 'gdf-incremental-reference'
ALBEDO = 0.5
BOXES = {  # name: (lo, hi) in cm, world coordinates
    'ground': ((-1500, -1500, -20), (1500, 1500, 0)),
    'wall_north': ((-600, 400, 0), (600, 500, 300)),
    'wall_west': ((-700, -400, 0), (-600, 400, 300)),
    'box': ((-250, -50, 0), (-150, 50, 100)),       # the mover: 100 cm cube, starts at x -200
}
BOX_START_X, BOX_END_X, MOVE_FIRST, MOVE_LAST = -200.0, 200.0, 100, 160
CAMERA_EYE, CAMERA_TARGET, FOV = (0.0, -900.0, 900.0), (0.0, 0.0, 0.0), 70.0
PROJECT_CVARS = {'r.RadianceCache': 1, 'r.GI.Reflections': 1, 'r.GI.FinalGather': 1, 'r.GI.GDF.Resolution': 256,
                 'r.GI.MeshSdf.Resolution': 128, 'r.GI.MeshSdf.TwoSided': 1, 'r.GI.RadianceCache.GridExtent': 2000,
                 'r.TAA': 1, 'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TechPanel.Show': 0, 'r.GTAO': 0,
                 'r.GI.RadianceCache.Diagnostic': 1}


def mesh_asset(name):
    return f'meshes/reference/{SCENE}_{name}.uzmesh'


def entity(name, components):
    return {'name': name, 'components': components}


def box_entity(name, extra=None):
    comps = {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': mesh_asset(name)},
        'material': {'baseColorR': ALBEDO, 'baseColorG': ALBEDO, 'baseColorB': ALBEDO,
                     'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0},
    }
    if extra:
        comps.update(extra)
    return entity(name, comps)


def scene_json():
    ents = [
        entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-35, 0, 0], 'scale': [1, 1, 1]},
                       'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 10, 'cast_shadows': True,
                                 'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        entity('Camera', {'transform': {'position': list(CAMERA_EYE), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                          'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(CAMERA_TARGET)}}),
        entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                            'sky_light': {'intensity': 1.0, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                          'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
    ]
    for name in BOXES:
        ents.append(box_entity(name, {'script': {'path': 'scripts/gdf_box_move.lua'}} if name == 'box' else None))
    return {'name': 'GDF incremental reference', 'version': '1.0', 'cvars': {}, 'entities': ents}


def project_json():
    return {'name': 'GDF incremental reference', 'version': '1.0', 'content_root': '../sample/content',
            'default_scene': f'scenes/{SCENE}.uzscene', 'window': {'width': 640, 'height': 360},
            'cvars': PROJECT_CVARS}


def generate():
    written = []
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    for name, (lo, hi) in BOXES.items():
        path = os.path.join(CONTENT, *mesh_asset(name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    path = os.path.join(CONTENT, 'scenes', f'{SCENE}.uzscene')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(scene_json(), fh, indent=2)
    written.append(path)
    path = os.path.join(REPO, 'scripts', f'{SCENE}.uzproject')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(project_json(), fh, indent=2)
    written.append(path)
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
