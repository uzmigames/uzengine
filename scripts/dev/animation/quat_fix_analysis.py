"""
Analysis: Find correct MaxZupRH quaternion remap for UzEngine animation import.
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
    x,y,z,w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]
    ])

print("=" * 60)
print("POSITION REMAP ANALYSIS")
print("=" * 60)

# UE5 FBX space: right=+X, up=+Z, front=-Y (back=+Y), RH
# Engine space (from Maya): right=+Y, up=+Z, back=+X
#
# Logical mapping:
#   engine right(+Y) = UE5 right(+X):   engine_Y = ue5_X
#   engine up(+Z)    = UE5 up(+Z):      engine_Z = ue5_Z
#   engine back(+X)  = UE5 back(+Y):    engine_X = ue5_Y
#
# So correct remap: engine(x,y,z) = ue5(y, x, z)  [NOT -y, x, z !]

# Verify with Point005 world position
# Maya engine world = [-10.1318, -11.7134, 71.0067]
# To get UE5 FBX world, apply inverse engine->maya->ue5:
# engine_x = maya_z (front), engine_y = maya_x (right), engine_z = maya_y (up)
# So: maya_x = engine_y, maya_y = engine_z, maya_z = engine_x
maya_x = -11.7134
maya_y = 71.0067
maya_z = -10.1318

# Maya -> UE5 FBX: ue5_x = maya_x, ue5_y = -maya_z, ue5_z = maya_y
ue5_x = maya_x
ue5_y = -maya_z
ue5_z = maya_y
print(f"Point005 UE5 FBX position: ({ue5_x:.4f}, {ue5_y:.4f}, {ue5_z:.4f})")

# Apply current remap (-y, x, z):
current = (-ue5_y, ue5_x, ue5_z)
print(f"Current remap (-y,x,z):  ({current[0]:.4f}, {current[1]:.4f}, {current[2]:.4f})")

# Apply correct remap (y, x, z):
correct = (ue5_y, ue5_x, ue5_z)
print(f"Correct remap (y,x,z):   ({correct[0]:.4f}, {correct[1]:.4f}, {correct[2]:.4f})")
print(f"Engine expected:         (-10.1318, -11.7134, 71.0067)")
print()

print("=" * 60)
print("QUATERNION REMAP ANALYSIS")
print("=" * 60)

# For quaternions representing ROTATIONS, the basis change is:
# q_engine = R(M) * q_ue5 * R(M)^{-1}
# where M maps UE5 space to engine space: M(ue5_vec) = engine_vec
# M = [[0,1,0],[1,0,0],[0,0,1]] (y->x, x->y, z->z)
# This is a reflection (det=-1)! Not a pure rotation.
# For reflections, we can't directly convert via quaternion conjugation.

M = np.array([[0,1,0],[1,0,0],[0,0,1]], dtype=float)
print(f"M_ue5_to_engine = {M}")
print(f"det(M) = {np.linalg.det(M):.1f}")
print()

# Since det(M) = -1 (reflection), we need to handle handedness change.
# A reflection reverses rotation direction.
# For a rotation q about axis (ax, ay, az) by angle theta in RH space,
# the equivalent rotation in LH space (after reflection through plane) is:
# about reflected axis, same angle, but HANDEDNESS is flipped.

# Actually: in our case, going from RH (UE5 FBX) to LH (engine, per Maya import):
# The sign of the rotation (direction of rotation) flips.
# This means: q_engine = (-q_engine.xyz, q_engine.w) = conjugate of q_ue5 remapped

# Let's think about it differently.
# We're using ufbx which gives us the world matrix.
# The position part transforms as a vector.
# The rotation part transforms as: for column-vector convention R(v) = M * q * M^-1 * v
# For ROW-vector convention (which is what UzEngine uses based on MulRowPoint),
# R(v) = v * M^{-1} * q * M ...

# But actually, ufbx gives us a rotation QUATERNION in the FBX coordinate space.
# The question is: if q_ue5 rotates vector v to v' in UE5 space (q_ue5 * v * q_ue5_conj = v'),
# what is q_engine such that q_engine * M(v) * q_engine_conj = M(v') in engine space?

# We need: M( q_ue5 * v * q_ue5_conj ) = q_engine * M(v) * q_engine_conj
# Let u = M(v). Then v = M^{-1}(u) = M(u) (since M = M^T = M^{-1} for our reflection)
# M( q_ue5 * M(u) * q_ue5_conj ) = q_engine * u * q_engine_conj

# For a general linear map M applied to the rotation sandwich:
# This is NOT just quaternion multiplication. We need to represent M as a combination
# of rotation and reflection.

# M = [[0,1,0],[1,0,0],[0,0,1]] can be decomposed as:
# M = Reflect_xy * R where R is a rotation... or:
# M is a reflection through the plane x=y (the plane where x=y)
# Reflecting through x=y: (x,y,z) -> (y,x,z)

# For a reflection R_n (through plane with normal n), the quaternion transform is:
# If q represents rotation by angle theta about axis a,
# then the equivalent rotation after reflection R_n is:
# rotation by -theta about R_n(a)
# Which corresponds to: q' = conjugate of (R_n * q)
# where (R_n * q) applies R_n to the axis: q'_xyz = R_n * q_xyz, q'_w = q_w
# Then conjugate: q_engine = (-q'_xyz, q'_w) = (-R_n(q_xyz), q_w)

# For our M = reflect through x=y plane (swaps x and y):
# R_n * (qx, qy, qz) = (qy, qx, qz)
# Then negate: q_engine = (-(qy), -(qx), -(qz), qw)...
# Wait: conjugate means negate xyz: q_engine = -(qy, qx, qz), qw) = (-qy, -qx, -qz, qw)

# Actually let me be more careful:
# Step 1: Apply M to the axis part: (qx,qy,qz) -> M*(qx,qy,qz) = (qy, qx, qz)
# Step 2: Negate for reflection (reverses rotation): negate xyz -> (-qy, -qx, -qz, qw)

# Let's test this with Point001:
q_ue5_p1 = quat_norm((0.4975, -0.5025, 0.5025, 0.4975))
q_eng_p1_expected = quat_norm((0.7107, 0.0, 0.0, 0.7035))

# Apply: (qx,qy,qz,qw) -> (-qy, -qx, -qz, qw) [reflection through x=y plane]
def remap_reflection_xy(q):
    qx,qy,qz,qw = q
    return quat_norm((-qy, -qx, -qz, qw))

result_p1 = remap_reflection_xy(q_ue5_p1)
print(f"Reflection-xy remap for Point001:")
print(f"  Result:   {tuple(round(x,4) for x in result_p1)}")
print(f"  Expected: {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Also try without the negation (in case handedness is preserved):
def remap_permute_xy(q):
    qx,qy,qz,qw = q
    return quat_norm((qy, qx, qz, qw))

result_p1_perm = remap_permute_xy(q_ue5_p1)
print(f"Permute-xy remap (no negate) for Point001:")
print(f"  Result:   {tuple(round(x,4) for x in result_p1_perm)}")
print(f"  Expected: {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Try the formula from actual coordinate analysis:
# UE5 FBX space -> Engine space
# We need to find the actual rotation that the UE5 quat represents,
# then re-express it in engine frame.

# UE5 FBX Point001 rot matrix in UE5 space:
R_ue5 = quat_to_mat(q_ue5_p1)
print(f"UE5 FBX Point001 rotation matrix:")
print(R_ue5.round(4))

# Transform this to engine space: M * R_ue5 * M^{-1} = M * R_ue5 * M (since M^{-1}=M)
# But M is a REFLECTION matrix, so the transformation formula changes sign of one eigenvalue
# Actually for coordinate change: R_engine = M * R_ue5 * M^T
R_engine_computed = M @ R_ue5 @ M.T
print(f"\nEngine rotation matrix via M * R_ue5 * M^T:")
print(R_engine_computed.round(4))

# Expected engine rotation matrix from q_eng_p1_expected:
R_eng_expected = quat_to_mat(q_eng_p1_expected)
print(f"\nExpected engine rotation matrix:")
print(R_eng_expected.round(4))
print()

# Convert the computed engine matrix back to quaternion
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

q_from_matrix = mat_to_quat(R_engine_computed)
print(f"Quaternion from M * R_ue5 * M^T = {tuple(round(x,4) for x in q_from_matrix)}")
print(f"Expected                          = {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Analyze what happened: M * R_ue5 * M^T may give rotation or improper rotation
# If it gives an improper rotation (det=-1), we need to negate to get proper rotation
R_check = M @ R_ue5 @ M.T
print(f"det(M * R_ue5 * M^T) = {np.linalg.det(R_check):.4f}")
print()

# Let's try: if the matrix has det=-1, negate it:
if abs(np.linalg.det(R_check) + 1.0) < 0.01:
    R_check_neg = -R_check
    q_neg = mat_to_quat(R_check_neg)
    print(f"After negation quaternion: {tuple(round(x,4) for x in q_neg)}")
    print(f"Expected:                  {tuple(round(x,4) for x in q_eng_p1_expected)}")
