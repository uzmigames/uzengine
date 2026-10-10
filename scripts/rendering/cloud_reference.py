"""Cloud reference: a large floor, the sky atmosphere, a fixed sun at 40 degrees elevation, a sky light and one
volumetric-cloud layer without wind, seen from fixed camera poses. Rulebook task phase0-3.1.0-volumetric-clouds-rewrite:
the scene of the Phase 0 baseline (0.12) and of the later gates (1.3/1.9 banding and stability, 2.7 silver linings,
5.4 aerial perspective, 8.x), measured by scripts/rendering/test_clouds.py.

One project per camera pose, because uzengine.exe does not parse camera arguments (there is no --camera-* flag; the
camera comes from the scene's `look_at`):
  sun      eye 2 m above the floor at the origin, azimuth 0 (toward the sun), pitch +25 deg. The sun (elevation 40 deg)
           sits 15 deg above the frame centre, at about row 282 of 1080 (upper quarter); the frame spans elevations
           -4..+54 deg, so the floor and the horizon fill only the bottom ~90 rows.
  away     same eye, azimuth 180 deg (away from the sun), pitch +25 deg: the anti-solar half of the same elevations.
  horizon  same eye, azimuth 90 deg (the sun 90 deg to the side, outside the frame), pitch +8 deg: the horizon at
           about row 675, 0..37 deg of sky; the clouds near the horizon are 50-300 km away (aerial perspective).
  above    eye 2 km above the layer top (15 km), azimuth 90 deg, pitch -25 deg: the layer seen from above, the
           horizon in the top rows (optional pose; the floor is far below the 2 km far plane). Near plane 100 m
           (POSES 'near'): the sky and cloud shaders rebuild each ray in float32 from the points the inverse
           view-projection gives at depths 0.25 and 0.75, which with the 10 cm near plane lie 13-40 cm in front of a
           camera at 1.5e6 cm, where one float32 step is 0.125 cm: a simulation of that arithmetic gives ray errors of
           0.15 deg mean, 0.41 deg max (7.6 px), different from pixel to pixel, i.e. per-pixel noise in the sky and the
           clouds that the banding and stability metrics would read as artifacts. At 100 m the points are 130-350 m out
           and the error is 0.0005 deg max (0.01 px); nothing lies within 2 km of this camera, so nothing is clipped.

Sun convention (lighting block A, verified; fog_shaft_reference.py): `sunDir = rotation * Vec3::Forward()` (+X) and
the light's rotation points toward the sun, so rotation [-e, 0, 0] is the sun at elevation e and AZIMUTH 0, i.e.
toward +X (azimuth measured from +X toward +Y): SUN_DIR = (cos 40, 0, sin 40) = (0.766, 0, 0.643). cloud_test's log
confirms the convention for the cloud pass: rotation [-30, 0, 60] reaches the cloud CB as sunDir = (0.866, 0, 0.500)
(the third Euler angle is a roll about the light's forward axis and leaves the direction unchanged).

Clouds (keys of VolumetricCloudsComponent's serializer, volumetric_clouds_component.cpp; every key is written
explicitly so the baseline does not move when a later phase changes a component default):
  wind 0 (g_time only scrolls the noise through the wind, so the frames are static and runs are reproducible);
  coverage 0.45, i.e. "about 0.5": the engine's remap is saturate(FBM + coverage - 0.5)^2 over an FBM that averages
  ~0.47, so 0.5 already gives a near-overcast deck. A CPU port of cloud_raymarch_cs.hlsl (HEAD) predicts, with the
  values below, 25-47 % of the sky opaque (T < 0.5) and 3-15 % clear (T > 0.95) per pose, against 51-71 % and 0-8 %
  at 0.5: moderate coverage with gaps, so the mask has edges and the silver-lining metric has interiors;
  extinction 0.04 (cloud_test's value; the component default 0.01 leaves them mostly translucent: the CPU port gives
  a median transmittance of 0.77-0.84 per pose and 7-10 % opaque at coverage 0.5);
  primary_steps 64 and light_steps 6: the shader's caps (min(g_primarySteps, 64), min(g_lightSteps, 6)), i.e. the
  uniform 64-step march the rewrite replaces. The other values are the component defaults.

CVars: SCENE_CVARS of fog_shaft_reference.py (r.AutoExposure 0, r.Exposure 1, r.TAA 1, the screen-space and
probe GI off), plus r.Fog 0 in the projects; there is no HeightFog entity (height fog off, and the froxel fog
needs one). TAA: every scripts/*-reference.uzproject sets r.TAA 1 and these projects do too (cloud_test and the other
sample projects set nothing, so TAA is off there: r.TAA defaults to false). With TAA on, the region dump's TAA and
OUTPUT records are written: linear HDR after CloudComposite (taa_raw before the TAA resolve, the OUTPUT record after
it); without TAA only the deferred-lighting groups (LIGHTING, LIGHTING_EXTRA) are, and none of them holds the sky's
colour. The cloud pass traces with the unjittered matrix, so TAA does not move the clouds. Exposure: r.Exposure 1, the value of every reference project, with auto exposure off. Measured on the
reflection reference (the same atmosphere, sun_illuminance 10; region dump and screenshot of one frame), the sky's
linear luminance runs from 0.17 at 20 deg elevation to 0.44 at 2 deg, which the engine's tonemap at exposure 1 maps
to display codes 85-155: mid-range, out of the toe (where one 8-bit code is a large relative step and the sky gradient
would read as banding) and below the shoulder, so the clouds, several times brighter (1.5-2.7 by a CPU port of the
shader; the same capture maps 2.24 to code 235), stay below clipping. Region-dump values are linear HDR and do not
depend on the exposure; it only matters for the screenshots (montage, temporal metric).

Variants (VARIANTS, one scene + project per pose and variant): `noclouds` removes the clouds entity (the control that
gives the cloud mask, the sky behind the clouds and the metrics' floors); `steps16` / `steps32` change only
primary_steps (the step-count dose response of the banding metric). Later phases add entries: a variant can override
clouds keys, remove the entity, or add CVars (written to the scene and to the project). Unregistered CVars are skipped
with a warning by the loader, so a variant can name a CVar a later phase registers.

Writes sample/content/meshes/reference/cloudref_floor.uzmesh, sample/content/scenes/cloud-reference-<pose>[-<variant>].uzscene
and scripts/cloud-reference-<pose>[-<variant>].uzproject.
Usage: python scripts/rendering/cloud_reference.py [--poses sun,away,horizon,above] [--variants noclouds,steps16,steps32] [--list]
"""
import argparse
import copy
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import box_mesh_bytes  # noqa: E402
import fog_shaft_reference as shaft  # noqa: E402

REPO = shaft.REPO
CONTENT = shaft.CONTENT
SCENE = 'cloud-reference'
W, H = 1920, 1080
FOV = 58.51                       # vertical, degrees (the reference scenes' camera)
NEAR, FAR = 10.0, 200000.0        # cm (POSES may override `near`); the floor's farthest corner is 707 m away, but
                                  # beyond ~667 m its reversed-Z depth falls under the 1e-4 the deferred lighting,
                                  # the sky and the cloud composite take for sky: those pixels lie below -0.15 deg,
                                  # outside the harness's sky region
SUN_ELEVATION = shaft.SUN_ELEVATION          # 40 deg
SUN_AZIMUTH = 0.0                            # toward +X; rotation [-40, 0, 0]
SUN_DIR = shaft.SUN_DIR                      # (cos 40, 0, sin 40), toward the sun
SUN_INTENSITY = 10.0                         # directional light: the cloud CB's sunColor = colour x intensity
SKY_SUN_ILLUMINANCE = 10.0                   # sky_atmosphere, as every reference scene
SKY_LIGHT_INTENSITY = 1.0                    # the clouds' ambient is the sky capture's SH L0 x this intensity
FLOOR = ((-50000.0, -50000.0, -100.0), (50000.0, 50000.0, 0.0))   # 1 km x 1 km, top face at z = 0
FLOOR_ALBEDO = 0.3
EYE_HEIGHT = 200.0                           # cm above the floor

#: The cloud layer, every key the serializer reads (volumetric_clouds_component.cpp).
CLOUDS = {
    'enabled': True,
    'layer_bottom_altitude_km': 7.0,
    'layer_height_km': 6.0,
    'coverage': 0.45,
    'density': 0.6,
    'cloud_type': 0.5,
    'wind_velocity': [0.0, 0.0, 0.0],
    'detail_wind_scale': 2.0,
    'extinction': 0.04,
    'scattering_albedo': 0.99,
    'phase_g': 0.5,
    'phase_g2': -0.3,
    'phase_blend': 0.7,
    'ambient_multiplier': 6.0,
    'tracing_max_distance_km': 50.0,
    'primary_steps': 64,
    'light_steps': 6,
}
LAYER_TOP_KM = CLOUDS['layer_bottom_altitude_km'] + CLOUDS['layer_height_km']

#: Camera poses: eye (cm), azimuth (deg, from +X toward +Y), pitch (deg, up positive), a title and optionally `near`
#: (cm, default NEAR).
POSES = {
    'sun':     {'eye': (0.0, 0.0, EYE_HEIGHT), 'azimuth': 0.0, 'pitch': 25.0, 'title': 'toward the sun'},
    'away':    {'eye': (0.0, 0.0, EYE_HEIGHT), 'azimuth': 180.0, 'pitch': 25.0, 'title': 'away from the sun'},
    'horizon': {'eye': (0.0, 0.0, EYE_HEIGHT), 'azimuth': 90.0, 'pitch': 8.0, 'title': 'at the horizon'},
    'above':   {'eye': (0.0, 0.0, (LAYER_TOP_KM + 2.0) * 1.0e5), 'azimuth': 90.0, 'pitch': -25.0,
                'title': 'above the cloud layer', 'near': 10000.0},     # 100 m: float32 ray precision, see above
}
DEFAULT_POSES = ('sun', 'away', 'horizon', 'above')

#: Variants: `clouds` = keys merged into CLOUDS, `remove_clouds` = no clouds entity, `cvars` = CVars added to the
#: scene and the project. Later phases add entries here (and re-run this script).
VARIANTS = {
    'noclouds': {'title': 'no clouds', 'remove_clouds': True},
    'steps32': {'title': '32 primary steps', 'clouds': {'primary_steps': 32}},
    'steps16': {'title': '16 primary steps', 'clouds': {'primary_steps': 16}},
}

SCENE_CVARS = dict(shaft.SCENE_CVARS)        # r.AutoExposure 0, r.Exposure 1, GI off, r.TAA 1
PROJECT_CVARS = {'r.Fog': 0, 'r.TechPanel.Show': 0}


def mesh_asset(name):
    return f'meshes/reference/cloudref_{name}.uzmesh'


def scene_name(pose, variant=None):
    return f'{SCENE}-{pose}' + (f'-{variant}' if variant else '')


def project_path(pose, variant=None):
    return os.path.join(REPO, 'scripts', 'rendering', f'{scene_name(pose, variant)}.uzproject')


def scene_path(pose, variant=None):
    return os.path.join(CONTENT, 'scenes', f'{scene_name(pose, variant)}.uzscene')


def variant_scene(key, pose='sun'):
    return scene_name(pose, key)


def variant_project(key, pose='sun'):
    """Project of a variant at a pose (reflection_reference.variant_project with the pose added)."""
    if key and key not in VARIANTS:
        raise KeyError(f'unknown cloud-reference variant {key!r} (have {sorted(VARIANTS)})')
    return project_path(pose, key or None)


def view_direction(pose):
    p = POSES[pose]
    az, pit = math.radians(p['azimuth']), math.radians(p['pitch'])
    return (math.cos(pit) * math.cos(az), math.cos(pit) * math.sin(az), math.sin(pit))


def camera(pose):
    """(eye, target) in cm, as written to the scene's camera (`look_at` 10 m ahead)."""
    eye = POSES[pose]['eye']
    d = view_direction(pose)
    return tuple(eye), tuple(round(e + 1000.0 * c, 4) for e, c in zip(eye, d))


def clouds_for(variant=None):
    """The clouds component of a variant, or None when the variant removes it."""
    v = VARIANTS.get(variant, {}) if variant else {}
    if v.get('remove_clouds'):
        return None
    c = copy.deepcopy(CLOUDS)
    c.update(v.get('clouds', {}))
    return c


def variant_cvars(variant=None):
    return dict(VARIANTS.get(variant, {}).get('cvars', {})) if variant else {}


def _entity(name, components):
    return {'name': name, 'components': components}


def scene_json(pose, variant=None):
    eye, target = camera(pose)
    title = f'Cloud reference ({POSES[pose]["title"]}' + (f', {VARIANTS[variant]["title"]})' if variant else ')')
    ents = [
        _entity('Sun', {'transform': {'position': [0, 0, 0], 'rotation': [-SUN_ELEVATION, 0, 0], 'scale': [1, 1, 1]},
                        'light': {'type': 'directional', 'color': [1, 1, 1], 'intensity': SUN_INTENSITY,
                                  'cast_shadows': True, 'atmosphere_sun_light': True, 'light_source_angle': 0.53}}),
        _entity('Camera', {'transform': {'position': list(eye), 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                           'camera': {'fov': FOV, 'near': POSES[pose].get('near', NEAR), 'far': FAR, 'active': True,
                                      'look_at': list(target)}}),
        _entity('Sky', {'transform': {'position': [0, 0, 0]},
                        'sky_atmosphere': {'sun_illuminance': [SKY_SUN_ILLUMINANCE] * 3}}),
        _entity('SkyLight', {'transform': {'position': [0, 0, 0]},
                             'sky_light': {'intensity': SKY_LIGHT_INTENSITY, 'light_color': [1, 1, 1],
                                           'indirect_lighting_intensity': 1.0, 'min_occlusion': 0.0,
                                           'lower_hemisphere_is_black': True}}),
        _entity('Floor', {'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
                          'mesh': {'asset': mesh_asset('floor')},
                          'material': {'baseColorR': FLOOR_ALBEDO, 'baseColorG': FLOOR_ALBEDO, 'baseColorB': FLOOR_ALBEDO,
                                       'roughnessMultiplier': 1.0, 'metallicMultiplier': 0.0}}),
    ]
    clouds = clouds_for(variant)
    if clouds is not None:
        ents.append(_entity('VolumetricClouds', {'transform': {'position': [0, 0, 0]}, 'volumetric_clouds': clouds}))
    return {'name': title, 'version': '1.0', 'cvars': {**SCENE_CVARS, **variant_cvars(variant)}, 'entities': ents}


def project_json(pose, variant=None):
    scene = scene_json(pose, variant)
    return {
        'name': scene['name'], 'version': '1.0', 'content_root': '../../sample/content',
        'default_scene': f'scenes/{scene_name(pose, variant)}.uzscene',
        'window': {'width': W, 'height': H},
        'cvars': {**SCENE_CVARS, **PROJECT_CVARS, **variant_cvars(variant)},
    }


def generate(poses=DEFAULT_POSES, variants=tuple(VARIANTS)):
    written = []
    os.makedirs(os.path.join(CONTENT, 'meshes', 'reference'), exist_ok=True)
    path = os.path.join(CONTENT, *mesh_asset('floor').split('/'))
    with open(path, 'wb') as fh:
        fh.write(box_mesh_bytes(*FLOOR))
    written.append(path)
    for pose in poses:
        for variant in (None,) + tuple(variants):
            with open(scene_path(pose, variant), 'w', encoding='utf-8') as fh:
                json.dump(scene_json(pose, variant), fh, indent=2)
            written.append(scene_path(pose, variant))
            with open(project_path(pose, variant), 'w', encoding='utf-8') as fh:
                json.dump(project_json(pose, variant), fh, indent=2)
            written.append(project_path(pose, variant))
    return written


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--poses', default=','.join(DEFAULT_POSES))
    ap.add_argument('--variants', default=','.join(VARIANTS), help='comma list; empty = the base scenes only')
    ap.add_argument('--list', action='store_true', help='print the poses and variants and exit')
    args = ap.parse_args()
    poses = [p for p in args.poses.split(',') if p]
    variants = [v for v in args.variants.split(',') if v]
    for name, known in (('pose', POSES), ('variant', VARIANTS)):
        bad = [x for x in (poses if name == 'pose' else variants) if x not in known]
        if bad:
            raise SystemExit(f'unknown {name}(s) {bad}; have {sorted(known)}')
    if args.list:
        for p in poses:
            eye, target = camera(p)
            print(f'{p:8s} eye {eye} target {tuple(round(t, 1) for t in target)}  ({POSES[p]["title"]})')
        for v, spec in VARIANTS.items():
            print(f'variant {v}: {spec}')
        return 0
    for path in generate(poses, variants):
        print('wrote', os.path.relpath(path, REPO))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
