"""
Analysis v3: Verify the q_T_left correction is universal.
q_T_left = (0.7071, 0, 0, 0.7071) = +90 deg around X axis.
This should map UE5 FBX bone rotations to Maya FBX bone rotations.
"""
import struct
import numpy as np
import sys

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
    if n < 1e-10:
        return (0,0,0,1)
    return (x/n, y/n, z/n, w/n)

def quat_conj(q):
    x,y,z,w = q
    return (-x,-y,-z,w)

# The proposed formula for MaxZupRH quat remap:
# q_eng = q_M_maya * conj(q_T_left) * q_ue5 * q_M_maya_inv
# where q_T_left = (0.7071, 0, 0, 0.7071) = +90 deg around X
# q_M_maya = (0.5, 0.5, 0.5, 0.5) = 120 deg around (1,1,1)/sqrt(3)

# But these are constants! So the combined left-side constant is:
# q_L = q_M_maya * conj(q_T_left)
# And right-side: q_M_maya_inv

q_T_left = quat_norm((0.7071, 0, 0, 0.7071))
q_M_maya = quat_norm((0.5, 0.5, 0.5, 0.5))
q_M_maya_inv = quat_conj(q_M_maya)

q_L = quat_norm(quat_mul(q_M_maya, quat_conj(q_T_left)))
print(f"q_L = q_M_maya * conj(q_T_left) = {tuple(round(x,4) for x in q_L)}")
print(f"q_M_maya_inv                     = {tuple(round(x,4) for x in q_M_maya_inv)}")

# Verify for Point001
q_ue5_p1 = quat_norm((0.4975, -0.5025, 0.5025, 0.4975))
q_eng_p1_expected = quat_norm((0.7107, 0.0, 0.0, 0.7035))

q_result_p1 = quat_norm(quat_mul(q_L, quat_mul(q_ue5_p1, q_M_maya_inv)))
print(f"\nPoint001: q_L * q_ue5 * q_M_maya_inv = {tuple(round(x,4) for x in q_result_p1)}")
print(f"Expected:                               {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Simplify: q_L = q_M_maya * conj(q_T_left)
# Note: conj(q_T_left) = (-0.7071, 0, 0, 0.7071) = -90 deg around X
# q_M_maya = (0.5, 0.5, 0.5, 0.5) = 120 deg around (1,1,1)/sqrt(3)
# q_L = q_M_maya * ((-90 around X))
# Let me compute what rotation q_L and q_M_maya_inv represent together:
# The full transform: q_result = q_L * q_ue5 * q_M_maya_inv

# For this to simplify, let's see if q_L = some simple rotation:
print(f"q_L = {tuple(round(x,4) for x in q_L)}")
# q_L = (0, -sqrt(2)/2, 0, sqrt(2)/2)? That would be -90 around Y.
# Let me compute: q_M_maya * conj(q_T_left)
# conj(q_T_left) = (-0.7071, 0, 0, 0.7071)
# q_M_maya = (0.5, 0.5, 0.5, 0.5)
# Product: (q_M_maya.w * conj_x + q_M_maya.x * conj_w + q_M_maya.y * conj_z - q_M_maya.z * conj_y, ...)

# Actually, let me check: is the combined remap equivalent to cyclic(q) (just the MayaYupRH formula)?
# Because if the child bone locals in UE5 FBX are IDENTICAL to Maya FBX,
# then applying cyclic to them directly should give the same result as applying MaxZupRH formula.

# If child locals are IDENTICAL (q_ue5_child == q_maya_child) AND we want the same engine result,
# then we should apply the SAME remap to both.
# The current Maya remap (cyclic) applied to q_ue5_child gives what we want.
# But the root bone is DIFFERENT (q_ue5_root != q_maya_root due to ConvertScene).

# Let me verify: is q_ue5_child == q_maya_child for non-root bones in this rig?
# From the summary: "most child bones have identical local transforms in both Maya and UE5 FBX files"
# If IDENTICAL, then applying cyclic (MayaYupRH) to them gives the correct engine result.

# For the ROOT bone: q_ue5_root = q_T_left * q_maya_root
# So for root: apply conj(q_T_left) first, then cyclic:
# q_eng_root = cyclic(conj(q_T_left) * q_ue5_root)
#            = q_M_maya * conj(q_T_left) * q_ue5_root * q_M_maya_inv
#            = q_L * q_ue5_root * q_M_maya_inv

# But if we apply the SAME formula (cyclic) to both root and children,
# the root would be WRONG.
# Unless... q_T_left == identity (which it's clearly not: q_T_left = (0.7071,0,0,0.7071)).

# Wait -- let me re-examine the problem. The engine imports:
# 1. Skeleton from Maya FBX: apply MayaYupRH cyclic to each bone's WORLD matrix
#    -> stores inverse bind world matrices
# 2. Animation from UE5 FBX: evaluate WORLD matrices, apply MaxZupRH remap
#    -> compute local from world in engine space
#    -> store local transforms in AnimationClip
#
# At runtime:
# - reconstruct world from locals (TRS walk)
# - palette[i] = inv_bind_world[i] * world[i]
#
# The bind pose engine world (from skeleton import) must equal the animation's
# reconstructed world at frame 0 (the bind frame), otherwise the "bind pose" frame
# would look deformed.

# The root cause: world matrices transformed by MayaYupRH != world matrices by MaxZupRH.
# Both should represent the SAME physical world positions/orientations.

# World matrix = chain of local matrices.
# If child locals are IDENTICAL in Maya and UE5 FBX, AND the root is different,
# then the WORLD matrices for ALL bones differ (since world = root_local * child_local * ...).

# The world matrix difference is entirely due to the root bone difference.
# In WORLD SPACE (ufbx world space, before any remap):
# Maya world_p1 = maya_local_p1 (root, no parent)
# UE5 world_p1 = ue5_local_p1 (root, same -- no parent in UE5 either)
#              = q_T_left * q_maya_p1 * conj(q_T_left) -- conjugation for rotation part

# Maya world_pelvis = maya_local_p1 * maya_local_pelvis  [chain compose]
# UE5 world_pelvis = ue5_local_p1 * ue5_local_pelvis    [= (q_T*q_maya_p1) * maya_local_pelvis]
# Since ue5_local_pelvis == maya_local_pelvis (same data in FBX):
# UE5 world_pelvis = (q_T_left * q_maya_p1) * maya_local_pelvis  [different from Maya]

# So the difference compounds through the hierarchy. This means the correction factor
# is NOT just applied to the root; it affects all bone world rotations when extracted
# via ufbx_evaluate_scene.

# THE FUNDAMENTAL ISSUE:
# We cannot simply "fix the root bone" - the entire world matrix chain from ufbx
# is computed in a space that differs from Maya by q_T_left applied to the root.
# After that, the entire sub-tree is rotated.

# The CORRECT fix must account for this.
# For POSITIONS: the current MaxZupRH remap correctly transforms them (verified).
# For ROTATIONS: needs additional correction.

# From the position data, the current remap IS correct for positions.
# This means the MATRIX portion of the remap is correct.
# For rotations, the current remap = q_M * q * q_M_inv where q_M = (0,0,sqrt(2)/2, sqrt(2)/2).
# The expected remap (to match Maya) = q_M_maya * q_T_left_inv * q * q_T_left * q_M_maya_inv...
# No wait, that's applying the correction to each individual world quat.

# Hmm, but if the FBX already has UE5 world matrices with the T_left baked into the root,
# then every bone's WORLD matrix is T_left-rotated relative to Maya.
# The correction would be: for each world rotation q_world_ue5:
#   q_world_corrected = conj(q_T_left) * q_world_ue5

# Let's test: Point001 is the root. Its UE5 world = UE5 local = q_T_left * q_maya_p1.
# q_world_corrected_p1 = conj(q_T_left) * q_T_left * q_maya_p1 = q_maya_p1 ✓

# For MushroomPelvis:
# UE5 world_pelvis in FBX space = compose(q_T_left * q_maya_p1, q_maya_pelvis)
# In ufbx, this is just the full world matrix.
# q_world_ue5_pelvis = q_T_left * q_maya_p1 * q_maya_pelvis  [rotation part of compose]
#                    = q_T_left * q_maya_world_pelvis
# q_corrected_pelvis = conj(q_T_left) * q_world_ue5_pelvis = q_maya_world_pelvis ✓

# For ANY bone: q_corrected = conj(q_T_left) * q_world_ue5 = q_world_maya
# The T_left factor cancels out!
#
# Then: q_engine = cyclic(q_maya_world) = q_M_maya * q_maya_world * q_M_maya_inv
# = q_M_maya * conj(q_T_left) * q_world_ue5 * q_M_maya_inv

# Combined: q_engine = q_L * q_world_ue5 * q_M_maya_inv
# where q_L = q_M_maya * conj(q_T_left)
print(f"VERIFICATION PLAN: q_L = q_M_maya * conj(q_T_left)")
print(f"  q_T_left = {tuple(round(x,4) for x in q_T_left)}")
print(f"  q_L = {tuple(round(x,4) for x in q_L)}")
print()

# Now, the position remap must also account for T_left.
# For positions: ue5_world_pos = T_left * maya_world_pos (as a vector rotation)
# Where T_left = +90 around X in FBX Maya space.
# T_left rotation matrix:
import numpy as np
def quat_to_mat(q):
    x,y,z,w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]
    ])

R_T_left = quat_to_mat(q_T_left)
print(f"R_T_left matrix (what the root FbxAxisSystem transform does):")
print(R_T_left.round(4))
print()

# For positions: ue5_pos = R_T_left * maya_pos
# But wait - we established that positions ARE correct with current remap.
# So the position formula must already be implicitly correct.

# For Point001 position = (0, 0.0859, 78.3807) in both Maya and UE5 engine space.
# That's because the ROOT position in this rig is ~zero lateral offset.
# Let me check with a bone that has significant lateral position.

# Point005 world position:
# Maya FBX: (maya_x, maya_y, maya_z) = (engine_y, engine_z, engine_x) = (-11.7134, 71.0067, -10.1318)
# UE5 FBX world pos = R_T_left * maya_FBX_pos:
maya_world_pos_p5 = np.array([-11.7134, 71.0067, -10.1318])  # in Maya FBX space
ue5_world_pos_p5 = R_T_left @ maya_world_pos_p5
print(f"Point005 UE5 FBX world pos (derived): {ue5_world_pos_p5.round(4)}")

# Now apply current MaxZupRH remap (-y, x, z) to ue5_world_pos:
remap_pos = np.array([-ue5_world_pos_p5[1], ue5_world_pos_p5[0], ue5_world_pos_p5[2]])
print(f"After current MaxZupRH (-y,x,z):     {remap_pos.round(4)}")
print(f"Expected Maya engine world pos P5:   [-10.1318, -11.7134, 71.0067]")
print()

# So positions still work! Even though T_left rotates the world, the MaxZupRH remap
# correctly maps the (T_left-rotated) UE5 positions to engine positions.
# Let me verify this algebraically:
# engine_pos = M_max * ue5_pos = M_max * R_T_left * maya_pos
# Should equal: M_maya * maya_pos
# Need: M_max * R_T_left = M_maya?

M_max = np.array([[0,-1,0],[1,0,0],[0,0,1]], dtype=float)
M_maya_mat = np.array([[0,0,1],[1,0,0],[0,1,0]], dtype=float)
R_T_left_mat = quat_to_mat(q_T_left)

check = M_max @ R_T_left_mat
print(f"M_max * R_T_left =")
print(check.round(4))
print(f"M_maya =")
print(M_maya_mat)
print(f"Are they equal? {np.allclose(check, M_maya_mat, atol=0.001)}")
print()

# So M_max * R_T_left = M_maya! That's why positions work!
# For ROTATIONS, we have:
# current: q_M * q_ue5_world * q_M_inv (where q_M is +90 around Z)
# but the UE5 world rotation = q_T_left * q_maya_world
# so current gives: q_M * q_T_left * q_maya_world * q_M_inv
# we NEED: q_M_maya * q_maya_world * q_M_maya_inv
# The relation: q_M * q_T_left = q_M_maya?

q_M = quat_norm((0, 0, np.sqrt(2)/2, np.sqrt(2)/2))
q_product = quat_norm(quat_mul(q_M, q_T_left))
print(f"q_M * q_T_left = {tuple(round(x,4) for x in q_product)}")
print(f"q_M_maya =       {tuple(round(x,4) for x in q_M_maya)}")
print(f"Are they equal? {np.allclose(np.abs(np.array(q_product)), np.abs(np.array(q_M_maya)), atol=0.001)}")
print()

# If q_M * q_T_left = q_M_maya, then:
# current: q_M * q_T_left * q_maya_world * q_M_inv
#        = q_M_maya * q_maya_world * q_M_inv
# but we need: q_M_maya * q_maya_world * q_M_maya_inv
# The difference: q_M_inv != q_M_maya_inv (unless q_M = q_M_maya)
# q_M = (0, 0, sqrt(2)/2, sqrt(2)/2) -- 90 around Z
# q_M_maya = (0.5, 0.5, 0.5, 0.5) -- 120 around (1,1,1)/sqrt(3)
# These are different quaternions.

# So current = q_M_maya * q_maya_world * q_M_inv
# Need      = q_M_maya * q_maya_world * q_M_maya_inv
# The right-hand side differs: q_M_inv vs q_M_maya_inv
# Extra right-side factor = q_M_maya_inv * q_M = conj(q_M_maya) * q_M

q_right_correction = quat_norm(quat_mul(q_M_maya_inv, q_M))
print(f"Right correction = conj(q_M_maya) * q_M = {tuple(round(x,4) for x in q_right_correction)}")

# So: correct = current_result * q_right_correction?
# No: correct = q_M_maya * q_maya_world * q_M_maya_inv
#             = q_M_maya * q_maya_world * q_M_inv * q_M * q_M_maya_inv
#             = (q_M * q_T_left * q_maya_world * q_M_inv) * (q_M * q_M_maya_inv)
# Wait this is getting complicated. Let me just state the formula directly:

# Correct MaxZupRH remap:
# q_eng = q_M_maya * conj(q_T_left) * q_ue5_world * q_M_maya_inv
# Since q_ue5_world = q_T_left * q_maya_world:
# = q_M_maya * conj(q_T_left) * q_T_left * q_maya_world * q_M_maya_inv
# = q_M_maya * q_maya_world * q_M_maya_inv ✓

# Now, how does this decompose in terms of the raw q_ue5?
# q_L = q_M_maya * conj(q_T_left)
# q_eng = q_L * q_ue5 * q_M_maya_inv

# Can we express q_L in terms of simple components?
print(f"\nq_L = {tuple(round(x,4) for x in q_L)}")
# If q_L = (a, b, c, d), and q_M_maya_inv = (-0.5, -0.5, -0.5, 0.5)
print(f"q_M_maya_inv = {tuple(round(x,4) for x in q_M_maya_inv)}")

# Let's figure out what simple rotation these are:
def quat_to_axis_angle(q):
    x,y,z,w = q
    angle = 2 * np.arccos(min(1.0, abs(w)))
    s = np.sqrt(1 - w*w)
    if s < 0.001:
        return (1,0,0), 0
    return (x/s, y/s, z/s), np.degrees(angle)

axis_L, angle_L = quat_to_axis_angle(q_L)
print(f"\nq_L rotation: {angle_L:.1f} degrees around {tuple(round(x,3) for x in axis_L)}")

axis_Mmaya_inv, angle_Mmaya_inv = quat_to_axis_angle(q_M_maya_inv)
print(f"q_M_maya_inv rotation: {angle_Mmaya_inv:.1f} degrees around {tuple(round(x,3) for x in axis_Mmaya_inv)}")

# The formula q_eng = q_L * q_ue5 * q_M_maya_inv in component form:
# Let's expand symbolically:
# q_L = some constant quaternion (L_x, L_y, L_z, L_w)
# q_M_maya_inv = (-0.5, -0.5, -0.5, 0.5)

print(f"\nFINAL FORMULA:")
print(f"MaxZupRH quat remap: q_eng = q_L * q_ue5 * q_R")
print(f"  q_L = {tuple(round(x,6) for x in q_L)}")
print(f"  q_R = q_M_maya_inv = {tuple(round(x,6) for x in q_M_maya_inv)}")
print()

# Verify once more with Point001:
q_result_p1_v2 = quat_norm(quat_mul(q_L, quat_mul(q_ue5_p1, q_M_maya_inv)))
print(f"Point001 result: {tuple(round(x,4) for x in q_result_p1_v2)}")
print(f"Point001 expected: {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# Now verify for a non-root bone.
# For a non-root bone where child locals are IDENTICAL in Maya and UE5:
# UE5 FBX child local = Maya FBX child local (same quaternion)
# UE5 FBX world = compose(root_ue5_world, child_local_ue5)
#               = compose(q_T_left * q_maya_root, q_maya_child_local)
# ue5_world_child_rot = q_T_left * q_maya_root_rot * q_maya_child_local_rot
#                     = q_T_left * q_maya_world_child_rot
# So q_L * q_ue5_child * q_M_maya_inv
# = q_L * q_T_left * q_maya_world_child * q_M_maya_inv
# = q_M_maya * conj(q_T_left) * q_T_left * q_maya_world_child * q_M_maya_inv
# = q_M_maya * q_maya_world_child * q_M_maya_inv
# = cyclic(q_maya_world_child) ✓
print("The formula works for ALL bones (root and children) because:")
print("  child world_ue5 = q_T_left * maya_world_child")
print("  q_L * (q_T_left * q_maya_world) * q_R")
print("  = q_M_maya * conj(q_T_left) * q_T_left * q_maya_world * q_M_maya_inv")
print("  = q_M_maya * q_maya_world * q_M_maya_inv = cyclic formula")
print()

# SCALE FIX:
print("=" * 60)
print("SCALE FIX")
print("=" * 60)
# Scale magnitudes transform as position magnitudes.
# Position: (-y, x, z) -- first component gets negated.
# Scale should NOT be negated: scales are always positive magnitudes.
# The correct scale remap: (|y|, |x|, z) = (y, x, z) when y>=0 and x>=0
# But actually for non-uniform scale in arbitrary direction:
# Scale (1,1,1) -> should stay (1,1,1)
# Current: (1,1,1) -> (-1, 1, 1) ← BUG
# Fix: (1,1,1) -> (1, 1, 1) = (y, x, z) for the MaxZupRH case
#
# More precisely: scale is absolute, sign doesn't represent direction.
# The magnitude of scale in each axis should just be permuted without sign change.
# MaxZupRH pos: engine_x = -ue5_y, engine_y = ue5_x, engine_z = ue5_z
# Scale magnitudes: |engine_x| = |ue5_y|, |engine_y| = |ue5_x|, |engine_z| = |ue5_z|
# So scale remap: (abs(y), abs(x), z) or equivalently for positive scales: (y, x, z)
print("Scale should use absolute values of axis components:")
print("MaxZupRH scale: (|sy|, |sx|, sz) = (sy, sx, sz) for positive FBX scales")
print("Current (wrong): (-sy, sx, sz)")
print()

# Summary of fixes needed:
print("=" * 60)
print("SUMMARY OF FIXES:")
print("=" * 60)
print()
print("1. RemapSrcQuat for MaxZupRH:")
print("   Current: (-qy, qx, qz, qw)  [= q_M * q * q_M_inv]")
print(f"   Fix:     q_L * q * q_M_maya_inv")
print(f"   where q_L    = {tuple(round(x,6) for x in q_L)}")
print(f"   where q_R    = q_M_maya_inv = {tuple(round(x,6) for x in q_M_maya_inv)}")
print()
print("2. RemapSrcScale for MaxZupRH:")
print("   Current: RemapSrcVec3(sx, sy, sz) = (-sy, sx, sz)")
print("   Fix:     (sy, sx, sz)  [= abs values, no sign flip]")
