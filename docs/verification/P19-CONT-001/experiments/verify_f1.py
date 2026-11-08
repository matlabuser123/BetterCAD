"""Verify review finding 1 independently: accumulationResidual's denominator.

The claim under test: the numerator |globalNetFlux - netBoundaryFlux| is a
rounding artifact of a sum over ALL CELLS, whose error scale is the ALL-FACES
flux magnitude, while CfdContinuity.cpp divides it by the BOUNDARY-ONLY
magnitude. If true, a mesh with large interior flux and small boundary flux
reports a huge residual for perfectly correct arithmetic.

Modelled on the two-cell fixture: 1 internal face, 6 boundary faces, 3 of them
on each cell, with the scatter ADR-071 specifies (+flux to the owner, -flux to
the neighbour) done exactly as the operator does it.
"""


def model(internal, boundary, n_boundary_per_cell=3):
    # Per-cell accumulation, in FaceIndex order: internal face first (ADR-064
    # numbers internal faces first), then that cell's boundary faces.
    owner = 0.0
    owner += internal                      # +flux
    for _ in range(n_boundary_per_cell):
        owner += boundary
    neighbour = 0.0
    neighbour += -internal                 # -flux, the negation
    for _ in range(n_boundary_per_cell):
        neighbour += boundary

    # globalNetFlux: the divergence sums the per-cell residuals
    global_net = 0.0
    for v in (owner, neighbour):
        global_net += v

    # netBoundaryFlux: the divergence sums the boundary faces
    net_boundary = 0.0
    for _ in range(2 * n_boundary_per_cell):
        net_boundary += boundary

    artifact = abs(global_net - net_boundary)

    # the two candidate denominators
    boundary_only = 0.0
    for _ in range(2 * n_boundary_per_cell):
        boundary_only += abs(boundary)
    all_faces = boundary_only + abs(internal)

    def ratio(num, den):
        if den <= 0.0:
            return 0.0
        try:
            return num / den
        except (ZeroDivisionError, OverflowError):
            return float("inf")

    return {
        "artifact": artifact,
        "mine (boundary-only denom)": ratio(artifact, boundary_only),
        "ADR-071 (all-faces denom)": ratio(artifact, all_faces),
    }


print("VERIFYING REVIEW FINDING 1")
print("kCfdContinuityAggregationTolerance = 1e-14, and the suite asserts")
print("accumulationResidual < that bound.")
print()
cases = [
    ("balanced scales (what every fixture uses)", 1.0, 1.0),
    ("interior 1e6, boundary 1e-6", 1.0e6, 1.0e-6),
    ("interior 1e12, boundary 1e-12", 1.0e12, 1.0e-12),
    ("interior 1e200, boundary 1e-200", 1.0e200, 1.0e-200),
    ("interior 1e300, boundary 1e-300", 1.0e300, 1.0e-300),
]
for label, internal, boundary in cases:
    r = model(internal, boundary)
    print("%-42s" % label)
    print("    artifact                  %.6e" % r["artifact"])
    print("    mine  (boundary denom)    %.6e   %s" % (
        r["mine (boundary-only denom)"],
        "FAILS the 1e-14 bound" if r["mine (boundary-only denom)"] > 1e-14 else "within bound"))
    print("    ADR-071 (all-faces denom) %.6e   %s" % (
        r["ADR-071 (all-faces denom)"],
        "FAILS the 1e-14 bound" if r["ADR-071 (all-faces denom)"] > 1e-14 else "within bound"))
    print()

print("AND THE CLOSED-DOMAIN CASE (finding 2): a wall boundary gives bit-zero")
print("boundary phi, so the boundary-only denominator is EXACTLY ZERO and")
print("relativeDeparture returns 0.0 by its scale<=0 arm, whatever the interior")
print("flux or the scatter did.")
r = model(1.0e6, 0.0)
print("    interior 1e6, boundary 0.0")
print("    artifact                  %.6e   (NON-ZERO: the real artifact)" % r["artifact"])
print("    mine  (boundary denom)    %.6e   <- reported, and it cannot read false"
      % r["mine (boundary-only denom)"])
print("    ADR-071 (all-faces denom) %.6e   <- what it should be" % r["ADR-071 (all-faces denom)"])
