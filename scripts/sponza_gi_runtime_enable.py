"""Writes the Sponza variant that loads without SDF GI and switches it on at frame 60 through
`sample/content/scripts/gi_enable_at_frame.lua` (lighting block B, item 3: gates C3 and C5 of
scripts/test_sdf_bake_cache.py). Output: sample/content/scenes/sponza-gi-runtime-enable.uzscene
(the Sponza scene plus one script entity) and scripts/sponza-gi-runtime-enable.uzproject (the
Sponza project with `r.RadianceCache` 0 and the TechPanel off).

Usage: python scripts/sponza_gi_runtime_enable.py
"""
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_IN = os.path.join(REPO, 'sample', 'content', 'scenes', 'sponza.uzscene')
SCENE_OUT = os.path.join(REPO, 'sample', 'content', 'scenes', 'sponza-gi-runtime-enable.uzscene')
PROJECT_IN = os.path.join(REPO, 'sample', 'projects', 'sponza.uzproject')
PROJECT_OUT = os.path.join(REPO, 'scripts', 'sponza-gi-runtime-enable.uzproject')


def main():
    with open(SCENE_IN, encoding='utf-8') as fh:
        scene = json.load(fh)
    scene['name'] = 'Sponza Atrium (SDF GI enabled at frame 60)'
    scene['entities'] = [e for e in scene['entities'] if e.get('name') != 'GiEnabler']
    scene['entities'].append({
        'name': 'GiEnabler',
        'components': {
            'transform': {'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1]},
            'script': {'path': 'scripts/gi_enable_at_frame.lua'},
        },
    })
    with open(SCENE_OUT, 'w', encoding='utf-8') as fh:
        json.dump(scene, fh, indent=2)
    with open(PROJECT_IN, encoding='utf-8') as fh:
        project = json.load(fh)
    project['name'] = 'Sponza SDF GI runtime enable'
    project['content_root'] = '../sample/content'
    project['default_scene'] = 'scenes/sponza-gi-runtime-enable.uzscene'
    project['cvars']['r.RadianceCache'] = 0
    project['cvars']['r.TechPanel.Show'] = 0
    with open(PROJECT_OUT, 'w', encoding='utf-8') as fh:
        json.dump(project, fh, indent=2)
    print('wrote', os.path.relpath(SCENE_OUT, REPO), 'and', os.path.relpath(PROJECT_OUT, REPO))


if __name__ == '__main__':
    main()
