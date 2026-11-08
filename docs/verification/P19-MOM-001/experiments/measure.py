"""P19-MOM-001 -- the arithmetic claims of ADR-073, measured BEFORE any C++.

P19-DIV-001's first ADR draft PREDICTED a bit-exact global sum and was wrong; a
test asserting it would have failed on a correct operator. P19-CONV-001
therefore measured first, and found two of its own predictions wrong. This
script does the same for the momentum assembly's five arithmetic claims.

It models the assembly exactly as the C++ will do it: a slot-wise sum of two
assembled matrices in CSR slot order, the transient diagonal added afterwards,
and the source accumulated term by term in a fixed order. No numpy, so the
arithmetic is plain IEEE-754 double in the order written.

Seed 202611081, fixed and recorded.
"""

import random
import struct

SEED = 202611081
random.seed(SEED)


def bits(x):
    """The exact bit pattern, so 'bit-exact' means bit-exact."""
    return struct.pack("<d", x)


def rel(a, b):
    """Relative departure of a from b, with b's scale."""
    scale = max(abs(a), abs(b))
    if scale == 0.0:
        return 0.0
    return abs(a - b) / scale


class Stat:
    def __init__(self, name, note=""):
        self.name = name
        self.note = note
        self.n = 0
        self.exact = 0
        self.worst = 0.0

    def add(self, a, b):
        self.n += 1
        if bits(a) == bits(b):
            self.exact += 1
        self.worst = max(self.worst, rel(a, b))

    def line(self):
        pct = 100.0 * self.exact / self.n if self.n else 0.0
        return (f"  {self.name:<52} {self.n:>7} cases  "
                f"{pct:>6.2f}% bit-exact  worst rel {self.worst:.3e}"
                + (f"\n      {self.note}" if self.note else ""))


def loguniform(lo, hi):
    return 10.0 ** random.uniform(lo, hi)


print(f"P19-MOM-001 / ADR-073 -- arithmetic measured before implementation")
print(f"seed {SEED}, plain IEEE-754 double, no numpy, order as written\n")

# ---------------------------------------------------------------------------
# Q1  The sum order of the three matrix terms is part of the contract.
#
# The C++ will compute  (conv + diff)  slot-wise, then add the transient to the
# diagonal: ((conv + diff) + trans). A reader might assume any order gives the
# same double. Measure whether it does.
# ---------------------------------------------------------------------------
q1a = Stat("Q1a ((conv+diff)+trans) vs (conv+(diff+trans))",
           "if this is not 100%, the accumulation ORDER is part of the contract")
q1b = Stat("Q1b ((conv+diff)+trans) vs ((trans+diff)+conv)")
for _ in range(200000):
    conv = random.uniform(-1.0, 1.0) * loguniform(-3, 3)
    diff = random.uniform(0.0, 1.0) * loguniform(-3, 3)
    trans = random.uniform(0.0, 1.0) * loguniform(-3, 3)
    a = (conv + diff) + trans
    q1a.add(a, conv + (diff + trans))
    q1b.add(a, (trans + diff) + conv)
print("Q1  matrix term accumulation order")
print(q1a.line())
print(q1b.line())
print()

# ---------------------------------------------------------------------------
# Q2  Hydrostatic cancellation: a fluid at rest stays at rest.
#
# With gravity g and a pressure field whose gradient is EXACTLY rho g, the
# momentum source must vanish:  -V (grad p)_i + rho V g_i == 0.
#
# The two terms are the same product in DIFFERENT groupings, because the
# gradient arrives from a caller and the gravity term is formed here:
#     pressure term  -(V * gradp)      with gradp = fl(rho * g)
#     gravity  term  +((rho * V) * g)
# Measure the residual relative to the term magnitude. This decides the
# tolerance of the hydrostatic test, and whether a bit-zero claim is possible.
# ---------------------------------------------------------------------------
# A RELATIVE measure against exact zero is meaningless -- rel(a, 0) is 1.0 for
# every non-zero a -- and the first run of this script reported exactly that:
# "worst rel 1.000e+00", a number about nothing. The measure that means
# something is |residual| / |term|, which is reported instead. Recorded rather
# than quietly fixed, because the figure looked like a catastrophic failure and
# was an artefact of the metric.
q2bitzero = 0
q2n = 0
q2worst_abs_over_term = 0.0
for _ in range(200000):
    V = loguniform(-9, -3)          # tet volumes, m^3
    rho = loguniform(-1, 4)         # kg/m^3
    g = random.uniform(-20.0, 20.0)  # m/s^2, any axis
    gradp = rho * g                  # what an exact reconstruction would give
    pressure_term = -(V * gradp)
    gravity_term = (rho * V) * g
    total = pressure_term + gravity_term
    q2n += 1
    if total == 0.0:
        q2bitzero += 1
    term = abs(gravity_term)
    if term > 0.0:
        q2worst_abs_over_term = max(q2worst_abs_over_term, abs(total) / term)
print("Q2  hydrostatic cancellation")
print(f"  Q2a -(V*fl(rho*g)) + ((rho*V)*g) == 0.0 exactly: "
      f"{q2bitzero}/{q2n} ({100.0 * q2bitzero / q2n:.2f}%)")
print(f"  Q2b worst |residual| / |gravity term| = {q2worst_abs_over_term:.3e}")
print("      so a bit-zero claim would be FALSE, and the bound is RELATIVE to the term")
print()

# ---------------------------------------------------------------------------
# Q3  Row-sum additivity.
#
# rowSumSi(A_mom, c) sums the COMBINED coefficients in ascending column order.
# The term-wise answer sums each operator's row separately and then adds. Same
# addends, different order, so a bit-exact claim needs measuring -- this is the
# exact shape of claim P19-DIV-001 and P19-CONV-001 each got wrong.
# ---------------------------------------------------------------------------
q3 = Stat("Q3a rowSum(conv+diff) vs rowSum(conv)+rowSum(diff), rel to the SUM")
# Q3a DIVIDES BY THE SUM, WHICH CAN CANCEL TO NEARLY NOTHING, so its worst case
# measures the cancellation and not the assembly. The bound a test can use
# divides by the row's own SCALE -- the largest coefficient in it -- which is
# the quantity that does not vanish. Both are reported, because quoting only
# the first would justify a tolerance two decades looser than the operator
# needs, and quoting only the second would hide that the sum itself is
# ill-conditioned near a null row.
q3scale_worst = 0.0
q3scale_exact = 0
q3scale_n = 0
for _ in range(50000):
    width = random.randint(2, 8)
    conv = [random.uniform(-1.0, 1.0) * loguniform(-3, 3) for _ in range(width)]
    diff = [random.uniform(-1.0, 1.0) * loguniform(-3, 3) for _ in range(width)]
    combined = 0.0
    for i in range(width):
        combined += conv[i] + diff[i]
    sc = 0.0
    for v in conv:
        sc += v
    sd = 0.0
    for v in diff:
        sd += v
    termwise = sc + sd
    q3.add(combined, termwise)
    scale = max(abs(conv[i] + diff[i]) for i in range(width))
    q3scale_n += 1
    if bits(combined) == bits(termwise):
        q3scale_exact += 1
    if scale > 0.0:
        q3scale_worst = max(q3scale_worst, abs(combined - termwise) / scale)
print("Q3  row-sum additivity")
print(q3.line())
print(f"  Q3b the same departure relative to the ROW SCALE (max |a_ij|): "
      f"{100.0 * q3scale_exact / q3scale_n:>6.2f}% bit-exact  "
      f"worst {q3scale_worst:.3e}")
print("      Q3b is the bound a test may assert; Q3a is why it must not divide by the sum")
print()

# ---------------------------------------------------------------------------
# Q4  The transient term's weight, and when it stops being visible.
#
# The transient diagonal is rho V / dt, in kg/s -- the same unit as phi and as
# mu A / d. As dt grows the term vanishes; measure at what ratio it stops
# changing the diagonal at all, which is the bound the "large dt approaches
# steady" test may assert.
# ---------------------------------------------------------------------------
print("Q4  the transient diagonal against a steady diagonal, in kg/s")
# THE FIRST RUN SAMPLED dt IN 1e-4..1e4 AND REPORTED 0/200000 INVISIBLE, which
# reads as "the transient term is always visible" and is a statement about the
# SAMPLING and not about the arithmetic: within those ranges rho V / dt never
# fell sixteen orders below the steady diagonal. The ranges now reach the limit
# deliberately, and the threshold ratio is what is reported.
lost = 0
trials = 200000
threshold = None
for _ in range(trials):
    rho = loguniform(-1, 4)
    V = loguniform(-9, -3)
    dt = loguniform(-4, 20)          # reaches the limit on purpose
    steady = loguniform(-6, 0)
    trans = rho * V / dt
    if steady != 0.0 and steady + trans == steady:
        lost += 1
        ratio = abs(trans / steady)
        threshold = ratio if threshold is None else max(threshold, ratio)
print(f"  transient term invisible in the diagonal sum: {lost}/{trials} "
      f"({100.0 * lost / trials:.2f}%)")
print(f"  largest ratio |trans/steady| at which it was still LOST: "
      f"{threshold:.3e}" if threshold is not None
      else "  never lost within these ranges")
print("      so 'a large dt approaches the steady system' is a BOUND, not an equality:")
print("      |A_transient - A_steady| <= max_c rho V_c / dt, which is what a test may assert")
print()

# ---------------------------------------------------------------------------
# Q5  The source accumulation order.
#
# b = b_conv + b_diff + b_transient + b_pressure + b_gravity, five addends of
# very different magnitudes. Measure whether the order matters, and by how
# much, so the header can state the order rather than leave it to chance.
# ---------------------------------------------------------------------------
q5 = Stat("Q5a declared order vs reversed order")
q5b = Stat("Q5b declared order vs ascending-magnitude order",
           "ascending magnitude is the numerically best order and is NOT used:")
for _ in range(200000):
    terms = [random.uniform(-1.0, 1.0) * loguniform(-6, 2) for _ in range(5)]
    declared = 0.0
    for v in terms:
        declared += v
    reverse = 0.0
    for v in reversed(terms):
        reverse += v
    ascending = 0.0
    for v in sorted(terms, key=abs):
        ascending += v
    q5.add(declared, reverse)
    q5b.add(declared, ascending)
print("Q5  source accumulation order")
print(q5.line())
print(q5b.line())
print("      the declared order is fixed and documented; it is not the most accurate")
print("      order, and claiming otherwise would be a claim no measurement supports")
print()

print("CONCLUSIONS CARRIED INTO ADR-073")
print("  1. No order-independence may be claimed of either accumulation (Q1, Q3, Q5).")
print("     The assembly fixes ONE order and the suite reproduces that order.")
print("  2. Hydrostatic cancellation is NOT bit-zero (Q2). The test asserts a")
print("     RELATIVE bound against the term magnitude, measured here.")
print("  3. The transient term vanishes into the diagonal for a large dt (Q4), so")
print("     the steady limit is a bound and never an equality.")
print("  4. Term decomposition can still be asserted BIT-EXACTLY, because the test")
print("     reproduces the production ORDER rather than a mathematically equal one.")
