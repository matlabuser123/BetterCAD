# P17-QUAL-001 — ADR audit

```text
SUBJECT:  whether every architectural decision P17 took is still what the code
          does
METHOD:   each clause of each decision checked against the committed tree by
          search and by reading the cited file:line, not by reading the ADR
          back to itself; every IMPLEMENTED row names the test that pins it
ADRs:     ADR-034 .. ADR-048, fifteen decisions
CLAUSES:  206 checked -- 195 IMPLEMENTED, 11 DRIFT, 0 NOT FOUND
FINDINGS: 11, every one a sentence that was true when written and is now
          false in a detail; 0 invariant violations; 8 ADRs amended, and
          docs/ARCHITECTURE.md corrected
```

The clause-by-clause tables, with the file:line of every implementation and
the name of every pinning test, are in
[ADR_AUDIT_CLAUSES.md](ADR_AUDIT_CLAUSES.md). This file is the summary and
the resolution of each finding.

## Per ADR

```text
| ADR     | DECISION                                             | CLAUSES | IMPL | DRIFT |
| ADR-034 | linear-static Tet4 on one solid, refused not approx. |    8    |   7  |   1   |
| ADR-035 | structural at layer 50, table respaced in tens       |    9    |   8  |   1   |
| ADR-036 | one validated input boundary, possession the proof   |   11    |   9  |   2   |
| ADR-037 | index space from the mesh enumeration, interleaved   |   16    |  15  |   1   |
| ADR-038 | a BetterCAD-owned CSR that Eigen can map             |   17    |  14  |   3   |
| ADR-039 | SimplicialLDLT with a pivot gate, Eigen PRIVATE      |   17    |  17  |   0   |
| ADR-040 | recovered fields a stage product, Voigt conventions  |   18    |  18  |   0   |
| ADR-041 | a reaction is the constrained residual, shared once  |   13    |  13  |   0   |
| ADR-042 | quality on measured accuracy laws, one solve entry   |   16    |  15  |   1   |
| ADR-043 | deformed display is an override position buffer      |   12    |  11  |   1   |
| ADR-044 | a contour is a colour per facet corner               |    8    |   8  |   0   |
| ADR-045 | an overlay is segments in SI metres                  |   16    |  16  |   0   |
| ADR-046 | a tetrahedron-derived boundary carries its owner     |   12    |  12  |   0   |
| ADR-047 | commands edit one definition; settings canonical     |   15    |  14  |   1   |
| ADR-048 | persistence is one more object type; nodal force     |   18    |  18  |   0   |
|         | refuses the save                                     |         |      |       |
| TOTAL   |                                                      |  206    | 195  |  11   |
```

**Every load-bearing invariant holds in the code**, each at a line that was
read: three identity domains with `DofIndex` outside `core/Id.hpp`;
interleaved `3k + c + 1` numbering with a stamp-plus-count remesh refusal; a
0-based `FreeEquationIndex` whose default constructor is deleted; a two-pass
CSR assembly where every contribution is `+=` and nothing is pruned; the row
space is the free numbering of the empty constraint set; `SimplicialLDLT`
behind a `vectorD()` pivot gate and an independent residual computed over
BetterCAD's own CSR, with no penalty, pinning or epsilon anywhere; Voigt
`XX YY ZZ XY YZ ZX` with engineering shear in the field names and `mu` on
D's shear diagonal; `tensorOf(Strain6)` halving each gamma; reactions as the
constrained entries of the full residual with the owned/shared split;
quality floors `1e-10` and `3e5` with no dihedral failure and `< 6` DOFs a
hard failure; one `m -> mm` conversion at the kernel; the nodal-force save
refusal atomic and the reader refusing `"nodal_force"` by name;
`kDocumentFormatVersion == 2`; no public header includes Eigen, and
`CheckLayering.cmake` rule 7 fails the build if one does. ADR-039's licence
evidence was re-verified against the vendored Eigen tree: no `COPYING.LGPL`
or `COPYING.GPL`, the MPL statement in `Amd.h`, and the library's own
`d == RealScalar(0)` check that the pivot gate exists to strengthen.

## The eleven findings, and what was done about each

CLAUDE.md's conflict order puts verification evidence above architecture, so
where an ADR disagreed with the code the question was whether the CODE broke
an invariant of the decision. In no case did it. Each finding is a sentence
overtaken by a later milestone, a count, or a mechanism described
differently from how it was built -- the same category as P16-QUAL-001's
ADR-031 finding, and resolved the same way: **the ADR carries a dated
amendment recording what shipped and why; the code is right and is not
touched.**

```text
| #   | ADR     | WHAT THE ADR SAYS                        | WHAT THE TREE HAS                              | RESOLUTION |
| F1  | ADR-034 | "no element, no DOF, no stiffness and    | src/structural compiles Tet4Element,           | amended:   |
|     |         | no solve exists in src/structural/"      | StructuralDof, StructuralSystem,               | scoped to  |
|     |         | (Verification)                           | StructuralSolve -- each added by the milestone | ARCH-001   |
|     |         |                                          | the ADR names for it                           |            |
| F2  | ADR-035 | the respace is reflected in prose in     | docs/ARCHITECTURE.md still said sketch 1 ..     | ARCHITEC-  |
|     |         | ARCHITECTURE.md                          | renderer 6, no meshing/structural row          | TURE.md    |
|     |         |                                          |                                                | corrected; |
|     |         |                                          |                                                | ADR amended|
| F3  | ADR-036 | "nine cases" (Verification)              | ten TEST_CASEs                                 | amended    |
| F4  | ADR-036 | quality/load/restraint/sufficiency gates | they live in validateStructuralAnalysisForSolve| amended:   |
|     |         | "are added to this function"             | (ADR-042 Decision 4), not requireStructuralModel| superseded|
| F5  | ADR-037 | "P17-BC-001 ... never stores an index"   | RestraintResolution::constrained holds the     | amended:   |
|     |         |                                          | DofIndex list since ADR-041; derived, never    | invariant  |
|     |         |                                          | persisted                                      | intact     |
| F6  | ADR-038 | "links no linear algebra at all"         | PRIVATE_LINK BetterCAD::eigen (ADR-039)        | amended    |
| F7  | ADR-038 | "wraps the same arrays in Eigen::Map at  | StructuralSolve.cpp CONVERTS through           | amended:   |
|     |         | zero copy"                               | Eigen::Triplet + setFromTriplets, and says why | exact, not |
|     |         |                                          | (unsigned 64-bit index vs Eigen's signed one)  | zero-copy  |
| F8  | ADR-038 | "no Eigen::Triplet anywhere in the tree" | StructuralSolve.cpp:265                        | amended    |
|     |         | (Context)                                |                                                |            |
| F9  | ADR-042 | solveStructuralAnalysis(..., settings)   | (..., origin = Point3D{}, tolerance = {});     | amended    |
|     |         |                                          | settings read from the definition (ADR-047)    |            |
| F10 | ADR-043 | toModelLength "is the project's only     | a second identical one-line wrapper in         | amended:   |
|     |         | metres-to-millimetres conversion"        | OcctOverlayPresentation.cpp (ADR-045); both    | kernel     |
|     |         |                                          | call the kernel's single toModel               | still one  |
| F11 | ADR-047 | the command class list                   | omits DeleteStructuralAnalysisCommand and      | amended    |
|     |         |                                          | SetStructuralAnalysisDefinitionCommand         |            |
```

Why none of these is a gate failure, stated once: the gate asks whether the
architecture the ADRs decided is the architecture the tree implements. A
divergence touching an invariant -- a solver index that became CAD intent, a
result on a document object, a second stiffness formula, a penalty on the
diagonal -- would be an automatic FAIL with no reconciliation available.
Each of the eleven is instead a document describing an earlier state of a
tree that then moved in the direction the decision itself pointed, or a
count, or a mechanism chosen for a reason the code records at the line. The
invariant each sentence was protecting was re-checked individually and holds.

## Three observations beside the ADR texts

Recorded for the reader and NOT edited, because the qualification changed no
production file and these are comments and a name:

```text
tests/structural/StructuralValidationTests.cpp:299
    named "...ExactlyTwoAreWarnings", asserts warnings == 3 at :308, and the
    header (StructuralValidation.hpp:284-285) says three. QualityPolicyUnusable
    was demoted to a warning after the name was written. The ASSERTION is the
    one that runs and it is right; the name is stale.
include/bettercad/structural/StructuralSystem.hpp:10-16
    repeats F6 and F7 ("still links none", "Eigen::Map ... at zero copy").
include/bettercad/structural/StructuralValidation.hpp:649-656
    says the moment origin "is explicit because ... a mutation proved a
    default of zero made an origin-ignoring path a no-op", then defaults it to
    Point3D{}. The mutation that comment remembers is caught by the tests that
    pass a non-zero origin (StructuralReaction_BalancesTheMomentAboutANonZeroOriginToo;
    RM-FEA-06's placed document, whose reactions have three non-zero
    components about the origin), so the default is a convenience and not a
    hole.
include/bettercad/structural/StructuralReaction.hpp:433-434
    says the constrained set is "compared element by element"; the code
    compares the count and per-resolution membership, because SolvedSystem
    keeps no constrained list to compare against. Same refusal, different
    mechanism.
cmake/BetterCADDependencies.cmake:50
    still describes Eigen as "private to bettercad_sketch"; sketch, assembly
    and structural each link it PRIVATE.
```

## What could not be checked from the tree

```text
ADR-035   "447 files, 0 violations"            needs the checker to run; the
                                               architecture.* tests run it in
                                               every preset of this qualification
ADR-042   "no meshing source file changed"     a VCS claim; the P17-VALID-001
ADR-046   "no change to the qualified P16      and P17-VIZ-001 FREEZE files
          meshing module"                      record the subtree hashes that
                                               prove it, and this audit did not
                                               re-derive them
ADR-047   "three predecessors are requalified" P17-CMD-001's FREEZE records it
ADR-041/042 the threshold MEASUREMENTS         the numbers are in the code; the
                                               sweeps are the owning milestones'
                                               evidence, and the warning
                                               envelope is re-measured on every
                                               reference mesh by
                                               StructuralValidation_TheEnvelopeBoundsReallyAreTheMeasuredWorstCase
```

## Revision

First issue, 2026-10-15. The clause tables were produced by a reviewing agent
reading the tree with no write access, and every DRIFT row was re-read by the
qualifier at the cited line before an amendment was written.
