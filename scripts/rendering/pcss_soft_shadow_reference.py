"""Soft-shadow reference scene for the directional PCSS (r.Shadows.SoftPCSS, stage 1).

A flat ground, a 24 m thin wall and a 4 m pillar under a 30-degree sun, seen from the side so one
shadow edge runs across the frame from the wall's base (receiver touching its blocker: no penumbra)
to the shadow's tip (blocker 48 m away along the light: 22 cm of penumbra radius with the sun's
0.5357-degree disc, r.Shadows.SoftPCSS.LightSize 1). Every stochastic or temporal pass is off in the
project so a frame differs from the next only if the shadow path itself does.

A second project, `pcss-soft-shadow-grazing`, lowers the sun to 10 degrees: the ground's depth slope
across the shadow map is then tan(80 deg) = 5.7, past the 2.0 cap of the centre slope bias, so a wide
kernel self-shadows unless the receiver-plane bias per tap holds (that is what it verifies).

Writes (idempotent):
  scripts/rendering/pcss-soft-shadow-reference.uzproject / .uzscene   (30 degree sun)
  scripts/rendering/pcss-soft-shadow-grazing.uzproject / .uzscene     (10 degree sun)
  sample/content/meshes/reference/pcss_{ground,wall,pillar}.uzmesh  (box meshes, world units)

Usage: python scripts/rendering/pcss_soft_shadow_reference.py
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

from probe_side_reference import box_mesh_bytes  # noqa: E402

MESH_DIR = os.path.join(REPO, 'sample', 'content', 'meshes', 'reference')
VARIANTS = (('pcss-soft-shadow-reference', 30.0), ('pcss-soft-shadow-grazing', 10.0))
WALL_HEIGHT = 2400.0

# name, lo, hi (cm, Z up). The sun shines towards -X (it sits at +X, 30 degrees up), so the wall's
# shadow runs from x = 0 to x = -WALL_HEIGHT / tan(30 deg) = -4157 along y in [-300, 300].
BOXES = [
    ('ground', (-7000.0, -7000.0, -20.0), (7000.0, 7000.0, 0.0)),
    ('wall', (-25.0, -300.0, 0.0), (25.0, 300.0, WALL_HEIGHT)),
    ('pillar', (-1225.0, -1325.0, 0.0), (-1175.0, -1275.0, 400.0)),
]
ALBEDO = {'ground': (0.55, 0.55, 0.55), 'wall': (0.75, 0.6, 0.45), 'pillar': (0.45, 0.55, 0.75)}


def mesh_asset(name):
    return f'sample/content/meshes/reference/pcss_{name}.uzmesh'


def _entity(name, components):
    return {'name': name, 'components': components}


def scene_json(sun_elevation_deg):
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-sun_elevation_deg, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 5, 'cast_shadows': True,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.5357}}),
        _entity('Camera', {'transform': {'position': [-2000.0, -4500.0, 2500.0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': 60, 'near': 5, 'far': 20000, 'active': True, 'look_at': [-2000.0, 0.0, 0.0]}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [5, 5, 5]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 0.5, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1,
                                           'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
    ]
    for name, _, _ in BOXES:
        r, g, b = ALBEDO[name]
        ents.append(_entity(name.capitalize(), {
            'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
            'mesh': {'asset': mesh_asset(name)},
            'material': {'baseColorR': r, 'baseColorG': g, 'baseColorB': b,
                         'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0}}))
    return {'name': f'PCSS soft shadow reference (24 m wall, 4 m pillar, {sun_elevation_deg:g} degree sun)', 'version': '1.0',
            'cvars': {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.GTAO': 0, 'r.SSGI': 0, 'r.RadianceCache': 0,
                      'r.GI.FinalGather': 0, 'r.GI.Reflections': 0},
            'entities': ents}


def project_json(name):
    return {'name': 'PCSS soft shadow reference', 'version': '1.0', 'content_root': '../..',
            'default_scene': f'scripts/rendering/{name}.uzscene',
            'window': {'width': 1280, 'height': 720},
            'cvars': {'r.AutoExposure': 0, 'r.Exposure': 1, 'r.Bloom': 0, 'r.TAA': 0, 'r.Fog': 0,
                      'r.VolumetricFog': 0, 'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0, 'r.GTAO': 0, 'r.SSGI': 0,
                      'r.RadianceCache': 0, 'r.GI.FinalGather': 0, 'r.GI.Reflections': 0, 'r.SSR': 0,
                      'r.ContactShadows': 0}}


def generate():
    written = []
    os.makedirs(MESH_DIR, exist_ok=True)
    for name, lo, hi in BOXES:
        path = os.path.join(REPO, *mesh_asset(name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    for name, elevation in VARIANTS:
        for path, payload in ((os.path.join(HERE, f'{name}.uzscene'), scene_json(elevation)),
                              (os.path.join(HERE, f'{name}.uzproject'), project_json(name))):
            with open(path, 'w', encoding='utf-8', newline='\n') as fh:
                json.dump(payload, fh, indent=2)
                fh.write('\n')
            written.append(path)
    return written


if __name__ == '__main__':
    for p in generate():
        print(os.path.relpath(p, REPO))
