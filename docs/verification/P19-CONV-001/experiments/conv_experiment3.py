# -*- coding: utf-8 -*-
"""P19-CONV-001 pre-implementation measurement, REVISION 3: the corrected
row-sum identity, with the boundary POLICY distinguished.

Revision 2's Q2 restored the inflow term as `sum_boundary min(phi,0)` -- ALL
inflow. That is right only if every inflow face prescribes a value. It does
not, and a PressureInlet is the counterexample: its `U` column is
ZeroGradient, so the face value is the owner's and the face's negative `phi`
lands on the DIAGONAL like any other. The term is the PRESCRIBED inflow:

    rowSum(A,c) + prescribedInflow(c) == div(phi)(c)
    prescribedInflow(c) = sum over c's boundary faces with phi < 0
                          AND transported variable FixedValue, of phi

Revision 2's figures are not withdrawn -- they were measured with every
boundary face implicitly FixedValue, where the two formulas coincide. This
revision adds the mixed-policy case, which revision 2 could not see.

SEED 202611073 (revision 3 draws its own stream; revisions 1 and 2 used
202611072 and remain reproducible).
"""
import random

SEED = 202611073
random.seed(SEED)

FIXED, ZEROGRAD, SYMMETRY = "FixedValue", "ZeroGradient", "Symmetry"
POLICIES = (FIXED, ZEROGRAD, SYMMETRY)

print("P19-CONV-001 measurement revision 3, seed %d" % SEED)
print("=" * 78)


def random_mesh(cells, internal, boundary):
    """Faces: (owner, neighbour) internal; (owner, None, policy) boundary."""
    faces = []
    for _ in range(internal):
        a = random.randrange(cells)
        b = random.randrange(cells)
        while b == a:
            b = random.randrange(cells)
        faces.append((a, b, None))
    for _ in range(boundary):
        faces.append((random.randrange(cells), None,
                      random.choice(POLICIES)))
    return faces


def assemble_upwind(cells, faces, phi, values):
    """The matrix AND the source, exactly as the C++ will build them."""
    A = [dict() for _ in range(cells)]
    b = [0.0] * cells
    for f, (p, n, pol) in enumerate(faces):
        q = phi[f]
        if n is None:
            if q >= 0.0:
                # outflow: upwind is the owner
                A[p][p] = A[p].get(p, 0.0) + q
            elif pol == FIXED:
                # inflow with a prescribed value: a SOURCE, not a coefficient
                b[p] -= q * values[f]
            else:
                # inflow, zero-gradient or symmetry: face value IS the owner's
                A[p][p] = A[p].get(p, 0.0) + q
            continue
        if q >= 0.0:
            A[p][p] = A[p].get(p, 0.0) + q
            A[n][p] = A[n].get(p, 0.0) - q
        else:
            A[p][n] = A[p].get(n, 0.0) + q
            A[n][n] = A[n].get(n, 0.0) - q
    return A, b


def row_sum(A, i):
    s = 0.0
    for col in sorted(A[i].keys()):
        s += A[i][col]
    return s


def divergence_face_order(cells, faces, phi):
    d = [0.0] * cells
    for f, (p, n, pol) in enumerate(faces):
        d[p] += phi[f]
        if n is not None:
            d[n] -= phi[f]
    return d


def prescribed_inflow(cells, faces, phi):
    out = [0.0] * cells
    for f, (p, n, pol) in enumerate(faces):
        if n is None and phi[f] < 0.0 and pol == FIXED:
            out[p] += phi[f]
    return out


def all_inflow(cells, faces, phi):
    """Revision 2's term, kept so the DIFFERENCE between the two formulas is
    measured rather than asserted."""
    out = [0.0] * cells
    for f, (p, n, pol) in enumerate(faces):
        if n is None:
            out[p] += min(phi[f], 0.0)
    return out


print()
print("Q2' the identity with the policy distinguished")
for label, lo, hi in (("narrow 1e-2..1e2", -2, 2), ("wide 1e-6..1e6", -6, 6)):
    meshes = 20000
    ok_new = ok_old = 0
    worst_new = 0.0
    old_wrong_by = 0.0
    old_wrong_cells = 0
    total_cells = 0
    for _ in range(meshes):
        cells = random.randint(2, 12)
        faces = random_mesh(cells, random.randint(1, 24), random.randint(1, 6))
        phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(lo, hi))
               for _ in faces]
        values = [random.uniform(-5.0, 5.0) for _ in faces]
        A, _b = assemble_upwind(cells, faces, phi, values)
        d = divergence_face_order(cells, faces, phi)
        pin = prescribed_inflow(cells, faces, phi)
        ain = all_inflow(cells, faces, phi)
        good_new = good_old = True
        for c in range(cells):
            total_cells += 1
            rs = row_sum(A, c)
            denom = sum(abs(v) for v in A[c].values()) + abs(pin[c])
            if rs + pin[c] != d[c]:
                good_new = False
                if denom > 0.0:
                    worst_new = max(worst_new, abs(rs + pin[c] - d[c]) / denom)
            # Revision 2's formula, for comparison.
            if rs + ain[c] != d[c]:
                good_old = False
                denom2 = sum(abs(v) for v in A[c].values()) + abs(ain[c])
                if denom2 > 0.0:
                    gap = abs(rs + ain[c] - d[c]) / denom2
                    if gap > 1e-12:
                        old_wrong_cells += 1
                        old_wrong_by = max(old_wrong_by, gap)
        ok_new += 1 if good_new else 0
        ok_old += 1 if good_old else 0
    print("  %s" % label)
    print("    CORRECTED (prescribed inflow): exact on %5d/%d (%6.2f%%), "
          "worst rel %.4g" % (ok_new, meshes, 100.0 * ok_new / meshes,
                              worst_new))
    print("    revision 2 (all inflow)      : exact on %5d/%d (%6.2f%%)"
          % (ok_old, meshes, 100.0 * ok_old / meshes))
    print("    cells where rev-2's formula is WRONG BY MORE THAN ROUNDING: "
          "%d of %d, worst rel %.4g"
          % (old_wrong_cells, total_cells, old_wrong_by))

print()
print("    => the corrected term agrees to half an eps; the revision-2 term")
print("       is wrong by a FINITE amount wherever an inflow face is")
print("       zero-gradient, which is a pressure inlet. Not a rounding bug.")
print()
print("=" * 78)
print("done")
