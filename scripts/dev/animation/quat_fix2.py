"""
Analysis v2: Current position remap (-y,x,z) is CORRECT for positions.
The issue is only with quaternions and scale.
"""
import numpy as np

def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw*bx + ax*bw + ay*bz - az*by,
        aw*by - ax*bz + ay*bw + az*bx,
        aw*bz + ax*by - ay*bx + az*bw,
        aw*bw - ax*bx - ay*by - az*bz
    )

def quat_norm(q):
    x,y,z,w = q
    n = np.sqrt(x*x+y*y+z*z+w*w)
    return (x/n, y/n, z/n, w/n)

def quat_conj(q):
    x,y,z,w = q
    return (-x,-y,-z,w)

def quat_to_mat(q):
    # Column-vector convention: v' = R*v
    x,y,z,w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]
    ])

def mat_to_quat(R):
    trace = R[0,0] + R[1,1] + R[2,2]
    if trace > 0:
        s = 0.5 / np.sqrt(trace + 1.0)
        w = 0.25 / s
        x = (R[2,1] - R[1,2]) * s
        y = (R[0,2] - R[2,0]) * s
        z = (R[1,0] - R[0,1]) * s
    elif R[0,0] > R[1,1] and R[0,0] > R[2,2]:
        s = 2.0 * np.sqrt(1.0 + R[0,0] - R[1,1] - R[2,2])
        w = (R[2,1] - R[1,2]) / s
        x = 0.25 * s
        y = (R[0,1] + R[1,0]) / s
        z = (R[0,2] + R[2,0]) / s
    elif R[1,1] > R[2,2]:
        s = 2.0 * np.sqrt(1.0 + R[1,1] - R[0,0] - R[2,2])
        w = (R[0,2] - R[2,0]) / s
        x = (R[0,1] + R[1,0]) / s
        y = 0.25 * s
        z = (R[1,2] + R[2,1]) / s
    else:
        s = 2.0 * np.sqrt(1.0 + R[2,2] - R[0,0] - R[1,1])
        w = (R[1,0] - R[0,1]) / s
        x = (R[0,2] + R[2,0]) / s
        y = (R[1,2] + R[2,1]) / s
        z = 0.25 * s
    return quat_norm((x, y, z, w))

# The ACTUAL transformation M that maps UE5 FBX space to engine space
# is the one implied by the working position remap: (x,y,z) -> (-y, x, z)
# engine_x = -ue5_y
# engine_y =  ue5_x
# engine_z =  ue5_z
# So M_vec: engine = M * ue5  -> M = [[ 0,-1,0],[1,0,0],[0,0,1]]
M = np.array([[ 0,-1, 0],
              [ 1, 0, 0],
              [ 0, 0, 1]], dtype=float)
print(f"M_vec (position remap): det = {np.linalg.det(M):.1f}")
print(M)
print()

# Since det(M) = +1, this IS a pure rotation (90 deg CCW around Z).
# For a pure rotation M, quaternion conjugation is valid:
# q_engine = R(M) * q_ue5 * R(M)^{-1}
# where R(M) is the quaternion representing the rotation M.

# M = [[0,-1,0],[1,0,0],[0,0,1]] is 90 deg CCW around Z axis
# q_M = (0, 0, sin(45), cos(45)) = (0, 0, sqrt(2)/2, sqrt(2)/2)
q_M = (0, 0, np.sqrt(2)/2, np.sqrt(2)/2)
q_M_inv = quat_conj(q_M)  # for unit quaternion, conj = inv

print(f"q_M (90 CCW around Z): {q_M}")
print()

# Verify M is 90 CCW around Z:
# R(q_M) should equal M
R_qM = quat_to_mat(q_M)
print(f"quat_to_mat(q_M):")
print(R_qM.round(4))
print(f"M:")
print(M)
print()

# Now compute q_engine = q_M * q_ue5 * q_M_inv for Point001
q_ue5_p1 = quat_norm((0.4975, -0.5025, 0.5025, 0.4975))
q_eng_p1_expected = quat_norm((0.7107, 0.0, 0.0, 0.7035))

q_result_p1 = quat_norm(quat_mul(q_M, quat_mul(q_ue5_p1, q_M_inv)))
print(f"q_M * q_ue5_p1 * q_M_inv = {tuple(round(x,4) for x in q_result_p1)}")
print(f"Expected                  = {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Let's also verify via rotation matrix approach
R_ue5_p1 = quat_to_mat(q_ue5_p1)
R_eng_computed = M @ R_ue5_p1 @ M.T
q_from_mat = mat_to_quat(R_eng_computed)
print(f"mat approach: M*R_ue5*M^T -> quat = {tuple(round(x,4) for x in q_from_mat)}")
print(f"Expected:                          = {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# The current remap for MaxZupRH quaternion is: (-qy, qx, qz, qw)
# This is EXACTLY what q_M * q * q_M_inv gives!
# q_M = (0,0,sqrt(2)/2, sqrt(2)/2)
# q_M * q * q_M_inv:
# Let q = (qx, qy, qz, qw)
# = (sqrt(2)/2) * (0,0,sqrt(2)/2,sqrt(2)/2) * (qx,qy,qz,qw) * (0,0,-sqrt(2)/2,sqrt(2)/2)
# Algebraic expansion...

# Let's verify the current remap formula matches:
def current_max_remap_q(q):
    qx,qy,qz,qw = q
    return quat_norm((-qy, qx, qz, qw))

result_current = current_max_remap_q(q_ue5_p1)
print(f"Current remap (-qy,qx,qz,qw): {tuple(round(x,4) for x in result_current)}")
print(f"q_M * q * q_M_inv:            {tuple(round(x,4) for x in q_result_p1)}")
print()

# The two are the same! So current quat remap = q_M * q * q_M_inv = MATHEMATICALLY CORRECT.
# Then why does the rotation look wrong?

# The issue must be that the UE5 FBX q_ue5_p1 does NOT represent the same rotation
# as the Maya FBX q_maya_p1 when converted to UE5 space.
# Let me verify: does q_M * q_maya_raw * q_M_inv != q_eng_p1_expected?

q_maya_raw_p1 = (0.0, 0.0, 0.7107, 0.7035)
q_maya_cyclic = (q_maya_raw_p1[2], q_maya_raw_p1[0], q_maya_raw_p1[1], q_maya_raw_p1[3])

# Maya space to engine space via M_maya = [[0,0,1],[1,0,0],[0,1,0]] (cyclic permute)
# q_M_maya represents this rotation... but cyclic permutation also has det=+1
# M_maya = (x,y,z) -> (z,x,y): engine_x=maya_z, engine_y=maya_x, engine_z=maya_y
# This is a rotation: 120 deg around (1,1,1)/sqrt(3) axis
# Let's compute q_M_maya:

M_maya = np.array([[0,0,1],[1,0,0],[0,1,0]], dtype=float)
print(f"M_maya (cyclic): det = {np.linalg.det(M_maya):.1f}")
q_M_maya = mat_to_quat(M_maya)
print(f"q_M_maya = {tuple(round(x,4) for x in q_M_maya)}")

q_M_maya_inv = quat_conj(q_M_maya)
q_result_maya = quat_norm(quat_mul(q_M_maya, quat_mul(q_maya_raw_p1, q_M_maya_inv)))
print(f"q_M_maya * q_maya_raw * q_M_maya_inv = {tuple(round(x,4) for x in q_result_maya)}")
print(f"Expected (from cyclic formula):       = {tuple(round(x,4) for x in q_maya_cyclic)}")
print()

# Now, the engine frame from MayaYupRH cyclic is the reference.
# q_eng_p1_expected = q_M_maya * q_maya_raw * q_M_maya_inv
# But the current MaxZupRH gives: q_M * q_ue5_raw * q_M_inv != q_M_maya * q_maya_raw * q_M_maya_inv

# Let's see what Q converts: q_M * q_ue5_raw * q_M_inv -> q_M_maya * q_maya_raw * q_M_maya_inv
# Both SHOULD represent the same physical pose. The issue is the data itself.

# From earlier analysis:
# q_M_prev (prev analysis, maya to ue5) = (-0.7071, 0, 0, 0.7071) -- rotate -90 around X
# This was: M_maya2ue5: ue5_x = maya_x, ue5_y = -maya_z, ue5_z = maya_y
# So M_maya2ue5 * q_maya_raw * M_maya2ue5^{-1} = (0, 0.7107, 0, 0.7035)
# But q_ue5_raw = (0.4975, -0.5025, 0.5025, 0.4975)
# These are NOT equal => FBX SDK ConvertScene does NOT just apply M_maya2ue5 to the rotations.

# The ACTUAL DIFFERENCE is that UE5's FBX exporter goes:
# UE5 internal bone (X-fwd, Y-right, Z-up LH) -> FBX SDK (Z-up, -Y-front, RH)
# The ConvertMatrix function does this conversion.

# The UE5 internal to FBX SDK conversion for matrices:
# UE5: X=fwd, Y=right, Z=up, LH
# FBX: X=right, Z=up, -Y=front, RH
# Mapping: fbx_X = ue5_Y (right), fbx_Y = -ue5_X (back=front), fbx_Z = ue5_Z (up)
# Plus handedness flip (LH to RH)

# For a rotation R in UE5 LH space, the equivalent in FBX RH space:
# R_fbx = T * R * T^{-1}  where T handles both axis permutation AND handedness
# For LH->RH: one standard approach is to negate one axis.
# ConvertMatrix from UE5 source negates Y-row and Y-column.

# Let's define T_ue5_to_fbx:
# UE5 (X-fwd, Y-right, Z-up LH) -> FBX (X-right, Z-up, -Y-front, RH)
# fbx_X = ue5_Y: because fbx right = ue5 right = ue5 Y
# fbx_Y = ue5_X: because fbx "back" (+Y) = ue5 "fwd" (+X) ... wait RH vs LH changes sign
# In UE5 LH, X is forward. In FBX RH with front=-Y, the forward is -FBX_Y.
# So FBX -Y = UE5 X -> FBX Y = -UE5 X
# fbx_Z = ue5_Z: both are up

# So T: fbx_x = ue5_y, fbx_y = -ue5_x, fbx_z = ue5_z
# As matrix (column-major): T = [[0,1,0],[-1,0,0],[0,0,1]]
T = np.array([[0, 1, 0], [-1, 0, 0], [0, 0, 1]], dtype=float)
print(f"T (ue5_internal -> fbx_sdk): det = {np.linalg.det(T):.1f}")
print(T)

# Wait, det=-1 again. But UE5 LH -> FBX RH is supposed to flip handedness (det changes sign)
# Actually no: LH coordinate system is just a reflection of RH. If we go from LH to RH,
# it IS a det=-1 transformation. So T with det=-1 is CORRECT for LH->RH.

# For a rotation matrix R in LH space, converting to RH space with T (det=-1):
# R_rh = T * R_lh * T^{-1}   -> This gives det(R_rh) = det(T)*det(R_lh)*det(T^{-1}) = (-1)*(1)*(-1) = 1
# Good, still a proper rotation.

# But for a quaternion, this can't be done with simple conjugation (det=-1).
# We'd need to extract the rotation matrix, transform it, then re-extract the quaternion.

# In practice for UE5's ConvertMatrix: it converts matrix element-by-element
# Let's compute what the Maya rotation matrix becomes after ConvertMatrix:
# Maya FBX space: X-right, Y-up, Z-front (front = +Z = towards viewer... or away?)
# The Maya FBX standard: FbxAxisSystem(eYAxis, eParityOdd, eRightHanded)
# eParityOdd means front=+Z? Let me check...
# eParityOdd = the front vector points in the positive direction along its axis
# So if front axis = Z (default for Maya), eParityOdd means front=+Z
# Maya: right=+X, up=+Y, front=+Z, RH

# UE5 export FBX: right=+X, up=+Z, front=-Y (eParityOdd with axis=Y means front=+Y, but
# UE5 uses -eParityOdd so front=-Y)

# So Maya to UE5 FBX is:
# ue5_X = maya_X (right stays right)
# ue5_Z = maya_Y (up goes from Y to Z)
# ue5_-Y = maya_Z (front goes from +Z to -Y, so ue5_Y = -maya_Z)

# This is M_maya2ue5 = [[1,0,0],[0,0,1],[0,-1,0]] which matches the earlier analysis

# Now to go from UE5 internal LH to FBX:
# ue5_LH: X=fwd, Y=right, Z=up
# fbx_ue5: X=right, Z=up, -Y=front(=fwd in ue5)
# fbx_X = ue5_Y (right), fbx_Y = -ue5_X (back/front flip), fbx_Z = ue5_Z (up)
# But this is a LH->RH change...

# Actually, one key insight: UE5 uses X=forward (looking down +X in the world editor).
# The FBX exported with right=+X, Z=up, -Y=front is slightly confusing.
# If FBX "front" = -Y means the world's front direction is -Y in FBX space,
# and UE5's forward is +X, then: FBX(-Y) = UE5(+X) -> FBX(Y) = UE5(-X)
# FBX(X) = UE5(Y) (right in both)
# FBX(Z) = UE5(Z) (up in both)
# This is a LH->RH conversion...

# The point is: UE5 internal -> FBX involves a handedness flip.
# This means rotating bones in UE5 internal and then converting to FBX produces
# quaternions where the rotation DIRECTION appears flipped vs what we'd expect
# from simple axis remapping.

# The most reliable approach: examine the DATA empirically.
# We have q_maya_raw in Maya FBX space and q_ue5_raw in UE5 FBX space.
# We know both represent the same physical rotation for the ROOT bone at bind pose.

# Find transformation T such that T * q_maya_raw = q_ue5_raw (in quaternion algebra)
q_maya_raw = quat_norm((0.0, 0.0, 0.7107, 0.7035))
q_ue5_raw = quat_norm((0.4975, -0.5025, 0.5025, 0.4975))

# q_T_left = q_ue5_raw * conj(q_maya_raw)   [left multiplication]
q_T_left = quat_norm(quat_mul(q_ue5_raw, quat_conj(q_maya_raw)))
print(f"\nq_T_left  (so that q_T * q_maya = q_ue5): {tuple(round(x,4) for x in q_T_left)}")

# q_T_right = conj(q_maya_raw) * q_ue5_raw  [right multiplication]
q_T_right = quat_norm(quat_mul(quat_conj(q_maya_raw), q_ue5_raw))
print(f"q_T_right (so that q_maya * q_T = q_ue5): {tuple(round(x,4) for x in q_T_right)}")
print()

# Now apply the engine remaps:
# Maya->engine: q_M_maya * q_maya_raw * q_M_maya_inv = q_eng (expected)
# UE5->engine:  q_M * q_ue5_raw * q_M_inv = ???

# We need: q_M * q_ue5_raw * q_M_inv = q_M_maya * q_maya_raw * q_M_maya_inv
# Substituting q_ue5_raw = q_T_left * q_maya_raw:
# q_M * q_T_left * q_maya_raw * q_M_inv = q_M_maya * q_maya_raw * q_M_maya_inv

# This would require: q_M * q_T_left = q_M_maya  (if we can factor out q_maya_raw)
# But q_maya_raw cancels only if we multiply both sides by q_maya_raw_inv on the right
# and q_maya_raw on the left... it doesn't simplify cleanly.

# The REAL question: what remap function f(q_ue5_raw) gives q_eng_expected?
# For the MayaYupRH case we have: f_maya(q) = (q.z, q.x, q.y, q.w)  [cyclic]
# For MaxZupRH: q_eng_expected = q_M_maya * q_maya_raw * q_M_maya_inv
#                               = q_M_maya * q_T_right^{-1} * q_ue5_raw * q_T_right * q_M_maya_inv
# where q_maya_raw = q_T_right^{-1} * q_ue5_raw * q_T_right (if right-conjugation holds)

# Actually: q_ue5_raw = q_maya_raw * q_T_right means
# q_maya_raw = q_ue5_raw * q_T_right^{-1}
# Let's check: q_ue5_raw = q_maya_raw * q_T_right?
test_check = quat_mul(q_maya_raw, q_T_right)
print(f"q_maya_raw * q_T_right = {tuple(round(x,4) for x in quat_norm(test_check))}")
print(f"q_ue5_raw              = {tuple(round(x,4) for x in q_ue5_raw)}")
print()

# So q_ue5_raw = q_maya_raw * q_T_right (verified)
# Therefore q_maya_raw = q_ue5_raw * conj(q_T_right)

# q_eng = q_M_maya * q_maya_raw * q_M_maya_inv
#       = q_M_maya * q_ue5_raw * conj(q_T_right) * q_M_maya_inv

# Current MaxZupRH gives: q_M * q_ue5_raw * q_M_inv
# For these to be equal: q_M * q_ue5_raw * q_M_inv = q_M_maya * q_ue5_raw * conj(q_T_right) * q_M_maya_inv
# This would require q_M = q_M_maya ... no that's not generally true.

# Instead: the correct remap for MaxZupRH should be:
# q_eng = q_M_maya * q_ue5_raw * conj(q_T_right) * q_M_maya_inv
# But conj(q_T_right) is a per-file constant, so this adds a right-side correction.

# Let's compute what this gives for Point001:
q_T_right_conj = quat_conj(q_T_right)
q_eng_computed = quat_norm(quat_mul(q_M_maya, quat_mul(q_ue5_raw, quat_mul(q_T_right_conj, q_M_maya_inv))))
print(f"q_M_maya * q_ue5 * conj(q_T_right) * q_M_maya_inv = {tuple(round(x,4) for x in q_eng_computed)}")
print(f"Expected: {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Alternatively: the remap should just BE q_M_maya applied to q_maya_raw.
# Since q_ue5 = q_maya * q_T_right, and we want q_M_maya * q_maya * q_M_maya_inv,
# = q_M_maya * q_ue5 * q_T_right^{-1} * q_M_maya_inv

# Simplest approach: derive directly what formula maps q_ue5_raw to q_eng_expected
# For a general quat q_ue5 = (qx, qy, qz, qw), find a, b, c, d (each +-1) and
# a permutation such that:
# f(qx,qy,qz,qw) = (a*q_i, b*q_j, c*q_k, d*qw)

# For Point001:
# q_ue5 = (0.4975, -0.5025, 0.5025, 0.4975) -> need (0.7107, 0, 0, 0.7035)
# The magnitudes are completely different, so simple permutation won't work.

print("=" * 60)
print("REAL FIX APPROACH: World matrix approach")
print("=" * 60)

# The key insight I've been avoiding: the world rotation matrices computed by
# ufbx_evaluate_scene in UE5 FBX space ARE correct 3D rotations in UE5 FBX space.
# When we apply the remap, we get the world rotation in engine space.
# The skeleton bind was imported from Maya, also remapped to engine space.
#
# The question: why does Maya engine world != UE5 engine world for bind frame?
#
# If child bone locals are IDENTICAL in Maya vs UE5 FBX, then:
# - For child bones: local_ue5 == local_maya
# - For the root: local_ue5 != local_maya (ConvertScene changes root)
#
# Engine world = chain of locals (root * child * grandchild...)
# Maya engine world = f_maya(root_maya) * f_maya(child_maya) * ...
# UE5 engine world = f_max(root_ue5) * f_max(child_ue5) * ...
#
# Since child locals are IDENTICAL (child_ue5 == child_maya), applying different
# remaps to them produces DIFFERENT results:
# f_maya(child_maya) != f_max(child_ue5)  (even though child_maya == child_ue5)
#
# So the issue propagates through all bones!
#
# CONCLUSION: The problem is fundamental. The data in UE5 FBX represents
# the SAME physical skeleton in a DIFFERENT coordinate space than Maya FBX.
# The remap must transform from UE5 space to engine space.
# Both the SKELETON (from Maya FBX) and the ANIMATION (from UE5 FBX) must
# end up in the SAME engine space.
#
# Currently:
# - Skeleton import: f_maya applied to Maya FBX -> engine space A
# - Animation import: f_max applied to UE5 FBX -> engine space B
# - A != B! That's the bug.
#
# Fix: either
# 1. Import animation with f_maya (need Maya-compatible data from UE5 FBX)
# 2. Import skeleton with f_max (need Max-compatible data from Maya FBX)
# 3. Find new f_max' such that f_max'(ue5) = f_maya(maya) for same physical pose
#
# Option 3 requires knowing the exact relationship between maya and ue5 data.
# We established: q_ue5_raw = q_maya_raw * q_T_right
# And positions: ue5_pos = M_maya2ue5 * maya_pos
# (where M_maya2ue5: ue5_x=maya_x, ue5_y=-maya_z, ue5_z=maya_y)
#
# So: f_max'(q_ue5) = f_maya(q_ue5 * conj(q_T_right))
#                   = cyclic(q_ue5 * conj(q_T_right))  [where cyclic permutes (z,x,y,w)]
#
# And for positions: f_max'(pos_ue5) = f_maya(M_maya2ue5^{-1} * pos_ue5)
# M_maya2ue5: ue5=M*maya -> maya = M^{-1}*ue5 = M^T*ue5
# M_maya2ue5 = [[1,0,0],[0,0,1],[0,-1,0]] -> M^T = [[1,0,0],[0,0,-1],[0,1,0]]
# So maya_pos = (ue5_x, -ue5_z, ue5_y)  ... wait that gives Maya Y = ue5_z... hmm

# But we verified that the CURRENT position remap for MaxZupRH IS CORRECT (matches expected).
# So positions are fine. Only quaternions need fixing.

# For the rotation fix, let's check:
# Current MaxZupRH quat: (-qy, qx, qz, qw)
# This equals q_M * q * q_M_inv where q_M = (0,0,sqrt(2)/2, sqrt(2)/2)
#
# What should it be?
# q_eng = f_maya(q_maya_raw) = q_M_maya * q_maya_raw * q_M_maya_inv  [Maya cyclic]
# q_maya_raw = q_ue5_raw * conj(q_T_right)
# So: q_eng = q_M_maya * q_ue5_raw * conj(q_T_right) * q_M_maya_inv

# Let's expand: this is (constant_left) * q_ue5 * (constant_right)
# = q_M_maya * q_ue5 * (conj(q_T_right) * q_M_maya_inv)
# = q_M_maya * q_ue5 * conj(q_M_maya * q_T_right)  [since conj(AB) = conj(B)*conj(A)]

# Wait: conj(q_T_right) * q_M_maya_inv = conj(q_T_right) * conj(q_M_maya) = conj(q_M_maya * q_T_right)

print(f"\nq_T_right = {tuple(round(x,4) for x in q_T_right)}")
print(f"q_M_maya = {tuple(round(x,4) for x in q_M_maya)}")

q_right_side = quat_norm(quat_mul(quat_conj(q_T_right), q_M_maya_inv))
print(f"conj(q_T_right) * q_M_maya_inv = {tuple(round(x,4) for x in q_right_side)}")

# Correct formula: q_eng = q_M_maya * q_ue5 * q_right_side
q_result = quat_norm(quat_mul(q_M_maya, quat_mul(q_ue5_raw, q_right_side)))
print(f"\nq_M_maya * q_ue5 * q_right_side = {tuple(round(x,4) for x in q_result)}")
print(f"Expected:                         = {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# The constant 'q_right_side' can be computed from q_T_right and q_M_maya.
# These are constants that can be precomputed.
# BUT: is q_T_right truly constant for ALL bones, or just the root?
# q_T_right was derived from the root bone (Point001) relationship.
# For child bones, locals are identical in Maya and UE5, so the relationship might differ.

# If the child bone data in UE5 FBX also has q_ue5 = q_maya * q_T_right,
# then the formula works for all bones.
# Otherwise we need a different approach.

# Let's try to find q_T_right from the positions instead of rotations,
# since positions are axis-permuted:
# We know ue5_pos = M_maya2ue5 * maya_pos for WORLD positions.
# And the rotation M_maya2ue5 corresponds to a quaternion.

M_maya2ue5 = np.array([[1,0,0],[0,0,1],[0,-1,0]], dtype=float)
print(f"M_maya2ue5 det = {np.linalg.det(M_maya2ue5):.1f}")
q_maya2ue5 = mat_to_quat(M_maya2ue5)
print(f"q_maya2ue5 = {tuple(round(x,4) for x in q_maya2ue5)}")

# If bone rotations also transform as q_ue5 = M_maya2ue5(q_maya) via conjugation:
# q_ue5 = q_maya2ue5 * q_maya * conj(q_maya2ue5)
q_ue5_check = quat_norm(quat_mul(q_maya2ue5, quat_mul(q_maya_raw, quat_conj(q_maya2ue5))))
print(f"\nq_maya2ue5 * q_maya * conj(q_maya2ue5) = {tuple(round(x,4) for x in q_ue5_check)}")
print(f"Actual q_ue5_raw =                       = {tuple(round(x,4) for x in q_ue5_raw)}")
print(f"Do they match? {np.allclose(np.abs(np.array(q_ue5_check)), np.abs(np.array(q_ue5_raw)), atol=0.001)}")
