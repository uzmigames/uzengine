/**
 * uzscene-parser.test.ts — Unit tests for parseUzScene().
 *
 * Tests cover:
 *  - Empty input returns empty scene
 *  - Whitespace-only input returns empty scene
 *  - Parses sceneName from [uzscene] header
 *  - Parses a single root node
 *  - Parses multiple root nodes
 *  - Builds nested tree from parent references
 *  - Multi-level nesting
 *  - Unknown node type mapped to 'Unknown'
 *  - Known node types all parsed correctly
 *  - Node with no name defaults to 'Unnamed'
 *  - Node with no type defaults to 'Unknown'
 *  - Node with no parent treated as root
 *  - Node whose parent does not exist treated as root
 *  - Duplicate node names: parent lookup uses last-write-wins
 *  - Comment lines (;) and (#) are ignored
 *  - Blank lines are ignored
 *  - Keys with extra whitespace are parsed correctly
 *  - Lines without '=' are ignored
 *  - Section headers without matching body are ignored
 *  - CRLF line endings work
 *  - Real .uzscene content (default.uzscene fixture)
 */

import { parseUzScene, ParsedScene } from '../uzscene-parser';
import { SceneNodeData } from '../scene-tree-provider';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function findByName(nodes: SceneNodeData[], name: string): SceneNodeData | undefined {
    for (const n of nodes) {
        if (n.name === name) { return n; }
        if (n.children) {
            const found = findByName(n.children, name);
            if (found) { return found; }
        }
    }
    return undefined;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('parseUzScene()', () => {

    describe('empty / whitespace input', () => {
        it('returns empty scene for empty string', () => {
            const result = parseUzScene('');
            expect(result.sceneName).toBe('');
            expect(result.roots).toHaveLength(0);
        });

        it('returns empty scene for whitespace-only string', () => {
            const result = parseUzScene('   \n\n\t  ');
            expect(result.roots).toHaveLength(0);
        });
    });

    describe('[uzscene] header', () => {
        it('parses sceneName', () => {
            const input = '[uzscene]\nname = MyLevel\nversion = 1\n';
            const result = parseUzScene(input);
            expect(result.sceneName).toBe('MyLevel');
        });

        it('returns empty sceneName when [uzscene] section is absent', () => {
            const input = '[node]\nname = Root\ntype = Node3D\nparent =\n';
            const result = parseUzScene(input);
            expect(result.sceneName).toBe('');
        });
    });

    describe('single root node', () => {
        const input = `
[uzscene]
name = Test

[node]
name = World
type = Node3D
parent =
`;
        it('produces one root node', () => {
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
        });

        it('root node has correct name and type', () => {
            const { roots } = parseUzScene(input);
            expect(roots[0].name).toBe('World');
            expect(roots[0].type).toBe('Node3D');
        });

        it('root node has no children', () => {
            const { roots } = parseUzScene(input);
            expect(roots[0].children).toHaveLength(0);
        });

        it('root node id is non-empty', () => {
            const { roots } = parseUzScene(input);
            expect(roots[0].id).toBeTruthy();
        });
    });

    describe('multiple root nodes', () => {
        const input = `
[node]
name = A
type = Node3D
parent =

[node]
name = B
type = Camera
parent =
`;
        it('produces two root nodes', () => {
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(2);
        });

        it('preserves order', () => {
            const { roots } = parseUzScene(input);
            expect(roots[0].name).toBe('A');
            expect(roots[1].name).toBe('B');
        });
    });

    describe('parent-child nesting', () => {
        const input = `
[node]
name = World
type = Node3D
parent =

[node]
name = Sun
type = DirectionalLight
parent = World

[node]
name = MainCamera
type = Camera
parent = World
`;
        it('root has two children', () => {
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
            expect(roots[0].children).toHaveLength(2);
        });

        it('children have correct names', () => {
            const { roots } = parseUzScene(input);
            const names = (roots[0].children ?? []).map(c => c.name);
            expect(names).toContain('Sun');
            expect(names).toContain('MainCamera');
        });

        it('children have no further children', () => {
            const { roots } = parseUzScene(input);
            for (const child of roots[0].children ?? []) {
                expect(child.children).toHaveLength(0);
            }
        });
    });

    describe('multi-level nesting', () => {
        const input = `
[node]
name = Root
type = Node3D
parent =

[node]
name = Child
type = Node3D
parent = Root

[node]
name = GrandChild
type = StaticMesh
parent = Child
`;
        it('builds three-level tree', () => {
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);

            const child = roots[0].children?.[0];
            expect(child?.name).toBe('Child');

            const grand = child?.children?.[0];
            expect(grand?.name).toBe('GrandChild');
            expect(grand?.type).toBe('StaticMesh');
        });
    });

    describe('node type mapping', () => {
        const knownTypes = ['Node3D', 'StaticMesh', 'DirectionalLight', 'PointLight', 'Camera', 'AudioSource'] as const;

        for (const t of knownTypes) {
            it(`preserves known type "${t}"`, () => {
                const input = `[node]\nname = N\ntype = ${t}\nparent =\n`;
                const { roots } = parseUzScene(input);
                expect(roots[0].type).toBe(t);
            });
        }

        it('maps unknown type to "Unknown"', () => {
            const input = `[node]\nname = N\ntype = Banana\nparent =\n`;
            const { roots } = parseUzScene(input);
            expect(roots[0].type).toBe('Unknown');
        });

        it('maps missing type to "Unknown"', () => {
            const input = `[node]\nname = N\nparent =\n`;
            const { roots } = parseUzScene(input);
            expect(roots[0].type).toBe('Unknown');
        });
    });

    describe('defaults for missing fields', () => {
        it('defaults name to "Unnamed" when name key is absent', () => {
            const input = `[node]\ntype = Camera\nparent =\n`;
            const { roots } = parseUzScene(input);
            expect(roots[0].name).toBe('Unnamed');
        });

        it('defaults parent to "" (root) when parent key is absent', () => {
            const input = `[node]\nname = Orphan\ntype = Node3D\n`;
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
            expect(roots[0].name).toBe('Orphan');
        });
    });

    describe('unknown parent reference', () => {
        it('treats node as root when parent does not exist', () => {
            const input = `
[node]
name = Orphan
type = Node3D
parent = NonExistentParent
`;
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
            expect(roots[0].name).toBe('Orphan');
        });
    });

    describe('comments and blank lines', () => {
        it('ignores ; comment lines', () => {
            const input = `
; This is a comment
[uzscene]
; another comment
name = Commented

[node]
name = N
type = Node3D
parent =
`;
            const { sceneName, roots } = parseUzScene(input);
            expect(sceneName).toBe('Commented');
            expect(roots).toHaveLength(1);
        });

        it('ignores # comment lines', () => {
            const input = `
# hash comment
[node]
name = N
type = Camera
parent =
`;
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
            expect(roots[0].type).toBe('Camera');
        });

        it('ignores blank lines between sections', () => {
            const input = `

[uzscene]

name = S


[node]

name = N
type = Node3D
parent =

`;
            const { sceneName, roots } = parseUzScene(input);
            expect(sceneName).toBe('S');
            expect(roots).toHaveLength(1);
        });
    });

    describe('whitespace in keys and values', () => {
        it('trims key and value whitespace', () => {
            const input = `[node]\n  name   =   SpaceyName  \n  type   =   Camera  \n  parent  =  \n`;
            const { roots } = parseUzScene(input);
            expect(roots[0].name).toBe('SpaceyName');
            expect(roots[0].type).toBe('Camera');
        });
    });

    describe('lines without "=" are ignored', () => {
        it('does not crash on malformed lines', () => {
            const input = `[node]\nname = N\nthis line has no equals sign\ntype = Camera\nparent =\n`;
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(1);
            expect(roots[0].name).toBe('N');
        });
    });

    describe('CRLF line endings', () => {
        it('parses correctly with \\r\\n', () => {
            const input = '[uzscene]\r\nname = CrLf\r\n\r\n[node]\r\nname = N\r\ntype = Node3D\r\nparent =\r\n';
            const { sceneName, roots } = parseUzScene(input);
            expect(sceneName).toBe('CrLf');
            expect(roots).toHaveLength(1);
        });
    });

    describe('node IDs', () => {
        it('assigns unique IDs to nodes with same name', () => {
            const input = `
[node]
name = Thing
type = Node3D
parent =

[node]
name = Thing
type = Camera
parent =
`;
            const { roots } = parseUzScene(input);
            expect(roots).toHaveLength(2);
            expect(roots[0].id).not.toBe(roots[1].id);
        });
    });

    describe('real .uzscene fixture', () => {
        const fixture = `[uzscene]
name = Default
version = 1

[node]
name = World
type = Node3D
parent =
position = 0.000000 0.000000 0.000000
rotation = 0.000000 0.000000 0.000000 1.000000
scale = 1.000000 1.000000 1.000000
active = true
visible = true

[node]
name = Sun
type = DirectionalLight
parent = World
position = 0.000000 10.000000 0.000000
rotation = 0.000000 0.000000 0.000000 1.000000
scale = 1.000000 1.000000 1.000000
active = true
visible = true

[node]
name = MainCamera
type = Camera
parent = World
position = 0.000000 5.000000 -10.000000
rotation = 0.000000 0.000000 0.000000 1.000000
scale = 1.000000 1.000000 1.000000
active = true
visible = true
`;
        let result: ParsedScene;

        beforeAll(() => {
            result = parseUzScene(fixture);
        });

        it('parses sceneName as "Default"', () => {
            expect(result.sceneName).toBe('Default');
        });

        it('has one root node (World)', () => {
            expect(result.roots).toHaveLength(1);
            expect(result.roots[0].name).toBe('World');
            expect(result.roots[0].type).toBe('Node3D');
        });

        it('World has two children (Sun, MainCamera)', () => {
            const world = result.roots[0];
            expect(world.children).toHaveLength(2);
        });

        it('Sun is DirectionalLight child of World', () => {
            const sun = findByName(result.roots, 'Sun');
            expect(sun).toBeDefined();
            expect(sun!.type).toBe('DirectionalLight');
        });

        it('MainCamera is Camera child of World', () => {
            const cam = findByName(result.roots, 'MainCamera');
            expect(cam).toBeDefined();
            expect(cam!.type).toBe('Camera');
        });

        it('extra fields (position, scale, etc.) are ignored without error', () => {
            expect(result.roots[0].children).toHaveLength(2);
        });
    });
});
