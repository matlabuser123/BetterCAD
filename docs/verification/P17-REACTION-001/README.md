# P17-REACTION-001 — Reaction Forces / Equilibrium

```text
TASK:            P17-REACTION-001
RESULT:          PASS
DATE:            2026-10-10
```

Recovers support reactions as the **constrained components of `K u - F`**,
attributes them per restraint without double-counting a shared degree of
freedom, and proves global force **and** moment equilibrium against the exact
assembled `F` that was solved. The tolerance was measured, not assumed.

## Baseline

```text
git status --short      clean
git rev-parse HEAD      5682b98e81cfcdcf268b8313e99c05cad4d58d31
git rev-parse origin/main
                        5682b98e81cfcdcf268b8313e99c05cad4d58d31
git rev-parse HEAD^{tree}
                        5ca50549d45d3fd59b3e6288ce9a6e37a5697628
git log -1 --oneline    5682b98 BetterCAD: add structural result recovery
```

## Prerequisites

Every one `[x]` with evidence, and no open checkbox anywhere above this
milestone's own section in `TODO.md`.

```text
P17-ARCH-001      [x]   536e14b5          P17-BC-001        [x]  60e01a98
P17-DATA-001      [x]   fcf2ea16          P17-ASSEMBLY-001  [x]  6d36b39e
P17-MAT-001       [x]   4d4698b3          P17-SOLVE-001     [x]  4e41fdd2
P17-DOF-001       [x]   9301b643          P17-POST-001      [x]  src bba30676
P17-ELEM-001      [x]   845b118c
P17-LOAD-001      [x]   27afe8d3
P16               QUALIFIED (P16-QUAL-001, 2026-10-06)
```

## What the audit found already decided

```text
the reaction quantity    SolvedSystem::fullResidual() is ALREADY `K u - F`
                         over the full system, computed in one pass with
                         per-entry finiteness -- and its header retained it for
                         THIS milestone by name: "CONSTRAINED ENTRIES ARE THE
                         SUPPORT REACTIONS [...] P17-REACTION-001 owns reaction
                         semantics and equilibrium"
u_c is EXACTLY zero      the solver assigns the whole vector 0.0 and writes
                         only free rows, so R_c = K_cf u_f - F_c holds with no
                         u_c term to approximate
the constrained set      ConstraintSet::constrained(), ascending and unique
the load oracles         PreparedLoads::resultantForce() and
                         resultantMomentAbout(mesh, origin) -- with the origin
                         ALREADY explicit and a mesh refusal already in place
the moment of a force    core's momentOf(lever, force) = lever x force, whose
                         signature already forces the caller to form x - O
an empty free system     SOLVES rather than factorising, so a fully
                         constrained model is a usable fixture
```

**What was missing was identity**: which restraint produced which constrained
degree of freedom. That gap is the one production finding.

## Architecture

[ADR-041](../../architecture/decisions/ADR-041-a-reaction-is-the-constrained-residual-and-a-shared-degree-of-freedom-is-counted-once.md),
six decisions with the candidates compared:

```text
1  the reaction is the constrained component of `K u - F` and nothing else --
   not a stress integral, not a recomputation, not the free residual
2  the sign convention, VERIFIED before it was documented: R is the force the
   support applies to the structure, so sum(F_ext) + sum(R) = 0
3  RestraintResolution gains its resolved DofIndex list. This changes
   qualified P17-BC code, and the alternative -- re-resolving after the solve
   -- is how a source mismatch hides
4  a shared degree of freedom is counted ONCE globally, and per-restraint
   summaries are ADDITIVE BY CONSTRUCTION rather than additive-with-a-warning
5  equilibrium is normalised by participating MAGNITUDES, not by the net
   resultant, with dimensioned floors
6  a support moment is DERIVED from the translational distribution; there are
   no rotational reaction degrees of freedom
```

Architecture rules preserved: the Document still owns canonical state; no OCCT
and no Qt near this module; `structural` at layer 50 includes only lower
layers; no Eigen in any public header; stable typed IDs; SI units throughout;
structured errors; no GUI state; nothing persisted.

## Implementation

```text
NEW   include/bettercad/structural/StructuralReaction.hpp
      src/structural/StructuralReaction.cpp
      tests/structural/StructuralReactionTests.cpp            17 cases
      tests/reference/StructuralReactionReferenceTests.cpp     4 cases
      tests/compile_fail/StructuralReactionMisuse.cpp          6 cases
                                                              27 in total
      docs/architecture/decisions/ADR-041-*.md

MOD   src/structural/CMakeLists.txt        the new source, nothing else
      tests/CMakeLists.txt                 two registrations
      tests/compile_fail/CMakeLists.txt    one group, six cases

MOD, AND THIS IS QUALIFIED P17-BC-001 CODE:
      include/bettercad/structural/StructuralConstraints.hpp
      src/structural/StructuralConstraints.cpp
      -- RestraintResolution gains its resolved DofIndex list. Additive, and
         it keeps a value the loop already computed and discarded.
```

### The public surface

```text
SupportReaction          { NodeId, Force3D, RestraintComponents constrained }
RestraintReaction        owned/shared DOF counts, forces and moments
ForceBalance             external, reaction, imbalance, 2 norms, scale, ratio
MomentBalance            the same, plus the ORIGIN as a field
EquilibriumTolerance     two ratios and two DIMENSIONED floors

recoverSupportReactions(model, system, restraints, loads, solution, origin,
                        tolerance)                  -> Result<SupportReactions>
reactionProblem(...)                                -> optional<ReactionProblem>
assembledForceResultant(mesh, numbering, force)     -> Result<Force3D>
assembledMomentResultant(mesh, numbering, force, O) -> Result<Moment3D>

SupportReactions::at(mesh, node)      nodal(), restraints(), of(id)
                 ::momentAbout(mesh, O)             forceBalance(), momentBalance()
                 ::sharedForce(), sharedMoment(), sharedDegreesOfFreedom()
```

## Conventions

[REACTION_CONVENTION.md](REACTION_CONVENTION.md). The short form:

```text
R = (K u - F) on constrained DOFs = the force the SUPPORT applies to the
                                    STRUCTURE
equilibrium                         sum(F_external) + sum(R) = 0
free DOFs                           NOT reactions -- solver diagnostics
units                               N, and N m for moments. No MPa, no N/mm
cardinality                         one entry per node with >= 1 constrained
                                    DOF; an unrestrained node is ABSENT
ordering                            nodes ascending by NodeId, components
                                    Ux Uy Uz, restraints as given
rotational reaction DOFs            NONE. A moment is derived, with an
                                    explicit origin
```

Source searches over `src/structural/StructuralReaction.cpp`:

```text
stiffness()                         0   -- the residual is READ, not recomputed
a second K u product                0
pressure / traction / facet / area  0   -- no load is re-integrated
a hand-written cross product        0   -- every moment goes through momentOf
unordered_map / unordered_set       0
stress / Stress6 / strainFrom       0   -- no stress integration
```

## Validation

[ANALYTICAL_REFERENCES.md](ANALYTICAL_REFERENCES.md),
[FORCE_EQUILIBRIUM.md](FORCE_EQUILIBRIUM.md),
[MOMENT_EQUILIBRIUM.md](MOMENT_EQUILIBRIUM.md),
[EQUILIBRIUM_TOLERANCE.md](EQUILIBRIUM_TOLERANCE.md),
[PER_RESTRAINT_AGGREGATION.md](PER_RESTRAINT_AGGREGATION.md).

The headline measurements:

```text
FULLY CONSTRAINED, the primary sign gate -- no factorisation in it
    applied (300, -500, 700) N  ->  reaction (-300, 500, -700) N
    ||e_F||2 = 0 N EXACTLY, eta_F = 0, eta_M = 0

PARTITION FORMULA, hand-derived on a 3x3 system
    R_1 = -1, R_2 = -1 exactly; agreement to 1e-14

ANALYTICAL CONTINUUM RESULTANTS
    pressure   F = -p A = -16800 N   ->  relative 4.33093e-16
    gravity    W = rho V g = 22.6328 N -> relative 1.56972e-16
    gravity moment c x W = (-0.792147, 1.357965, 0) N m -> 1e-9 x scale

SCALE INVARIANCE across nine orders of load
    1e-3 N -> eta_F 2.18e-16      1e6 N -> eta_F 1.76e-16

FAR ORIGIN, nodes to 0.126 m
    eta_F 1.44e-16, eta_M 8.67e-17 -- no cancellation penalty

LARGE MESH, RM-MESH-02: 850 nodes, 2550 DOFs, 300 constrained, 100 entries
    eta_F 1.48e-14, eta_M 2.50e-14   <-- the worst case, and 40x inside the gate

PURE COUPLE: sum(F_ext) = 0 with scale_F > 1000 N and |M_ext| > 1 N m
    both gates pass; force equilibrium alone would have missed any moment error

OVERLAPPING SUPPORTS
    listed > unique, shared > 0, global eta_F <= 1e-12
    owned(R1) + owned(R2) + shared == R_total exactly
    the NAIVE sum overshoots by exactly sharedForce -- measured
```

### The tolerance, measured

```text
selected   force 1e-12, moment 1e-12, floors 1e-12 N and 1e-12 N m
measured   worst eta_F 1.48e-14, worst eta_M 2.50e-14
margin     ~40x on the largest fixture, ~4000x on the small ones

solver residual vs equilibrium error: 2-3e-17 against 1-3e-16, consistently an
order apart. The solver's gate is on the FREE equations; this one is on the
whole body. P17-SOLVE's tolerance is referenced nowhere, and a compile-failure
case proves the field is ABSENT from EquilibriumTolerance.

The GEOMETRIC path measured 4e-16 -- NOT the 1e-9 the brief anticipated --
because a planar pressure face and a box under gravity are integrated exactly.
So ONE threshold covers both layers, on the strength of the measurement.
```

## Adversarial review

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
attacks              29  (25 from the brief, 4 added)
credible findings     3
production findings   1  -- fixed
test defects          2  -- both fixed
remaining blockers    0
```

```text
P1  PER-RESTRAINT INSPECTION WAS NOT BUILDABLE on P17-BC's provenance, which
    recorded counts rather than identities. The only alternative -- re-resolving
    each target after the solve -- is how a source mismatch hides. Fixed by
    retaining the DofIndex list the loop already computed, which also made a
    stronger attribution policy possible: additive by construction rather than
    additive-with-a-warning.
T1  EVERY EQUILIBRIUM CASE USED THE GLOBAL ORIGIN, where x - O == x, so a path
    that ignored the origin was a no-op and the probe SURVIVED. Three origins
    now, with guards requiring the moments to differ.
T2  a non-vacuity floor set ABOVE the quantity it guarded: |M| > 1 N m on a
    block whose moment is 0.792 N m. The result was exact; the guard was wrong.
```

## Mutation protection

[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

```text
20 probes, 20 KILLED, 0 SURVIVED
```

The sign flip (`F - Ku`) is killed by **18 of 20** tests. The reversed moment
lever by 16. The tolerance inflated to `1e-3` is killed, so the gate cannot be
loosened unnoticed. One probe survived its first run and exposed finding T1 —
a test gap, not a production defect — and is killed after the gap closed.

## Determinism

[DETERMINISM.md](DETERMINISM.md). Five repeats **bitwise** identical over every
channel including both balances; the multiplicity table is a sorted vector
rather than a hash map precisely because its result routes reactions into
buckets that are summed. Zero unordered containers in the module. Compensated
summation considered and **not** used, with the measurement that justifies
leaving it out.

## What is NOT claimed, and NOT built

```text
a predicted two-support split      a 3D continuum with two fixed faces is
                                   statically indeterminate, and a symmetric
                                   prediction would need a provably symmetric
                                   MESH, which P16 does not guarantee. The
                                   exact statics (R_A + R_B = -F) IS claimed;
                                   the split is measured and printed
analytical agreement on a CURVED
  pressure face                    the facet tiling approximates the area, so
                                   that comparison would measure P17-LOAD's
                                   discretisation rather than equilibrium
nonzero prescribed displacement    deferred. u_c = 0 throughout, and the
                                   algebra is written so a u_c term would
                                   extend it
a reaction application point       not computed. Force and moment resultants
                                   per support region are the scope
stress integration                 not used as a reaction source
mesh-quality acceptance            P17-VALID-001
display, arrows, scaling           P17-VIZ-001
persistence                        never: reactions are derived
bitwise cross-preset equality      not claimed
performance                        not measured as a gate
P17 as a whole                     not qualified. P17-QUAL-001 is separate
```

## Known limitations

```text
four ReactionProblem values are not reachable by any input today
    DegreeOfFreedomOutOfRange, NodeMissing, NonFiniteReaction,
    NonFiniteExternal and NonFiniteBalance are closed by what possession
    already proves: a SolvedSystem has finite entries and a validated
    ConstraintSet indexes within the system. They are kept because the refusal
    must be structural rather than a comment, and a later milestone building a
    model from a file opens the paths

the shared-attribution fields are not additive across restraints
    by nature, not by defect. They are named sharedForce/sharedMoment, the
    count is exposed, and the global aggregate counted ONCE is what makes the
    identity exact. A CLI or GUI must use owned + shared, which is what the
    tests assert
```

## Acceptance gate

```text
TASK:            P17-REACTION-001
IMPLEMENTATION:  one new public header, one new source, three new test files,
                 ADR-041, and an additive provenance field in qualified
                 P17-BC-001 code
TESTS:           21 unit + reference, 6 compile-failure, inside the full
                 unfiltered suite in three presets
VALIDATION:      a hand-derived partition formula, analytical continuum
                 resultants for pressure and gravity, the test's own moment
                 accumulation and origin-shift relation, and the exact
                 fully-constrained identity R = -F
RESULT:          PASS
EVIDENCE:        docs/verification/P17-REACTION-001/
TODO:            updated only on PASS
```
