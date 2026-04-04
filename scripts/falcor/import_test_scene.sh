#!/usr/bin/env bash
# Import DamagedHelmet textures into the content directory structure for
# the Falcor comparison .uzscene.
#
# Prerequisites:
#   - DamagedHelmet.gltf extracted into .sandbox/scenes/ (textures as .jpg)
#   - runtime built (for mesh import: uzengine.exe --import)
#
# Usage:
#   bash scripts/falcor/import_test_scene.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SANDBOX="$REPO_ROOT/.sandbox/scenes"
CONTENT="$SANDBOX/content"

# ── Verify source textures exist ──────────────────────────────────────────
REQUIRED_TEXTURES=(
    "Default_albedo.jpg"
    "Default_normal.jpg"
    "Default_metalRoughness.jpg"
    "Default_AO.jpg"
    "Default_emissive.jpg"
)

echo "=== DamagedHelmet Test Scene Setup ==="
echo "Source:  $SANDBOX"
echo "Content: $CONTENT"
echo ""

for tex in "${REQUIRED_TEXTURES[@]}"; do
    if [[ ! -f "$SANDBOX/$tex" ]]; then
        echo "ERROR: Missing texture: $SANDBOX/$tex"
        echo "Extract DamagedHelmet.gltf first (e.g., from glTF-Sample-Models)"
        exit 1
    fi
done

# ── Create content directories ────────────────────────────────────────────
mkdir -p "$CONTENT/textures" "$CONTENT/meshes" "$CONTENT/scenes"

# ── Copy textures ─────────────────────────────────────────────────────────
echo "Copying textures..."
for tex in "${REQUIRED_TEXTURES[@]}"; do
    cp "$SANDBOX/$tex" "$CONTENT/textures/$tex"
    echo "  -> textures/$tex"
done

# ── Check for .uzmesh ────────────────────────────────────────────────────
if [[ -f "$CONTENT/meshes/DamagedHelmet.uzmesh" ]]; then
    echo "Mesh already exists: meshes/DamagedHelmet.uzmesh"
else
    echo ""
    echo "NOTE: meshes/DamagedHelmet.uzmesh not found."
    echo "To import the mesh, run:"
    echo "  uzengine.exe --import .sandbox/scenes/DamagedHelmet.gltf --output .sandbox/scenes/content/meshes/"
    echo ""
    echo "Or copy a pre-imported .uzmesh into: $CONTENT/meshes/"
fi

echo ""
echo "=== Setup Complete ==="
echo "Scene:   $CONTENT/scenes/damaged_helmet.uzscene"
echo "Project: $SANDBOX/damaged_helmet.uzproject"
echo ""
echo "Run:"
echo "  uzengine.exe --project .sandbox/scenes/damaged_helmet.uzproject --editor"
