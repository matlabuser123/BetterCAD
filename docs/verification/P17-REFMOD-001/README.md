# P17-REFMOD-001 — Structural Reference Models

```text
AUTHORIZED 2026-10-15 by an explicit scope decision from the owner ("start
P17-REFMOD-001"). TODO.md had recorded "NOT AUTHORIZED: P17-CLI-001 passing
will not be permission to start it", which blocked CLAUDE from starting it
unprompted -- not the owner from authorizing it. P17-QUAL-001 is NOT started.
```

```text
BASELINE:     460321a  (P17-CLI-001), HEAD == origin/main, working tree clean
              but for the untracked .claude/ settings directory
SCOPE:        a committed, deterministic, independently validated structural
              reference suite: ten model IDs in twelve documents, their
              analytical oracles, and the gates that make them qualification
              references rather than demos. No new structural capability, no
              new CLI command, no document format change, NO CORE CHANGE.
```

## Evidence

| Document | What it holds |
| --- | --- |
| [AUDIT.md](AUDIT.md) | what already existed, the six questions the brief asked that the tree had answered, and the four findings that changed the models' shape |
| [REFERENCE_MODELS.md](REFERENCE_MODELS.md) | each model: geometry, mesh, loads, restraints, expected outcome, main parameter, and its oracle |
| [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md) | the oracles, the tolerance table with every measurement beside its gate, the convergence study, and **findings F1, F2, F3** |
| [RESULTS.md](RESULTS.md) | every measured table: the twelve documents through the production path, the model-change gate on all ten solvable ones, the bands, persistence, determinism |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | the attack list, 3 measured limitations carried to the owner, 6 suite defects found and fixed |
| [MUTATION.md](MUTATION.md) | 14 probes, 14 killed: 11 on the models, the catalog and the oracles, 3 on production formulas |
| [FREEZE.md](FREEZE.md) | the three-preset qualification, the fingerprints, the cross-preset equivalence |
| [qualification/](qualification/) | the runner's output, the harness scripts and logs |

## What was built, and what was reused

The audit came before any production code, and its finding is the same one
P16-REFMOD-001 made: **BetterCAD already has a reference-model system, now on
its sixth suite**, and this milestone adds the same five things in the same
five places and nothing else.

```text
NEW
  examples/reference_models/StructuralReferenceModels.hpp  the declaration
  examples/reference_models/StructuralModels.cpp           the builders
  examples/reference_models/StructuralCatalog.cpp          kind -> document
  tests/reference/StructuralTestSupport.hpp                the shared checks
  tests/reference/StructuralModelsTests.cpp                16 cases

EXTENDED
  examples/reference_models/main.cpp        a --structural loop, which is also
                                            the CLI fixture and the evidence
                                            generator, and which runs the
                                            stale-mesh refusal on EVERY model
  examples/reference_models/CMakeLists.txt  three sources, BetterCAD::structural
  tests/reference/Analytic.hpp              an SI linear-elasticity section:
                                            Lame constants, Hooke's law, the
                                            affine strain, von Mises by the
                                            deviatoric route, the axial bar,
                                            the Timoshenko cantilever, the two
                                            EXACT mean-stress identities
  tests/CMakeLists.txt                      the suite, and 41 fresh-process
                                            CLI fixtures
  tests/cli/ZeroMatchGuard.cmake            this milestone's two filters

PRODUCTION CHANGE
  none. Not one file under src/, include/ or apps/.
```

## Reference suite contract

Ten mandatory model IDs in twelve documents -- RM-FEA-06 and RM-FEA-07 are
pairs, because comparing a body with the same body rigidly transformed, and a
root refinement with its mirror, IS the case. Every document carries a
synthetic steel assigned at document level, a `MeshControl` named "Mesh" and
one `StructuralAnalysis` named "Study"; the two refusals, RM-FEA-08 and
RM-FEA-10, are committed in the state that is refused, so the refusal is
about the model and not about a missing analysis.

Full statement: [REFERENCE_MODELS.md](REFERENCE_MODELS.md).

## Analytical oracles

`tests/reference/Analytic.hpp` includes no BetterCAD header and **cannot**
call a production formula. Three of the oracles are exact where a reader
would expect a tolerance, and the suite holds the solver to them at 1e-9 and
measures 1e-15:

- the axial bar on ROLLERS, whose exact solution is affine and which a linear
  tetrahedron therefore reproduces: `delta = FL/AE`, `sigma = F/A`, `R = -F`,
  `U = F delta / 2`, every node and every element;
- the mean axial stress under a pressure (`-p`) and under self-weight
  (`-rho g H / 2`), which are statements of discrete equilibrium by the
  virtual work of `u = z e_z` and hold whatever the mesh;
- the patch test, where `K u` for an affine field equals the boundary
  tractions `sigma S / 3` per corner and is zero at the interior node.

Tolerance table with every measurement:
[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Results

```text
MODEL       OUTCOME   NODES  TET4  FREE  RESIDUAL  PIVOT    eta_F     eta_M
RM-FEA-01   solve         9    12    15  2.7e-17   0.130    1.5e-16   1.9e-16
RM-FEA-02   solve        10    16    18  1.8e-17   0.0951   5.4e-16   1.5e-15
RM-FEA-03   solve       138   489   306  1.8e-17   4.8e-04  1.1e-13   6.0e-13
RM-FEA-04   solve         8     6    12  3.7e-17   0.330    5.0e-18   1.5e-17
RM-FEA-05   solve        10    16    18  4.6e-17   0.0329   6.2e-16   5.6e-16
RM-FEA-06   solve x 2     9    12    15  4.7e-17   0.0859   1.4e-16   8.7e-17
RM-FEA-07   solve x 2    78   369   222  2.7e-17   0.0386   1.1e-15   2.5e-15
            (mirror)     60   271   168  2.0e-17   0.0360   3.1e-15   6.1e-15
RM-FEA-08   REFUSAL by the pivot gate, 8 constrained DOFs, smallest pivot 1.1e-17
RM-FEA-09   solve         9    12    15  4.0e-17   0.147    5.5e-17   1.3e-16
RM-FEA-10   REFUSAL at the load stage, loads_unresolvable, nothing substituted

10 model IDs expected, 10 executed, 10 solved, 2 refused, 12 documents
every solvable document refused as mesh_stale after its main dimension moved,
  then remeshed and re-solved to a different answer
```

Tables: [RESULTS.md](RESULTS.md).

## The three findings worth a reviewer's attention

**F1. A correct bending solve crosses P17-REACTION-001's moment threshold.**
The production `EquilibriumTolerance` refuses a normalized moment imbalance
above 1e-12, a value measured on well-conditioned blocks with a 40x margin
and a recorded caveat that a larger mesh "could cross it without anything
being wrong". The cantilever's fine level (2465 needle elements) measured
1.15e-12 and the committed cantilever lengthened to 120 mm 1.27e-12; the
reactions still balance the applied load to 1e-9. The committed cantilever
passes the default (6.0e-13) and is solved under it everywhere; the
convergence levels are solved under a stated 1e-9 band with every `eta_M`
recorded. **No threshold was changed**: revising a measured, qualified value
is an ADR for the owner, and the measurement is now on file.

**F2. Linear Tet4 locks in bending.** The cantilever deflects 3.8%, 4.7% and
4.8% of Timoshenko across its three levels. Monotone, error falling, 1.27x
from coarse to fine -- and nothing more is claimed.

**F3. OCCT never subdivides a developable wall along its ruling.** Every
wall triangle of the cylinder spans its full 100 mm at every deflection, so
every tetrahedron owning one is a needle with a 100 mm edge at the outer
fibre, and refinement through the global target cannot shorten it. This is
the planar-face limitation P16-REFMOD-001 recorded, in its curved form, and
it is the cause of F2's slow trend and F1's conditioning. Asserted as a
measurement (`longest boundary edge > 0.99 L`) so a future surface mesher
that fixes it is noticed here.

Two more measurements changed the models themselves and are recorded in
[AUDIT.md](AUDIT.md): an 80 x 50 x 30 block gave two DIFFERENT
tetrahedralisations of the same corners in its two placements (RM-FEA-06 now
uses the pair P17-POST-001 measured, and asserts the correspondence), and a
slab control on a cap refines nothing unless size-tree boxes fit inside the
section (RM-FEA-07 is 40 x 60 for that reason).

## How to run the suite

```text
bettercad_tests "[refmod][structural]"       16 cases, 133 872 assertions

ctest -R "unit\.Structural(Patch|AxialBar|Cantilever|PressureBlock|
                           GravityBlock|Transformed|LocalRefinement|
                           UnderConstrained|StaleMesh|Unresolved|Reference)"
                                             16, in process
ctest -R "refmod\.structural\."              41, fresh processes
```

Both filters are in `ZeroMatchGuard.cmake` with asserted minimums; the
repeat filter of every qualification since P17-ASSEMBLY-001 (`unit\.Structural`,
`refmod\.`) already covers both without a change.

The models themselves, as twelve `.bcad` documents, come from

```text
bettercad_example_reference_models --structural [--out <dir>]
```

whose own exit code is an assertion: it fails if any model does not build or
regenerate, solves when it must not, fails to solve when it must, publishes a
result after a refusal, or is not refused as `mesh_stale` after its main
dimension is edited.

## Determinism

Five fresh documents each of six models, built, regenerated, meshed and
solved from nothing shared: one fingerprint each, bit for bit, over every
displacement, strain, stress, principal value, von Mises, hydrostatic stress
and reaction; a changed pressure changes it. All twelve documents give the
same outcome and the two refusals the same diagnostic text over three
attempts. Save, load, regenerate, mesh and solve reproduces every solution
bit for bit.

## Pre-freeze gate, debug-ext

```text
ctest, unfiltered     4006 / 4006, 1262.85 s     (3949 + 16 + 41)
```

## Mutation testing

```text
14 probes: 14 KILLED, 0 survived, 0 equivalent
  3 on production formulas -- a consistent load of A t / 2, a D with 2 mu on
    its shear diagonal, an outward pressure -- each killed by a closed form
  2 on the ORACLE itself, both killed, so the comparison is between two
    independent computations
```

[MUTATION.md](MUTATION.md).

## Three-preset qualification

See [FREEZE.md](FREEZE.md).
