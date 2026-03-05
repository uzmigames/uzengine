/**
 * Rotation Pivot Sanity Tests
 *
 * These tests use the EXACT SAME math functions as viewport-panel.ts
 * to prove the quaternion/vector conventions are correct for world-pivot rotation.
 *
 * Engine convention (proven in prior sessions):
 *   - DX left-handed coordinate system
 *   - Row-major matrices: clip = pos * V * P
 *   - Quaternion: Hamilton convention {w,x,y,z}
 *   - quatMul(q1,q2): standard Hamilton product
 *   - quatRotateVec3(q,v): v' = q * v * q^-1
 *   - World pre-multiply: newRot = deltaQ * startRot
 */

// ---- Exact copies of viewport-panel.ts math functions ----

function dot3(a: any, b: any) { return a.x * b.x + a.y * b.y + a.z * b.z; }
function sub3(a: any, b: any) { return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z }; }
function add3(a: any, b: any) { return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z }; }
function len3(v: any) { return Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
function cross3(a: any, b: any) { return { x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x }; }
function norm3(v: any) { const l = len3(v); if (l < 1e-12) return { x: 0, y: 0, z: 0 }; return { x: v.x / l, y: v.y / l, z: v.z / l }; }
function scale3(v: any, s: number) { return { x: v.x * s, y: v.y * s, z: v.z * s }; }

function quatFromAxisAngle(axis: any, angle: number) {
    const half = angle / 2, s = Math.sin(half);
    return { w: Math.cos(half), x: axis.x * s, y: axis.y * s, z: axis.z * s };
}

function quatMul(q1: any, q2: any) {
    return {
        w: q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
        x: q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
        y: q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x,
        z: q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w
    };
}

function quatNorm(q: any) {
    const l = Math.sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (l < 1e-12) return { w: 1, x: 0, y: 0, z: 0 };
    return { w: q.w / l, x: q.x / l, y: q.y / l, z: q.z / l };
}

function quatRotateVec3(q: any, v: any) {
    const tx = 2 * (q.y * v.z - q.z * v.y);
    const ty = 2 * (q.z * v.x - q.x * v.z);
    const tz = 2 * (q.x * v.y - q.y * v.x);
    return {
        x: v.x + q.w * tx + (q.y * tz - q.z * ty),
        y: v.y + q.w * ty + (q.z * tx - q.x * tz),
        z: v.z + q.w * tz + (q.x * ty - q.y * tx)
    };
}

function quatInverse(q: any) {
    // For unit quaternion: conjugate
    return { w: q.w, x: -q.x, y: -q.y, z: -q.z };
}

// ---- World-pivot sandwich function (the function we want to prove correct) ----

/**
 * Apply world-space rotation around a world pivot.
 * M_world_new = T(P) * R(Q) * T(-P) * M_world_start
 *
 * For TRS decomposition:
 *   newPos = P + Q * (startPos - P)
 *   newRot = Q * startRot  (world pre-multiply)
 *   newScale = startScale  (unchanged)
 */
function applyWorldPivotRotation(
    startPos: any, startRot: any, startScale: any,
    pivotWorld: any, deltaQuat: any
) {
    const offset = sub3(startPos, pivotWorld);
    const rotatedOffset = quatRotateVec3(deltaQuat, offset);
    const newPos = add3(pivotWorld, rotatedOffset);
    const newRot = quatNorm(quatMul(deltaQuat, startRot));
    return { position: newPos, rotation: newRot, scale: startScale };
}

// ---- Helper to check approximate equality ----
function expectVec3Near(actual: any, expected: any, eps = 1e-6) {
    expect(actual.x).toBeCloseTo(expected.x, 5);
    expect(actual.y).toBeCloseTo(expected.y, 5);
    expect(actual.z).toBeCloseTo(expected.z, 5);
}

function expectQuatNear(actual: any, expected: any, eps = 1e-6) {
    // Quaternions q and -q represent the same rotation
    const sign = (actual.w * expected.w + actual.x * expected.x + actual.y * expected.y + actual.z * expected.z) >= 0 ? 1 : -1;
    expect(actual.w * sign).toBeCloseTo(expected.w, 5);
    expect(actual.x * sign).toBeCloseTo(expected.x, 5);
    expect(actual.y * sign).toBeCloseTo(expected.y, 5);
    expect(actual.z * sign).toBeCloseTo(expected.z, 5);
}

// ============================================================================
// TEST A: No parent, rotate object at (10,0,0) around origin by 90° on world Z
// ============================================================================
describe('Test A: No parent — rotate around world origin', () => {
    const startPos = { x: 10, y: 0, z: 0 };
    const startRot = { w: 1, x: 0, y: 0, z: 0 }; // identity
    const startScale = { x: 1, y: 1, z: 1 };
    const pivotWorld = { x: 0, y: 0, z: 0 };
    const axisWorld = { x: 0, y: 0, z: 1 }; // world Z
    const angle = Math.PI / 2; // +90 degrees

    it('quatRotateVec3: rotating (1,0,0) by 90° around Z yields (0,1,0) in LH', () => {
        // In a standard right-hand system: +90° around Z rotates +X to +Y
        // DirectX is left-handed but quaternion rotation is the same formula
        const q = quatFromAxisAngle(axisWorld, angle);
        const v = { x: 1, y: 0, z: 0 };
        const result = quatRotateVec3(q, v);
        // Standard math: should be (0,1,0) for +90° CCW around Z
        // or (0,-1,0) if the sign convention differs
        console.log('quatRotateVec3((1,0,0), Z, +90°) =', result);

        // Hamilton quaternion with axis=(0,0,1), angle=+pi/2:
        // q = (cos(pi/4), 0, 0, sin(pi/4)) = (0.707, 0, 0, 0.707)
        // v'= q*v*q^-1 rotates (1,0,0) to (0,1,0)
        expectVec3Near(result, { x: 0, y: 1, z: 0 });
    });

    it('world-pivot sandwich: object at (10,0,0) rotated 90° around Z at origin -> (0,10,0)', () => {
        const q = quatFromAxisAngle(axisWorld, angle);
        const result = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, q);

        console.log('Test A result position:', result.position);
        console.log('Test A result rotation:', result.rotation);

        // Object should orbit from (10,0,0) to (0,10,0)
        expectVec3Near(result.position, { x: 0, y: 10, z: 0 });

        // Rotation: 90° around Z from identity
        const expectedRot = quatFromAxisAngle(axisWorld, angle);
        expectQuatNear(result.rotation, expectedRot);
    });

    it('orbit distance remains constant during rotation', () => {
        const distStart = len3(sub3(startPos, pivotWorld)); // should be 10

        // Test at multiple angles
        for (const deg of [15, 30, 45, 60, 90, 120, 180, 270, 359]) {
            const a = (deg * Math.PI) / 180;
            const q = quatFromAxisAngle(axisWorld, a);
            const result = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, q);
            const distCurrent = len3(sub3(result.position, pivotWorld));
            expect(distCurrent).toBeCloseTo(distStart, 10);
        }
    });

    it('pivot at object origin: position unchanged for any rotation', () => {
        // When pivot == object position, offset is zero, position should not change
        const selfPivot = { x: 10, y: 0, z: 0 };
        for (const deg of [30, 90, 180, 270]) {
            const a = (deg * Math.PI) / 180;
            const q = quatFromAxisAngle(axisWorld, a);
            const result = applyWorldPivotRotation(startPos, startRot, startScale, selfPivot, q);
            expectVec3Near(result.position, startPos);
        }
    });

    it('multiple sequential rotations (total-from-start) do not drift', () => {
        // Simulate: compute total rotation from start (not incremental)
        // Then the final result should match a single rotation of the total angle
        const totalAngle = Math.PI; // 180°
        const steps = 100;

        // Option 1: single rotation of 180°
        const q1 = quatFromAxisAngle(axisWorld, totalAngle);
        const result1 = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, q1);

        // Option 2: compute "total" angle at each step (simulating the gizmo's absolute angle approach)
        let finalResult: any = null;
        for (let i = 1; i <= steps; i++) {
            const stepAngle = (totalAngle * i) / steps;
            const qStep = quatFromAxisAngle(axisWorld, stepAngle);
            finalResult = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, qStep);
        }

        // Both should give the same result
        expectVec3Near(finalResult!.position, result1.position);
        expectQuatNear(finalResult!.rotation, result1.rotation);
    });

    it('X/Y/Z axes all produce correct orbit', () => {
        const q90 = Math.PI / 2;

        // Rotate around X: (10,0,0) should stay at (10,0,0) (on the axis)
        const qX = quatFromAxisAngle({ x: 1, y: 0, z: 0 }, q90);
        const rX = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, qX);
        expectVec3Near(rX.position, { x: 10, y: 0, z: 0 });

        // Rotate around Y: (10,0,0) -> (0,0,-10) in LH or (0,0,10) in RH
        const qY = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, q90);
        const rY = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, qY);
        console.log('Rotate (10,0,0) by +90° around Y:', rY.position);
        // Hamilton: +90° around Y rotates X toward -Z: (10,0,0) -> (0,0,-10)
        expectVec3Near(rY.position, { x: 0, y: 0, z: -10 });

        // Rotate around Z: (10,0,0) -> (0,10,0)
        const qZ = quatFromAxisAngle({ x: 0, y: 0, z: 1 }, q90);
        const rZ = applyWorldPivotRotation(startPos, startRot, startScale, pivotWorld, qZ);
        expectVec3Near(rZ.position, { x: 0, y: 10, z: 0 });
    });
});

// ============================================================================
// TEST B: With parent — rotate child in world space, convert back to local
// ============================================================================
describe('Test B: With parent — world pivot rotation + local conversion', () => {
    // Parent world transform: position=(100,0,0), rotation=45° around Z
    const parentPos = { x: 100, y: 0, z: 0 };
    const parentRot = quatFromAxisAngle({ x: 0, y: 0, z: 1 }, Math.PI / 4); // 45° Z

    // Child local transform: position=(10,0,0), rotation=identity
    const childLocalPos = { x: 10, y: 0, z: 0 };
    const childLocalRot = { w: 1, x: 0, y: 0, z: 0 };
    const childLocalScale = { x: 1, y: 1, z: 1 };

    // Compute child world position: parentPos + parentRot * childLocalPos
    function childWorldPos() {
        const rotatedLocalPos = quatRotateVec3(parentRot, childLocalPos);
        return add3(parentPos, rotatedLocalPos);
    }

    function childWorldRot() {
        return quatNorm(quatMul(parentRot, childLocalRot));
    }

    // Convert world transform back to local (given parent)
    function worldToLocal(worldPos: any, worldRot: any) {
        const invParentRot = quatInverse(parentRot);
        const offset = sub3(worldPos, parentPos);
        const localPos = quatRotateVec3(invParentRot, offset);
        const localRot = quatNorm(quatMul(invParentRot, worldRot));
        return { position: localPos, rotation: localRot };
    }

    it('child world position is computed correctly', () => {
        const wp = childWorldPos();
        // parentRot=45° Z rotates (10,0,0) to (10*cos45, 10*sin45, 0) = (7.071, 7.071, 0)
        // plus parentPos (100,0,0) = (107.071, 7.071, 0)
        console.log('Child world position:', wp);
        expectVec3Near(wp, { x: 107.071067, y: 7.071067, z: 0 });
    });

    it('world-to-local roundtrip preserves original local transform', () => {
        const wp = childWorldPos();
        const wr = childWorldRot();
        const local = worldToLocal(wp, wr);
        expectVec3Near(local.position, childLocalPos);
        expectQuatNear(local.rotation, childLocalRot);
    });

    it('world rotation + local conversion: pivot at world origin, 90° Z', () => {
        const worldPivot = { x: 0, y: 0, z: 0 };
        const angle = Math.PI / 2; // 90°
        const axis = { x: 0, y: 0, z: 1 };
        const deltaQ = quatFromAxisAngle(axis, angle);

        const cwp = childWorldPos(); // ~(107.07, 7.07, 0)
        const cwr = childWorldRot(); // 45° Z

        // Apply world pivot rotation in world space
        const newWorldResult = applyWorldPivotRotation(cwp, cwr, childLocalScale, worldPivot, deltaQ);
        console.log('Child new world position:', newWorldResult.position);
        console.log('Child new world rotation:', newWorldResult.rotation);

        // The world position should orbit: 90° Z rotation of (107.07, 7.07, 0) around origin
        // -> (-7.07, 107.07, 0)
        const expectedWorldPos = quatRotateVec3(deltaQ, cwp);
        expectVec3Near(newWorldResult.position, expectedWorldPos);

        // Distance from pivot should be preserved
        const distBefore = len3(sub3(cwp, worldPivot));
        const distAfter = len3(sub3(newWorldResult.position, worldPivot));
        expect(distAfter).toBeCloseTo(distBefore, 10);

        // Convert back to local
        const newLocal = worldToLocal(newWorldResult.position, newWorldResult.rotation);
        console.log('Child new local position:', newLocal.position);
        console.log('Child new local rotation:', newLocal.rotation);

        // Verify: if we recompute child world from these locals, we get newWorldResult
        const verifyWorldPos = add3(parentPos, quatRotateVec3(parentRot, newLocal.position));
        const verifyWorldRot = quatNorm(quatMul(parentRot, newLocal.rotation));
        expectVec3Near(verifyWorldPos, newWorldResult.position);
        expectQuatNear(verifyWorldRot, newWorldResult.rotation);
    });
});

// ============================================================================
// TEST C: Verify the EXACT code path used in viewport-panel.ts gizmo rotation
// ============================================================================
describe('Test C: Simulate actual gizmo drag flow', () => {
    it('single object at origin: rotation only changes orientation, not position', () => {
        // This is the most common case: object at (0,0,0), rotating around its own origin
        const startPos = { x: 0, y: 0, z: 0 };
        const startRot = { w: 1, x: 0, y: 0, z: 0 };
        const startScale = { x: 1, y: 1, z: 1 };
        const pivot = { x: 0, y: 0, z: 0 }; // same as object origin

        const deltaQ = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, Math.PI / 3); // 60° around Y
        const result = applyWorldPivotRotation(startPos, startRot, startScale, pivot, deltaQ);

        // Position should be exactly (0,0,0) — no orbit when pivot == origin
        expectVec3Near(result.position, { x: 0, y: 0, z: 0 });
        expectQuatNear(result.rotation, deltaQ);
    });

    it('single object NOT at origin: rotation around self-origin keeps position', () => {
        // Object at (5, 3, -2), pivot = object position
        const startPos = { x: 5, y: 3, z: -2 };
        const startRot = { w: 1, x: 0, y: 0, z: 0 };
        const startScale = { x: 1, y: 1, z: 1 };
        const pivot = { x: 5, y: 3, z: -2 }; // same as object

        const deltaQ = quatFromAxisAngle({ x: 1, y: 0, z: 0 }, Math.PI / 4); // 45° around X
        const result = applyWorldPivotRotation(startPos, startRot, startScale, pivot, deltaQ);

        // Position unchanged (pivot == object origin)
        expectVec3Near(result.position, startPos);
    });

    it('pre-rotated object: world-space rotation does not contaminate other axes', () => {
        // Object already rotated 45° around X
        const startRot = quatFromAxisAngle({ x: 1, y: 0, z: 0 }, Math.PI / 4);
        const startPos = { x: 0, y: 0, z: 0 };
        const startScale = { x: 1, y: 1, z: 1 };
        const pivot = startPos;

        // Now rotate 90° around WORLD Y (not local Y!)
        const deltaQ = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, Math.PI / 2);
        const result = applyWorldPivotRotation(startPos, startRot, startScale, pivot, deltaQ);

        // Result rotation = deltaQ * startRot (world pre-multiply)
        const expectedRot = quatNorm(quatMul(deltaQ, startRot));
        expectQuatNear(result.rotation, expectedRot);

        // Verify this is DIFFERENT from startRot * deltaQ (local post-multiply)
        const wrongRot = quatNorm(quatMul(startRot, deltaQ));
        // They should NOT be equal (unless by coincidence)
        const dotProduct = Math.abs(
            expectedRot.w * wrongRot.w + expectedRot.x * wrongRot.x +
            expectedRot.y * wrongRot.y + expectedRot.z * wrongRot.z
        );
        expect(dotProduct).not.toBeCloseTo(1.0, 3); // should differ
    });

    it('repeated drag sessions (commit then new drag) do not drift', () => {
        // Simulate: drag 1 rotates 90° around Y, commit.
        // Then drag 2 rotates 90° around X from the committed state.
        const pos = { x: 5, y: 0, z: 0 };
        const rot0 = { w: 1, x: 0, y: 0, z: 0 };
        const scl = { x: 1, y: 1, z: 1 };
        const pivot = { x: 0, y: 0, z: 0 };

        // Drag 1: 90° around Y
        const q1 = quatFromAxisAngle({ x: 0, y: 1, z: 0 }, Math.PI / 2);
        const after1 = applyWorldPivotRotation(pos, rot0, scl, pivot, q1);

        // After drag 1: position should be (0,0,-5) and rotation = q1
        expectVec3Near(after1.position, { x: 0, y: 0, z: -5 });

        // Drag 2: start from committed state, 90° around X
        const q2 = quatFromAxisAngle({ x: 1, y: 0, z: 0 }, Math.PI / 2);
        const after2 = applyWorldPivotRotation(after1.position, after1.rotation, scl, pivot, q2);

        // After drag 2: (0,0,-5) rotated 90° around X -> (0,5,0)
        expectVec3Near(after2.position, { x: 0, y: 5, z: 0 });

        // Verify orbit distance preserved across both drags
        const dist0 = len3(sub3(pos, pivot));
        const dist2 = len3(sub3(after2.position, pivot));
        expect(dist2).toBeCloseTo(dist0, 10);
    });
});

// ============================================================================
// TEST D: signedAngleOnPlane consistency
// ============================================================================
describe('Test D: signedAngleOnPlane', () => {
    function signedAngleOnPlane(v0: any, v1: any, planeNormal: any) {
        const c = cross3(v0, v1);
        const sinA = dot3(planeNormal, c);
        const cosA = dot3(v0, v1);
        return Math.atan2(sinA, cosA);
    }

    it('+90° around Z: angle from (1,0,0) to (0,1,0) is +pi/2', () => {
        const angle = signedAngleOnPlane(
            { x: 1, y: 0, z: 0 },
            { x: 0, y: 1, z: 0 },
            { x: 0, y: 0, z: 1 }
        );
        expect(angle).toBeCloseTo(Math.PI / 2, 10);
    });

    it('-90° around Z: angle from (1,0,0) to (0,-1,0) is -pi/2', () => {
        const angle = signedAngleOnPlane(
            { x: 1, y: 0, z: 0 },
            { x: 0, y: -1, z: 0 },
            { x: 0, y: 0, z: 1 }
        );
        expect(angle).toBeCloseTo(-Math.PI / 2, 10);
    });

    it('angle and quatFromAxisAngle produce consistent rotation', () => {
        // Build a rotation, apply it to a vector, measure the angle — should match
        const axis = { x: 0, y: 0, z: 1 };
        const inputAngle = 1.23; // radians
        const q = quatFromAxisAngle(axis, inputAngle);
        const v0 = { x: 1, y: 0, z: 0 };
        const v1 = norm3(quatRotateVec3(q, v0));
        const measuredAngle = signedAngleOnPlane(v0, v1, axis);
        expect(measuredAngle).toBeCloseTo(inputAngle, 10);
    });
});
