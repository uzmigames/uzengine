#!/usr/bin/env bash
# create_patch.sh — Create a patch for a modified thirdparty file (macOS/Linux)
#
# Usage:
#   ./scripts/create_patch.sh --library stb --file stb_truetype.h --description "add-uzengine-compat"
#
# This creates a unified diff patch in runtime/thirdparty/<library>/patches/
# following Godot's convention: NNNN-description.patch
#
# Workflow:
#   1. Modify the thirdparty file directly
#   2. Run this script to capture the diff as a numbered patch
#   3. Commit the patch file (NOT the modified thirdparty file)
#   4. setup.sh will re-download clean sources and apply patches automatically

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
THIRDPARTY_DIR="$ROOT_DIR/runtime/thirdparty"

# ─── Colors ──────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

# ─── Argument Parsing ─────────────────────────────────────────────────────────

LIBRARY=""
FILE=""
DESCRIPTION=""

print_usage() {
    echo "Usage: $0 --library <name> --file <relative-path> --description <slug>"
    echo ""
    echo "Example:"
    echo "  $0 --library stb --file stb_truetype.h --description add-uzengine-compat"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --library|-l)
            LIBRARY="$2"
            shift 2
            ;;
        --file|-f)
            FILE="$2"
            shift 2
            ;;
        --description|-d)
            DESCRIPTION="$2"
            shift 2
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}ERROR: Unknown argument: $1${NC}" >&2
            print_usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "$LIBRARY" ]] || [[ -z "$FILE" ]] || [[ -z "$DESCRIPTION" ]]; then
    echo -e "${RED}ERROR: --library, --file, and --description are all required.${NC}" >&2
    echo "" >&2
    print_usage >&2
    exit 1
fi

LIB_PATH="$THIRDPARTY_DIR/$LIBRARY"
FILE_PATH="$LIB_PATH/$FILE"
PATCH_DIR="$LIB_PATH/patches"

# ─── Check file exists ────────────────────────────────────────────────────────

if [[ ! -f "$FILE_PATH" ]]; then
    echo -e "${RED}ERROR: File not found: $FILE_PATH${NC}" >&2
    exit 1
fi

# ─── Detect changes via git diff ─────────────────────────────────────────────

# Check unstaged changes first, then staged
DIFF="$(git -C "$ROOT_DIR" diff -- "$FILE_PATH" 2>/dev/null || true)"
if [[ -z "$DIFF" ]]; then
    DIFF="$(git -C "$ROOT_DIR" diff --cached -- "$FILE_PATH" 2>/dev/null || true)"
fi

if [[ -z "$DIFF" ]]; then
    echo -e "${RED}ERROR: No changes detected in $FILE_PATH.${NC}" >&2
    echo "Modify the file first, then run this script." >&2
    exit 1
fi

# ─── Create patches directory ────────────────────────────────────────────────

mkdir -p "$PATCH_DIR"

# ─── Determine next patch number ─────────────────────────────────────────────

LAST_NUM=0
if compgen -G "$PATCH_DIR/*.patch" > /dev/null 2>&1; then
    # Extract numeric prefix from the highest-sorted patch filename, strip leading zeros
    LAST_PATCH="$(ls "$PATCH_DIR"/*.patch 2>/dev/null | sort | tail -1 | xargs basename)"
    LAST_NUM="$(printf '%s' "$LAST_PATCH" | cut -c1-4 | sed 's/^0*//' | grep -E '^[0-9]+$' || echo 0)"
    LAST_NUM="${LAST_NUM:-0}"
fi

NEXT_NUM=$(( LAST_NUM + 1 ))
PATCH_NAME="$(printf '%04d' "$NEXT_NUM")-${DESCRIPTION}.patch"
PATCH_PATH="$PATCH_DIR/$PATCH_NAME"

# ─── Write the patch ─────────────────────────────────────────────────────────

printf '%s\n' "$DIFF" > "$PATCH_PATH"

echo -e "${GREEN}Patch created: $PATCH_PATH${NC}"
echo ""
echo -e "${CYAN}Next steps:${NC}"
echo "  1. git add $PATCH_PATH"
echo "  2. git checkout -- $FILE_PATH  (restore original)"
echo "  3. Commit the patch file"
echo "  4. setup.sh will apply it automatically on next run"
