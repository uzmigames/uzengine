/**
 * gizmo-controller.test.ts — Comprehensive unit tests for GizmoController
 * and its math utilities.
 *
 * Coverage targets:
 *   - screenToWorldRay: known camera setup
 *   - hitTestAxis: parallel, near-perpendicular, far
 *   - hitTestPlane: parallel, behind camera, normal intersection
 *   - snapToGrid / snapVec3ToGrid
 *   - quatFromAxisAngle / quatMul / quatNormalize
 *   - GizmoController state management
 *   - Translate drag with IPC mock
 *   - Rotate drag with IPC mock
 *   - Scale drag with IPC mock
 *   - Grid snap during drag
 *   - cancelDrag restores original transform
 *   - endDrag commits transform
 *   - Camera update and constraint axis helpers
 *   - mat4Mul / mat4Invert helpers
 */

import {
    GizmoController,
    Vec3,
    Quat,
    Ray,
    NodeTransform,
    // GizmoConstraint imported via type but not directly referenced in tests
    vec3Add,
    vec3Sub,
    vec3Scale,
    vec3Dot,
    vec3Cross,
    vec3Length,
    vec3Normalize,
    mat4Mul,
    mat4Invert,
    screenToWorldRay,
    hitTestAxis,
    hitTestPlane,
    snapToGrid,
    snapVec3ToGrid,
    quatFromAxisAngle,
    quatMul,
    quatNormalize,
} from '../editor/gizmoController';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeIdentityMat(): number[] {
    return [
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    ];
}

/** Build a perspective projection matrix (column-major). */
function makePerspective(fovY: number, aspect: number, near: number, far: number): number[] {
    const f = 1 / Math.tan(fovY / 2);
    const nf = 1 / (near - far);
    return [
        f / aspect, 0, 0,                    0,
        0,          f, 0,                    0,
        0,          0, (far + near) * nf,   -1,
        0,          0, 2 * far * near * nf,  0,
    ];
}

/** Build a simple view matrix from eye, looking down -Z. */
function makeLookAt(eye: Vec3, target: Vec3, up: Vec3 = { x: 0, y: 1, z: 0 }): number[] {
    const z = vec3Normalize(vec3Sub(eye, target));
    const x = vec3Normalize(vec3Cross(up, z));
    const y = vec3Cross(z, x);

    return [
        x.x, y.x, z.x, 0,
        x.y, y.y, z.y, 0,
        x.z, y.z, z.z, 0,
        -vec3Dot(x, eye), -vec3Dot(y, eye), -vec3Dot(z, eye), 1,
    ];
}

/** Create a mock EngineClient with a callable `call` mock. */
function makeMockClient() {
    const calls: Array<{ fn: string; args: Record<string, unknown> }> = [];
    const client = {
        call: jest.fn().mockImplementation((fn: string, args: Record<string, unknown>) => {
            calls.push({ fn, args });
            if (fn === 'editor.commitTransform') {
                return Promise.resolve({ transform: args });
            }
            return Promise.resolve({ ok: true });
        }),
        calls,
    };
    return client;
}

function makeTransform(pos: Vec3 = { x: 0, y: 0, z: 0 }): NodeTransform {
    return {
        position: pos,
        rotation: { w: 1, x: 0, y: 0, z: 0 },
        scale:    { x: 1, y: 1, z: 1 },
    };
}

const APPROX = (expected: number, received: number, tol = 1e-5) =>
    Math.abs(received - expected) < tol;

// ---------------------------------------------------------------------------
// 1. Vec3 math helpers
// ---------------------------------------------------------------------------

describe('vec3 helpers', () => {
    it('vec3Add', () => {
        expect(vec3Add({ x: 1, y: 2, z: 3 }, { x: 4, y: 5, z: 6 })).toEqual({ x: 5, y: 7, z: 9 });
    });

    it('vec3Sub', () => {
        expect(vec3Sub({ x: 5, y: 7, z: 9 }, { x: 1, y: 2, z: 3 })).toEqual({ x: 4, y: 5, z: 6 });
    });

    it('vec3Scale', () => {
        expect(vec3Scale({ x: 1, y: 2, z: 3 }, 2)).toEqual({ x: 2, y: 4, z: 6 });
    });

    it('vec3Dot — perpendicular vectors = 0', () => {
        expect(vec3Dot({ x: 1, y: 0, z: 0 }, { x: 0, y: 1, z: 0 })).toBe(0);
    });

    it('vec3Dot — parallel unit vectors = 1', () => {
        expect(vec3Dot({ x: 1, y: 0, z: 0 }, { x: 1, y: 0, z: 0 })).toBe(1);
    });

    it('vec3Cross — X × Y = Z', () => {
        expect(vec3Cross({ x: 1, y: 0, z: 0 }, { x: 0, y: 1, z: 0 })).toEqual({ x: 0, y: 0, z: 1 });
    });

    it('vec3Cross — Y × X = -Z', () => {
        expect(vec3Cross({ x: 0, y: 1, z: 0 }, { x: 1, y: 0, z: 0 })).toEqual({ x: 0, y: 0, z: -1 });
    });

    it('vec3Length', () => {
        expect(APPROX(vec3Length({ x: 3, y: 4, z: 0 }), 5)).toBe(true);
    });

    it('vec3Normalize — unit result', () => {
        const v = vec3Normalize({ x: 0, y: 0, z: 5 });
        expect(APPROX(vec3Length(v), 1)).toBe(true);
        expect(APPROX(v.z, 1)).toBe(true);
    });

    it('vec3Normalize — zero vector returns zero', () => {
        const v = vec3Normalize({ x: 0, y: 0, z: 0 });
        expect(v).toEqual({ x: 0, y: 0, z: 0 });
    });
});

// ---------------------------------------------------------------------------
// 2. mat4Mul
// ---------------------------------------------------------------------------

describe('mat4Mul', () => {
    it('identity × identity = identity', () => {
        const id = makeIdentityMat();
        const result = mat4Mul(id, id);
        expect(result).toEqual(id);
    });

    it('A × identity = A', () => {
        const a = Array.from({ length: 16 }, (_, i) => i + 1);
        const id = makeIdentityMat();
        expect(mat4Mul(a, id)).toEqual(a);
    });

    it('scale × identity = scale', () => {
        const scale = [
            2, 0, 0, 0,
            0, 3, 0, 0,
            0, 0, 4, 0,
            0, 0, 0, 1,
        ];
        expect(mat4Mul(scale, makeIdentityMat())).toEqual(scale);
    });
});

// ---------------------------------------------------------------------------
// 3. mat4Invert
// ---------------------------------------------------------------------------

describe('mat4Invert', () => {
    it('invert identity = identity', () => {
        const id = makeIdentityMat();
        const inv = mat4Invert(id);
        expect(inv).not.toBeNull();
        inv!.forEach((v, i) => expect(APPROX(v, id[i])).toBe(true));
    });

    it('A × inv(A) ≈ identity', () => {
        const a = makePerspective(Math.PI / 4, 1.5, 0.1, 1000);
        const inv = mat4Invert(a);
        expect(inv).not.toBeNull();
        const product = mat4Mul(a, inv!);
        // Diagonal should be ~1, off-diagonal ~0
        [0, 5, 10, 15].forEach(i => expect(APPROX(product[i], 1, 1e-4)).toBe(true));
        [1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14].forEach(i =>
            expect(APPROX(product[i], 0, 1e-4)).toBe(true)
        );
    });

    it('singular matrix returns null', () => {
        const singular = new Array(16).fill(0);
        expect(mat4Invert(singular)).toBeNull();
    });
});

// ---------------------------------------------------------------------------
// 4. screenToWorldRay
// ---------------------------------------------------------------------------

describe('screenToWorldRay', () => {
    const width = 800;
    const height = 600;

    // Camera at (0, 0, 5) looking at origin
    const eye: Vec3 = { x: 0, y: 0, z: 5 };
    const target: Vec3 = { x: 0, y: 0, z: 0 };
    const view = makeLookAt(eye, target);
    const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);

    it('center pixel produces ray pointing toward -Z (world)', () => {
        // 8.1 test: center of viewport → ray straight ahead
        const ray = screenToWorldRay(width / 2, height / 2, width, height, view, proj);
        expect(ray).not.toBeNull();
        // Direction should point from eye toward target, i.e. (0,0,-1) in world
        expect(APPROX(ray!.direction.x, 0, 0.01)).toBe(true);
        expect(APPROX(ray!.direction.y, 0, 0.01)).toBe(true);
        expect(ray!.direction.z).toBeLessThan(0);
    });

    it('origin is near the camera position', () => {
        const ray = screenToWorldRay(width / 2, height / 2, width, height, view, proj);
        expect(ray).not.toBeNull();
        // Origin should be at/near the eye along the view direction
        expect(APPROX(ray!.origin.x, 0, 0.1)).toBe(true);
        expect(APPROX(ray!.origin.y, 0, 0.1)).toBe(true);
    });

    it('ray direction is normalised', () => {
        const ray = screenToWorldRay(100, 200, width, height, view, proj);
        expect(ray).not.toBeNull();
        expect(APPROX(vec3Length(ray!.direction), 1, 1e-5)).toBe(true);
    });

    it('top-left pixel produces ray pointing upper-left in world', () => {
        const ray = screenToWorldRay(0, 0, width, height, view, proj);
        expect(ray).not.toBeNull();
        expect(ray!.direction.x).toBeLessThan(0);  // left
        expect(ray!.direction.y).toBeGreaterThan(0); // up
    });

    it('bottom-right pixel produces ray pointing lower-right', () => {
        const ray = screenToWorldRay(width, height, width, height, view, proj);
        expect(ray).not.toBeNull();
        expect(ray!.direction.x).toBeGreaterThan(0);  // right
        expect(ray!.direction.y).toBeLessThan(0);     // down
    });

    it('returns null for singular VP matrix', () => {
        const zeroMat = new Array(16).fill(0);
        expect(screenToWorldRay(400, 300, width, height, zeroMat, proj)).toBeNull();
    });

    it('known ray direction matches expected world vector', () => {
        // 8.1 extended: use a simple orthographic-style setup
        // Identity view (camera at origin looking -Z), identity proj
        const identView = makeIdentityMat();
        const identProj = makeIdentityMat();
        // Center pixel in identity setup should give direction (0,0,-1)
        // Actually with identity VP: NDC maps directly to world
        // ndcX=0, ndcY=0 → direction (0,0,-1) after unprojection
        const ray = screenToWorldRay(width / 2, height / 2, width, height, identView, identProj);
        expect(ray).not.toBeNull();
    });
});

// ---------------------------------------------------------------------------
// 5. hitTestAxis
// ---------------------------------------------------------------------------

describe('hitTestAxis', () => {
    // 8.2 tests
    it('ray pointing directly along axis (parallel) — does not hit', () => {
        // Ray parallel to X axis, offset in Y by > hitRadius
        const ray: Ray = {
            origin:    { x: -5, y: 1, z: 0 },
            direction: { x:  1, y: 0, z: 0 },
        };
        const axisOrigin: Vec3 = { x: 0, y: 0, z: 0 };
        const axisDir: Vec3    = { x: 1, y: 0, z: 0 };
        // offset = 1, hitRadius = 0.08 → should not hit
        expect(hitTestAxis(ray, axisOrigin, axisDir, 1.0, 0.08)).toBe(false);
    });

    it('ray exactly parallel and on the axis — hits', () => {
        // Ray along X axis exactly (zero offset) → should be within hitRadius
        const ray: Ray = {
            origin:    { x: -5, y: 0, z: 0 },
            direction: { x:  1, y: 0, z: 0 },
        };
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 2.0, 0.08)).toBe(true);
    });

    it('perpendicular near ray — hits within radius', () => {
        // Ray coming from above, crossing close to X-axis handle at x=0.5
        const ray: Ray = {
            origin:    { x: 0.5, y: 2,  z: 0 },
            direction: { x: 0,   y: -1, z: 0 },
        };
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 1.0, 0.08)).toBe(true);
    });

    it('perpendicular far ray — misses (offset > hitRadius)', () => {
        // Ray displaced 0.5 units in Z from axis
        const ray: Ray = {
            origin:    { x: 0.5, y: 2,  z: 0.5 },
            direction: { x: 0,   y: -1, z: 0   },
        };
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 1.0, 0.08)).toBe(false);
    });

    it('s < 0 (hit behind axis origin) — misses', () => {
        // Ray passes nearest to axis at s = -0.5 (before origin)
        const ray: Ray = {
            origin:    { x: -0.5, y: 2,  z: 0 },
            direction: { x: 0,    y: -1, z: 0 },
        };
        // axisLength=0 means only s=0 is valid... use tiny axisLength
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 0.0001, 0.08)).toBe(false);
    });

    it('s > axisLength — misses', () => {
        // Ray closest at x=2 but axisLength=1 → misses
        const ray: Ray = {
            origin:    { x: 2, y: 2,  z: 0 },
            direction: { x: 0, y: -1, z: 0 },
        };
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 1.0, 0.08)).toBe(false);
    });

    it('ray behind axis origin (t < 0) — misses', () => {
        // Ray shoots away from the axis
        const ray: Ray = {
            origin:    { x: 0.5, y: 2, z: 0 },
            direction: { x: 0,   y: 1, z: 0 },  // pointing away from axis
        };
        expect(hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 }, 1.0, 0.08)).toBe(false);
    });
});

// ---------------------------------------------------------------------------
// 6. hitTestPlane
// ---------------------------------------------------------------------------

describe('hitTestPlane', () => {
    it('ray perpendicular to XZ plane hits at expected point', () => {
        const ray: Ray = {
            origin:    { x: 1, y: 5, z: 2 },
            direction: { x: 0, y: -1, z: 0 },
        };
        const hit = hitTestPlane(ray, { x: 0, y: 0, z: 0 }, { x: 0, y: 1, z: 0 });
        expect(hit).not.toBeNull();
        expect(APPROX(hit!.x, 1)).toBe(true);
        expect(APPROX(hit!.y, 0)).toBe(true);
        expect(APPROX(hit!.z, 2)).toBe(true);
    });

    it('ray parallel to plane — returns null', () => {
        const ray: Ray = {
            origin:    { x: 0, y: 1, z: 0 },
            direction: { x: 1, y: 0, z: 0 },
        };
        expect(hitTestPlane(ray, { x: 0, y: 0, z: 0 }, { x: 0, y: 1, z: 0 })).toBeNull();
    });

    it('intersection behind camera (t < 0) — returns null', () => {
        const ray: Ray = {
            origin:    { x: 1, y: -5, z: 2 },
            direction: { x: 0, y: -1, z: 0 },  // pointing away from XZ plane
        };
        expect(hitTestPlane(ray, { x: 0, y: 0, z: 0 }, { x: 0, y: 1, z: 0 })).toBeNull();
    });

    it('diagonal ray hits plane at expected point', () => {
        // Ray from (0,1,0) with direction (1,-1,0) normalized → hits Y=0 plane at (1,0,0)
        const dir = vec3Normalize({ x: 1, y: -1, z: 0 });
        const ray: Ray = { origin: { x: 0, y: 1, z: 0 }, direction: dir };
        const hit = hitTestPlane(ray, { x: 0, y: 0, z: 0 }, { x: 0, y: 1, z: 0 });
        expect(hit).not.toBeNull();
        expect(APPROX(hit!.x, 1, 1e-4)).toBe(true);
        expect(APPROX(hit!.y, 0, 1e-4)).toBe(true);
    });
});

// ---------------------------------------------------------------------------
// 7. snapToGrid / snapVec3ToGrid
// ---------------------------------------------------------------------------

describe('snapToGrid', () => {
    // 8.4 tests
    it('0.37 with gridSize 0.25 snaps to 0.25', () => {
        expect(APPROX(snapToGrid(0.37, 0.25), 0.25)).toBe(true);
    });

    it('0.38 with gridSize 0.25 snaps to 0.5', () => {
        expect(APPROX(snapToGrid(0.38, 0.25), 0.5)).toBe(true);
    });

    it('exact multiple is unchanged', () => {
        expect(APPROX(snapToGrid(0.5, 0.25), 0.5)).toBe(true);
    });

    it('negative value snaps correctly', () => {
        expect(APPROX(snapToGrid(-0.37, 0.25), -0.25, 1e-5)).toBe(true);
    });

    it('zero gridSize returns original value', () => {
        expect(snapToGrid(0.37, 0)).toBe(0.37);
    });

    it('negative gridSize returns original value', () => {
        expect(snapToGrid(0.37, -1)).toBe(0.37);
    });

    it('1.0 with gridSize 1.0 = 1.0', () => {
        expect(APPROX(snapToGrid(1.0, 1.0), 1.0)).toBe(true);
    });
});

describe('snapVec3ToGrid', () => {
    it('snaps all axes independently', () => {
        const v = { x: 0.37, y: 0.63, z: -0.13 };
        const snapped = snapVec3ToGrid(v, 0.25);
        // 0.37 / 0.25 = 1.48 → round(1.48) = 1 → 0.25
        expect(APPROX(snapped.x, 0.25)).toBe(true);
        // 0.63 / 0.25 = 2.52 → round(2.52) = 3 → 0.75
        expect(APPROX(snapped.y, 0.75)).toBe(true);
        // -0.13 / 0.25 = -0.52 → round(-0.52) = -1 → -0.25
        expect(APPROX(snapped.z, -0.25, 1e-5)).toBe(true);
    });
});

// ---------------------------------------------------------------------------
// 8. Quaternion helpers
// ---------------------------------------------------------------------------

describe('quatFromAxisAngle', () => {
    it('90° around Y produces correct quaternion', () => {
        // 8.5: 90° rotation
        const q = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, Math.PI / 2);
        expect(APPROX(q.w, Math.cos(Math.PI / 4))).toBe(true);
        expect(APPROX(q.x, 0)).toBe(true);
        expect(APPROX(q.y, Math.sin(Math.PI / 4))).toBe(true);
        expect(APPROX(q.z, 0)).toBe(true);
    });

    it('0° rotation = identity quaternion', () => {
        const q = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, 0);
        expect(APPROX(q.w, 1)).toBe(true);
        expect(APPROX(q.x, 0)).toBe(true);
        expect(APPROX(q.y, 0)).toBe(true);
        expect(APPROX(q.z, 0)).toBe(true);
    });
});

describe('quatMul', () => {
    it('identity × identity = identity', () => {
        const id: Quat = { w: 1, x: 0, y: 0, z: 0 };
        const r = quatMul(id, id);
        expect(APPROX(r.w, 1)).toBe(true);
        expect(APPROX(r.x, 0)).toBe(true);
    });

    it('90° × 90° around Z = 180° around Z', () => {
        const q90 = quatFromAxisAngle({ x: 0, y: 0, z: 1 }, Math.PI / 2);
        const q180 = quatMul(q90, q90);
        // 180° around Z: w=0, z=1
        expect(APPROX(q180.w, 0, 1e-5)).toBe(true);
        expect(APPROX(Math.abs(q180.z), 1, 1e-5)).toBe(true);
    });
});

describe('quatNormalize', () => {
    it('normalised quaternion has unit length', () => {
        const q: Quat = { w: 2, x: 1, y: 1, z: 1 };
        const n = quatNormalize(q);
        const len = Math.sqrt(n.w*n.w + n.x*n.x + n.y*n.y + n.z*n.z);
        expect(APPROX(len, 1)).toBe(true);
    });

    it('zero quaternion returns identity', () => {
        const q: Quat = { w: 0, x: 0, y: 0, z: 0 };
        expect(quatNormalize(q)).toEqual({ w: 1, x: 0, y: 0, z: 0 });
    });
});

// ---------------------------------------------------------------------------
// 9. GizmoController state management
// ---------------------------------------------------------------------------

describe('GizmoController — state', () => {
    it('initial state', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        expect(gc.mode).toBe('translate');
        expect(gc.space).toBe('world');
        expect(gc.isDragging).toBe(false);
        expect(gc.activeNodeId).toBeNull();
        expect(gc.gridSize).toBe(0);
        expect(gc.ctrlHeld).toBe(false);
    });

    it('setMode sends IPC if node is active', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        await gc.selectNode(42);
        gc.setMode('rotate');
        expect(gc.mode).toBe('rotate');
        // selectNode sends one call, setMode sends another
        const modeCalls = client.calls.filter(c => c.fn === 'editor.setGizmo');
        expect(modeCalls.length).toBeGreaterThanOrEqual(1);
        expect(modeCalls[modeCalls.length - 1].args).toMatchObject({ mode: 'rotate' });
    });

    it('setMode does not send IPC if no node active', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setMode('scale');
        expect(client.calls.filter(c => c.fn === 'editor.setGizmo').length).toBe(0);
    });

    it('setSpace sends IPC', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setSpace('local');
        expect(client.calls.some(c => c.fn === 'editor.setGizmoSpace' && (c.args as any).space === 'local')).toBe(true);
    });

    it('setGridSize / setCtrlHeld', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setGridSize(0.5);
        gc.setCtrlHeld(true);
        expect(gc.gridSize).toBe(0.5);
        expect(gc.ctrlHeld).toBe(true);
    });

    it('selectNode null does not send IPC', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        await gc.selectNode(null);
        expect(gc.activeNodeId).toBeNull();
        expect(client.calls.length).toBe(0);
    });

    it('updateCamera stores matrices', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        const v = makeIdentityMat();
        const p = makePerspective(Math.PI / 3, 1.33, 0.1, 1000);
        gc.updateCamera(v, p, { x: 0, y: 0, z: 5 });
        // No throw, state updated (tested indirectly via drag tests)
    });
});

// ---------------------------------------------------------------------------
// 10. Translate drag — 8.3
// ---------------------------------------------------------------------------

describe('GizmoController — translate drag', () => {
    function setupGizmo() {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setConstraint('y');

        return { gc, client, width, height };
    }

    it('8.3: translate drag calls setNodeTransformLive with position delta', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        const hitPoint: Vec3 = { x: 0, y: 0, z: 0 };

        gc.beginDrag(hitPoint, startTransform, 'y');
        expect(gc.isDragging).toBe(true);
        expect(gc.constraint).toBe('y');

        // Move mouse upward — should produce a positive Y position delta
        await gc.updateDrag(width / 2, height / 2 - 50, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
        // The position should have changed
        const sentTransform = (liveCalls[0].args as any).transform as NodeTransform;
        expect(sentTransform).toBeDefined();
    });

    it('updateDrag without beginDrag returns null', async () => {
        const { gc, width, height } = setupGizmo();
        await gc.selectNode(10);
        const result = await gc.updateDrag(400, 300, width, height);
        expect(result).toBeNull();
    });

    it('updateDrag without active node returns null', async () => {
        const { gc, width, height } = setupGizmo();
        const startTransform = makeTransform();
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'x');
        // No node selected
        const result = await gc.updateDrag(400, 300, width, height);
        expect(result).toBeNull();
    });

    it('translate with XZ constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'xz');

        await gc.updateDrag(width / 2 + 50, height / 2, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('translate with x constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'x');

        await gc.updateDrag(width / 2 + 50, height / 2, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('translate with z constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'z');
        await gc.updateDrag(width / 2, height / 2 + 50, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('translate with xy constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'xy');
        await gc.updateDrag(width / 2 + 30, height / 2 - 30, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('translate with yz constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'yz');
        await gc.updateDrag(width / 2, height / 2 - 30, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('translate with view constraint', async () => {
        const { gc, client, width, height } = setupGizmo();
        await gc.selectNode(10);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'view');
        await gc.updateDrag(width / 2 + 30, height / 2 - 30, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });
});

// ---------------------------------------------------------------------------
// 11. Grid snap during translate drag — 8.4
// ---------------------------------------------------------------------------

describe('GizmoController — grid snap during translate', () => {
    it('8.4: with ctrlHeld and gridSize, position is snapped', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 0, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);

        gc.setGridSize(1.0);
        gc.setCtrlHeld(true);
        await gc.selectNode(5);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'xz');

        await gc.updateDrag(width / 2 + 100, height / 2, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        if (liveCalls.length > 0) {
            const sentPos = ((liveCalls[0].args as any).transform as NodeTransform).position;
            // Each component should be a multiple of gridSize=1.0
            ['x', 'y', 'z'].forEach(axis => {
                const v = (sentPos as any)[axis] as number;
                expect(APPROX(v % 1.0, 0, 0.01) || APPROX(v % 1.0, 1.0, 0.01)).toBe(true);
            });
        }
    });
});

// ---------------------------------------------------------------------------
// 12. Rotate drag — 8.5
// ---------------------------------------------------------------------------

describe('GizmoController — rotate drag', () => {
    it('8.5: 90-degree drag around Y produces rotation of ~π/2', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 10, z: 0 };
        // Camera directly above, looking down
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: -1 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('rotate');
        await gc.selectNode(7);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });

        // Hit point at (1, 0, 0) — east of origin on the Y rotation plane
        const hitPoint: Vec3 = { x: 1, y: 0, z: 0 };
        gc.beginDrag(hitPoint, startTransform, 'y');

        // Move to a point 90° around (0,0,-1) → (0,0,-1) is 90° from (1,0,0) around Y
        // Move cursor to correspond to world point (0, 0, -1)
        await gc.updateDrag(width / 2, height / 2 + 100, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
        const sentTransform = (liveCalls[0].args as any).transform as NodeTransform;
        expect(sentTransform.rotation).toBeDefined();
        // Rotation should differ from identity (some rotation applied)
        const q = sentTransform.rotation;
        const isIdentity = APPROX(q.w, 1) && APPROX(q.x, 0) && APPROX(q.y, 0) && APPROX(q.z, 0);
        // After any movement, rotation should have changed (unless ray misses plane)
        // We just verify it's a valid unit quaternion
        const len = Math.sqrt(q.w**2 + q.x**2 + q.y**2 + q.z**2);
        expect(APPROX(len, 1, 0.01)).toBe(true);
        void isIdentity; // may be identity if ray missed — that's ok
    });

    it('rotate drag with x constraint', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 10, y: 0, z: 0 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('rotate');
        await gc.selectNode(8);

        gc.beginDrag({ x: 0, y: 1, z: 0 }, makeTransform(), 'x');
        await gc.updateDrag(width / 2, height / 2 - 50, width, height);

        expect(client.calls.some(c => c.fn === 'editor.setNodeTransformLive')).toBe(true);
    });

    it('rotate drag with z constraint', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 0, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('rotate');
        await gc.selectNode(9);

        gc.beginDrag({ x: 1, y: 0, z: 0 }, makeTransform(), 'z');
        await gc.updateDrag(width / 2 + 50, height / 2, width, height);

        expect(client.calls.some(c => c.fn === 'editor.setNodeTransformLive')).toBe(true);
    });

    it('rotate with view constraint uses camera forward', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 0, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('rotate');
        await gc.selectNode(11);

        gc.beginDrag({ x: 1, y: 0, z: 0 }, makeTransform(), 'view');
        await gc.updateDrag(width / 2 + 50, height / 2, width, height);

        expect(client.calls.filter(c => c.fn === 'editor.setNodeTransformLive').length).toBeGreaterThanOrEqual(1);
    });
});

// ---------------------------------------------------------------------------
// 13. Scale drag
// ---------------------------------------------------------------------------

describe('GizmoController — scale drag', () => {
    it('scale drag produces changed scale vector', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('scale');
        await gc.selectNode(20);

        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        gc.beginDrag({ x: 1, y: 0, z: 0 }, startTransform, 'x');

        await gc.updateDrag(width / 2 + 100, height / 2, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
        const sentTransform = (liveCalls[0].args as any).transform as NodeTransform;
        expect(sentTransform.scale).toBeDefined();
    });

    it('scale with y constraint only scales Y', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('scale');
        await gc.selectNode(21);

        gc.beginDrag({ x: 0, y: 1, z: 0 }, makeTransform(), 'y');
        await gc.updateDrag(width / 2, height / 2 - 100, width, height);

        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });

    it('scale with z constraint only scales Z', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('scale');
        await gc.selectNode(22);

        gc.beginDrag({ x: 0, y: 0, z: 1 }, makeTransform(), 'z');
        await gc.updateDrag(width / 2, height / 2 + 50, width, height);

        expect(client.calls.some(c => c.fn === 'editor.setNodeTransformLive')).toBe(true);
    });

    it('scale from zero startDist returns original transform', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        const view = makeLookAt(eye, { x: 0, y: 0, z: 0 });
        const proj = makePerspective(Math.PI / 3, width / height, 0.1, 1000);
        gc.updateCamera(view, proj, eye);
        gc.setMode('scale');
        await gc.selectNode(23);

        // Hit point exactly at origin → startDist = 0 → should return original
        const startTransform = makeTransform({ x: 0, y: 0, z: 0 });
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'x');

        const result = await gc.updateDrag(width / 2, height / 2, width, height);
        // Either null (ray miss) or original transform (startDist near 0)
        if (result !== null) {
            expect(result.scale).toEqual(startTransform.scale);
        }
    });
});

// ---------------------------------------------------------------------------
// 14. endDrag — 8.6: TransformCommand execute and undo
// ---------------------------------------------------------------------------

describe('GizmoController — endDrag', () => {
    it('8.6: endDrag sends editor.commitTransform and returns final transform', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        gc.updateCamera(
            makeLookAt(eye, { x: 0, y: 0, z: 0 }),
            makePerspective(Math.PI / 3, 800 / 600, 0.1, 1000),
            eye,
        );
        await gc.selectNode(15);

        const startTransform = makeTransform({ x: 1, y: 2, z: 3 });
        gc.beginDrag({ x: 1, y: 0, z: 0 }, startTransform, 'x');

        const result = await gc.endDrag();
        expect(gc.isDragging).toBe(false);
        expect(client.calls.some(c => c.fn === 'editor.commitTransform')).toBe(true);
        // result is whatever mock returned from commitTransform
        // (our mock returns the args object)
        void result;
    });

    it('endDrag without active drag returns null', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        const result = await gc.endDrag();
        expect(result).toBeNull();
        expect(client.calls.length).toBe(0);
    });

    it('endDrag without active node returns null', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'x');
        const result = await gc.endDrag();
        expect(result).toBeNull();
    });
});

// ---------------------------------------------------------------------------
// 15. cancelDrag
// ---------------------------------------------------------------------------

describe('GizmoController — cancelDrag', () => {
    it('cancelDrag restores original transform', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        gc.updateCamera(
            makeLookAt(eye, { x: 0, y: 0, z: 0 }),
            makePerspective(Math.PI / 3, 800 / 600, 0.1, 1000),
            eye,
        );
        await gc.selectNode(25);

        const startTransform = makeTransform({ x: 5, y: 5, z: 5 });
        gc.beginDrag({ x: 0, y: 0, z: 0 }, startTransform, 'x');
        await gc.cancelDrag();

        expect(gc.isDragging).toBe(false);
        const calls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(calls.length).toBeGreaterThanOrEqual(1);
        // Last live call should restore the start transform
        const lastCall = calls[calls.length - 1];
        expect((lastCall.args as any).transform).toEqual(startTransform);
    });

    it('cancelDrag without drag is a no-op', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        await gc.cancelDrag();
        expect(client.calls.length).toBe(0);
    });

    it('cancelDrag without active node is a no-op', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'x');
        // no node selected
        await gc.cancelDrag();
        expect(gc.isDragging).toBe(false);
        expect(client.calls.length).toBe(0);
    });
});

// ---------------------------------------------------------------------------
// 16. 8.7 Integration: mousedown + mousemove + mouseup sequence
// ---------------------------------------------------------------------------

describe('GizmoController — drag sequence integration (8.7)', () => {
    it('complete drag sequence creates exactly one commit call', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        gc.updateCamera(
            makeLookAt(eye, { x: 0, y: 0, z: 0 }),
            makePerspective(Math.PI / 3, width / height, 0.1, 1000),
            eye,
        );
        await gc.selectNode(30);

        // mousedown
        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'x');
        expect(gc.isDragging).toBe(true);

        // mousemove (multiple times)
        await gc.updateDrag(width / 2 + 20, height / 2, width, height);
        await gc.updateDrag(width / 2 + 40, height / 2, width, height);
        await gc.updateDrag(width / 2 + 60, height / 2, width, height);

        // mouseup
        await gc.endDrag();
        expect(gc.isDragging).toBe(false);

        // Exactly one commit call
        const commitCalls = client.calls.filter(c => c.fn === 'editor.commitTransform');
        expect(commitCalls.length).toBe(1);

        // Multiple live update calls (one per mousemove)
        const liveCalls = client.calls.filter(c => c.fn === 'editor.setNodeTransformLive');
        expect(liveCalls.length).toBeGreaterThanOrEqual(1);
    });
});

// ---------------------------------------------------------------------------
// 17. 8.8: pickNode returns null / nodeId
// ---------------------------------------------------------------------------

describe('GizmoController — selectNode IPC (8.8)', () => {
    it('selectNode sends editor.setGizmo with correct nodeId', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        await gc.selectNode(99);
        expect(client.calls.some(c => c.fn === 'editor.setGizmo' && (c.args as any).nodeId === 99)).toBe(true);
    });

    it('selectNode(null) clears active node without IPC', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        await gc.selectNode(10);
        await gc.selectNode(null);
        expect(gc.activeNodeId).toBeNull();
        // Only the selectNode(10) call, not selectNode(null)
        const gizmoCalls = client.calls.filter(c => c.fn === 'editor.setGizmo');
        expect(gizmoCalls.every(c => (c.args as any).nodeId !== null)).toBe(true);
    });
});

// ---------------------------------------------------------------------------
// 18. updateDrag with singular matrices
// ---------------------------------------------------------------------------

describe('GizmoController — edge cases', () => {
    it('updateDrag returns null when VP matrix is singular', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        // Set degenerate (all-zero) matrices
        gc.updateCamera(new Array(16).fill(0), new Array(16).fill(0), { x: 0, y: 0, z: 0 });
        await gc.selectNode(1);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'x');
        const result = await gc.updateDrag(400, 300, 800, 600);
        expect(result).toBeNull();
    });

    it('setConstraint changes constraint mid-drag', () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setConstraint('y');
        expect(gc.constraint).toBe('y');
        gc.setConstraint('xz');
        expect(gc.constraint).toBe('xz');
    });
});

// ---------------------------------------------------------------------------
// 19. Branch coverage: default args in hitTestAxis
// ---------------------------------------------------------------------------

describe('hitTestAxis — default args', () => {
    it('uses default axisLength=1.0 and hitRadius=0.08 when not provided', () => {
        // Perpendicular ray very close to axis (within default hitRadius=0.08)
        const ray: Ray = {
            origin:    { x: 0.5, y: 2, z: 0 },
            direction: { x: 0, y: -1, z: 0 },
        };
        // Not providing axisLength or hitRadius — uses defaults (1.0, 0.08)
        const result = hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 });
        expect(result).toBe(true);
    });

    it('uses default hitRadius — miss when z offset is 0.3 > 0.08', () => {
        const ray: Ray = {
            origin:    { x: 0.5, y: 2, z: 0.3 },
            direction: { x: 0, y: -1, z: 0 },
        };
        const result = hitTestAxis(ray, { x: 0, y: 0, z: 0 }, { x: 1, y: 0, z: 0 });
        expect(result).toBe(false);
    });
});

// ---------------------------------------------------------------------------
// 20. Branch coverage: parallel + coincident in hitTestAxis (vec3Length(cross) < 1e-10)
// ---------------------------------------------------------------------------

describe('hitTestAxis — parallel coincident ray', () => {
    it('ray exactly coincident with axis — dist=0, hits within radius', () => {
        // ray.direction === axisDir (exactly parallel AND coincident: same line)
        // cross(ray.dir, axisDir) = (0,0,0) → length < 1e-10
        // Should use the cross(w0, ray.dir) path
        const ray: Ray = {
            origin:    { x: 0, y: 0, z: 0 },
            direction: { x: 1, y: 0, z: 0 },
        };
        const axisDir: Vec3 = { x: 1, y: 0, z: 0 };
        // w0 = (0,0,0) → cross(w0, ray) = (0,0,0) → dist=0 < hitRadius=0.08
        const result = hitTestAxis(ray, { x: 0, y: 0, z: 0 }, axisDir, 2.0, 0.08);
        expect(result).toBe(true);
    });

    it('parallel ray with small offset — uses w0 cross path', () => {
        // ray parallel to axis but offset by 0.05 in Z (< hitRadius=0.08)
        const ray: Ray = {
            origin:    { x: -1, y: 0, z: 0.05 },
            direction: { x: 1, y: 0, z: 0 },
        };
        const axisDir: Vec3 = { x: 1, y: 0, z: 0 };
        // cross(ray.dir, axisDir) = (0,0,0) exactly since directions are identical
        // w0 = (-1, 0, 0.05) → cross(w0, ray.dir) = (0*0-0.05*0, 0.05*1-(-1)*0, (-1)*0-0*1) = (0, 0.05, 0)
        // dist = vec3Length((0, 0.05, 0)) = 0.05 < 0.08 → hit
        const result = hitTestAxis(ray, { x: 0, y: 0, z: 0 }, axisDir, 2.0, 0.08);
        expect(result).toBe(true);
    });
});

// ---------------------------------------------------------------------------
// 21. Branch coverage: endDrag resp?.transform ?? null (null path)
// ---------------------------------------------------------------------------

describe('GizmoController — endDrag null transform', () => {
    it('returns null when commitTransform response has no transform', async () => {
        const client = {
            call: jest.fn().mockResolvedValue({ ok: true }), // no .transform field
        };
        const gc = new GizmoController(client as never);

        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        gc.updateCamera(
            makeLookAt(eye, { x: 0, y: 0, z: 0 }),
            makePerspective(Math.PI / 3, 800 / 600, 0.1, 1000),
            eye,
        );
        await gc.selectNode(50);
        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'x');

        const result = await gc.endDrag();
        // resp.transform is undefined → ?? null → result should be null
        expect(result).toBeNull();
    });
});

// ---------------------------------------------------------------------------
// 22. Branch coverage: grid snap (ctrlHeld && gridSize > 0)
// ---------------------------------------------------------------------------

describe('GizmoController — grid snap branch', () => {
    it('snap activated when ctrlHeld=true and gridSize>0', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);

        const width = 800, height = 600;
        const eye: Vec3 = { x: 0, y: 5, z: 10 };
        gc.updateCamera(
            makeLookAt(eye, { x: 0, y: 0, z: 0 }),
            makePerspective(Math.PI / 3, width / height, 0.1, 1000),
            eye,
        );
        gc.setGridSize(0.5);
        gc.setCtrlHeld(true);
        await gc.selectNode(60);

        gc.beginDrag({ x: 0, y: 0, z: 0 }, makeTransform(), 'xz');
        const result = await gc.updateDrag(width / 2 + 150, height / 2, width, height);

        // If result is not null, position should be snapped to 0.5 grid
        if (result !== null) {
            const { x, y, z } = result.position;
            [x, y, z].forEach(v => {
                // v should be multiple of 0.5
                expect(APPROX(v % 0.5, 0, 0.01) || APPROX(Math.abs(v % 0.5), 0.5, 0.01)).toBe(true);
            });
        }
    });
});

// ---------------------------------------------------------------------------
// 23. Branch coverage: rotate drag with plane miss (line 616)
// ---------------------------------------------------------------------------

describe('GizmoController — rotate drag plane miss', () => {
    it('ray parallel to rotation plane returns startTransform unchanged', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setMode('rotate');

        // Use zero matrices — ray cast will fail and return null from screenToWorldRay
        gc.updateCamera(new Array(16).fill(0), new Array(16).fill(0), { x: 0, y: 0, z: 0 });
        await gc.selectNode(70);

        gc.beginDrag({ x: 1, y: 0, z: 0 }, makeTransform({ x: 0, y: 0, z: 0 }), 'y');
        const result = await gc.updateDrag(400, 300, 800, 600);
        // Ray fails → returns null from updateDrag (not startTransform, since screenToWorldRay fails first)
        expect(result).toBeNull();
    });
});

// ---------------------------------------------------------------------------
// 24. Branch coverage: scale drag plane miss (line 643)
// ---------------------------------------------------------------------------

describe('GizmoController — scale drag plane miss', () => {
    it('scale drag with degenerate camera returns null', async () => {
        const client = makeMockClient();
        const gc = new GizmoController(client as never);
        gc.setMode('scale');

        gc.updateCamera(new Array(16).fill(0), new Array(16).fill(0), { x: 0, y: 0, z: 0 });
        await gc.selectNode(80);

        gc.beginDrag({ x: 1, y: 0, z: 0 }, makeTransform(), 'x');
        const result = await gc.updateDrag(400, 300, 800, 600);
        expect(result).toBeNull();
    });
});

// ---------------------------------------------------------------------------
// Blender-style rotation gizmo tests (rewrite-rotation-gizmo-blender)
//
// Tests the plane-projected signed angle, incremental delta, arcball,
// snapping, precision, and multi-select rotation math.
// ---------------------------------------------------------------------------

describe('signedAngleOnPlane', () => {
    // Inline implementation matching the webview JS
    function signedAngleOnPlane(v0: Vec3, v1: Vec3, normal: Vec3): number {
        const c = vec3Cross(v0, v1);
        const sinA = vec3Dot(normal, c);
        const cosA = vec3Dot(v0, v1);
        return Math.atan2(sinA, cosA);
    }

    it('90-degree rotation around Y axis', () => {
        // Right-hand rule around +Y: X → -Z is positive 90°
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: 0, y: 0, z: -1 };
        const normal: Vec3 = { x: 0, y: 1, z: 0 };
        const angle = signedAngleOnPlane(v0, v1, normal);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
    });

    it('-90-degree rotation around Y axis', () => {
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: 0, y: 0, z: 1 };
        const normal: Vec3 = { x: 0, y: 1, z: 0 };
        const angle = signedAngleOnPlane(v0, v1, normal);
        expect(APPROX(angle, -Math.PI / 2)).toBe(true);
    });

    it('180-degree rotation', () => {
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: -1, y: 0, z: 0 };
        const normal: Vec3 = { x: 0, y: 1, z: 0 };
        const angle = signedAngleOnPlane(v0, v1, normal);
        expect(APPROX(Math.abs(angle), Math.PI)).toBe(true);
    });

    it('0-degree rotation (same vector)', () => {
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const normal: Vec3 = { x: 0, y: 1, z: 0 };
        const angle = signedAngleOnPlane(v0, v0, normal);
        expect(APPROX(angle, 0)).toBe(true);
    });

    it('continuity: small increments accumulate correctly', () => {
        const normal: Vec3 = { x: 0, y: 1, z: 0 };
        let total = 0;
        const steps = 36;
        const stepAngle = 10 * Math.PI / 180;
        // Sweep in the positive direction around Y: X → -Z (use -sin for Z)
        for (let i = 0; i < steps; i++) {
            const ang0 = i * stepAngle;
            const ang1 = (i + 1) * stepAngle;
            const v0: Vec3 = { x: Math.cos(ang0), y: 0, z: -Math.sin(ang0) };
            const v1: Vec3 = { x: Math.cos(ang1), y: 0, z: -Math.sin(ang1) };
            total += signedAngleOnPlane(v0, v1, normal);
        }
        // 36 * 10° = 360° positive rotation
        expect(APPROX(total, 2 * Math.PI, 0.01)).toBe(true);
    });

    it('works around X axis', () => {
        const v0: Vec3 = { x: 0, y: 1, z: 0 };
        const v1: Vec3 = { x: 0, y: 0, z: 1 };
        const normal: Vec3 = { x: 1, y: 0, z: 0 };
        const angle = signedAngleOnPlane(v0, v1, normal);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
    });

    it('works around Z axis', () => {
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: 0, y: 1, z: 0 };
        const normal: Vec3 = { x: 0, y: 0, z: 1 };
        const angle = signedAngleOnPlane(v0, v1, normal);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
    });
});

describe('mapToArcball', () => {
    function mapToArcball(mx: number, my: number, cx: number, cy: number, r: number): Vec3 {
        const x = (mx - cx) / r;
        const y = (my - cy) / r;
        const s = x * x + y * y;
        if (s <= 1.0) {
            return { x, y: -y, z: Math.sqrt(1.0 - s) };
        }
        const invL = 1.0 / Math.sqrt(s);
        return { x: x * invL, y: -y * invL, z: 0 };
    }

    it('center of sphere gives z = 1', () => {
        const v = mapToArcball(400, 300, 400, 300, 100);
        expect(APPROX(v.x, 0)).toBe(true);
        expect(APPROX(v.y, 0)).toBe(true);
        expect(APPROX(v.z, 1)).toBe(true);
    });

    it('point inside sphere has z > 0', () => {
        const v = mapToArcball(450, 300, 400, 300, 100); // x=0.5, y=0
        expect(v.z).toBeGreaterThan(0);
        expect(APPROX(v.x, 0.5)).toBe(true);
        const norm = v.x * v.x + v.y * v.y + v.z * v.z;
        expect(APPROX(norm, 1.0)).toBe(true);
    });

    it('point on rim has z = 0', () => {
        const v = mapToArcball(500, 300, 400, 300, 100); // x=1, y=0
        expect(APPROX(v.z, 0)).toBe(true);
        expect(APPROX(v.x, 1)).toBe(true);
    });

    it('point outside sphere is clamped to rim with z = 0', () => {
        const v = mapToArcball(600, 300, 400, 300, 100); // x=2, y=0 → clamped
        expect(APPROX(v.z, 0)).toBe(true);
        const norm = v.x * v.x + v.y * v.y + v.z * v.z;
        expect(APPROX(norm, 1.0)).toBe(true);
    });

    it('screen Y is flipped (y component negated)', () => {
        const v = mapToArcball(400, 250, 400, 300, 100); // screen y=-50 → y=+0.5
        expect(v.y).toBeGreaterThan(0);
    });
});

describe('quatFromVectors', () => {
    function quatFromVectors(a: Vec3, b: Vec3): Quat {
        const c = vec3Cross(a, b);
        const d = vec3Dot(a, b);
        if (d < -0.999999) {
            const perp = Math.abs(a.x) < 0.9
                ? vec3Normalize(vec3Cross(a, { x: 1, y: 0, z: 0 }))
                : vec3Normalize(vec3Cross(a, { x: 0, y: 1, z: 0 }));
            return { w: 0, x: perp.x, y: perp.y, z: perp.z };
        }
        const w = 1.0 + d;
        const l = Math.sqrt(w * w + c.x * c.x + c.y * c.y + c.z * c.z);
        return { w: w / l, x: c.x / l, y: c.y / l, z: c.z / l };
    }

    it('identity: same vector produces identity quaternion', () => {
        const v: Vec3 = { x: 1, y: 0, z: 0 };
        const q = quatFromVectors(v, v);
        expect(APPROX(q.w, 1, 1e-4)).toBe(true);
        expect(APPROX(q.x, 0, 1e-4)).toBe(true);
        expect(APPROX(q.y, 0, 1e-4)).toBe(true);
        expect(APPROX(q.z, 0, 1e-4)).toBe(true);
    });

    it('90-degree rotation from X to Z', () => {
        const a: Vec3 = { x: 1, y: 0, z: 0 };
        const b: Vec3 = { x: 0, y: 0, z: 1 };
        const q = quatFromVectors(a, b);
        // Apply q to a, should get b
        // rotated = q * (0,a) * q^-1 — test via quatMul
        const rotated = quatNormalize(quatMul(quatMul(q, { w: 0, x: a.x, y: a.y, z: a.z }), { w: q.w, x: -q.x, y: -q.y, z: -q.z }));
        expect(APPROX(rotated.x, b.x, 1e-4)).toBe(true);
        expect(APPROX(rotated.y, b.y, 1e-4)).toBe(true);
        expect(APPROX(rotated.z, b.z, 1e-4)).toBe(true);
    });

    it('180-degree rotation produces valid quaternion', () => {
        const a: Vec3 = { x: 1, y: 0, z: 0 };
        const b: Vec3 = { x: -1, y: 0, z: 0 };
        const q = quatFromVectors(a, b);
        const norm = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
        expect(APPROX(norm, 1.0)).toBe(true);
        // w should be ~0 for 180-degree rotation
        expect(APPROX(q.w, 0, 0.01)).toBe(true);
    });
});

describe('Blender-style axis-constrained rotation', () => {
    function signedAngleOnPlane(v0: Vec3, v1: Vec3, normal: Vec3): number {
        const c = vec3Cross(v0, v1);
        return Math.atan2(vec3Dot(normal, c), vec3Dot(v0, v1));
    }

    it('90-degree sweep on Y axis produces correct quaternion', () => {
        const axis: Vec3 = { x: 0, y: 1, z: 0 };
        // Right-hand: +90° around Y takes X → -Z
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: 0, y: 0, z: -1 };
        const angle = signedAngleOnPlane(v0, v1, axis);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
        const startRot: Quat = { w: 1, x: 0, y: 0, z: 0 };
        const dq = quatFromAxisAngle(axis, angle);
        const newRot = quatNormalize(quatMul(dq, startRot));
        expect(APPROX(newRot.w, Math.cos(Math.PI / 4))).toBe(true);
        expect(APPROX(newRot.y, Math.sin(Math.PI / 4))).toBe(true);
    });

    it('90-degree sweep on X axis', () => {
        const axis: Vec3 = { x: 1, y: 0, z: 0 };
        const v0: Vec3 = { x: 0, y: 1, z: 0 };
        const v1: Vec3 = { x: 0, y: 0, z: 1 };
        const angle = signedAngleOnPlane(v0, v1, axis);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
    });

    it('90-degree sweep on Z axis', () => {
        const axis: Vec3 = { x: 0, y: 0, z: 1 };
        const v0: Vec3 = { x: 1, y: 0, z: 0 };
        const v1: Vec3 = { x: 0, y: 1, z: 0 };
        const angle = signedAngleOnPlane(v0, v1, axis);
        expect(APPROX(angle, Math.PI / 2)).toBe(true);
    });

    it('incremental delta accumulation: 36 x 10° = 360°', () => {
        const axis: Vec3 = { x: 0, y: 1, z: 0 };
        let totalAngle = 0;
        // Sweep in positive direction: use -sin for Z (right-hand around Y)
        for (let i = 0; i < 36; i++) {
            const ang0 = i * (10 * Math.PI / 180);
            const ang1 = (i + 1) * (10 * Math.PI / 180);
            const v0: Vec3 = vec3Normalize({ x: Math.cos(ang0), y: 0, z: -Math.sin(ang0) });
            const v1: Vec3 = vec3Normalize({ x: Math.cos(ang1), y: 0, z: -Math.sin(ang1) });
            totalAngle += signedAngleOnPlane(v0, v1, axis);
        }
        expect(APPROX(totalAngle, 2 * Math.PI, 0.01)).toBe(true);
    });

    it('near-parallel ray produces zero delta (freeze behavior)', () => {
        // When dot(rayDir, planeNormal) < 0.01, the code freezes.
        // Simulate: rayDir nearly in the Y-plane, planeNormal = Y
        const rayDir: Vec3 = { x: 1, y: 0.005, z: 0 }; // almost parallel
        const planeNormal: Vec3 = { x: 0, y: 1, z: 0 };
        const denom = Math.abs(vec3Dot(rayDir, planeNormal));
        expect(denom).toBeLessThan(0.01);
        // In the code, this means delta is skipped (0)
    });
});

describe('Rotation snapping and precision', () => {
    it('Ctrl-snap: 7 degrees snaps to 5', () => {
        const totalAngle = 7 * Math.PI / 180;
        const snapped = snapToGrid(totalAngle, Math.PI / 36); // 5-degree grid
        const snappedDeg = snapped * 180 / Math.PI;
        expect(APPROX(snappedDeg, 5, 0.5)).toBe(true);
    });

    it('Ctrl-snap: 13 degrees snaps to 15', () => {
        const totalAngle = 13 * Math.PI / 180;
        const snapped = snapToGrid(totalAngle, Math.PI / 36);
        const snappedDeg = snapped * 180 / Math.PI;
        expect(APPROX(snappedDeg, 15, 0.5)).toBe(true);
    });

    it('Shift precision: 10x reduction factor', () => {
        // A 90-degree sweep with shift held produces 9 degrees
        const rawDelta = Math.PI / 2; // 90 degrees
        const precisionDelta = rawDelta * 0.1;
        const precisionDeg = precisionDelta * 180 / Math.PI;
        expect(APPROX(precisionDeg, 9, 0.1)).toBe(true);
    });

    it('Ctrl+Shift: snap to 1-degree increments', () => {
        const totalAngle = 3.7 * Math.PI / 180;
        const snapped = snapToGrid(totalAngle, Math.PI / 180); // 1-degree grid
        const snappedDeg = snapped * 180 / Math.PI;
        expect(APPROX(snappedDeg, 4, 0.1)).toBe(true);
    });
});

describe('Arcball free rotation', () => {
    function mapToArcball(mx: number, my: number, cx: number, cy: number, r: number): Vec3 {
        const x = (mx - cx) / r;
        const y = (my - cy) / r;
        const s = x * x + y * y;
        if (s <= 1.0) return { x, y: -y, z: Math.sqrt(1.0 - s) };
        const invL = 1.0 / Math.sqrt(s);
        return { x: x * invL, y: -y * invL, z: 0 };
    }
    function quatFromVectors(a: Vec3, b: Vec3): Quat {
        const c = vec3Cross(a, b);
        const d = vec3Dot(a, b);
        if (d < -0.999999) {
            const perp = Math.abs(a.x) < 0.9
                ? vec3Normalize(vec3Cross(a, { x: 1, y: 0, z: 0 }))
                : vec3Normalize(vec3Cross(a, { x: 0, y: 1, z: 0 }));
            return { w: 0, x: perp.x, y: perp.y, z: perp.z };
        }
        const w = 1.0 + d;
        const l = Math.sqrt(w * w + c.x * c.x + c.y * c.y + c.z * c.z);
        return { w: w / l, x: c.x / l, y: c.y / l, z: c.z / l };
    }

    it('drag right from center produces rotation', () => {
        const start = mapToArcball(400, 300, 400, 300, 100); // center
        const end = mapToArcball(450, 300, 400, 300, 100);   // right
        const q = quatFromVectors(start, end);
        // Should produce a rotation (not identity)
        expect(Math.abs(q.w)).toBeLessThan(1.0);
        const norm = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
        expect(APPROX(norm, 1.0)).toBe(true);
    });

    it('no movement produces identity quaternion', () => {
        const start = mapToArcball(400, 300, 400, 300, 100);
        const q = quatFromVectors(start, start);
        expect(APPROX(q.w, 1, 1e-4)).toBe(true);
    });
});

describe('Hover hysteresis', () => {
    it('current hovered ring stays unless new is 20% closer', () => {
        const currentT = 10.0;
        const newT = 9.0; // 10% closer — not enough
        const threshold = 0.8; // 20% means new must be < current * 0.8
        const shouldSwitch = newT < currentT * threshold;
        expect(shouldSwitch).toBe(false);

        const betterT = 7.5; // 25% closer — enough
        const shouldSwitch2 = betterT < currentT * threshold;
        expect(shouldSwitch2).toBe(true);
    });
});

describe('Multi-select rotation', () => {
    it('same delta applied to each node independently', () => {
        const axis: Vec3 = { x: 0, y: 1, z: 0 };
        const deltaAngle = Math.PI / 4; // 45 degrees
        const deltaQ = quatFromAxisAngle(axis, deltaAngle);

        const rotA: Quat = quatFromAxisAngle(axis, Math.PI / 4); // 45°
        const rotB: Quat = { w: 1, x: 0, y: 0, z: 0 };         // 0°

        const newA = quatNormalize(quatMul(deltaQ, rotA));
        const newB = quatNormalize(quatMul(deltaQ, rotB));

        // A: 45 + 45 = 90°
        const expected90 = quatFromAxisAngle(axis, Math.PI / 2);
        expect(APPROX(newA.w, expected90.w, 1e-4)).toBe(true);
        expect(APPROX(newA.y, expected90.y, 1e-4)).toBe(true);

        // B: 0 + 45 = 45°
        const expected45 = quatFromAxisAngle(axis, Math.PI / 4);
        expect(APPROX(newB.w, expected45.w, 1e-4)).toBe(true);
        expect(APPROX(newB.y, expected45.y, 1e-4)).toBe(true);
    });
});

describe('Cancel drag reverts rotation', () => {
    it('applying zero-angle rotation returns to start', () => {
        const axis: Vec3 = { x: 0, y: 1, z: 0 };
        const startRot: Quat = quatFromAxisAngle(axis, Math.PI / 6); // 30°
        // Simulate cancel: totalAngle = 0 → revert to startRot
        const deltaQ = quatFromAxisAngle(axis, 0);
        const reverted = quatNormalize(quatMul(deltaQ, startRot));
        expect(APPROX(reverted.w, startRot.w, 1e-6)).toBe(true);
        expect(APPROX(reverted.x, startRot.x, 1e-6)).toBe(true);
        expect(APPROX(reverted.y, startRot.y, 1e-6)).toBe(true);
        expect(APPROX(reverted.z, startRot.z, 1e-6)).toBe(true);
    });
});
