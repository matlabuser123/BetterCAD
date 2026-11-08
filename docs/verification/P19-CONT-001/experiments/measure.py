"""P19-CONT-001 pre-design measurements.

Every number ADR-074 will claim about floating-point behaviour is measured HERE,
before any C++ exists, because four measurement-tool defects in P19-MOM-001 and
three false published claims in ADR-072 all came from reasoning about arithmetic
instead of running it.

Measured, in order:

  Q1  the per-cell continuity residual of a field that is conservative BY
      CONSTRUCTION -- the floating-point floor the cell tolerance must clear
  Q2  whether `outflow - inflow` reconstructs the single-pass boundary net
      bit-for-bit  (items 4 and 7 report them separately)
  Q3  whether summing per-PATCH subtotals reconstructs the single-pass total
      bit-for-bit  (item 9 reports multiple inlets/outlets)
  Q4  whether a closed domain's boundary flux is bit-zero
  Q5  the scale of the relative measures, to check the 0.5*sum|phi| throughput
      is the right denominator and not a near-zero one

Deliberately NOT measured here: the throughput floor and any acceptance
threshold.  CfdData.hpp and ADR-058 both assign those to P19-MASSBAL-001.
"""

import random
import math

random.seed(20261108)  # recorded: every figure below is reproducible from it


# ---------------------------------------------------------------- helpers

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(a):
    return math.sqrt(dot(a, a))


def tet_outward_areas(p):
    """The four outward area vectors of a tetrahedron, as a solver forms them.

    Each face is 0.5 * (e1 x e2) with the winding chosen so the vector points
    away from the opposite vertex.  In EXACT arithmetic the four sum to zero;
    the whole point of Q1 is that in floating point they do not.
    """
    a, b, c, d = p
    faces = [(a, b, c, d), (a, b, d, c), (a, c, d, b), (b, c, d, a)]
    out = []
    for (u, v, w, opp) in faces:
        n = cross(sub(v, u), sub(w, u))
        n = (0.5 * n[0], 0.5 * n[1], 0.5 * n[2])
        # point it away from the opposite vertex
        if dot(n, sub(opp, u)) > 0.0:
            n = (-n[0], -n[1], -n[2])
        out.append(n)
    return out


def rel(a, b):
    """|a - b| relative to the MAGNITUDE ACCUMULATED, which the caller passes.

    Not relative to the answer.  Stating a floating-point error against the
    answer it arrived at is P19-GRAD-001's finding r8, and this milestone's
    predecessor reported 9.992e+284 for correct arithmetic by doing it.
    """
    return abs(a - b)


# ---------------------------------------------------------------- Q1

def q1(trials=20000, scale=1.0):
    """Per-cell residual of a UNIFORM velocity field: conservative by construction.

    phi_f = rho * (U . S_f).  Summed over a closed cell this is
    rho * (U . sum_f S_f), which vanishes in exact arithmetic, so whatever is
    left is purely the floating-point floor.
    """
    worst = 0.0
    worst_closure = 0.0
    bitzero = 0
    denom_zero = 0
    for _ in range(trials):
        p = [tuple(random.uniform(-scale, scale) for _ in range(3)) for _ in range(4)]
        areas = tet_outward_areas(p)
        rho = random.uniform(0.5, 1200.0)
        u = tuple(random.uniform(-10.0, 10.0) for _ in range(3))

        phi = [rho * dot(u, s) for s in areas]
        residual = 0.0
        for v in phi:                      # FaceIndex order, as the operator sums
            residual += v
        magnitude = 0.0
        for v in phi:
            magnitude += abs(v)

        if magnitude == 0.0:
            denom_zero += 1
            continue
        r = abs(residual) / magnitude
        worst = max(worst, r)
        if residual == 0.0:
            bitzero += 1

        # the mesh closure, for comparison with P19-DIV-001's own figure
        s_sum = (0.0, 0.0, 0.0)
        for s in areas:
            s_sum = (s_sum[0] + s[0], s_sum[1] + s[1], s_sum[2] + s[2])
        s_mag = 0.0
        for s in areas:
            s_mag += norm(s)
        if s_mag > 0.0:
            worst_closure = max(worst_closure, norm(s_sum) / s_mag)

    return worst, bitzero, trials, worst_closure, denom_zero


# ---------------------------------------------------------------- Q2 / Q3

def boundary_field(n, spread, balanced):
    """n boundary phi values spanning `spread` orders of magnitude.

    balanced=True makes the net small relative to the throughput, which is the
    physical regime; False leaves it large, which is the regime a FAILING mass
    balance sits in and which the measure must still describe.
    """
    phi = []
    for _ in range(n):
        mag = 10.0 ** random.uniform(-spread / 2.0, spread / 2.0)
        phi.append(mag * random.choice((-1.0, 1.0)))
    if balanced:
        # remove the net by spreading its negation over the faces
        net = math.fsum(phi)
        phi = [v - net / n for v in phi]
    return phi


def q2_q3(trials=20000, n=60, spread=6.0, balanced=True, patches=5):
    """Q2: (outflow - inflow) vs the single-pass net.  Q3: patch subtotals vs total."""
    worst_q2 = 0.0
    bitzero_q2 = 0
    worst_q3 = 0.0
    bitzero_q3 = 0
    counted = 0
    for _ in range(trials):
        phi = boundary_field(n, spread, balanced)

        magnitude = 0.0
        for v in phi:
            magnitude += abs(v)
        if magnitude == 0.0:
            continue
        counted += 1

        # single pass, BoundaryFaceIndex order
        net = 0.0
        for v in phi:
            net += v

        # two passes, split by SIGN (never by condition kind)
        outflow = 0.0
        inflow = 0.0
        for v in phi:
            if v > 0.0:
                outflow += v
            elif v < 0.0:
                inflow += -v
        q2 = rel(outflow - inflow, net) / magnitude
        worst_q2 = max(worst_q2, q2)
        if (outflow - inflow) == net:
            bitzero_q2 += 1

        # per-patch subtotals, then a sum over patches
        sub = [0.0] * patches
        for i, v in enumerate(phi):
            sub[i % patches] += v
        total = 0.0
        for s in sub:
            total += s
        q3 = rel(total, net) / magnitude
        worst_q3 = max(worst_q3, q3)
        if total == net:
            bitzero_q3 += 1

    return worst_q2, bitzero_q2, worst_q3, bitzero_q3, counted


# ---------------------------------------------------------------- Q4

def q4(trials=5000, n=60):
    """A closed domain: every boundary facet is a wall, so the table gives phi = 0.0.

    The sum of exact zeros is exactly zero, so this should be bit-zero on every
    trial.  It is measured anyway, because 'should be' is how ADR-072 published
    three false claims.
    """
    nonzero = 0
    for _ in range(trials):
        phi = [0.0] * n
        net = 0.0
        for v in phi:
            net += v
        if net != 0.0:
            nonzero += 1
    return nonzero, trials


# ---------------------------------------------------------------- Q5

def q5(trials=20000, n=60, spread=6.0):
    """The relative measures' own scale, balanced and unbalanced.

    Checks that 0.5*sum|phi| is a denominator of the right order -- i.e. that a
    balanced field gives a SMALL relative imbalance and an unbalanced one a
    number of order 1, so the measure discriminates.
    """
    worst_balanced = 0.0
    worst_unbalanced = 0.0
    for _ in range(trials):
        for balanced in (True, False):
            phi = boundary_field(n, spread, balanced)
            magnitude = 0.0
            for v in phi:
                magnitude += abs(v)
            if magnitude == 0.0:
                continue
            throughput = 0.5 * magnitude
            net = 0.0
            for v in phi:
                net += v
            measure = abs(net) / throughput
            if balanced:
                worst_balanced = max(worst_balanced, measure)
            else:
                worst_unbalanced = max(worst_unbalanced, measure)
    return worst_balanced, worst_unbalanced


# ---------------------------------------------------------------- report

print("P19-CONT-001 pre-design measurements, seed 20261108")
print()

for scale, label in ((1.0, "metre-scale tets"), (1.0e-3, "millimetre-scale tets")):
    worst, bz, n, closure, dz = q1(scale=scale)
    print("Q1  per-cell residual of a conservative field, %s" % label)
    print("      worst |sum phi| / sum|phi|   %.4e" % worst)
    print("      bit-zero                     %d / %d  = %.2f%%" % (bz, n, 100.0 * bz / n))
    print("      worst closure |sum S|/sum|S| %.4e" % closure)
    print("      skipped (zero denominator)   %d" % dz)
    print()

for balanced in (True, False):
    w2, b2, w3, b3, c = q2_q3(balanced=balanced)
    tag = "balanced" if balanced else "unbalanced"
    print("Q2  (outflow - inflow) vs single-pass net, %s" % tag)
    print("      worst departure / sum|phi|   %.4e" % w2)
    print("      bit-exact                    %d / %d  = %.2f%%" % (b2, c, 100.0 * b2 / c))
    print("Q3  sum of patch subtotals vs single-pass net, %s" % tag)
    print("      worst departure / sum|phi|   %.4e" % w3)
    print("      bit-exact                    %d / %d  = %.2f%%" % (b3, c, 100.0 * b3 / c))
    print()

nz, n = q4()
print("Q4  closed domain (every facet a wall, phi = 0.0 exactly)")
print("      non-zero boundary nets        %d / %d" % (nz, n))
print()

wb, wu = q5()
print("Q5  the relative measure's discrimination, denominator 0.5*sum|phi|")
print("      worst for a BALANCED field    %.4e" % wb)
print("      worst for an UNBALANCED field %.4e" % wu)
