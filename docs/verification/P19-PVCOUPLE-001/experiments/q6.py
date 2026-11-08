"""Q6  THE IDENTITY. Is the per-cell continuity residual after the face-flux
correction identically the pressure system's own linear residual?

If it is, then (a) continuity closure is exactly as good as the pressure solve
and no better, so the tolerance must be tied to the SOLVE tolerance and not to
1e-14; and (b) the reference-cell row is where the compatibility error lands.

Tested against an EXACT solve and against a DELIBERATELY WRONG p', because an
identity only checked on the happy path has not been checked.
"""
import numpy as np
import sys
sys.path.insert(0, r"C:/Users/Hasib/AppData/Local/Temp/claude/8b17a8f29dfea518/527dbd1e-d40d-42f7-8715-a186b5519185/scratchpad")
from measure_pvc import Box, SEED

rng = np.random.default_rng(SEED)
box = Box(4, 4, 4)
rho = 998.2
TRIALS = 50

worst_identity_exact = 0.0
worst_identity_wrong = 0.0
worst_nonref = 0.0
worst_at_ref = 0.0
worst_compat = 0.0
scale_at_ref = 0.0

for _ in range(TRIALS):
    ap = rng.lognormal(0.0, 1.0, box.cells) * 1.0e3
    dcoef = box.volume / ap
    a_f = np.empty(len(box.internal))
    for f, (o, n, area, dist) in enumerate(box.internal):
        df = 0.5 * dcoef[o] + 0.5 * dcoef[n]
        a_f[f] = rho * df * area / dist

    phi_int = rng.normal(0.0, 1.0, len(box.internal))
    phi_bnd = np.zeros(len(box.boundary))
    rate = rng.lognormal(0.0, 0.5)
    phi_bnd[0] = -rate
    phi_bnd[1] = +rate

    def divergence(pi, pb):
        d = np.zeros(box.cells)
        for (o, n, _a, _d), f in zip(box.internal, pi):
            d[o] += f
            d[n] -= f
        for (o, _a, _d, _ax, _s), f in zip(box.boundary, pb):
            d[o] += f
        return d

    div_star = divergence(phi_int, phi_bnd)
    rhs = -div_star

    A = np.zeros((box.cells, box.cells))
    for f, (o, n, _a, _d) in enumerate(box.internal):
        A[o, o] += a_f[f]
        A[n, n] += a_f[f]
        A[o, n] -= a_f[f]
        A[n, o] -= a_f[f]
    A_orig = A.copy()

    ref = 0
    A[ref, :] = 0.0
    A[:, ref] = 0.0
    A[ref, ref] = 1.0
    rhs_pinned = rhs.copy()
    rhs_pinned[ref] = 0.0

    pprime = np.linalg.solve(A, rhs_pinned)

    def corrected_divergence(pp):
        phi_c = phi_int.copy()
        for f, (o, n, _a, _d) in enumerate(box.internal):
            phi_c[f] = phi_int[f] - a_f[f] * (pp[n] - pp[o])
        return divergence(phi_c, phi_bnd)

    # THE IDENTITY, with the EXACT solve.
    div_c = corrected_divergence(pprime)
    predicted = A_orig @ pprime - rhs
    # SCALED BY THE BOUNDARY THROUGHPUT, not per-component. A per-component
    # ratio of two values at the noise floor reports 2.0 when their signs
    # differ, which says nothing about the identity.
    thr = 0.5 * np.abs(phi_bnd).sum()
    gap = np.abs(div_c - predicted)
    worst_identity_exact = max(worst_identity_exact, gap.max() / thr)

    # THE IDENTITY, with a DELIBERATELY WRONG p'.
    wrongp = pprime + rng.normal(0.0, 1.0, box.cells) * np.abs(pprime).max()
    div_w = corrected_divergence(wrongp)
    predicted_w = A_orig @ wrongp - rhs
    gapw = np.abs(div_w - predicted_w)
    scalew = max(np.abs(predicted_w).max(), thr)
    worst_identity_wrong = max(worst_identity_wrong, gapw.max() / scalew)

    # Where does the error land?
    mask = np.ones(box.cells, dtype=bool)
    mask[ref] = False
    worst_nonref = max(worst_nonref, np.abs(div_c[mask]).max())
    worst_at_ref = max(worst_at_ref, abs(div_c[ref]))
    worst_compat = max(worst_compat, abs(rhs.sum()))
    scale_at_ref = max(scale_at_ref, 0.5 * np.abs(phi_bnd).sum())

print("Q6  div(phi_corrected) == A p' - rhs ?")
print(f"      trials                              {TRIALS}")
print(f"      gap/throughput, exact solve        {worst_identity_exact:.6e}")
print(f"      gap/scale, WRONG p'               {worst_identity_wrong:.6e}")
print()
print("      where the residual lands")
print(f"      worst |div| over cells != ref       {worst_nonref:.6e}")
print(f"      worst |div| AT the reference cell   {worst_at_ref:.6e}")
print(f"      worst |compatibility sum|           {worst_compat:.6e}")
print(f"      boundary throughput for scale       {scale_at_ref:.6e}")
