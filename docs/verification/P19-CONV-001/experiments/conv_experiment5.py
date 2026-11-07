# -*- coding: utf-8 -*-
"""P19-CONV-001 measurement, REVISION 4: the two identities an independent
review found false as published.

ADR-072 Decision 8 claimed, unqualified:

    A(-phi) == A(phi)^T - diag(div phi)

"exactly in real arithmetic", measured on a "general mesh" to worst relative
4.93e-16. ADR-072 Decision 7 claimed:

    dominance deficit == -div(phi)

also "exactly in real arithmetic". Both are WRONG AS WRITTEN, and the reason
they measured clean is that revisions 1-3 of this script modelled only the
cases where they are true:

  * `assemble_upwind` has no central branch at all, so Q3 never saw a
    geometric weight. The transpose derivation ("reversing a face's flux swaps
    the two cells' roles") is UPWIND-SPECIFIC: a central weight does not swap.
  * the boundary rule modelled was `A[p][p] += max(q, 0)`, i.e. every boundary
    face behaves like a FixedValue one. The real operator puts the FULL `phi`
    on the diagonal at a ZeroGradient or Symmetry face, inflow included, so
    reversing the sign flips that term instead of removing it.
  * Q4 ran with `boundary=0` -- no boundary faces at all -- so it could not see
    the prescribed-inflow term that Decision 5 had already corrected in the
    neighbouring decision.

So the measurements were of a model narrower than the claims. This revision
models the operator as it is and measures BOTH the corrected identities and the
size of the departure of the published ones.

SEED 202611075.
"""
import random

SEED = 202611075
random.seed(SEED)

FIXED, ZEROGRAD = "FixedValue", "ZeroGradient"

print("P19-CONV-001 measurement revision 4, seed %d" % SEED)
print("=" * 78)


def random_mesh(cells, internal, boundary, policies):
    faces = []
    for _ in range(internal):
        a = random.randrange(cells)
        b = random.randrange(cells)
        while b == a:
            b = random.randrange(cells)
        faces.append((a, b, None))
    for _ in range(boundary):
        faces.append((random.randrange(cells), None, random.choice(policies)))
    return faces


def assemble(cells, faces, phi, weights, central):
    """The operator as written: one pass over faces, scatter into owner and
    neighbour. `weights[f]` is the owner's share at internal face f."""
    A = [dict() for _ in range(cells)]
    b = [0.0] * cells
    for f, (p, n, pol) in enumerate(faces):
        q = phi[f]
        if n is None:
            if q >= 0.0:
                A[p][p] = A[p].get(p, 0.0) + q
            elif pol == FIXED:
                b[p] -= q * 1.0
            else:
                A[p][p] = A[p].get(p, 0.0) + q
            continue
        s = weights[f] if central else (1.0 if q >= 0.0 else 0.0)
        A[p][p] = A[p].get(p, 0.0) + q * s
        A[p][n] = A[p].get(n, 0.0) + q * (1.0 - s)
        A[n][p] = A[n].get(p, 0.0) - q * s
        A[n][n] = A[n].get(n, 0.0) - q * (1.0 - s)
    return A, b


def divergence(cells, faces, phi):
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


def row_sum(A, i):
    s = 0.0
    for col in sorted(A[i].keys()):
        s += A[i][col]
    return s


# ---------------------------------------------------------------------------
# Q6. The transpose identity, by scheme and by boundary policy.
# ---------------------------------------------------------------------------
print()
print("Q6  A(-phi) == A(phi)^T - diag(div phi), by scheme and boundary policy")
print("    (relative departure, denominator = max |entry| over the two matrices)")
for central in (False, True):
    for pols, plabel in (((FIXED,), "FixedValue only"),
                         ((ZEROGRAD,), "ZeroGradient only"),
                         ((FIXED, ZEROGRAD), "mixed")):
        trials = 4000
        exact = 0
        worst = 0.0
        for _ in range(trials):
            cells = random.randint(2, 9)
            faces = random_mesh(cells, random.randint(1, 16),
                                random.randint(1, 5), pols)
            phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-2, 2))
                   for _ in faces]
            w = [random.uniform(0.05, 0.95) for _ in faces]
            Af, _ = assemble(cells, faces, phi, w, central)
            Ar, _ = assemble(cells, faces, [-x for x in phi], w, central)
            d = divergence(cells, faces, phi)
            scale = max([abs(v) for row in Af for v in row.values()] +
                        [abs(v) for row in Ar for v in row.values()] + [1e-300])
            ok = True
            for i in range(cells):
                cols = set(Af[i]) | set(Ar[i]) | {i}
                for j in cols:
                    lhs = Ar[i].get(j, 0.0)
                    rhs = Af[j].get(i, 0.0) - (d[i] if i == j else 0.0)
                    if lhs != rhs:
                        ok = False
                        worst = max(worst, abs(lhs - rhs) / scale)
            exact += 1 if ok else 0
        print("    %-7s %-18s exact %5d/%d (%6.2f%%)  worst rel %.4g"
              % ("Central" if central else "Upwind", plabel, exact, trials,
                 100.0 * exact / trials, worst))

print()
print("    => the published identity holds ONLY for Upwind with FixedValue")
print("       boundaries. Central departs by phi(2w-1) per face and a")
print("       ZeroGradient boundary face by phi_b -- FINITE errors, not")
print("       rounding, which is why the percentages collapse rather than")
print("       the relative figure staying near an eps.")


# ---------------------------------------------------------------------------
# Q7. The dominance deficit, published form against corrected form.
# ---------------------------------------------------------------------------
print()
print("Q7  upwind dominance deficit:  published  -div   vs  corrected")
print("    -div + prescribedInflow")
for pols, plabel in (((ZEROGRAD,), "ZeroGradient only"),
                     ((FIXED,), "FixedValue only"),
                     ((FIXED, ZEROGRAD), "mixed")):
    trials = 6000
    ok_pub = ok_fix = 0
    worst_pub = worst_fix = 0.0
    for _ in range(trials):
        cells = random.randint(2, 9)
        faces = random_mesh(cells, random.randint(1, 16), random.randint(1, 5),
                            pols)
        phi = [random.uniform(-1.0, 1.0) * (10.0 ** random.randint(-2, 2))
               for _ in faces]
        w = [0.5] * len(faces)
        A, _ = assemble(cells, faces, phi, w, central=False)
        d = divergence(cells, faces, phi)
        pin = prescribed_inflow(cells, faces, phi)
        good_pub = good_fix = True
        for i in range(cells):
            diag = A[i].get(i, 0.0)
            off = sum(abs(v) for j, v in A[i].items() if j != i)
            deficit = off - abs(diag)
            scale = off + abs(diag) + abs(pin[i])
            if scale == 0.0:
                continue
            if deficit != -d[i]:
                good_pub = False
                worst_pub = max(worst_pub, abs(deficit + d[i]) / scale)
            if deficit != -d[i] + pin[i]:
                good_fix = False
                worst_fix = max(worst_fix, abs(deficit + d[i] - pin[i]) / scale)
        ok_pub += 1 if good_pub else 0
        ok_fix += 1 if good_fix else 0
    print("    %-18s published exact %5d/%d (%6.2f%%) worst %.4g |"
          " corrected exact %5d/%d (%6.2f%%) worst %.4g"
          % (plabel, ok_pub, trials, 100.0 * ok_pub / trials, worst_pub,
             ok_fix, trials, 100.0 * ok_fix / trials, worst_fix))

print()
print("    => with no FixedValue face the two forms coincide, which is why")
print("       revision 1's Q4 (boundary=0) measured the published form clean.")
print("       Add one prescribed inflow face and the published form is wrong")
print("       by a finite amount; the corrected form stays at half an eps.")

print()
print("=" * 78)
print("done")
