"""
Verify the expanded q_L * q * q_R formula component-by-component.
q_L = (0, 0, s, s)  s = sqrt(2)/2
q_R = (-h, -h, -h, h)  h = 0.5
"""
import numpy as np

s = np.sqrt(2)/2
h = 0.5

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

q_L = (0, 0, s, s)
q_R = (-h, -h, -h, h)

def formula_full(q):
    return quat_mul(q_L, quat_mul(q, q_R))

# Compute mid = q * q_R
# a=(qx,qy,qz,qw), b=(-h,-h,-h,h)
# mx = qw*(-h) + qx*h + qy*(-h) - qz*(-h) = h*(-qw + qx - qy + qz)
# my = qw*(-h) - qx*(-h) + qy*h + qz*(-h) = h*(-qw + qx + qy - qz)
# mz = qw*(-h) + qx*(-h) - qy*(-h) + qz*h = h*(-qw - qx + qy + qz)
# mw = qw*h - qx*(-h) - qy*(-h) - qz*(-h) = h*(qw + qx + qy + qz)

def mid(q):
    qx,qy,qz,qw = q
    mx = h*(-qw + qx - qy + qz)
    my = h*(-qw + qx + qy - qz)
    mz = h*(-qw - qx + qy + qz)
    mw = h*( qw + qx + qy + qz)
    return (mx, my, mz, mw)

# out = q_L * mid
# a=(0,0,s,s), b=(mx,my,mz,mw)
# rx = aw*bx + ax*bw + ay*bz - az*by = s*mx + 0 + 0 - s*my = s*(mx-my)
# ry = aw*by - ax*bz + ay*bw + az*bx = s*my - 0 + 0 + s*mx = s*(my+mx)  -- NOTE: s*(mx+my)
# rz = aw*bz + ax*by - ay*bx + az*bw = s*mz + 0 - 0 + s*mw = s*(mz+mw)
# rw = aw*bw - ax*bx - ay*by - az*bz = s*mw - 0 - 0 - s*mz = s*(mw-mz)

def formula_expanded(q):
    qx,qy,qz,qw = q
    mx = h*(-qw + qx - qy + qz)
    my = h*(-qw + qx + qy - qz)
    mz = h*(-qw - qx + qy + qz)
    mw = h*( qw + qx + qy + qz)
    rx = s*(mx - my)
    ry = s*(mx + my)   # note: mx+my, not my+mx (same thing but let's clarify sign)
    rz = s*(mz + mw)
    rw = s*(mw - mz)
    return (rx, ry, rz, rw)

# Expand further (substitute mx, my, mz, mw):
# rx = s*(mx - my) = s * h * ((-qw+qx-qy+qz) - (-qw+qx+qy-qz))
#    = s * h * (-qy+qz - qy+qz) = s * h * (-2qy + 2qz)
#    = s * h * 2 * (qz - qy)
#    = s * (qz - qy)  [since 2h = 1]
#
# ry = s*(mx + my) = s * h * ((-qw+qx-qy+qz) + (-qw+qx+qy-qz))
#    = s * h * (-2qw + 2qx) = s * (qx - qw)
#
# rz = s*(mz + mw) = s * h * ((-qw-qx+qy+qz) + (qw+qx+qy+qz))
#    = s * h * (2qy + 2qz) = s * (qy + qz)
#
# rw = s*(mw - mz) = s * h * ((qw+qx+qy+qz) - (-qw-qx+qy+qz))
#    = s * h * (2qw + 2qx) = s * (qw + qx)

def formula_simplified(q):
    qx,qy,qz,qw = q
    rx = s * (qz - qy)
    ry = s * (qx - qw)
    rz = s * (qy + qz)
    rw = s * (qw + qx)
    return (rx, ry, rz, rw)

# Verify all three formulas match:
test_cases = [
    (0.4975, -0.5025, 0.5025, 0.4975),  # Point001 UE5
    (0.6816, 0.1882, -0.4029, -0.5811), # arbitrary
    (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1),
    (0.5, 0.5, 0.5, 0.5), (-0.3, 0.7, -0.1, 0.6)
]

print("Verifying formula_full == formula_simplified:")
all_ok = True
for q in test_cases:
    qn = tuple(np.array(q) / np.linalg.norm(q))
    r_full = formula_full(qn)
    r_simp = formula_simplified(qn)
    match = np.allclose(r_full, r_simp, atol=1e-6)
    if not match:
        print(f"  MISMATCH for {tuple(round(x,3) for x in qn)}")
        print(f"    full: {tuple(round(x,4) for x in r_full)}")
        print(f"    simp: {tuple(round(x,4) for x in r_simp)}")
        all_ok = False

if all_ok:
    print("  All match!")

print()
print("Verify Point001:")
q_ue5_p1 = tuple(np.array((0.4975, -0.5025, 0.5025, 0.4975)) / np.linalg.norm((0.4975, -0.5025, 0.5025, 0.4975)))
r = formula_simplified(q_ue5_p1)
rn = tuple(np.array(r) / np.linalg.norm(r))
print(f"  result:   {tuple(round(x,4) for x in rn)}")
print(f"  expected: (0.7107, 0, 0, 0.7035)")

print()
print("SIMPLIFIED FORMULA:")
print("  out_x = s * (qz - qy)     where s = sqrt(2)/2 ≈ 0.70710678")
print("  out_y = s * (qx - qw)")
print("  out_z = s * (qy + qz)")
print("  out_w = s * (qw + qx)")
print()
print("Equivalently with s = 1/sqrt(2):")
print("  (qz-qy, qx-qw, qy+qz, qw+qx) * (1/sqrt(2))")
print()
print("This requires renormalization (the output norm is |q|*1 for unit q, verified below):")

# Verify norm preservation:
q = (0.4975, -0.5025, 0.5025, 0.4975)
qn = tuple(np.array(q) / np.linalg.norm(q))
r = formula_simplified(qn)
norm_r = np.linalg.norm(r)
print(f"  Input norm: {np.linalg.norm(qn):.6f}")
print(f"  Output norm: {norm_r:.6f}")
print()
print("Good - output IS unit if input is unit (sandwich of unit quats).")
print()
print("FINAL C++ IMPLEMENTATION:")
print("""
case FbxSrcConvention::MaxZupRH: {
    // Correct two-sided quaternion remap for UE5 FBX (Z-up -Y-front RH) to engine frame.
    // Derivation: FbxAxisSystem::ConvertScene pre-multiplies every bone's world rotation
    // by q_T = +90 deg around X. The correct engine rotation is:
    //   q_eng = q_L * q_ue5 * q_R
    // where q_L = (0, 0, s, s), q_R = (-h, -h, -h, h), s = sqrt(2)/2, h = 0.5.
    // Expanded to closed-form:
    //   q_eng.x = s * (qz - qy)
    //   q_eng.y = s * (qx - qw)
    //   q_eng.z = s * (qy + qz)
    //   q_eng.w = s * (qw + qx)
    constexpr float s = 0.70710678118654752f;  // sqrt(2)/2
    const float ix = static_cast<float>(qx);
    const float iy = static_cast<float>(qy);
    const float iz = static_cast<float>(qz);
    const float iw = static_cast<float>(qw);
    return Quat{s * (iz - iy),
                s * (ix - iw),
                s * (iy + iz),
                s * (iw + ix)};
}
""")
