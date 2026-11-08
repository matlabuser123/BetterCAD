"""P19-PVCOUPLE-001 pre-design measurements.

NOTHING HERE IS PRODUCTION CODE AND NOTHING HERE IS A TEST. Every number this
prints is measured before any C++ exists, so that the ADR's decisions rest on
measurement rather than on what the author expects a floating-point assembly to
do.

THE MESH MODEL IS A STRUCTURED BOX, NOT A TETRAHEDRAL MESH, and that is a
limitation recorded with the results rather than hidden. The quantities these
questions are about -- owner/neighbour pairs, a face area, an orthogonal
distance, a cell volume and the ORDER coefficients are accumulated in -- are
faithfully modelled by a structured mesh; cell SHAPE is not, and no question
here depends on it. Q3's absolute residual level would differ on real tets.
"""

import numpy as np

SEED = 20261108


# ---------------------------------------------------------------------------
# A structured box of cells, presented the way CfdTopology presents a mesh:
# faces numbered with internal faces first, each internal face carrying an
# owner and a neighbour, each boundary face carrying an owner only.
# ---------------------------------------------------------------------------
class Box:
    def __init__(self, nx, ny, nz, lx=1.0, ly=1.0, lz=1.0):
        self.n = (nx, ny, nz)
        self.cells = nx * ny * nz
        self.h = (lx / nx, ly / ny, lz / nz)
        self.volume = np.full(self.cells, self.h[0] * self.h[1] * self.h[2])
        internal = []          # (owner, neighbour, area, distance)
        boundary = []          # (owner, area, distance, axis, sign)
        face_area = (self.h[1] * self.h[2], self.h[0] * self.h[2], self.h[0] * self.h[1])
        def cid(i, j, k):
            return (k * ny + j) * nx + i
        for axis in range(3):
            nxt = [(1, 0, 0), (0, 1, 0), (0, 0, 1)][axis]
            extent = self.n[axis]
            for k in range(nz):
                for j in range(ny):
                    for i in range(nx):
                        idx = (i, j, k)
                        c = cid(i, j, k)
                        if idx[axis] + 1 < extent:
                            nb = cid(i + nxt[0], j + nxt[1], k + nxt[2])
                            internal.append((c, nb, face_area[axis], self.h[axis]))
                        else:
                            boundary.append((c, face_area[axis], 0.5 * self.h[axis], axis, +1))
                        if idx[axis] == 0:
                            boundary.append((c, face_area[axis], 0.5 * self.h[axis], axis, -1))
        self.internal = internal
        self.boundary = boundary

    def __repr__(self):
        return (f"Box{self.n} cells={self.cells} internal={len(self.internal)} "
                f"boundary={len(self.boundary)}")


def interpolation_weight(dist_owner, dist_neighbour):
    """The standard distance weight toward the owner."""
    return dist_neighbour / (dist_owner + dist_neighbour)


# ---------------------------------------------------------------------------
# Q1  THE SYMMETRY OF THE FACE COEFFICIENT, and whether it survives being
#     read from each side of the face instead of computed once per face.
# ---------------------------------------------------------------------------
def q1(rng, trials=2_000_000):
    """One visit per face versus one visit per (cell, face).

    A single face loop computes a_f ONCE and writes it to both rows, so the
    matrix is symmetric whatever the interpolation does. A per-cell loop sees an
    internal face twice and recomputes the weight from that side. This measures
    whether the two visits agree bit-for-bit.

    THE FIRST FORM OF THIS QUESTION WAS WRONG AND IS RECORDED IN THE RESULTS.
    It drew `w` from `rng.random()`, which returns `k * 2**-53`, so `1 - w` is
    exact BY CONSTRUCTION OF THE SAMPLE and the answer came back 100% -- a
    property of the sampler, not of the assembly. A real weight is a QUOTIENT
    of two distances and is not a multiple of 2**-53.
    """
    do = rng.lognormal(0.0, 1.0, trials)      # owner-to-face distance
    dn = rng.lognormal(0.0, 1.0, trials)      # face-to-neighbour distance
    w = interpolation_weight(do, dn)          # the weight toward the owner
    dp = rng.lognormal(0.0, 2.0, trials)
    dv = rng.lognormal(0.0, 2.0, trials)

    owner_side = w * dp + (1.0 - w) * dv
    wprime = interpolation_weight(dn, do)     # the neighbour's own weight
    neighbour_side = wprime * dv + (1.0 - wprime) * dp

    same = owner_side == neighbour_side
    scale = np.maximum(np.abs(owner_side), np.abs(neighbour_side))
    rel = np.where(scale > 0.0, np.abs(owner_side - neighbour_side) / scale, 0.0)
    exact = (1.0 - (1.0 - w)) == w
    compl = (w + wprime) == 1.0

    print("Q1  one face visit vs two, the interpolated face coefficient")
    print(f"      samples                       {trials}")
    print(f"      `1 - w` round-trips exactly   {exact.sum()} / {trials}"
          f"   = {100.0 * exact.mean():.4f}%")
    print(f"      w + w' == 1 exactly           {compl.sum()} / {trials}"
          f"   = {100.0 * compl.mean():.4f}%")
    print(f"      two visits bit-identical      {same.sum()} / {trials}"
          f"   = {100.0 * same.mean():.4f}%")
    print(f"      worst relative difference     {rel.max():.6e}")
    print("      CONTROL one value written to both rows is symmetric by")
    print("      construction; the asymmetry above is the RECOMPUTED WEIGHT.")
    return float(rel.max()), float(same.mean())


# ---------------------------------------------------------------------------
# Q2  THE COMPATIBILITY CONDITION of a pure-Neumann pressure equation:
#     sum over cells of the right-hand side must vanish, or there is no
#     solution at all.
# ---------------------------------------------------------------------------
def q2(rng, box, trials=200):
    """sum_P (-sum_f phi*_f) over every cell equals -(net boundary flux).

    Measured for a BALANCED prescribed flux (what P19-BC-001 admits) and for an
    unbalanced one (what it refuses), to see whether the sum discriminates.
    """
    worst_balanced = 0.0
    worst_unbalanced = 0.0
    for _ in range(trials):
        phi_int = rng.normal(0.0, 1.0, len(box.internal))
        # A balanced boundary flux: equal and opposite on two faces.
        phi_bnd = np.zeros(len(box.boundary))
        rate = rng.lognormal(0.0, 1.0)
        phi_bnd[0] = -rate
        phi_bnd[1] = +rate

        div = np.zeros(box.cells)
        for (o, n, _a, _d), f in zip(box.internal, phi_int):
            div[o] += f
            div[n] -= f
        for (o, _a, _d, _ax, _s), f in zip(box.boundary, phi_bnd):
            div[o] += f

        rhs = -div
        total = rhs.sum()
        net_boundary = phi_bnd.sum()
        worst_balanced = max(worst_balanced, abs(total), abs(net_boundary))

        phi_bnd[1] = +rate * 1.5
        div2 = np.zeros(box.cells)
        for (o, n, _a, _d), f in zip(box.internal, phi_int):
            div2[o] += f
            div2[n] -= f
        for (o, _a, _d, _ax, _s), f in zip(box.boundary, phi_bnd):
            div2[o] += f
        worst_unbalanced = max(worst_unbalanced, abs((-div2).sum()))

    print("Q2  the compatibility condition, sum of the right-hand side")
    print(f"      balanced   worst |sum|       {worst_balanced:.6e}")
    print(f"      unbalanced worst |sum|       {worst_unbalanced:.6e}")
    return worst_balanced, worst_unbalanced


# ---------------------------------------------------------------------------
# Q3  THE CENTRAL CLAIM. After an EXACT solve of the pressure-correction
#     system and the face-flux correction, does the corrected flux satisfy
#     discrete continuity, and to what level?
# ---------------------------------------------------------------------------
def q3(rng, box, rho=998.2, trials=20):
    """phi = phi* - a_f (p'_N - p'_P), then max_P |sum_f phi_f|.

    THE SOLVE IS numpy's DENSE DIRECT SOLVE, which is independent of BetterCAD
    entirely -- this is the reference, not a reimplementation of anything.
    """
    worst_before = 0.0
    worst_after = 0.0
    worst_rel = 0.0
    for _ in range(trials):
        # d_P = V_P / a_P with a plausible momentum diagonal.
        ap = rng.lognormal(0.0, 1.0, box.cells) * 1.0e3
        dcoef = box.volume / ap

        # One face coefficient per face, computed ONCE, as Q1 argues for.
        a_f = np.empty(len(box.internal))
        for f, (o, n, area, dist) in enumerate(box.internal):
            w = 0.5                      # a uniform box: the midpoint weight
            df = w * dcoef[o] + (1.0 - w) * dcoef[n]
            a_f[f] = rho * df * area / dist

        # A predicted flux that does NOT satisfy continuity, with a balanced
        # boundary flux so that the pure-Neumann system is compatible.
        phi_int = rng.normal(0.0, 1.0, len(box.internal))
        phi_bnd = np.zeros(len(box.boundary))
        rate = rng.lognormal(0.0, 0.5)
        phi_bnd[0] = -rate
        phi_bnd[1] = +rate

        div = np.zeros(box.cells)
        for (o, n, _a, _d), f in zip(box.internal, phi_int):
            div[o] += f
            div[n] -= f
        for (o, _a, _d, _ax, _s), f in zip(box.boundary, phi_bnd):
            div[o] += f
        worst_before = max(worst_before, np.abs(div).max())

        # Assemble the Laplacian, ONE VISIT PER FACE.
        A = np.zeros((box.cells, box.cells))
        for f, (o, n, _a, _d) in enumerate(box.internal):
            A[o, o] += a_f[f]
            A[n, n] += a_f[f]
            A[o, n] -= a_f[f]
            A[n, o] -= a_f[f]
        # Every boundary here prescribes the flux, so p' is zero-gradient
        # everywhere and the matrix is singular: pin the reference cell by
        # clearing its row AND column, which keeps symmetry exact.
        ref = 0
        A[ref, :] = 0.0
        A[:, ref] = 0.0
        A[ref, ref] = 1.0
        rhs = -div.copy()
        rhs[ref] = 0.0

        sym = np.abs(A - A.T).max()
        pprime = np.linalg.solve(A, rhs)

        # Correct the face fluxes.
        phi_corrected = phi_int.copy()
        for f, (o, n, _a, _d) in enumerate(box.internal):
            phi_corrected[f] = phi_int[f] - a_f[f] * (pprime[n] - pprime[o])

        div_after = np.zeros(box.cells)
        for (o, n, _a, _d), f in zip(box.internal, phi_corrected):
            div_after[o] += f
            div_after[n] -= f
        for (o, _a, _d, _ax, _s), f in zip(box.boundary, phi_bnd):
            div_after[o] += f

        throughput = 0.5 * np.abs(phi_bnd).sum()
        worst_after = max(worst_after, np.abs(div_after).max())
        worst_rel = max(worst_rel, np.abs(div_after).max() / throughput)

    print("Q3  the corrected flux against discrete continuity")
    print(f"      matrix asymmetry after pinning  {sym:.6e}")
    print(f"      worst |sum_f phi| BEFORE        {worst_before:.6e}")
    print(f"      worst |sum_f phi| AFTER         {worst_after:.6e}")
    print(f"      AFTER, relative to throughput   {worst_rel:.6e}")
    return worst_after, worst_rel


# ---------------------------------------------------------------------------
# Q4  THE RELAXED DIAGONAL. If momentum is relaxed implicitly, d_P must use
#     the RELAXED diagonal. This measures the size of getting it wrong.
# ---------------------------------------------------------------------------
def q4(rng, trials=200_000, alpha=0.7):
    ap = rng.lognormal(0.0, 1.5, trials) * 1.0e3
    vol = rng.lognormal(0.0, 0.5, trials) * 1.0e-6
    d_right = vol / (ap / alpha)
    d_wrong = vol / ap
    ratio = d_wrong / d_right
    print("Q4  d_P from the relaxed diagonal versus the unrelaxed one")
    print(f"      alpha_u                       {alpha}")
    print(f"      d_wrong / d_right  min        {ratio.min():.6f}")
    print(f"      d_wrong / d_right  max        {ratio.max():.6f}")
    print(f"      exactly 1/alpha?              "
          f"{'yes' if np.allclose(ratio, 1.0 / alpha, rtol=0, atol=1e-15) else 'no'}")
    return float(ratio.max())


# ---------------------------------------------------------------------------
# Q5  DETERMINISM of the face-loop accumulation.
# ---------------------------------------------------------------------------
def q5(rng, box, trials=200):
    """Does the per-cell divergence depend on the order faces are visited?"""
    same = 0
    worst = 0.0
    order = np.arange(len(box.internal))
    for _ in range(trials):
        phi = rng.normal(0.0, 1.0, len(box.internal))
        a = np.zeros(box.cells)
        for (o, n, _ar, _d), f in zip(box.internal, phi):
            a[o] += f
            a[n] -= f
        shuffled = order.copy()
        rng.shuffle(shuffled)
        b = np.zeros(box.cells)
        for idx in shuffled:
            o, n, _ar, _d = box.internal[idx]
            b[o] += phi[idx]
            b[n] -= phi[idx]
        if np.array_equal(a, b):
            same += 1
        scale = np.maximum(np.abs(a), np.abs(b))
        rel = np.where(scale > 0.0, np.abs(a - b) / scale, 0.0)
        worst = max(worst, rel.max())
    print("Q5  face-visit order and the accumulated divergence")
    print(f"      bit-identical under a shuffle  {same} / {trials}")
    print(f"      worst relative difference      {worst:.6e}")
    return worst


def main():
    rng = np.random.default_rng(SEED)
    print("P19-PVCOUPLE-001 pre-design measurements, seed", SEED)
    print()
    box = Box(4, 4, 4)
    print("mesh model:", box)
    print("  A STRUCTURED BOX, NOT A TET MESH. See this file's docstring: the")
    print("  quantities measured are owner/neighbour pairs, areas, distances,")
    print("  volumes and ACCUMULATION ORDER, none of which needs cell shape.")
    print()
    q1(rng)
    print()
    q2(rng, box)
    print()
    q3(rng, box)
    print()
    q4(rng)
    print()
    q5(rng, box)


if __name__ == "__main__":
    main()
