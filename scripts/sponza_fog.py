"""Writes the Sponza variant with the froxel volumetric fog on (lighting block B, item 4):
sample/content/scenes/sponza-fog.uzscene (Sponza plus one `height_fog` entity with
`enable_volumetric_fog`) and scripts/sponza-fog.uzproject (the Sponza project with the analytic
height fog off, `r.Fog 0`, so only the froxel fog runs, and the tech panel off).

The froxel fog reads the component's `density` as extinction per centimetre: 0.0005 gives a
transmittance of 1/e at 20 m, so the atrium is visibly foggy without going opaque. Height falloff
0 keeps the fog uniform (the gates compare floor areas at the same distances).

Usage: python scripts/sponza_fog.py [--density D]
"""
import argparse
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_IN = os.path.join(REPO, 'sample', 'content', 'scenes', 'sponza.uzscene')
SCENE_OUT = os.path.join(REPO, 'sample', 'content', 'scenes', 'sponza-fog.uzscene')
PROJECT_IN = os.path.join(REPO, 'sample', 'projects', 'sponza.uzproject')
PROJECT_OUT = os.path.join(REPO, 'scripts', 'sponza-fog.uzproject')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--density', type=float, default=0.0005, help='extinction per cm (default 0.0005: 1/e at 20 m)')
    args = ap.parse_args()
    with open(SCENE_IN, encoding='utf-8') as fh:
        scene = json.load(fh)
    scene['name'] = 'Sponza Atrium (froxel fog)'
    scene['entities'] = [e for e in scene['entities'] if e.get('name') != 'VolumetricFog']
    scene['entities'].append({
        'name': 'VolumetricFog',
        'components': {
            'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
            'height_fog': {
                'enabled': True,
                'enable_volumetric_fog': True,
                'density': args.density,
                'height_falloff': 0.0,
                'base_height': 0.0,
                'color': [0.6, 0.65, 0.7],
            },
        },
    })
    with open(SCENE_OUT, 'w', encoding='utf-8') as fh:
        json.dump(scene, fh, indent=2)
    with open(PROJECT_IN, encoding='utf-8') as fh:
        project = json.load(fh)
    project['name'] = 'Sponza froxel fog'
    project['content_root'] = '../sample/content'
    project['default_scene'] = 'scenes/sponza-fog.uzscene'
    project['cvars']['r.Fog'] = 0
    project['cvars']['r.TechPanel.Show'] = 0
    with open(PROJECT_OUT, 'w', encoding='utf-8') as fh:
        json.dump(project, fh, indent=2)
    print('wrote', os.path.relpath(SCENE_OUT, REPO), 'and', os.path.relpath(PROJECT_OUT, REPO))


if __name__ == '__main__':
    main()
