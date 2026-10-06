# P17-ARCH-001 — Structural FEA Architecture

```text
STATUS:   PASS
MILESTONE: P17-ARCH-001, the first milestone of P17 — Structural FEA
SCOPE:    architecture only. No element, no DOF, no stiffness, no solve.
```

## Baseline

```text
HEAD at start      4a41c4fb56670df70822757582fdd0f595f681cc
origin/main        4a41c4fb56670df70822757582fdd0f595f681cc
working tree       clean, 0 porcelain lines
P16 source trees   the eight paths still fingerprint 8ab30a31, P16's qualified
                   tree -- so the predecessor is demonstrably qualified on THIS
                   tree and not merely recorded as such
```

## Prerequisites

```text
P15 — Materials / Engineering Data    QUALIFIED, RESULT: PASS
P16 — Meshing                         QUALIFIED, RESULT: PASS, 15 of 15
                                      milestones, 3387/3387 x 3 presets
```

## What this milestone found before it built anything

**The audit changed the work.** Three things P17 was expected to design already
exist, built by the earlier phases *for this consumer, by name*:

```text
materials::LinearElasticConstants   "This is the boundary a structural solver
                                    consumes (P17). It hands over a complete
                                    set or it fails -- never a partial one, and
                                    never a fabricated default."
ConsumerKind::FeaLinearStatic and   P15 already split stiffness (E, nu) from
  FeaLinearStaticWithGravity        self-weight (+ density)
meshing::describesTheModel()        returns true only for MeshCurrency::Current
                                    -- the exact predicate a solver entry point
                                    needs
ADR-028                             already forbade a solver holding material
                                    data, naming P17
```

`requireLinearElasticConstants` already refuses a non-finite or non-positive
Young's modulus and a Poisson ratio outside `-1 < nu < 0.5` — the exact range
the brief asks `P17-MAT-001` to validate — and names every gap in one
diagnostic. So that validation is satisfied by consumption, not by
implementation.

What was left to build is the one thing nothing upstream could provide, because
P16's recorded gap is about **callers**:

> nothing FORCES a holder to call `isStale`

`Mesher::mesh()` returns a stale mesh deliberately — P16-VIZ-001 inspects stale
meshes — so the fix cannot live in P16 without breaking qualified behaviour. It
has to be a property of the consumer's signature.

Full audit: [DEPENDENCY_AUDIT.md](DEPENDENCY_AUDIT.md).

## The solver scope

```text
linear static | small displacement | small strain
isotropic linear elasticity | Tet4 | one solid | ux uy uz
```

`K u = F`, with `K` independent of `u`, infinitesimal strain
`epsilon = 1/2 (grad u + grad u^T)`, and `sigma = D epsilon`.

Small displacement is a **model assumption, not a verified property of a user's
load**, and is documented as such rather than implied to be checked.

Twenty-eight unsupported behaviours are listed and **refused rather than
approximated**, because a best-effort structural result is worse than a refusal:
the user cannot see the difference. [SCOPE.md](SCOPE.md),
[ADR-034](../../architecture/decisions/ADR-034-the-first-structural-solver-is-linear-static-tet4-on-one-solid.md).

## The module and its layer

A structural module **uses** meshing, so it cannot share meshing's layer — the
architecture check's rule is strictly-lower, so same-layer modules may not
include each other at all. It needed a number between `meshing` 4 and `io` 5,
and there was none.

That is the third time: ADR-006 moved `io` from 3 to 4 for `assembly`, ADR-015
moved it from 4 to 5 for `drawing`, and four roadmap phases sit in the same
band. So the table was **respaced in tens once**, relative order unchanged:

```text
core 0   sketch 10   features 20   assembly 30
drawing 40   meshing 40   structural 50   io 60   renderer 70   scripting 70
```

A layer value is now an ordinal with gaps. Nine free integers sit between any
two neighbours, and the checker says so.

The same change added **rule 6**, which no earlier rule could express: the
library must not include anything under `apps/`. Rule 2 keys on Qt's header
shape, so it stops `#include <QWidget>` in `src/structural/` but not
`#include "../../apps/bettercad_cli/Commands.hpp"`; and `apps/` is not a module,
so rule 3 has no layer to compare. A library file reaching a CLI parser or a
file dialog would have passed. Nothing in the tree had such an include, so it
closed the gap at zero cost — for every module.

[LAYERING.md](LAYERING.md),
[ADR-035](../../architecture/decisions/ADR-035-a-structural-module-at-layer-50-and-a-layer-table-respaced-in-tens.md).

## The input boundary

```cpp
Result<StructuralModel> requireStructuralModel(
    const Document&, const features::Regenerator&,
    const meshing::Mesher&, MeshControlId);
```

**No function in the module takes a `VolumeMesh`.** `StructuralModel` has one
private constructor and exactly one friend, so possession is the evidence and a
function that wanted to skip the checks could not construct its argument. The
device is P16's own — `VolumeMesh` and `GeometryMeshMap` are built the same way.

What possession proves, and what it does not:

```text
PROVES      the control exists; the body is eligible (regenerated, current, a
            solid, non-empty, valid, NOT behind a configuration override); a
            mesh is held and its currency is Current; the mapping and quality
            report came from that same mesh in ONE lookup; the material
            resolves to complete linear-elastic constants

DOES NOT    that the mesh is good enough for an accurate answer
PROVE       (P17-VALID-001), that loads and restraints resolve (P17-LOAD-001,
            P17-BC-001), or that the model is sufficiently constrained
            (P17-SOLVE-001). Those gates attach to this function.
```

Every check delegates to the phase that owns the question. Mesh **validity** is
not re-derived at all: P16 refuses to *hold* a mesh that fails its structural
verdict, so a `Current` mesh is valid by construction and P17 relies on the
contract rather than recomputing signed volumes.

[AUTHORITY_MODEL.md](AUTHORITY_MODEL.md),
[ADR-036](../../architecture/decisions/ADR-036-a-structural-analysis-input-is-validated-once-and-possession-is-the-proof.md).

## Authority and invalidation

The two halves of P17 are deliberately different: **analysis intent is
canonical, the solution is not.** Three rows of the invalidation matrix are
worth repeating:

```text
a material edit (E, nu)          -> result stale, mesh CURRENT. The mesh is a
                                    function of geometry and meshing intent;
                                    E is in neither
a regenerated geometry edit      -> mesh PERMANENTLY stale. Revision counters
                                    only move forward, so restoring the
                                    original dimension gives the original
                                    solid under a new stamp. Conservative on
                                    purpose
a boundary-set rename            -> nothing stale. P16 compares controls BY
                                    VALUE so a rename does not remesh, and P17
                                    must not undo that precision by keying on a
                                    control's revision
```

Full matrices, and the quality boundary (P16 measures, P17 judges):
[AUTHORITY_MODEL.md](AUTHORITY_MODEL.md).

## Tests

```text
tests/structural/StructuralAnalysisBoundaryTests.cpp     10 cases
  accepts a current mesh with a usable material          + possession carries
                                                           the mesh, a complete
                                                           map whose stamp
                                                           matches it, and P15's
                                                           four constants
  refuses a mesh of geometry that has since changed       THE sharp case: the
                                                           document is
                                                           REGENERATED, so only
                                                           the mesh is stale
  refuses a mesh whose sizing intent changed
  refuses after a failed generation of ELIGIBLE geometry
  refuses an ineligible body before looking at the mesh    proves check order
  refuses an unknown control
  refuses when nothing has been meshed
  refuses when the document names no material
  refuses a material that cannot describe linear elasticity
  reaches every InputProblem it declares

tests/architecture/  4 new fixtures, each one violation, each asserting the
                     message that names it
  structural -> io              REFUSED
  meshing -> structural         REFUSED  (the cycle)
  nglib.h in src/structural/    REFUSED
  src/ -> apps/                 REFUSED
  valid/src/structural/         ALLOWED, and it holds the layer-table entry in
                                place: delete the entry and this tree reports an
                                unknown module instead of passing
```

## Zero-match protection

Checked rather than assumed, with `-N` before the real run:

```text
ctest -R '^architecture\.' -N      Total Tests: 14   (10 existing + 4 new)
ctest -R 'StructuralInput' -N      Total Tests: 10
```

## Adversarial review

```text
QUESTIONS                 34  (30 from the brief, 4 of my own)
FINDINGS                   4
ARCHITECTURE DEFECTS       0
NARROWED                   1  the borrowed input type was copyable; now
                              move-only, with the residual hazard recorded
                              rather than claimed closed
ENUM CORRECTIONS           1  three unreachable values deleted
MY OWN TEST DEFECTS        1  fixed by sharpening the test, not by relaxing it
RECORDED LIMITATIONS       1  the permitted downward directions have no real
                              callers yet
GATE-BLOCKING              0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The permitted downward directions (io -> structural, renderer -> structural)
  are proved by the layer table and not by a compiled include, because nothing
  consumes the module yet. The FORBIDDEN directions are proved by fixtures,
  which is the half that prevents a mistake. P17-PERSIST-001, P17-VIZ-001 and
  P17-CLI-001 make the other half real.

A StructuralModel borrows the Mesher's mesh, map and quality report. Move-only
  removes the realistic way one escapes, but holding one across an edit still
  dangles. The mitigation is not a smarter type: re-preparing is a few lookups
  and one material resolution. The scoped-callback alternative is recorded in
  F1 as the fix if it ever bites.

The scope statement cannot be tested. It is a claim about what is NOT built,
  and what is verified is that the statement and the code agree -- two files in
  src/structural/, no element, no matrix, no linear algebra linked.

Five further input problems are named but not declared, because their canonical
  types do not exist yet. Declaring codes nothing could return is the defect F2
  corrected.

This MinGW toolchain has no ASan/UBSan, so P17's numerics will carry no
  sanitizer coverage. Inherited and recorded, not pretended otherwise.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 23:25:23 to 01:54:27,
2 h 29 min, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3401/3401
release-ext            0       0      0         0           0   3401/3401
debug-shared-ext       0       0      0         0           0   3401/3401

REPEAT (5x each of 579 selected tests, back to back)
release-ext            0                            579/579   229.10 s
debug-ext              0                            579/579   261.46 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        598 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs, including
                                   libbettercad_structural.dll
```

**3401** is P16's qualified 3387 plus this milestone's 10 structural cases and
4 checker self-tests. **598** objects is P16's 596 plus the structural library's
one translation unit, compiled once into the library and once into nothing else
— the test binary's object count moves with the new test file.

**Cross-preset equivalence.** The structural boundary gives the identical
result under `-O0 -g`, under the optimiser, and across a DLL boundary:

```text
debug-ext          All tests passed (234 assertions in 10 test cases)
release-ext        All tests passed (234 assertions in 10 test cases)
debug-shared-ext   All tests passed (234 assertions in 10 test cases)
```

The shared preset is the one that would catch a DLL-boundary defect — an
unexported out-of-line member on a data struct, or a runtime use of an exported
class's `static constexpr`. The previous three milestones each found one here.
This one did not, and the headers were grepped for the pattern before the
freeze rather than after.

## The qualified tree is the committed tree

The eight-path fingerprint, read three times:

```text
| WHEN                              | WHOLE FINGERPRINT                        |
| frozen, before the first build    | 536e14b585bbd82fc8fcb82c84da680d6860c2cd |
| recorded by the harness after the | 536e14b585bbd82fc8fcb82c84da680d6860c2cd |
|   last test of the last preset    |                                          |
| recomputed before the commit      | 536e14b585bbd82fc8fcb82c84da680d6860c2cd |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            b7aeeba2d1dfb6f0e57a159d4987c2f4c9cfdb9d
src                47a0eb3a9f51941aac846546c09c9946ff80f32f
tests              bb79c89b6d097eef5a7369c557afbf4fed379b41
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

The first two readings are the harness's own, written into
`qualification/qualification-times.txt` at both ends of the run — not a claim
made about it afterwards. The third was recomputed by the same
`read-tree` + `add -A` method immediately before `git add`.

The value is a function of the eight paths **plus the base tree HEAD pointed at
when it was taken**, which was `4a41c4f` throughout; the invariant that survives
a moving HEAD is the component list above, and it is checkable against the
published tree in one command:

```bash
git fetch origin
for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do
  echo "$p $(git rev-parse "origin/main^{tree}:$p")"
done
```

What moved after the freeze: `docs/verification/P17-ARCH-001/`,
`docs/architecture/decisions/ADR-034..036`, `ARCHITECTURE.md`, `TODO.md`,
`ROADMAP.md` and `README.md` — all documentation, none of it inside the
fingerprint, none of it configured, compiled, linked or read by a test.

## Result

```text
RESULT:   PASS
TESTS:    3401/3401 in debug-ext, release-ext and debug-shared-ext, each
          from clean; 0 warnings over 598 objects; 579 x 5 repeats in two
          presets; 17 stages, 0 failed
TREE:     536e14b585bbd82fc8fcb82c84da680d6860c2cd, identical at all three
          readings
EVIDENCE: this directory
ADRs:     ADR-034, ADR-035, ADR-036
```

## Revision

First issue, 2026-10-06.
