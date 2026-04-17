"""
Expand q_L * (qx,qy,qz,qw) * q_R symbolically to get the component formula.
q_L = (0, 0, s, s) where s = sqrt(2)/2
q_R = (-h, -h, -h, h) where h = 0.5
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
    if n < 1e-10:
        return (0,0,0,1)
    return (x/n, y/n, z/n, w/n)

s = np.sqrt(2)/2  # sin(45) = cos(45)
h = 0.5

q_L = (0, 0, s, s)   # +90 around Z: (0, 0, sin(45), cos(45))
q_R = (-h, -h, -h, h) # q_M_maya_inv = (-0.5, -0.5, -0.5, 0.5)

# Let's expand symbolically by testing with unit basis quaternions:
# q = (1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1)
# and record the output to get the transformation matrix.

print("Symbolic expansion of q_L * q * q_R:")
print("Input -> Output")
print()

# For q = (1,0,0,0):
q = (1,0,0,0)
r = quat_mul(q_L, quat_mul(q, q_R))
print(f"  (1,0,0,0) -> {tuple(round(x,4) for x in r)}")

q = (0,1,0,0)
r = quat_mul(q_L, quat_mul(q, q_R))
print(f"  (0,1,0,0) -> {tuple(round(x,4) for x in r)}")

q = (0,0,1,0)
r = quat_mul(q_L, quat_mul(q, q_R))
print(f"  (0,0,1,0) -> {tuple(round(x,4) for x in r)}")

q = (0,0,0,1)
r = quat_mul(q_L, quat_mul(q, q_R))
print(f"  (0,0,0,1) -> {tuple(round(x,4) for x in r)}")

print()
print("So the transformation matrix (each row = output for basis input) is:")
print("Note: we map (qx,qy,qz,qw) -> (out_x, out_y, out_z, out_w)")

# Build transformation matrix
T = np.zeros((4,4))
for i, q in enumerate([(1,0,0,0),(0,1,0,0),(0,0,1,0),(0,0,0,1)]):
    r = quat_mul(q_L, quat_mul(q, q_R))
    T[i,:] = r

print(T.round(4))
print()

# So the closed-form formula is:
# out_x = T[0][0]*qx + T[1][0]*qy + T[2][0]*qz + T[3][0]*qw
# out_y = T[0][1]*qx + T[1][1]*qy + T[2][1]*qz + T[3][1]*qw
# ... etc

# Let's read off the non-zero entries:
print("Component formula:")
components = ['qx', 'qy', 'qz', 'qw']
out_names = ['out_x', 'out_y', 'out_z', 'out_w']
for j in range(4):
    terms = []
    for i in range(4):
        if abs(T[i,j]) > 0.001:
            coeff = T[i,j]
            if abs(coeff - 1.0) < 0.001:
                terms.append(f"{components[i]}")
            elif abs(coeff + 1.0) < 0.001:
                terms.append(f"-{components[i]}")
            elif abs(coeff - 0.5) < 0.001:
                terms.append(f"0.5*{components[i]}")
            elif abs(coeff + 0.5) < 0.001:
                terms.append(f"-0.5*{components[i]}")
            elif abs(coeff - s) < 0.001:
                terms.append(f"s*{components[i]}")
            elif abs(coeff + s) < 0.001:
                terms.append(f"-s*{components[i]}")
            else:
                terms.append(f"{coeff:.4f}*{components[i]}")
    print(f"  {out_names[j]} = {' + '.join(terms) if terms else '0'}")

print()
print("This is complex (non-axis-aligned). Let's double check with Point001:")

q_ue5_p1 = quat_norm((0.4975, -0.5025, 0.5025, 0.4975))
q_eng_p1_expected = quat_norm((0.7107, 0.0, 0.0, 0.7035))

# Apply T matrix:
q_in = np.array(q_ue5_p1)
q_out = T.T @ q_in  # T stores row i = result for basis i, so T^T maps input to output
q_out_norm = q_out / np.linalg.norm(q_out)
print(f"T^T * q_ue5_p1 = {tuple(round(x,4) for x in q_out_norm)}")
print(f"Expected:        = {tuple(round(x,4) for x in q_eng_p1_expected)}")
print()

# The formula has sqrt(2)/2 = 0.7071 coefficients which means it's not
# a simple permutation/sign-flip formula anymore. This is complex to put into
# the C++ without precomputed constants.

# HOWEVER: we can express this as a quaternion sandwich product directly:
# q_eng = q_L * q_ue5 * q_R
# In C++: just use the quaternion multiplication function.
print("IMPLEMENTATION PLAN:")
print("In C++, compute q_eng = quat_mul(q_L, quat_mul(q_ue5, q_R))")
print("where q_L = (0, 0, sqrt(2)/2, sqrt(2)/2) stored as compile-time constants")
print("and   q_R = (-0.5, -0.5, -0.5, 0.5) stored as compile-time constants")
print()

# Let's also verify for a child bone to make sure.
# We need actual UE5 child bone data. Let's compute what MushroomPelvis world
# quaternion would be in UE5 FBX space, given it's the same rotation as Point001
# at bind pose (they share the same world rotation based on previous analysis).

# From previous: Maya engine world pelvis rot = (0.7107, 0, 0, 0.7035)
# Maya FBX world pelvis rot = (0, 0, 0.7107, 0.7035) [inverse cyclic]
# UE5 FBX world pelvis rot = q_T_left * maya_world_pelvis_rot [T_left applies to full world chain]
q_T_left = quat_norm((0.7071, 0, 0, 0.7071))  # +90 around X
q_maya_world_pelvis = (0, 0, 0.7107, 0.7035)
q_ue5_world_pelvis = quat_norm(quat_mul(q_T_left, q_maya_world_pelvis))
print(f"MushroomPelvis UE5 world rot (derived): {tuple(round(x,4) for x in q_ue5_world_pelvis)}")

# Apply our fix:
q_result_pelvis = quat_norm(quat_mul(q_L, quat_mul(q_ue5_world_pelvis, q_R)))
print(f"After fix:       {tuple(round(x,4) for x in q_result_pelvis)}")
print(f"Expected:        (0.7107, 0, 0, 0.7035)")
print()

# Now test with a deep child bone (Point005)
# Point005 Maya engine world rot = [-0.1971, -0.5441, 0.7668, 0.2778]
# Point005 Maya FBX world rot = inverse cyclic:
# cyclic (x,y,z) -> (z,x,y): out.x = in.z, out.y = in.x, out.z = in.y
# Inverse cyclic: out.x = in.y, out.y = in.z, out.z = in.x
q_maya_engine_p5 = quat_norm((-0.1971, -0.5441, 0.7668, 0.2778))
# engine(x,y,z) = maya(z,x,y) so maya = inverse_cyclic(engine)
q_maya_world_p5 = quat_norm((q_maya_engine_p5[1], q_maya_engine_p5[2], q_maya_engine_p5[0], q_maya_engine_p5[3]))
print(f"Point005 Maya FBX world rot: {tuple(round(x,4) for x in q_maya_world_p5)}")

q_ue5_world_p5 = quat_norm(quat_mul(q_T_left, q_maya_world_p5))
print(f"Point005 UE5 FBX world rot (derived): {tuple(round(x,4) for x in q_ue5_world_p5)}")

q_result_p5 = quat_norm(quat_mul(q_L, quat_mul(q_ue5_world_p5, q_R)))
print(f"After fix: {tuple(round(x,4) for x in q_result_p5)}")
print(f"Expected:  {tuple(round(x,4) for x in q_maya_engine_p5)}")
