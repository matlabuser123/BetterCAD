# -*- coding: utf-8 -*-
"""P19-CONV-001 pre-implementation measurement, REVISION 2.

Revision 1 (conv_experiment.py, kept) had three defects of its own, and the
first of them is a DESIGN finding rather than a measurement bug:

 (i) Q2 compared the matrix row sum against the divergence of phi and reported
     a "relative gap" of 4.34e+13. That is not rounding. At an INFLOW BOUNDARY
     face the convective flux is a SOURCE, not a coefficient, so the row sum
     never contains it:
         rowSum(A,c) = sum_internal ±phi + sum_boundary max(phi,0)
         div(c)      = sum_internal ±phi + sum_boundary      phi
     The two differ by sum_boundary min(phi,0), the inflow. Revision 1's
     oracle was therefore comparing two different quantities, and the absurd
     figure was a real difference divided by a small row magnitude. The
     cross-check against DIV-001 needs the inflow term restored, which is why
     the operator will REPORT it.

(ii) Q4's `worst_rel4` was only assigned inside the `> 1e-14` branch, so
     "worst relative departure: 0" meant "nothing exceeded the threshold", not
     "always exactly equal". A variable that records only failures, printed as
     if it were the worst case, is the same family of defect as a hand-typed
     count standing beside a computed one. Now recorded unconditionally.

(iii) Q3b drew fluxes across twelve orders of magnitude, so its relative
      figure measured CONDITIONING rather than the identity. Both a narrow and
      a wide range are reported now, separately, so the two are not conflated.

SEED 202611072, unchanged, so revision 1's figures are reproducible alongside.
"""
import random

SEED = 202611072
random.seed(SEED)

print("P19-CONV-001 pre-implementation measurement, revision 2, seed %d" % SEED)
print("=" * 78)


def random_mesh(cells, internal, boundary):
    faces = []
    for _ in range(internal):
        a = random.randrange(cells)
        b = random.randrange(cells)
        while b == a:
            b = random.randrange(cells)
        faces.append((a, b))
    for _ in range(boundary):
        faces.append((random.randrange(cells), None))
    return faces


def assemble_upwind(cells, faces, phi):
    A = [dict() for _ in range(cells)]
    for f, (p, n) in enumerate(faces):
        q = phi[f]
        if n is None:
            A[p][p] = A[p].get(p, 0.0) + max(q, 0.0)
            continue
        if q >= 0.0:
            A[p][p] = A[p].get(p, 0.0) + q
            A[n][p] = A[n].get(p, 0.0) - q
        else:
            A[p][n] = A[p].get(n, 0.0) + q
            A[n][n] = A[n].get(n, 0.0) - q
    return A


def row_sum(A, i):
    s = 0.0
    for col in sorted(A[i].keys()):
        s += A[i][col]
    return s


def divergence_face_order(cells, faces, phi):
    d = [0.0] * cells
    for f, (p, n) in enumerate(faces):
        d[p] += phi[f]
        if n is not None:
            d[n] -= phi[f]
    return d


def boundary_inflow(cells, faces, phi):
    """sum over c's BOUNDARY faces of min(phi,0): what the source took and the
    matrix did not. Negative or zero."""
    b = [0.0] * cells
    for f, (p, n) in enumerate(faces):
        if n is None:
            b[p] += min(phi[f], 0.0)
    return b


def draw(faces, lo, hi):
    return [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(lo, hi))
            for _ in faces]


# ---------------------------------------------------------------------------
# Q2 (corrected). Two separate questions.
#   Q2a: rowSum(A,c) vs the quantity it IS -- the interior-upwind outflow.
#        Pure accumulation order: ascending column vs face index.
#   Q2b: rowSum(A,c) + boundaryInflow(c) vs div(c). The cross-check against
#        DIV-001, now comparing the same quantity.
# ---------------------------------------------------------------------------
print()
print("Q2  the row-sum oracle, with the inflow term restored")
for label, lo, hi in (("narrow 1e-2..1e2", -2, 2), ("wide 1e-6..1e6", -6, 6)):
    meshes = 20000
    exact_a = exact_b = 0
    worst_a = worst_b = 0.0
    for _ in range(meshes):
        cells = random.randint(2, 12)
        faces = random_mesh(cells, random.randint(1, 24), random.randint(1, 6))
        phi = draw(faces, lo, hi)
        A = assemble_upwind(cells, faces, phi)
        d = divergence_face_order(cells, faces, phi)
        bin_ = boundary_inflow(cells, faces, phi)
        ok_a = ok_b = True
        for c in range(cells):
            rs = row_sum(A, c)
            scale = sum(abs(v) for v in A[c].values())
            # Q2a: the SAME quantity the row sum is, accumulated in FACE
            # order instead of ascending-column order. Each face contributes
            # what it wrote to this cell's row: the owner arm writes the
            # upwind coefficient, the neighbour arm writes its negation, and a
            # boundary face writes only its outflow part.
            want_a = 0.0
            for f, (p, n) in enumerate(faces):
                q = phi[f]
                if n is None:
                    if p == c:
                        want_a += max(q, 0.0)
                else:
                    if p == c:
                        want_a += q if q >= 0.0 else q
                    if n == c:
                        want_a += -q
            if rs != want_a:
                ok_a = False
                if scale > 0.0:
                    worst_a = max(worst_a, abs(rs - want_a) / scale)
            # Q2b: the cross-check proper.
            lhs = rs + bin_[c]
            denom = sum(abs(v) for v in A[c].values()) + abs(bin_[c])
            if lhs != d[c]:
                ok_b = False
                if denom > 0.0:
                    worst_b = max(worst_b, abs(lhs - d[c]) / denom)
        exact_a += 1 if ok_a else 0
        exact_b += 1 if ok_b else 0
    print("  %s" % label)
    print("    Q2a rowSum vs same quantity, face order : exact on %5d/%d"
          " (%6.2f%%), worst rel %.4g"
          % (exact_a, meshes, 100.0 * exact_a / meshes, worst_a))
    print("    Q2b rowSum + inflow vs div(phi)         : exact on %5d/%d"
          " (%6.2f%%), worst rel %.4g"
          % (exact_b, meshes, 100.0 * exact_b / meshes, worst_b))

# ---------------------------------------------------------------------------
# Q3 (corrected ranges). A(-phi) == A(phi)^T - diag(div phi).
# ---------------------------------------------------------------------------
print()
print("Q3  reverse flow:  A(-phi) == A(phi)^T - diag(div phi)")
chains = 2000
chain_exact = 0
for _ in range(chains):
    n = random.randint(3, 40)
    faces = [(i, i + 1) for i in range(n - 1)]
    q = random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-6, 6))
    phi = [q] * len(faces)
    A_f = assemble_upwind(n, faces, phi)
    A_r = assemble_upwind(n, faces, [-p for p in phi])
    worst = 0.0
    for i in range(1, n - 1):
        cols = set(A_f[i].keys()) | set(A_r[i].keys())
        for j in cols:
            worst = max(worst, abs(A_r[i].get(j, 0.0) - A_f[j].get(i, 0.0)))
    if worst == 0.0:
        chain_exact += 1
print("    3a 1D chain, uniform flux, INTERIOR rows (div is bit-zero):")
print("       %d chains, transpose BIT-EXACT on %d (%.2f%%)"
      % (chains, chain_exact, 100.0 * chain_exact / chains))

for label, lo, hi in (("narrow 1e-2..1e2", -2, 2), ("wide 1e-6..1e6", -6, 6)):
    gen = 5000
    gen_exact = 0
    worst_rel = 0.0
    for _ in range(gen):
        cells = random.randint(2, 10)
        faces = random_mesh(cells, random.randint(1, 20), random.randint(0, 4))
        phi = draw(faces, lo, hi)
        A_f = assemble_upwind(cells, faces, phi)
        A_r = assemble_upwind(cells, faces, [-p for p in phi])
        d = divergence_face_order(cells, faces, phi)
        ok = True
        for i in range(cells):
            cols = set(A_f[i].keys()) | set(A_r[i].keys()) | {i}
            scale = max([abs(v) for v in A_f[i].values()]
                        + [abs(v) for v in A_r[i].values()] + [abs(d[i])])
            for j in cols:
                lhs = A_r[i].get(j, 0.0)
                rhs = A_f[j].get(i, 0.0) - (d[i] if i == j else 0.0)
                if lhs != rhs:
                    ok = False
                    if scale > 0.0:
                        worst_rel = max(worst_rel, abs(lhs - rhs) / scale)
        gen_exact += 1 if ok else 0
    print("    3b general mesh, %s: exact on %d/%d (%.2f%%), worst rel %.4g"
          % (label, gen_exact, gen, 100.0 * gen_exact / gen, worst_rel))

# ---------------------------------------------------------------------------
# Q4 (corrected). Dominance deficit vs the divergence, worst recorded
# UNCONDITIONALLY, and with no boundary faces so the two are the same
# quantity (Q2's finding applied).
# ---------------------------------------------------------------------------
print()
print("Q4  boundedness: dominance deficit == -divergence (interior only)")
for label, lo, hi in (("narrow 1e-2..1e2", -2, 2), ("wide 1e-6..1e6", -6, 6)):
    q4 = 8000
    bit_exact = 0
    worst = 0.0
    for _ in range(q4):
        cells = random.randint(2, 10)
        faces = random_mesh(cells, random.randint(1, 20), 0)
        phi = draw(faces, lo, hi)
        A = assemble_upwind(cells, faces, phi)
        d = divergence_face_order(cells, faces, phi)
        ok = True
        for i in range(cells):
            diag = A[i].get(i, 0.0)
            off = sum(abs(v) for j, v in A[i].items() if j != i)
            deficit = off - abs(diag)
            scale = off + abs(diag)
            if scale == 0.0:
                continue
            gap = abs(deficit - (-d[i])) / scale     # recorded ALWAYS now
            worst = max(worst, gap)
            if deficit != -d[i]:
                ok = False
        bit_exact += 1 if ok else 0
    print("    %s: deficit BIT-EQUAL to -div on %d/%d (%.2f%%), worst rel %.4g"
          % (label, bit_exact, q4, 100.0 * bit_exact / q4, worst))
print("    => upwind is WEAKLY diagonally dominant exactly when phi is")
print("       conservative; the deficit IS the divergence, so the boundedness")
print("       policy points at DIV-001's measured imbalance, not a new number.")

print()
print("=" * 78)
print("done")
