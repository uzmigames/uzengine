/**
 * uzscene-parser.ts — Local parser for the .uzscene INI-like file format.
 *
 * Parses a .uzscene file into a tree of SceneNodeData without requiring the
 * engine to be running. Used to populate the Scene Tree from disk when IPC
 * is not available.
 *
 * Format:
 *   [uzscene]
 *   name = Default
 *   version = 1
 *
 *   [node]
 *   name = World
 *   type = Node3D
 *   parent =          ← empty = root node
 *
 *   [node]
 *   name = Sun
 *   type = DirectionalLight
 *   parent = World
 */

import { SceneNodeData, NodeType } from './scene-tree-provider';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** Raw parsed node before tree assembly. */
interface RawNode {
    name: string;
    type: NodeType;
    parent: string;   // empty string = root
}

/** Result of parsing a .uzscene file. */
export interface ParsedScene {
    /** Scene name from [uzscene] header. */
    readonly sceneName: string;
    /** Root nodes with children nested. */
    readonly roots: SceneNodeData[];
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

const KNOWN_TYPES: ReadonlySet<string> = new Set([
    'Node3D', 'StaticMesh', 'DirectionalLight', 'PointLight', 'Camera', 'AudioSource',
]);

function toNodeType(raw: string): NodeType {
    return KNOWN_TYPES.has(raw) ? (raw as NodeType) : 'Unknown';
}

/**
 * Parses the text content of a .uzscene file and returns a ParsedScene.
 *
 * - Unknown node types are mapped to 'Unknown'.
 * - Nodes whose `parent` field references a non-existent node are treated as roots.
 * - Duplicate node names: each gets a unique ID (name + index).
 * - Empty or whitespace-only input returns an empty scene.
 */
export function parseUzScene(text: string): ParsedScene {
    const lines = text.split(/\r?\n/);

    let sceneName = '';
    let currentSection = '';
    let currentNode: Partial<RawNode> | null = null;
    const rawNodes: RawNode[] = [];

    for (const raw of lines) {
        const line = raw.trim();

        // Skip blank lines and comments
        if (!line || line.startsWith(';') || line.startsWith('#')) {
            continue;
        }

        // Section header
        if (line.startsWith('[') && line.endsWith(']')) {
            // Flush previous node
            if (currentSection === 'node' && currentNode) {
                rawNodes.push(finalizeNode(currentNode));
                currentNode = null;
            }
            currentSection = line.slice(1, -1).toLowerCase();
            if (currentSection === 'node') {
                currentNode = {};
            }
            continue;
        }

        // Key = value
        const eqIdx = line.indexOf('=');
        if (eqIdx === -1) { continue; }

        const key = line.slice(0, eqIdx).trim().toLowerCase();
        const value = line.slice(eqIdx + 1).trim();

        if (currentSection === 'uzscene') {
            if (key === 'name') { sceneName = value; }
        } else if (currentSection === 'node' && currentNode) {
            if (key === 'name')   { currentNode.name   = value; }
            if (key === 'type')   { currentNode.type   = toNodeType(value); }
            if (key === 'parent') { currentNode.parent = value; }
        }
    }

    // Flush last node
    if (currentSection === 'node' && currentNode) {
        rawNodes.push(finalizeNode(currentNode));
    }

    const roots = buildTree(rawNodes);
    return { sceneName, roots };
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

function finalizeNode(partial: Partial<RawNode>): RawNode {
    return {
        name:   partial.name   ?? 'Unnamed',
        type:   partial.type   ?? 'Unknown',
        parent: partial.parent ?? '',
    };
}

/**
 * Converts a flat list of RawNodes into a nested SceneNodeData tree.
 *
 * Node IDs are generated as `name_index` to be stable for the session.
 */
function buildTree(rawNodes: RawNode[]): SceneNodeData[] {
    // Assign IDs
    const nodes: Array<SceneNodeData & { _parent: string }> = rawNodes.map((r, i) => ({
        id:       `${r.name}_${i}`,
        name:     r.name,
        type:     r.type,
        children: [],
        _parent:  r.parent,
    }));

    // Index by name for parent lookup (last one wins on duplicate names)
    const byName = new Map<string, typeof nodes[number]>();
    for (const n of nodes) {
        byName.set(n.name, n);
    }

    const roots: SceneNodeData[] = [];

    for (const node of nodes) {
        if (!node._parent) {
            roots.push(node);
        } else {
            const parent = byName.get(node._parent);
            if (parent) {
                (parent.children as SceneNodeData[]).push(node);
            } else {
                // Parent not found — treat as root
                roots.push(node);
            }
        }
    }

    return roots;
}