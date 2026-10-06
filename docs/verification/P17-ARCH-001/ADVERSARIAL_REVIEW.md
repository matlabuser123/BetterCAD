# P17-ARCH-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the structural architecture before it is built on
QUESTIONS: 30 from the brief, plus 4 of the reviewer's own
FINDINGS: 4 -- 1 design weakness narrowed, 1 enum correction, 1 test defect,
          1 recorded limitation
ARCHITECTURE DEFECTS REMAINING: 0
GATE-BLOCKING: 0
```

Each answer below was obtained by reading the code or running something. Where
the answer was "yes, it could", the fix came before the box was ticked.

## Findings

### F1 — the validated input was copyable, and it borrows (NARROWED)

`StructuralModel` holds three raw pointers into the `Mesher`'s held state and
had no copy or move declarations, so it was implicitly copyable. The realistic
failure is not a dangling original — that hazard is visible at the call site —
but a **copy stashed in a container or a member** that outlives the mesher it
came from, where nothing at the point of use suggests a lifetime at all.

Considered and rejected: a scoped form, `withStructuralModel(..., callback)`,
which makes escape structurally impossible. It would be strictly safer and was
rejected because `P17-ASSEMBLY-001`, `P17-SOLVE-001` and `P17-POST-001` will
each want to take a prepared model as a parameter, and forcing them to nest
inside one callback would distort three later milestones to fix a hazard that
has a cheaper partial answer.

**Fixed as far as it can be:** the type is now move-only. A move cannot be
stored by accident, because the source is visibly spent.

**The residual is recorded rather than claimed closed.** Holding a moved-from
model across an edit still dangles. The answer is not a smarter type but not
holding one: re-preparing is a few lookups and one material resolution, with no
geometry or mesh work. If the hazard ever bites in practice, the scoped form is
the fix and this finding is where to start.

### F2 — two InputProblem values nothing could return (CORRECTED)

The first draft declared `MeshNotStructurallyValid`, `MappingUnavailable` and
`MappingDoesNotMatchTheMesh`. The audit showed all three unreachable:

```text
Mesher::generate refuses to HOLD a mesh that fails volumeMeshFor, which
  validates -- so a held mesh is structurally valid by construction
Held { VolumeMesh mesh; GeometryMeshMap map; MeshQualityReport quality; ... }
  -- all three are members of one struct, so a held mesh's map can be neither
  absent nor from a different mesh
```

An enum value nothing can return is a placeholder, which this project treats as
a failed gate. All three were **deleted**, and the header now records *why* the
list is seven values rather than ten — turning a deletion into a statement
about what P17 relies on P16 for.

The upside is the better architecture: P17 does not re-derive signed volumes,
and the mesh/map pairing is guaranteed by taking both from one lookup rather
than by a runtime check that could never fail.

### F3 — my own test asserted the wrong diagnostic (FIXED)

`StructuralInput_RefusesAfterAFailedGeneration` deleted the body to force a
generation failure, then expected `MeshGenerationFailed`. It got
`GeometryIneligible` — correctly, because the boundary asks geometry **first**,
which is the ordering ADR-036 chose on purpose.

The test was wrong, not the code, and the fix was not to relax the assertion but
to find a scenario the enum value genuinely covers: a body that is perfectly
eligible with a **meshing request that cannot be honoured**. An unresolvable
local sizing reference does it — `resolveSizing` reports an unresolved control
and `volumeMeshFor` declines to proceed.

Worth recording because the first instinct was to wonder whether
`MeshGenerationFailed` was a fourth unreachable value. It is not; it needed a
sharper test. Measured:

```text
eligible body, failed remesh: 12 tetrahedra still held, currency generation
failed, analysis refused as mesh_generation_failed
```

### F4 — the permitted downward directions have no real callers yet (LIMITATION)

`io`, `renderer` and the CLI do not consume `structural` yet, so
`io -> structural` and `renderer -> structural` are proved only by the layer
table and not by a compiled include. The fixtures prove the **forbidden**
directions, which is the half that matters for preventing a mistake, but the
permitted half is currently a declaration.

`P17-PERSIST-001`, `P17-VIZ-001` and `P17-CLI-001` are where it becomes real.
Recorded rather than overstated.

## The brief's 30 questions

**Can P17 solve a `VolumeMesh` reference without checking whether it is stale?**
No, and not by discipline: **no function in the module takes a `VolumeMesh`.**
The whole public surface is four declarations, and the only entry is
`requireStructuralModel(document, regenerator, mesher, control)`.
`StructuralModel`'s single constructor is private with exactly one friend, so a
function that wanted to skip the checks could not construct its argument.

**Can a stale CAD body be solved because the old mesh is still valid
internally?** No, and this is the sharp case rather than the easy one.
`StructuralInput_RefusesAMeshOfGeometryThatHasSinceChanged` changes the depth
**and regenerates**, so the geometry is current again and the mesh is the only
stale thing. It is still internally valid — 12 tetrahedra, still returned by
`Mesher::mesh()` — and the analysis is refused on `MeshCurrency::StaleGeometry`.

**Can failed CAD regeneration leave an old FEA result current?** The question
belongs to `P17-DATA-001`, which defines result currency. What this milestone
guarantees is the input half: a failed or blocked regeneration is
`GeometryIneligible` through P16's boundary, so no new result can be produced.
The invalidation matrix in `AUTHORITY_MODEL.md` states the dependency a result
must carry.

**Can changing E or nu leave a result current?** No — and the matrix records
something sharper: a material edit invalidates the **result** and not the
**mesh**. The mesh is a function of geometry and meshing intent, and `E` is in
neither. An architecture that invalidated the mesh would remesh a hundred
thousand elements to answer a question about a number.

**Can a mesh-size change leave a result current?** No. `MeshCurrency::StaleIntent`,
tested by `StructuralInput_RefusesAMeshWhoseSizingIntentHasChanged`.

**Can P17 own a second material database?** No, and ADR-028 already forbade it
by name before this milestone existed. Verified by search:
`src/structural/` and `include/bettercad/structural/` contain **no raw
`double` or `float` data member at all**, so there is nowhere for a modulus to
live. The three textual mentions of "modulus" in each file are comments saying
why there is none.

**Can P17 copy P16's quality formulas?** No. Zero occurrences of `dihedral`,
`radiusRatio`, `aspect`, `circumradius`, `signedVolume` or `sqrt` in the module.
It reads `MeshQualityReport` and derives nothing.

**Can P17 create its own CAD-face mapping?** No. Zero occurrences of
`FaceSelector`, `facet` or any classification in the implementation. A load will
reach nodes through `GeometryMeshMap` and `NamedBoundarySet`, which already
exist and are already complete for every reference model.

**Can load intent persist NodeIds rather than a GeometryReference?**
Not yet decidable in code, because the load type is `P17-LOAD-001`'s. What is
decided is the rule (ADR-032, restated in the authority model) and the
instrument: P16 enforced the equivalent with compile-fail cases rather than
review, and the 45 mesh cases include `meshcmd.local-sizing-is-not-keyed-on-a-node`.
`P17-LOAD-001` should add its own rather than rely on a reviewer.

**Can restraint intent persist ElementIds?** Same answer, same owner.

**Can a remesh make an old load act on unrelated numeric NodeIds?** Not through
this boundary: a `StructuralModel` carries the `MeshStamp`-bearing mesh and its
map from one lookup, and `Mesh::owns(stamp)` exists for a holder to check a
handle. Proved for the pairing by
`CHECK(model->map().meshStamp() == model->mesh().mesh().stamp())`.

**Can an unresolved GeometryReference become an empty load and still solve?**
Not decidable yet, and deliberately not stubbed — the check needs the load
type. It is the first gate `P17-LOAD-001` adds to `requireStructuralModel`, and
the function is documented as the place it lands.

**Can viewer tessellation be fed into the structural solver?** No, structurally.
A viewer tessellation is `core/geometry`'s triangulation of a `Body`; the
boundary accepts neither it nor any mesh — it accepts a `Mesher` and a
`MeshControlId` and fetches the engineering mesh itself.

**Can P17 core depend on the GUI?** No, two ways. Rule 2 catches a Qt header in
`src/structural/` (`architecture.checker.qt-leak` covers every module), and
rule 6 — added by this milestone — catches
`#include "../../apps/bettercad/..."`, which **no earlier rule could**.

**Can P17 core depend on the CLI?** No, by the same rule 6. This was a genuine
gap: rule 2 keys on Qt's header shape and `apps/` is not a module, so rule 3 had
no layer to compare. Proved by `architecture.checker.library-reaches-apps`.

**Can P16 depend back on P17 and create a cycle?** No.
`architecture.checker.meshing-to-structural` is the fixture, and it is the one
that matters most: a mesher that asked a structural analysis what it wanted
would make the mesh depend on the solve that depends on the mesh.

**Can a persistence representation leak into solver APIs?** No. `structural`
does not link or include `io`, and `architecture.checker.structural-layer-violation`
proves `structural -> io` is refused.

**Can deformed visualisation mutate CAD or P16 node coordinates?** Not through
anything this milestone provides: `StructuralModel` exposes the mesh as
`const VolumeMesh&`, and the authority matrix states that
`x_display = x_mesh + s u` is display-only. `P17-VIZ-001` owns the display and
inherits the const.

**Can one result combine displacement from M1 with stresses from M2?** Not from
this boundary, and the reason is a signature rather than a check: there is no
way to supply a mesh and a map separately, because both come from one lookup.
A result's own provenance is `P17-DATA-001`'s, and the requirement that it carry
the source mesh's identity is recorded.

**Can unsupported multiple solids reach the solver?** No. P16 refuses to mesh a
multi-solid body, explicitly and tested there, so no mesh exists to prepare
from. The limit is inherited, not chosen, and `SCOPE.md` says so.

**Can configuration-stale geometry reach the solver through a cached mesh?**
No, and this is the one the carried defect makes dangerous. A cached mesh exists
and is internally fine; the geometry behind an active override describes the
base configuration. P16's `GeometryIneligibility::ConfigurationOverrideActive`
refuses it, and P17 inherits the refusal **by reusing the check** rather than by
remembering to make it — so it will stop being refused on the day the defect is
fixed, in one place.

**Can hidden backend mesh-quality defaults determine P17 acceptance?** No, and
the trap here is subtler than it looks. `MeshQualityReport::satisfiesPolicy()`
answers the question the **mesh control** asked, under
`reportOnlyThresholds()`, which classifies nothing — so it is true for any
structurally valid mesh. A structural policy that called it would be accepting
everything while appearing to check. The architecture says P17 reads
`summaries`, the measured numbers, and applies its own thresholds;
`StructuralModel::quality()` documents that at the point of use, and
`P17-VALID-001` owns the numbers.

**Can the chosen module layer bypass CheckLayering instead of fixing the
model?** No. Zero occurrences of `ALLOW_LAYER`, `SKIP`, an exclusion for
`structural` or an `IGNORE` in the checker. The layer was decided and the table
changed; nothing was suppressed.

**Can CheckLayering accept a dependency it should reject because the numeric
ordering was hacked?** This is the right question to ask of a respace, and the
answer is that **relative order is unchanged** — the only transformation was
multiplication by ten. Two independent confirmations: the checker gives the same
verdict on the same tree before and after (`445 files, 0 violations` →
`447 files, 0 violations`, the two new files being the only difference), and
every existing negative fixture still fires its own message, including
`'drawing' must not depend on 'io'` and `'core' must not depend on 'sketch'`.

**Can an architecture test pass because it matched zero test cases?** No, and it
was checked rather than assumed:

```text
ctest -R '^architecture\.' -N      Total Tests: 14   (10 existing + 4 new)
ctest -R 'StructuralInput' -N      Total Tests: 10
```

Both were run with `-N` first and then for real. A filter matching nothing is
also a failed stage in the harness, which sets `noTestsAction=error`.

**Can source change after the final qualification?** Guarded the way every
BetterCAD milestone guards it: the eight-path fingerprint is recorded before the
first build and after the last test by `qualify.cmd` itself, and recomputed
before the commit.

## Four of the reviewer's own

**Does the module actually enforce anything, or only describe it?** The
distinction this milestone could most easily have failed on. A header full of
comments would have satisfied every checkbox and enforced nothing. What is
enforced: the private constructor with one friend (a compile error, not a
review note), the five layering fixtures (a failing build, not a convention),
and rule 6 (a gap that was open until this change). What is only described: the
scope statement in ADR-034, which is a statement about what is not built and
cannot be tested — and `SCOPE.md` says so rather than implying otherwise.

**Is the respace overbuilding?** It is more change than this milestone strictly
needed — Option A, shifting by one, would have compiled. The argument is in
ADR-035 and rests on a measured pattern rather than taste: `io` has moved twice
in two module additions, and four roadmap phases sit in the same band. The
counter-argument that deserves recording is that **the respace is irreversible
in practice** — once ADRs and comments quote tens, going back is another sweep.
If the stack turns out to be finished after P17, the respace will have been
unnecessary. That seemed the smaller risk.

**Did the audit's convenience shape the architecture?** A fair suspicion: P15
and P16 happened to provide exactly what was needed, which is suspiciously
tidy. Checked by reading the upstream documentation rather than the upstream
code's shape — `requireLinearElasticConstants` says *"This is the boundary a
structural solver consumes (P17)"* and `reportOnlyThresholds` says *"`P17` owns
what a structural analysis needs from a mesh"*. Those were written before this
milestone, naming it. The fit is design, not coincidence, and the honest
conclusion is that the earlier phases did this work and P17-ARCH's job was to
notice.

**Is seven InputProblem values a complete list, or just the reachable one?**
Neither, and the header says which: it is the complete list of problems
**decidable with the types that exist today**. Five more are named as belonging
to later milestones — an unresolved load, a conflicting restraint, an
under-constrained model, an unacceptable mesh quality, an unsupported solver
setting — and each needs a canonical type this milestone does not define.
Declaring codes for them now would have put values in the enum that nothing
could return, which is exactly the defect F2 corrected.

## Result

```text
QUESTIONS:                      30 + 4 = 34
FINDINGS:                       4
ARCHITECTURE DEFECTS:           0
DESIGN WEAKNESSES NARROWED:     1  (F1, borrowed state made move-only; residual
                                   recorded, not claimed closed)
ENUM CORRECTIONS:               1  (F2, three unreachable values deleted)
TEST DEFECTS IN MY OWN WORK:    1  (F3, fixed by sharpening the test, not by
                                   relaxing the assertion)
RECORDED LIMITATIONS:           1  (F4, permitted directions have no callers yet)
GATE-BLOCKING:                  0
VERDICT:                        PASS
```
