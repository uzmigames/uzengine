"""Writes the four boxes of scripts/rendering/surface-cache-locallight.uzscene (Phase D stage 2, acceptance
B2) as separate meshes under sample/content/meshes/reference/.

The GI material table the ray passes shade their hits with is indexed by mesh (TLAS InstanceID =
meshId, SceneRenderer::UpdateGIMaterialTable), so every entity with its own material needs its own
mesh file: on a shared cube.uzmesh the emissive fixture's black base colour and radiance 8 land on
the floor, the block and the mirror alike. Geometry in world coordinates (cm, Z up), identity
transforms in the scene; the boxes are what the scene's scaled cube.uzmesh entities covered.

Usage: python scripts/rendering/surface_cache_locallight_meshes.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_side_reference import CONTENT, REPO, box_mesh_bytes  # noqa: E402

SCENE = 'surface-cache-locallight'

# name: (lo, hi)
BOXES = {
    # Mirror wall, faces the camera (-y side at y = 595).
    'mirror': ((-600.0, 595.0, -100.0), (600.0, 605.0, 500.0)),
    # Floor, top face at z = -100.
    'floor': ((-800.0, -500.0, -110.0), (800.0, 1100.0, -100.0)),
    # Off-screen block: outside the 40-degree frustum, its +y face and top seen in the mirror.
    'block': ((320.0, 240.0, -100.0), (440.0, 360.0, 20.0)),
    # Emissive fixture: its bottom face is the rect light's 120 x 80 rectangle at z = 160
    # (width 120 along y, height 80 along x), facing down.
    'fixture': ((340.0, 370.0, 160.0), (420.0, 490.0, 160.2)),
}


def mesh_asset(name):
    return f'sample/content/meshes/reference/{SCENE}_{name}.uzmesh'


def generate():
    written = []
    mesh_dir = os.path.join(CONTENT, 'meshes', 'reference')
    os.makedirs(mesh_dir, exist_ok=True)
    for name, (lo, hi) in BOXES.items():
        path = os.path.join(mesh_dir, f'{SCENE}_{name}.uzmesh')
        with open(path, 'wb') as fh:
            fh.write(box_mesh_bytes(lo, hi))
        written.append(path)
    return written


if __name__ == '__main__':
    for p in generate():
        print('wrote', os.path.relpath(p, REPO))
