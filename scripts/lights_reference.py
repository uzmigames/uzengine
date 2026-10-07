"""Many-lights reference scenes for lighting block B, item 7 (clustered light lists).

Geometry (cm, Z up): a 40 x 40 m floor whose top face is z = 0, four pillars (60 x 60 x 400 cm) and two
long low walls, albedo 0.5. The sun entity has intensity 0 (without a directional light the frame's
defaults light the whole floor with sun colour), the sky light is 0.05, so the frame is lit by the
local lights alone.

Scenes (one project each, scripts/lights-reference*.uzproject):
  lights-reference        64 point lights (8 x 8 grid at z = 150, ranges 500..900, six colours) and 16 spot
                          lights (4 x 4 grid at z = 450 aimed down, outer angle 35 deg, range 700), two of
                          the spots cast shadows; the camera looks over the whole floor from the south.
  lights-reference-none   the same geometry without local lights (the lighting pass's cost without them).
  lights-reference-p-*    one point light at (0, 0, 300) over the floor, for the per-light parameters
                          (block B.7 makes them live; the scene loader reads them since then):
                            p-inv     inverse-squared falloff (default), roughness 1
                            p-exp2    custom falloff, exponent 2 (`useInverseSquaredFalloff` false)
                            p-exp8    custom falloff, exponent 8
                            p-diff05  inverse-squared, diffuseScale 0.5
                            p-src0 / p-src20 / p-src50   glossy floor (roughness 0.2), sourceRadius 0 / 20 / 50 cm

`point_light_positions()` and `spot_light_positions()` return the grids; `LIGHT_RANGE(i)` the point
light ranges, so the harness can predict which pixels a light reaches.
Usage: python scripts/lights_reference.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import box_mesh_bytes  # noqa: E402
import fog_shaft_reference as shaft  # noqa: E402

REPO = shaft.REPO
CONTENT = shaft.CONTENT
SCENE = 'lights-reference'
PROJECT = os.path.join(REPO, 'scripts', f'{SCENE}.uzproject')
FOV = 58.51
ALBEDO = 0.5
FLOOR = ((-2000.0, -2000.0, -20.0), (2000.0, 2000.0, 0.0))
PILLARS = {f'pillar_{i}': ((x - 30.0, y - 30.0, 0.0), (x + 30.0, y + 30.0, 400.0))
           for i, (x, y) in enumerate(((-900.0, -900.0), (900.0, -900.0), (-900.0, 900.0), (900.0, 900.0)))}
WALLS = {'wall_south': ((-1200.0, -1600.0, 0.0), (1200.0, -1560.0, 120.0)),
         'wall_mid': ((-300.0, 200.0, 0.0), (300.0, 240.0, 160.0))}
MESHES = {'floor': FLOOR, **PILLARS, **WALLS}
CAMERA_EYE = (0.0, -2600.0, 1100.0)
CAMERA_TARGET = (0.0, 100.0, 0.0)
COLOURS = [(1.0, 0.85, 0.6), (0.6, 0.8, 1.0), (1.0, 0.5, 0.4), (0.5, 1.0, 0.6), (1.0, 1.0, 1.0), (0.9, 0.6, 1.0)]
POINT_INTENSITY = 150000.0
SPOT_INTENSITY = 900000.0
SINGLE_LIGHT = (0.0, 0.0, 300.0)
SINGLE_RANGE = 900.0
SINGLE_INTENSITY = 250000.0
SCENE_CVARS = dict(shaft.SCENE_CVARS)     # GI off, exposure 1, TAA on


def mesh_asset(name):
    return f'meshes/reference/lights_{name}.uzmesh'


def point_light_positions():
    return [(-1400.0 + 400.0 * (i % 8), -1400.0 + 400.0 * (i // 8), 150.0) for i in range(64)]


def LIGHT_RANGE(i):
    return 500.0 + 100.0 * ((i * 7) % 5)


def spot_light_positions():
    return [(-1200.0 + 800.0 * (i % 4), -1200.0 + 800.0 * (i // 4), 450.0) for i in range(16)]


def _entity(name, components):
    return {'name': name, 'components': components}


def _mesh_entity(name, asset, roughness=1.0):
    return _entity(name, {
        'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
        'mesh': {'asset': asset},
        'material': {'baseColorR': ALBEDO, 'baseColorG': ALBEDO, 'baseColorB': ALBEDO,
                     'roughnessMultiplier': roughness, 'metallicMultiplier': 0.0},
    })


def _point(name, pos, colour, intensity, rng, **extra):
    light = {'type': 'point', 'color': list(colour), 'intensity': intensity, 'range': rng, 'castShadows': False}
    light.update(extra)
    return _entity(name, {'transform': {'position': list(pos), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]}, 'light': light})


def _spot(name, pos, colour, intensity, rng, shadows):
    # rotation pitch +90: the spot's direction is rotation * Forward, straight down (local-shadow reference).
    return _entity(name, {'transform': {'position': list(pos), 'rotation': [90, 0, 0], 'scale': [1, 1, 1]},
                          'light': {'type': 'spot', 'color': list(colour), 'intensity': intensity, 'range': rng,
                                    'innerAngle': 25.0, 'outerAngle': 35.0, 'castShadows': shadows, 'shadowBias': 2.0}})


def _base_entities(roughness=1.0):
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-40, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': 0.0, 'cast_shadows': False,
                                  'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(CAMERA_EYE), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': FOV, 'near': 5, 'far': 10000, 'active': True, 'look_at': list(CAMERA_TARGET)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]}, 'sky_atmosphere': {'sun_illuminance': [10, 10, 10]}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': 0.05, 'light_color': [1, 1, 1], 'indirect_lighting_intensity': 1.0,
                                           'min_occlusion': 0.0, 'lower_hemisphere_is_black': True}}),
    ]
    for name in MESHES:
        ents.append(_mesh_entity(name, mesh_asset(name), roughness if name == 'floor' else 1.0))
    return ents


def many_lights_entities():
    ents = []
    for i, pos in enumerate(point_light_positions()):
        ents.append(_point(f'point_{i}', pos, COLOURS[i % len(COLOURS)], POINT_INTENSITY, LIGHT_RANGE(i)))
    for i, pos in enumerate(spot_light_positions()):
        ents.append(_spot(f'spot_{i}', pos, COLOURS[(i + 3) % len(COLOURS)], SPOT_INTENSITY, 700.0, shadows=i in (5, 10)))
    return ents


def single_light_entity(**extra):
    return _point('single', SINGLE_LIGHT, (1.0, 1.0, 1.0), SINGLE_INTENSITY, SINGLE_RANGE, **extra)


def dense_point_light_positions():
    """400 point lights, a 20 x 20 grid 190 cm apart at z = 120 (ranges 300..500): the scalability scene."""
    return [(-1805.0 + 190.0 * (i % 20), -1805.0 + 190.0 * (i // 20), 120.0) for i in range(400)]


def dense_spot_light_positions():
    return [(-1620.0 + 360.0 * (i % 10), -1620.0 + 360.0 * (i // 10), 420.0) for i in range(100)]


def dense_lights_entities():
    ents = []
    for i, pos in enumerate(dense_point_light_positions()):
        ents.append(_point(f'point_{i}', pos, COLOURS[i % len(COLOURS)], POINT_INTENSITY * 0.5, 300.0 + 50.0 * ((i * 7) % 5)))
    for i, pos in enumerate(dense_spot_light_positions()):
        ents.append(_spot(f'spot_{i}', pos, COLOURS[(i + 3) % len(COLOURS)], SPOT_INTENSITY * 0.5, 600.0, shadows=False))
    return ents


VARIANTS = {
    'lights-reference': (lambda: _base_entities() + many_lights_entities(), 'Many lights reference'),
    'lights-reference-400': (lambda: _base_entities() + dense_lights_entities(), 'Many lights reference, 400 point + 100 spot'),
    'lights-reference-none': (lambda: _base_entities(), 'Many lights reference, no local lights'),
    'lights-reference-none-glossy': (lambda: _base_entities(0.2), 'Glossy floor, no local lights (the ambient of the source-radius scenes)'),
    'lights-reference-p-inv': (lambda: _base_entities() + [single_light_entity()], 'One light, inverse square'),
    'lights-reference-p-exp2': (lambda: _base_entities() + [single_light_entity(useInverseSquaredFalloff=False, falloffExponent=2.0)], 'One light, falloff exponent 2'),
    'lights-reference-p-exp8': (lambda: _base_entities() + [single_light_entity(useInverseSquaredFalloff=False, falloffExponent=8.0)], 'One light, falloff exponent 8'),
    'lights-reference-p-diff05': (lambda: _base_entities() + [single_light_entity(diffuseScale=0.5)], 'One light, diffuse scale 0.5'),
    'lights-reference-p-src0': (lambda: _base_entities(0.2) + [single_light_entity(sourceRadius=0.0)], 'One light, glossy floor, source radius 0'),
    'lights-reference-p-src20': (lambda: _base_entities(0.2) + [single_light_entity(sourceRadius=20.0)], 'One light, glossy floor, source radius 20'),
    'lights-reference-p-src50': (lambda: _base_entities(0.2) + [single_light_entity(sourceRadius=50.0)], 'One light, glossy floor, source radius 50'),
}


def project_path(variant):
    return os.path.join(REPO, 'scripts', f'{variant}.uzproject')


def scene_json(variant):
    build, title = VARIANTS[variant]
    return {'name': title, 'version': '1.0', 'cvars': dict(SCENE_CVARS), 'entities': build()}


def project_json(variant):
    return {
        'name': VARIANTS[variant][1], 'version': '1.0', 'content_root': '../sample/content',
        'default_scene': f'scenes/{variant}.uzscene',
        'window': {'width': 1920, 'height': 1080},
        'cvars': {**SCENE_CVARS, 'r.Fog': 0, 'r.VolumetricFog': 0, 'r.TechPanel.Show': 0},
    }


def generate():
    written = []
    os.makedirs(os.path.join(CONTENT, 'meshes', 'reference'), exist_ok=True)
    for name, (lo, hi) in MESHES.items():
        path = os.path.join(CONTENT, *mesh_asset(name).split('/'))
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    for variant in VARIANTS:
        path = os.path.join(CONTENT, 'scenes', f'{variant}.uzscene')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(scene_json(variant), fh, indent=2)
        written.append(path)
        with open(project_path(variant), 'w', encoding='utf-8') as fh:
            json.dump(project_json(variant), fh, indent=2)
        written.append(project_path(variant))
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
