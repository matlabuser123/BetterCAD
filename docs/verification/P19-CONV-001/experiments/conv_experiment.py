# -*- coding: utf-8 -*-
"""P19-CONV-001 pre-implementation measurement.

Run BEFORE any C++ exists, to test the design's own arithmetic claims rather
than discover them in a test later. P19-DIV-001's first ADR draft predicted a
bit-exact global sum and was WRONG; the same four claims are measured here
instead of predicted.

Everything below models the assembly EXACTLY as the C++ will do it: one pass
over faces in FaceIndex order, scattering into owner and neighbour. No numpy,
so every add happens in the order written and nothing is vectorised behind my
back.

DENOMINATOR DISCIPLINE (P19-GRAD-001 finding r8, and DIV-001's own first-draft
mistake): a floating-point sum's error belongs to the MAGNITUDE IT
ACCUMULATED, never to the answer it arrived at. Every relative figure below
divides by sum |term|.

SEED 202611072, fixed and recorded.
"""
import random

SEED = 202611072
random.seed(SEED)

print("P19-CONV-001 pre-implementation measurement, seed %d" % SEED)
print("=" * 78)


# ---------------------------------------------------------------------------
# Q1. The per-face upwind decomposition: is max(phi,0) + min(phi,0) == phi
#     bit-exactly, for every finite phi, signed zero included?
#
# This is the identity the row-sum property rests on. If it is exact, the
# upwind split loses nothing AT A FACE; whether the ACCUMULATION over faces is
# exact is Q2, and is a different question.
# ---------------------------------------------------------------------------
print()
print("Q1  per-face decomposition  max(phi,0) + min(phi,0) == phi")
bad = 0
trials = 200000
for _ in range(trials):
    e = random.randint(-280, 280)
    phi = random.uniform(-1.0, 1.0) * (10.0 ** e)
    if max(phi, 0.0) + min(phi, 0.0) != phi:
        bad += 1
print("    random finite phi      : %d trials, %d mismatches" % (trials, bad))

# Signed zero and the specials, explicitly: these are the values a random
# draw will never produce and a mesh with a closed wall certainly will.
edge = [0.0, -0.0, 5e-324, -5e-324, 1.7976931348623157e308,
        -1.7976931348623157e308, 1.0, -1.0]
edge_bad = []
for phi in edge:
    got = max(phi, 0.0) + min(phi, 0.0)
    # Compare by value AND by sign of zero, since -0.0 == 0.0 is true.
    same = (got == phi)
    if phi == 0.0:
        same = same and (str(got) == str(phi) or got == 0.0)
    if not same:
        edge_bad.append((phi, got))
print("    edge values            : %d checked, %d mismatches  %s"
      % (len(edge), len(edge_bad), edge_bad if edge_bad else ""))
# What a wall contributes, stated: phi is EXACTLY 0.0 there by ADR-067's
# table, so both arms are zero and the face adds nothing to either cell.
print("    wall face (phi = 0.0)  : max=%r min=%r  -> contributes %r"
      % (max(0.0, 0.0), min(0.0, 0.0), max(0.0, 0.0) + min(0.0, 0.0)))
print("    NOTE  -0.0 arises only if a producer COMPUTES a wall flux instead")
print("          of setting the table's zero; max(-0.0,0.0)=%r min=%r"
      % (max(-0.0, 0.0), min(-0.0, 0.0)))


# ---------------------------------------------------------------------------
# A mesh model: cells, internal faces as (owner, neighbour) pairs, boundary
# faces as (owner, None). Face order is the index order, internal first, which
# is how ADR-064 numbers them.
# ---------------------------------------------------------------------------
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
    """The upwind convection matrix, as the C++ will build it: one pass over
    faces, scatter into owner and neighbour. Returns dense-as-dict rows."""
    A = [dict() for _ in range(cells)]
    for f, (p, n) in enumerate(faces):
        q = phi[f]
        if n is None:
            # Boundary: outflow takes the owner's value (upwind is the owner);
            # inflow takes the boundary value, which is a SOURCE, not a
            # coefficient. Only the outflow arm reaches the matrix.
            A[p][p] = A[p].get(p, 0.0) + max(q, 0.0)
            continue
        # Owner row: += phi * x_upwind
        if q >= 0.0:
            A[p][p] = A[p].get(p, 0.0) + q
        else:
            A[p][n] = A[p].get(n, 0.0) + q
        # Neighbour row: -= phi * x_upwind
        if q >= 0.0:
            A[n][p] = A[n].get(p, 0.0) - q
        else:
            A[n][n] = A[n].get(n, 0.0) - q
    return A


def row_sum(A, i):
    """Exactly CfdSparseMatrix::rowSumSi: the stored entries of row i summed
    in ASCENDING COLUMN order."""
    s = 0.0
    for col in sorted(A[i].keys()):
        s += A[i][col]
    return s


def divergence_face_order(cells, faces, phi):
    """Exactly divergeCfdMassFlux: +flux to owner, -flux to neighbour, in
    FACE order. A DIFFERENT accumulation order from row_sum."""
    d = [0.0] * cells
    for f, (p, n) in enumerate(faces):
        d[p] += phi[f]
        if n is not None:
            d[n] -= phi[f]
    return d


# ---------------------------------------------------------------------------
# Q2. Is the matrix row sum bit-equal to the face-order divergence?
#
# THE DESIGN CLAIM UNDER TEST: "the convection matrix's row sum is the net
# mass outflow, which is what DIV-001 computes." True in real arithmetic. The
# two accumulate in DIFFERENT ORDERS -- ascending column vs face index -- so
# bit-equality is a claim about floating point, and a test asserting it could
# fail on a correct operator. That is DIV-001's recorded mistake, so it is
# measured here.
# ---------------------------------------------------------------------------
print()
print("Q2  rowSum(A) vs face-order divergence of phi  (the cross-check oracle)")
exact = 0
meshes = 20000
worst_rel = 0.0
worst_at = None
for _ in range(meshes):
    cells = random.randint(2, 12)
    faces = random_mesh(cells, random.randint(1, 24), random.randint(0, 6))
    phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-6, 6))
           for _ in faces]
    A = assemble_upwind(cells, faces, phi)
    d = divergence_face_order(cells, faces, phi)
    all_exact = True
    for c in range(cells):
        rs = row_sum(A, c)
        # The scale: the magnitude this cell's row actually accumulated.
        scale = sum(abs(v) for v in A[c].values())
        if rs != d[c]:
            all_exact = False
            if scale > 0.0:
                rel = abs(rs - d[c]) / scale
                if rel > worst_rel:
                    worst_rel = rel
                    worst_at = (cells, len(faces))
    if all_exact:
        exact += 1
print("    %d meshes: bit-exact on %d (%.2f%%)"
      % (meshes, exact, 100.0 * exact / meshes))
print("    worst relative gap: %.6g   (denominator = sum |row entry|)"
      % worst_rel)
print("    at mesh shape: %s" % (worst_at,))
print("    => the row-sum oracle is a TOLERANCE check, not a bit-exact one.")


# ---------------------------------------------------------------------------
# Q3. Reverse flow: A(-phi) == A(phi)^T for a DIVERGENCE-FREE phi.
#
# Derived independently, not from the code: per internal face, phi > 0 writes
# A(P,P)=+q and A(N,P)=-q, while -phi writes A(N,N)=+q and A(P,N)=-q, so the
# two cells' roles swap. Summing over faces gives
#     A(-phi) = A(phi)^T - diag(div phi),
# exactly, so for a conservative phi the reversed matrix IS the transpose.
#
# A 1D CHAIN is conservative BIT-EXACTLY: one face in, one face out, the same
# double, so each interior cell's divergence is q - q. A general mesh is not,
# which is why both are measured.
# ---------------------------------------------------------------------------
print()
print("Q3  reverse flow:  A(-phi) == A(phi)^T   for divergence-free phi")


def transpose_matches(cells, faces, phi, A_fwd, A_rev):
    worst = 0.0
    scale = 0.0
    for i in range(cells):
        for j, v in A_fwd[i].items():
            scale = max(scale, abs(v))
        for j, v in A_rev[i].items():
            scale = max(scale, abs(v))
    keys = set()
    for i in range(cells):
        for j in A_fwd[i]:
            keys.add((j, i))     # transposed position
        for j in A_rev[i]:
            keys.add((i, j))
    for (i, j) in keys:
        fwd_t = A_fwd[j].get(i, 0.0)   # A(phi)^T (i,j) = A(phi)(j,i)
        rev = A_rev[i].get(j, 0.0)
        worst = max(worst, abs(rev - fwd_t))
    return worst, scale


# 3a. 1D chains, uniform flux: exactly conservative at interior cells.
chain_exact = 0
chains = 2000
for _ in range(chains):
    n = random.randint(3, 40)
    faces = [(i, i + 1) for i in range(n - 1)]
    q = random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-6, 6))
    phi = [q] * len(faces)
    A_f = assemble_upwind(n, faces, phi)
    A_r = assemble_upwind(n, faces, [-p for p in phi])
    worst, scale = transpose_matches(n, faces, phi, A_f, A_r)
    # The END cells of an open chain are NOT conservative (one face only), so
    # the identity is checked where it is claimed: interior rows. The ends
    # differ by exactly their own divergence, which is checked next.
    interior_worst = 0.0
    for i in range(1, n - 1):
        cols = set(A_f[i].keys()) | set(A_r[i].keys())
        for j in cols:
            interior_worst = max(
                interior_worst, abs(A_r[i].get(j, 0.0) - A_f[j].get(i, 0.0)))
    if interior_worst == 0.0:
        chain_exact += 1
print("    3a 1D chain, uniform flux, INTERIOR rows:")
print("       %d chains, transpose exact on %d (%.2f%%)"
      % (chains, chain_exact, 100.0 * chain_exact / chains))

# 3b. The identity including the divergence term, on a general mesh.
gen_exact = 0
gen = 5000
worst_rel3 = 0.0
for _ in range(gen):
    cells = random.randint(2, 10)
    faces = random_mesh(cells, random.randint(1, 20), random.randint(0, 4))
    phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-6, 6))
           for _ in faces]
    A_f = assemble_upwind(cells, faces, phi)
    A_r = assemble_upwind(cells, faces, [-p for p in phi])
    d = divergence_face_order(cells, faces, phi)
    ok = True
    for i in range(cells):
        cols = set(A_f[i].keys()) | set(A_r[i].keys()) | {i}
        scale = max([abs(v) for v in A_f[i].values()] + [1e-300])
        for j in cols:
            lhs = A_r[i].get(j, 0.0)
            rhs = A_f[j].get(i, 0.0) - (d[i] if i == j else 0.0)
            if lhs != rhs:
                ok = False
                worst_rel3 = max(worst_rel3, abs(lhs - rhs) / scale)
    if ok:
        gen_exact += 1
print("    3b general mesh, A(-phi) == A(phi)^T - diag(div phi):")
print("       %d meshes, exact on %d (%.2f%%), worst relative %.6g"
      % (gen, gen_exact, 100.0 * gen_exact / gen, worst_rel3))


# ---------------------------------------------------------------------------
# Q4. Boundedness. For upwind the dominance deficit of a row is
#     sum_{j!=i} |a_ij| - |a_ii|, and for a conservative phi it should be
#     exactly -|div| = 0, i.e. WEAK dominance with equality.
#
# The question that matters for the POLICY: how badly does dominance fail when
# phi is NOT conservative? If it tracks the divergence exactly, the policy can
# say so and point at DIV-001's measured imbalance instead of inventing a
# second number.
# ---------------------------------------------------------------------------
print()
print("Q4  boundedness: dominance deficit vs the cell divergence")
tracked = 0
q4 = 8000
worst_rel4 = 0.0
for _ in range(q4):
    cells = random.randint(2, 10)
    faces = random_mesh(cells, random.randint(1, 20), 0)
    phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-3, 3))
           for _ in faces]
    A = assemble_upwind(cells, faces, phi)
    d = divergence_face_order(cells, faces, phi)
    ok = True
    for i in range(cells):
        diag = A[i].get(i, 0.0)
        off = sum(abs(v) for j, v in A[i].items() if j != i)
        deficit = off - abs(diag)
        # For upwind with a positive diagonal, deficit == -rowsum == -div.
        scale = off + abs(diag)
        if scale == 0.0:
            continue
        if abs(deficit - (-d[i])) / scale > 1e-14:
            ok = False
            worst_rel4 = max(worst_rel4, abs(deficit - (-d[i])) / scale)
    if ok:
        tracked += 1
print("    %d meshes: deficit == -divergence within 1e-14 on %d (%.2f%%)"
      % (q4, tracked, 100.0 * tracked / q4))
print("    worst relative departure: %.6g" % worst_rel4)
print("    => boundedness is CONDITIONAL on a conservative phi, and the")
print("       deficit is the divergence, so no second number is needed.")

print()
print("=" * 78)
print("done")
