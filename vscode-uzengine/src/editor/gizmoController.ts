/**
 * gizmoController.ts — 3D transform gizmo math and state for the UzEngine editor.
 *
 * Responsibilities:
 *   - Maintain gizmo state: mode (translate/rotate/scale), constraint axis/plane,
 *     coordinate space (local/world), drag state.
 *   - Provide pure math utilities used for hit-testing and dragging:
 *       screenToWorldRay  — unproject a screen pixel into a world-space ray
 *       hitTestAxis       — ray vs. oriented cylinder (for translate/scale axis handles)
 *       hitTestPlane      — ray vs. infinite plane (for translate/rotate plane handles)
 *   - Compute transform deltas for translate, rotate, and scale drags.
 *   - Apply grid snap when ctrl is held.
 *   - Send IPC calls to the engine via EngineClient.
 *
 * All matrix math uses column-major 4x4 flat arrays (Float32Array or number[16])
 * matching the convention used in the C++ engine and WebGL.
 */

import { EngineClient } from '../engine-client';

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

/** A 3-component vector. */
export interface Vec3 {
    x: number;
    y: number;
    z: number;
}

/** A 4-component quaternion (w, x, y, z). */
export interface Quat {
    w: number;
    x: number;
    y: number;
    z: number;
}

/** A world-space ray with origin and normalised direction. */
export interface Ray {
    origin: Vec3;
    direction: Vec3;  // normalised
}

/** Gizmo manipulation mode. */
export type GizmoMode = 'translate' | 'rotate' | 'scale';

/** Axis/plane constraint for the active drag. */
export type GizmoConstraint =
    | 'x' | 'y' | 'z'       // single-axis
    | 'xy' | 'xz' | 'yz'    // planar
    | 'view';                 // camera-facing plane

/** Coordinate frame for the gizmo. */
export type GizmoSpace = 'local' | 'world';

/** Transform triplet sent to / received from the engine. */
export interface NodeTransform {
    position: Vec3;
    rotation: Quat;
    scale: Vec3;
}

// ---------------------------------------------------------------------------
// Vec3 math helpers (pure functions, exported for tests)
// ---------------------------------------------------------------------------

export function vec3Add(a: Vec3, b: Vec3): Vec3 {
    return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

export function vec3Sub(a: Vec3, b: Vec3): Vec3 {
    return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

export function vec3Scale(v: Vec3, s: number): Vec3 {
    return { x: v.x * s, y: v.y * s, z: v.z * s };
}

export function vec3Dot(a: Vec3, b: Vec3): number {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function vec3Cross(a: Vec3, b: Vec3): Vec3 {
    return {
        x: a.y * b.z - a.z * b.y,
        y: a.z * b.x - a.x * b.z,
        z: a.x * b.y - a.y * b.x,
    };
}

export function vec3Length(v: Vec3): number {
    return Math.sqrt(vec3Dot(v, v));
}

export function vec3Normalize(v: Vec3): Vec3 {
    const len = vec3Length(v);
    if (len < 1e-12) return { x: 0, y: 0, z: 0 };
    return vec3Scale(v, 1 / len);
}

// ---------------------------------------------------------------------------
// 4×4 matrix math (column-major, flat array of 16 numbers)
// ---------------------------------------------------------------------------

/** Multiply two 4×4 column-major matrices. Returns a new 16-element array. */
export function mat4Mul(a: number[], b: number[]): number[] {
    const out = new Array<number>(16).fill(0);
    for (let row = 0; row < 4; row++) {
        for (let col = 0; col < 4; col++) {
            let sum = 0;
            for (let k = 0; k < 4; k++) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
    return out;
}

/** Invert a 4×4 column-major matrix. Returns null if the matrix is singular. */
export function mat4Invert(m: number[]): number[] | null {
    const inv = new Array<number>(16).fill(0);

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];

    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];

    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];

    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    const det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (Math.abs(det) < 1e-12) return null;

    const invDet = 1 / det;
    for (let i = 0; i < 16; i++) inv[i] *= invDet;
    return inv;
}

// ---------------------------------------------------------------------------
// Core math: ray casting
// ---------------------------------------------------------------------------

/**
 * Unproject a screen pixel (px, py) into a world-space ray using the
 * provided view and projection matrices (column-major, 16 elements each).
 *
 * Convention: NDC x,y in [-1, 1]; y positive upward (WebGL/OpenGL style).
 * Pixel (0,0) is top-left of the viewport.
 *
 * @param px       Pixel x in [0, width)
 * @param py       Pixel y in [0, height) — top-left origin
 * @param width    Viewport width in pixels
 * @param height   Viewport height in pixels
 * @param viewMatrix   Column-major 4×4 view matrix
 * @param projMatrix   Column-major 4×4 projection matrix
 * @returns A normalised world-space ray, or null if the VP matrix is singular.
 */
export function screenToWorldRay(
    px: number,
    py: number,
    width: number,
    height: number,
    viewMatrix: number[],
    projMatrix: number[],
): Ray | null {
    // NDC: x in [-1,1], y in [-1,1] (flip y because screen y is top-down)
    const ndcX = (px / width) * 2 - 1;
    const ndcY = 1 - (py / height) * 2;

    const vp = mat4Mul(projMatrix, viewMatrix);
    const vpInv = mat4Invert(vp);
    if (!vpInv) return null;

    // Unproject two points at z=-1 (near) and z=1 (far) in NDC
    const unproject = (ndcZ: number): Vec3 => {
        const cx = vpInv[0]*ndcX + vpInv[4]*ndcY + vpInv[8]*ndcZ  + vpInv[12];
        const cy = vpInv[1]*ndcX + vpInv[5]*ndcY + vpInv[9]*ndcZ  + vpInv[13];
        const cz = vpInv[2]*ndcX + vpInv[6]*ndcY + vpInv[10]*ndcZ + vpInv[14];
        const cw = vpInv[3]*ndcX + vpInv[7]*ndcY + vpInv[11]*ndcZ + vpInv[15];
        const wInv = 1 / cw;
        return { x: cx * wInv, y: cy * wInv, z: cz * wInv };
    };

    const near = unproject(-1);
    const far  = unproject( 1);

    return {
        origin:    near,
        direction: vec3Normalize(vec3Sub(far, near)),
    };
}

/**
 * Hit-test a ray against an oriented axis handle (modelled as a thin cylinder).
 *
 * Uses the nearest-point-between-two-lines formula to compute the minimum
 * distance between the ray and the infinite line defined by
 * (axisOrigin, axisDir), then checks whether that closest point lies within
 * [0, axisLength] and the distance is within hitRadius.
 *
 * @param ray         World-space ray
 * @param axisOrigin  Base of the axis handle
 * @param axisDir     Normalised direction of the axis
 * @param axisLength  Length of the axis handle (default 1.0)
 * @param hitRadius   Cylinder radius for hit detection
 * @returns true if the ray intersects the cylinder within the given length.
 */
export function hitTestAxis(
    ray: Ray,
    axisOrigin: Vec3,
    axisDir: Vec3,
    axisLength = 1.0,
    hitRadius = 0.08,
): boolean {
    // Nearest point between two lines:
    // Line A: ray.origin + t * ray.direction
    // Line B: axisOrigin + s * axisDir
    const w0 = vec3Sub(ray.origin, axisOrigin);

    const a = vec3Dot(ray.direction, ray.direction);  // |ray.dir|^2 ≈ 1
    const b = vec3Dot(ray.direction, axisDir);
    const c = vec3Dot(axisDir, axisDir);              // |axisDir|^2 ≈ 1
    const d = vec3Dot(ray.direction, w0);
    const e = vec3Dot(axisDir, w0);

    const denom = a * c - b * b;
    if (Math.abs(denom) < 1e-10) {
        // Lines are (nearly) parallel — check if ray is close to the axis line
        const cross = vec3Cross(ray.direction, axisDir);
        const dist = vec3Length(cross) < 1e-10
            ? vec3Length(vec3Cross(w0, ray.direction))
            : vec3Length(cross) * vec3Length(w0);
        return dist < hitRadius;
    }

    const t = (b * e - c * d) / denom;
    const s = (a * e - b * d) / denom;

    // s must be in [0, axisLength] for the handle
    if (s < 0 || s > axisLength) return false;
    // t must be non-negative (in front of camera)
    if (t < 0) return false;

    const closestOnRay  = vec3Add(ray.origin,  vec3Scale(ray.direction, t));
    const closestOnAxis = vec3Add(axisOrigin,  vec3Scale(axisDir, s));
    const dist = vec3Length(vec3Sub(closestOnRay, closestOnAxis));

    return dist <= hitRadius;
}

/**
 * Intersect a ray with an infinite plane defined by a point and normal.
 *
 * @param ray          World-space ray
 * @param planeOrigin  A point on the plane
 * @param planeNormal  Normalised plane normal
 * @returns The intersection point in world space, or null if ray is parallel
 *          to the plane or the intersection is behind the ray origin.
 */
export function hitTestPlane(
    ray: Ray,
    planeOrigin: Vec3,
    planeNormal: Vec3,
): Vec3 | null {
    const denom = vec3Dot(planeNormal, ray.direction);
    if (Math.abs(denom) < 1e-10) return null;   // parallel

    const t = vec3Dot(planeNormal, vec3Sub(planeOrigin, ray.origin)) / denom;
    if (t < 0) return null;   // behind camera

    return vec3Add(ray.origin, vec3Scale(ray.direction, t));
}

// ---------------------------------------------------------------------------
// Grid snap
// ---------------------------------------------------------------------------

/**
 * Snap a value to the nearest multiple of gridSize.
 *
 * @param value    The raw value to snap.
 * @param gridSize The snap increment (e.g. 0.25).
 * @returns The snapped value.
 */
export function snapToGrid(value: number, gridSize: number): number {
    if (gridSize <= 0) return value;
    return Math.round(value / gridSize) * gridSize;
}

/**
 * Snap a Vec3 to the nearest grid point on each axis.
 */
export function snapVec3ToGrid(v: Vec3, gridSize: number): Vec3 {
    return {
        x: snapToGrid(v.x, gridSize),
        y: snapToGrid(v.y, gridSize),
        z: snapToGrid(v.z, gridSize),
    };
}

// ---------------------------------------------------------------------------
// Quaternion helpers
// ---------------------------------------------------------------------------

/** Create a rotation quaternion around a normalised axis by angle (radians). */
export function quatFromAxisAngle(axis: Vec3, angle: number): Quat {
    const half = angle / 2;
    const s = Math.sin(half);
    return {
        w: Math.cos(half),
        x: axis.x * s,
        y: axis.y * s,
        z: axis.z * s,
    };
}

/** Multiply two quaternions (apply q2 then q1). */
export function quatMul(q1: Quat, q2: Quat): Quat {
    return {
        w: q1.w*q2.w - q1.x*q2.x - q1.y*q2.y - q1.z*q2.z,
        x: q1.w*q2.x + q1.x*q2.w + q1.y*q2.z - q1.z*q2.y,
        y: q1.w*q2.y - q1.x*q2.z + q1.y*q2.w + q1.z*q2.x,
        z: q1.w*q2.z + q1.x*q2.y - q1.y*q2.x + q1.z*q2.w,
    };
}

/** Normalise a quaternion. */
export function quatNormalize(q: Quat): Quat {
    const len = Math.sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (len < 1e-12) return { w: 1, x: 0, y: 0, z: 0 };
    return { w: q.w/len, x: q.x/len, y: q.y/len, z: q.z/len };
}

// ---------------------------------------------------------------------------
// GizmoController class
// ---------------------------------------------------------------------------

/** Drag session state — reset to null when not dragging. */
interface DragState {
    startTransform: NodeTransform;
    startHitPoint: Vec3;   // initial hit point in world space
    constraint: GizmoConstraint;
}

export class GizmoController {
    private _mode: GizmoMode = 'translate';
    private _space: GizmoSpace = 'world';
    private _constraint: GizmoConstraint = 'x';
    private _isDragging = false;
    private _activeNodeId: number | null = null;
    private _dragState: DragState | null = null;
    private _gridSize = 0;     // 0 = snap disabled
    private _ctrlHeld = false;

    /** Camera matrices — updated every frame by the viewport. */
    private _viewMatrix: number[] = Array<number>(16).fill(0);
    private _projMatrix: number[] = Array<number>(16).fill(0);
    private _cameraPosition: Vec3 = { x: 0, y: 0, z: 0 };

    constructor(private readonly _client: EngineClient) {}

    // -----------------------------------------------------------------------
    // Public accessors
    // -----------------------------------------------------------------------

    get mode(): GizmoMode            { return this._mode; }
    get space(): GizmoSpace          { return this._space; }
    get constraint(): GizmoConstraint { return this._constraint; }
    get isDragging(): boolean        { return this._isDragging; }
    get activeNodeId(): number | null { return this._activeNodeId; }
    get gridSize(): number           { return this._gridSize; }
    get ctrlHeld(): boolean          { return this._ctrlHeld; }

    // -----------------------------------------------------------------------
    // State setters (also send IPC where appropriate)
    // -----------------------------------------------------------------------

    setMode(mode: GizmoMode): void {
        this._mode = mode;
        if (this._activeNodeId !== null) {
            void this._client.call('editor.setGizmo', {
                nodeId: this._activeNodeId,
                mode,
            });
        }
    }

    setSpace(space: GizmoSpace): void {
        this._space = space;
        void this._client.call('editor.setGizmoSpace', { space });
    }

    setGridSize(size: number): void {
        this._gridSize = size;
    }

    setCtrlHeld(held: boolean): void {
        this._ctrlHeld = held;
    }

    setConstraint(constraint: GizmoConstraint): void {
        this._constraint = constraint;
    }

    /**
     * Update the cached camera matrices. Called by the viewport every frame
     * after receiving an `editor.getCamera` IPC response.
     */
    updateCamera(viewMatrix: number[], projMatrix: number[], cameraPosition: Vec3): void {
        this._viewMatrix    = viewMatrix;
        this._projMatrix    = projMatrix;
        this._cameraPosition = cameraPosition;
    }

    /**
     * Set the active node and send `editor.setGizmo` to the engine.
     */
    async selectNode(nodeId: number | null): Promise<void> {
        this._activeNodeId = nodeId;
        if (nodeId !== null) {
            await this._client.call('editor.setGizmo', { nodeId, mode: this._mode });
        }
    }

    // -----------------------------------------------------------------------
    // Drag lifecycle
    // -----------------------------------------------------------------------

    /**
     * Begin a drag. Called on mousedown when an axis is hit.
     *
     * @param hitPoint       Initial hit point (from hitTestAxis / hitTestPlane)
     * @param startTransform Current transform of the active node
     * @param constraint     Which axis/plane to constrain the drag to
     */
    beginDrag(hitPoint: Vec3, startTransform: NodeTransform, constraint: GizmoConstraint): void {
        this._isDragging = true;
        this._constraint = constraint;
        this._dragState = {
            startTransform,
            startHitPoint: hitPoint,
            constraint,
        };
    }

    /**
     * Update the drag. Called on mousemove while dragging.
     * Computes the new transform and sends `editor.setNodeTransformLive`.
     *
     * @param px      Current mouse x
     * @param py      Current mouse y
     * @param width   Viewport width
     * @param height  Viewport height
     * @returns The new transform, or null if ray cast failed.
     */
    async updateDrag(px: number, py: number, width: number, height: number): Promise<NodeTransform | null> {
        if (!this._isDragging || !this._dragState || this._activeNodeId === null) return null;

        const ray = screenToWorldRay(px, py, width, height, this._viewMatrix, this._projMatrix);
        if (!ray) return null;

        const { startTransform, startHitPoint } = this._dragState;
        let newTransform: NodeTransform;

        switch (this._mode) {
            case 'translate':
                newTransform = this._computeTranslateDrag(ray, startTransform, startHitPoint);
                break;
            case 'rotate':
                newTransform = this._computeRotateDrag(ray, startTransform, startHitPoint);
                break;
            case 'scale':
                newTransform = this._computeScaleDrag(ray, startTransform, startHitPoint);
                break;
        }

        await this._client.call('editor.setNodeTransformLive', {
            nodeId: this._activeNodeId,
            transform: newTransform,
        });

        return newTransform;
    }

    /**
     * End the drag. Sends `editor.commitTransform` and returns the final transform.
     */
    async endDrag(): Promise<NodeTransform | null> {
        if (!this._isDragging || this._activeNodeId === null) return null;

        this._isDragging = false;

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const resp = await this._client.call('editor.commitTransform', {
            nodeId: this._activeNodeId,
        }) as any;

        this._dragState = null;
        return resp?.transform ?? null;
    }

    /**
     * Cancel the drag (e.g. on Escape key) — restores the original transform.
     */
    async cancelDrag(): Promise<void> {
        if (!this._isDragging) return;

        this._isDragging = false;

        if (!this._dragState || this._activeNodeId === null) {
            this._dragState = null;
            return;
        }

        await this._client.call('editor.setNodeTransformLive', {
            nodeId: this._activeNodeId,
            transform: this._dragState.startTransform,
        });
        this._dragState = null;
    }

    // -----------------------------------------------------------------------
    // Private drag computation
    // -----------------------------------------------------------------------

    private _getConstraintAxis(): Vec3 {
        switch (this._constraint) {
            case 'x':  return { x: 1, y: 0, z: 0 };
            case 'y':  return { x: 0, y: 1, z: 0 };
            case 'z':  return { x: 0, y: 0, z: 1 };
            case 'xy': return { x: 0, y: 0, z: 1 };  // normal of XY plane = Z
            case 'xz': return { x: 0, y: 1, z: 0 };  // normal of XZ plane = Y
            case 'yz': return { x: 1, y: 0, z: 0 };  // normal of YZ plane = X
            case 'view': return this._cameraForward();
        }
    }

    private _cameraForward(): Vec3 {
        // Extract forward from view matrix (negated Z column of view → world transform)
        // view[2], view[6], view[10] = world-space Z column → camera forward = -Z
        const vx = -this._viewMatrix[2];
        const vy = -this._viewMatrix[6];
        const vz = -this._viewMatrix[10];
        return vec3Normalize({ x: vx, y: vy, z: vz });
    }

    private _computeTranslateDrag(ray: Ray, startTransform: NodeTransform, startHit: Vec3): NodeTransform {
        const origin = startTransform.position;

        let currentHit: Vec3 | null = null;

        if (this._constraint === 'x' || this._constraint === 'y' || this._constraint === 'z') {
            // Project onto axis — find current point closest to the axis line
            const axisDir = this._getConstraintAxis();
            // Build a plane containing the axis that is most face-on to the camera
            const toCamera = vec3Normalize(vec3Sub(this._cameraPosition, origin));
            const planeNormal = vec3Normalize(vec3Cross(axisDir, vec3Cross(axisDir, toCamera)));
            const hit = hitTestPlane(ray, origin, planeNormal);
            if (hit) {
                // Project the hit point onto the axis
                const axisOffset = vec3Dot(vec3Sub(hit, origin), axisDir);
                currentHit = vec3Add(origin, vec3Scale(axisDir, axisOffset));
            }
        } else {
            // Planar constraint — intersect with the constraint plane
            const planeNormal = this._getConstraintAxis();
            currentHit = hitTestPlane(ray, origin, planeNormal);
        }

        if (!currentHit) return startTransform;

        // Delta in world space
        const startAxisHit = this._constrainToAxis(startHit, origin);
        const currentAxisHit = this._constrainToAxis(currentHit, origin);
        const delta = vec3Sub(currentAxisHit, startAxisHit);

        let newPos = vec3Add(startTransform.position, delta);

        if (this._ctrlHeld && this._gridSize > 0) {
            newPos = snapVec3ToGrid(newPos, this._gridSize);
        }

        return { ...startTransform, position: newPos };
    }

    private _constrainToAxis(point: Vec3, origin: Vec3): Vec3 {
        if (this._constraint === 'x' || this._constraint === 'y' || this._constraint === 'z') {
            const axisDir = this._getConstraintAxis();
            const offset = vec3Dot(vec3Sub(point, origin), axisDir);
            return vec3Add(origin, vec3Scale(axisDir, offset));
        }
        return point;
    }

    private _computeRotateDrag(ray: Ray, startTransform: NodeTransform, startHit: Vec3): NodeTransform {
        const origin = startTransform.position;
        const axisDir = this._getConstraintAxis();

        // Intersect with the rotation plane
        const currentHit = hitTestPlane(ray, origin, axisDir);
        if (!currentHit) return startTransform;

        // Vectors from gizmo origin to start and current hit
        const v0 = vec3Normalize(vec3Sub(startHit, origin));
        const v1 = vec3Normalize(vec3Sub(currentHit, origin));

        // Signed angle around axisDir
        const cosAngle = Math.max(-1, Math.min(1, vec3Dot(v0, v1)));
        const cross = vec3Cross(v0, v1);
        const sinSign = vec3Dot(cross, axisDir) >= 0 ? 1 : -1;
        const angle = sinSign * Math.acos(cosAngle);

        const deltaQuat = quatFromAxisAngle(axisDir, angle);
        const newRotation = quatNormalize(quatMul(deltaQuat, startTransform.rotation));

        return { ...startTransform, rotation: newRotation };
    }

    private _computeScaleDrag(ray: Ray, startTransform: NodeTransform, startHit: Vec3): NodeTransform {
        const origin = startTransform.position;

        // Project ray onto the constraint axis (same plane trick as translate)
        const axisDir = this._getConstraintAxis();
        const toCamera = vec3Normalize(vec3Sub(this._cameraPosition, origin));
        const planeNormal = vec3Normalize(vec3Cross(axisDir, vec3Cross(axisDir, toCamera)));

        const currentHit = hitTestPlane(ray, origin, planeNormal);
        if (!currentHit) return startTransform;

        const startDist  = vec3Dot(vec3Sub(startHit, origin), axisDir);
        const currentDist = vec3Dot(vec3Sub(currentHit, origin), axisDir);

        if (Math.abs(startDist) < 1e-6) return startTransform;

        const scaleFactor = currentDist / startDist;

        const newScale: Vec3 = {
            x: this._constraint === 'y' || this._constraint === 'z' ? startTransform.scale.x : startTransform.scale.x * scaleFactor,
            y: this._constraint === 'x' || this._constraint === 'z' ? startTransform.scale.y : startTransform.scale.y * scaleFactor,
            z: this._constraint === 'x' || this._constraint === 'y' ? startTransform.scale.z : startTransform.scale.z * scaleFactor,
        };

        return { ...startTransform, scale: newScale };
    }
}
