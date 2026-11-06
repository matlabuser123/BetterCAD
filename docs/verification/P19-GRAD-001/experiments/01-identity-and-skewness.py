# -*- coding: utf-8 -*-
# P19-GRAD-001, the pre-implementation experiment.
#
# The checklist says "Least-squares gradient IF NEEDED". That word has to be
# discharged by a measurement, not by a preference, and the measurement is pure
# arithmetic -- so it can be made before any C++ exists, as P19-DIFF-001 made
# its decomposition measurement.
#
# FOUR QUESTIONS, each answered on random tetrahedra:
#
#   Q1  Is the identity sum_f C_f (x) S_f = V I true? If it is, Green-Gauss with
#       the TRUE face value at the face centroid is linear-exact on ANY mesh,
#       and any inexactness is the INTERPOLATION's, not Gauss's. That changes
#       what the fix is, so it is worth pinning rather than assuming.
#   Q2  How large is Green-Gauss's error when phi_f comes from the usual
#       weighted interpolation between the two cell centroids?
#   Q3  Does that error track the SKEWNESS ADR-065 measured (up to 6.5 on
#       RM-MESH-03)?
#   Q4  Is least-squares linear-exact on the same configurations, and how well
#       conditioned is its 3x3 normal matrix on a tetrahedron's four
#       neighbours?
import math
import random

random.seed(20261107)  # recorded, as CLAUDE.md requires for any seed


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    return math.sqrt(dot(a, a))


def centroid(points):
    n = float(len(points))
    return (sum(p[0] for p in points) / n, sum(p[1] for p in points) / n, sum(p[2] for p in points) / n)


def tet_volume(a, b, c, d):
    """Positive volume of the tetrahedron abcd."""
    return abs(dot(sub(b, a), cross(sub(c, a), sub(d, a)))) / 6.0


def outward_faces(a, b, c, d):
    """The four faces of tet abcd as (area_vector_outward, face_centroid)."""
    cell = centroid([a, b, c, d])
    out = []
    for (p, q, r) in ((b, c, d), (a, c, d), (a, b, d), (a, b, c)):
        s = scale(cross(sub(q, p), sub(r, p)), 0.5)
        cf = centroid([p, q, r])
        # Orient it out of the cell: it must point away from the cell centroid.
        if dot(s, sub(cf, cell)) < 0.0:
            s = scale(s, -1.0)
        out.append((s, cf))
    return out


def solve3_symmetric(m, rhs):
    """Closed-form solve of a symmetric 3x3 system. Returns (solution, det)."""
    a, b, c = m[0][0], m[0][1], m[0][2]
    d, e = m[1][1], m[1][2]
    f = m[2][2]
    # cofactors of the symmetric matrix [[a,b,c],[b,d,e],[c,e,f]]
    c00 = d * f - e * e
    c01 = c * e - b * f
    c02 = b * e - c * d
    det = a * c00 + b * c01 + c * c02
    if det == 0.0:
        return None, 0.0
    c11 = a * f - c * c
    c12 = b * c - a * e
    c22 = a * d - b * b
    inv = ((c00, c01, c02), (c01, c11, c12), (c02, c12, c22))
    sol = tuple(sum(inv[i][j] * rhs[j] for j in range(3)) / det for i in range(3))
    return sol, det


# ---------------------------------------------------------------------------
print("Q1  the identity  sum_f C_f (x) S_f = V I  on 20000 random tetrahedra")
print("    (if true, Green-Gauss with the TRUE face value is linear-exact")
print("     on any mesh, so any error is the interpolation's)")
worst_identity = 0.0
for _ in range(20000):
    pts = [tuple(random.uniform(-1.0, 1.0) for _ in range(3)) for _ in range(4)]
    v = tet_volume(*pts)
    if v < 1.0e-6:
        continue
    faces = outward_faces(*pts)
    # sum_f C_f (x) S_f, as a 3x3
    acc = [[0.0] * 3 for _ in range(3)]
    for s, cf in faces:
        for i in range(3):
            for j in range(3):
                acc[i][j] += cf[i] * s[j]
    scale_of = sum(norm(s) * norm(cf) for s, cf in faces)
    for i in range(3):
        for j in range(3):
            expect = v if i == j else 0.0
            worst_identity = max(worst_identity, abs(acc[i][j] - expect) / max(scale_of, 1e-300))
print("    worst relative deviation: %.3e   (eps = %.3e)" % (worst_identity, 2.220446049250313e-16))
print("    VERDICT:", "IDENTITY HOLDS to rounding" if worst_identity < 1e-13 else "IDENTITY DOES NOT HOLD")
print()

# ---------------------------------------------------------------------------
print("Q2/Q3/Q4  a cell and four neighbours, an EXACT linear field, three methods")
print()
print("  built so skewness varies: each neighbour centroid is placed off the")
print("  line through the face centroid by a controlled amount.")
print()
print("   skewness    Gauss(true phi_f)   Gauss(interpolated)   least-squares   LSQ cond")
print("   --------    -----------------   -------------------   -------------   --------")

g_exact = (0.37, -1.21, 2.05)       # the gradient every method must return
a_exact = 11.0


def phi(x):
    return a_exact + dot(g_exact, x)


for skew_target in (0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 6.5):
    worst_gauss_true = 0.0
    worst_gauss_interp = 0.0
    worst_lsq = 0.0
    worst_cond = 0.0
    samples = 0
    for _ in range(4000):
        pts = [tuple(random.uniform(-1.0, 1.0) for _ in range(3)) for _ in range(4)]
        v = tet_volume(*pts)
        if v < 1.0e-3:
            continue
        cp = centroid(pts)
        faces = outward_faces(*pts)

        # Place each neighbour centroid. The UNSKEWED placement is the mirror of
        # C_P through the face centroid; skew displaces it in the face plane, so
        # the segment C_P -> C_N no longer passes through C_f. That displacement
        # IS the skewness ADR-065 measures.
        neighbours = []
        for s, cf in faces:
            n = scale(s, 1.0 / norm(s))
            base = add(cf, sub(cf, cp))                      # mirror: no skew
            # a direction inside the face plane
            trial = (0.31, -0.77, 0.56)
            tang = sub(trial, scale(n, dot(trial, n)))
            if norm(tang) < 1e-9:
                continue
            tang = scale(tang, 1.0 / norm(tang))
            d_half = norm(sub(cf, cp))
            cn = add(base, scale(tang, skew_target * d_half))
            neighbours.append((s, cf, cn))
        if len(neighbours) < 4:
            continue
        samples += 1

        # --- Green-Gauss with the TRUE face value --------------------------
        acc = (0.0, 0.0, 0.0)
        for s, cf, cn in neighbours:
            acc = add(acc, scale(s, phi(cf)))
        gg_true = scale(acc, 1.0 / v)
        worst_gauss_true = max(worst_gauss_true, norm(sub(gg_true, g_exact)) / norm(g_exact))

        # --- Green-Gauss with the usual interpolation ----------------------
        # phi_f = w phi_P + (1-w) phi_N, w from the normal projection, which is
        # the form P19-DIFF-001 settled on for the face viscosity.
        acc = (0.0, 0.0, 0.0)
        for s, cf, cn in neighbours:
            n = scale(s, 1.0 / norm(s))
            total = dot(sub(cn, cp), n)
            owner = dot(sub(cf, cp), n)
            w = (total - owner) / total if total != 0.0 else 0.5
            phi_f = w * phi(cp) + (1.0 - w) * phi(cn)
            acc = add(acc, scale(s, phi_f))
        gg_interp = scale(acc, 1.0 / v)
        worst_gauss_interp = max(worst_gauss_interp, norm(sub(gg_interp, g_exact)) / norm(g_exact))

        # --- least squares, inverse-distance weighted ----------------------
        m = [[0.0] * 3 for _ in range(3)]
        rhs = [0.0, 0.0, 0.0]
        for s, cf, cn in neighbours:
            d = sub(cn, cp)
            w = 1.0 / dot(d, d)
            dphi = phi(cn) - phi(cp)
            for i in range(3):
                rhs[i] += w * d[i] * dphi
                for j in range(3):
                    m[i][j] += w * d[i] * d[j]
        sol, det = solve3_symmetric(m, rhs)
        if sol is None:
            continue
        worst_lsq = max(worst_lsq, norm(sub(sol, g_exact)) / norm(g_exact))
        trace = m[0][0] + m[1][1] + m[2][2]
        worst_cond = max(worst_cond, (trace ** 3) / (27.0 * det) if det > 0 else float("inf"))

    print("   %8.2f    %17.2e   %19.2e   %13.2e   %8.1f"
          % (skew_target, worst_gauss_true, worst_gauss_interp, worst_lsq, worst_cond))

print()
print("READ THE COLUMNS, not the headline:")
print("  Gauss(true phi_f)     stays at rounding at EVERY skewness -> Gauss itself")
print("                        is linear-exact; Q1's identity is why")
print("  Gauss(interpolated)   grows with skewness -> the INTERPOLATION is what")
print("                        breaks linear exactness, and a skewness")
print("                        correction needs the gradient it is computing")
print("  least-squares         stays at rounding at every skewness, with no")
print("                        interpolation at all")
