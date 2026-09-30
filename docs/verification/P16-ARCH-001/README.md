# P16-ARCH-001 — Meshing Architecture

```text
TASK:        P16-ARCH-001 -- Meshing architecture / backend decision
PHASE:       P16 -- Meshing (authorized current phase)
DATE:        2026-09-30
BASELINE:    11b6c76 -- "BetterCAD: authorize P16 Meshing and retire the P15 TODO"
             P15 qualified at 0757b8b; 2814 tests passing in three presets
RESULT:      see RESULT, below
```

## Scope

Architecture only. This milestone decides how meshing is structured, records the decisions as
ADRs, and closes one enforcement gap that would have made a P16 invariant unenforceable.

```text
IN SCOPE     the repository audit; the canonical/derived boundary; ownership;
             lifetime and invalidation; identity semantics; geometry mapping;
             the backend boundary and its admission rule; module layering;
             the persistence boundary; the P17 consumer contract; ADRs;
             architecture adversarial review

NOT IN SCOPE, and NOT STARTED
             P16-DATA-001, P16-GEOM-001, P16-SURF-001, P16-VOL-001,
             P16-SIZE-001, P16-QUALITY-001, P16-MAP-001, P16-VIZ-001,
             P16-CMD-001, P16-PERSIST-001, P16-CLI-001, P16-REFMOD-001,
             P16-QUAL-001

             No mesh type exists. No src/meshing/ exists. No backend has been
             chosen, added, linked or built. No meshing code was written.
```

The milestone brief forbids starting from a library choice or from a `Tet4` struct, and requires
the audit first. That order was followed, and it changed the outcome: four of the six findings in
the adversarial review came out of the audit rather than out of the design.

## Baseline

`11b6c76`, clean tree. P15 — Materials / Engineering Data — qualified at `0757b8b`: 2814/2814
tests in Debug, Release and Debug-shared, 36582 test executions, 0 failures, 0 warnings in all six
build and rebuild logs.

Two carried defects were in force at the start and still are. Neither is P16's, and one of them
constrains this architecture directly:

```text
FileIo replace           a document save can still lose to a file synchroniser
configuration            a configuration override does not rebuild the geometry it
regeneration             changes, so mass properties REFUSE under one rather than
                         reporting the base configuration's numbers as the
                         override's
```

## Audit

Full record: [AUDIT.md](AUDIT.md). What it established:

```text
engineering mesher            NONE
volume mesher                 NONE -- and OCCT 8.0.1 ships none either: the 14
                              headers matching "tet" are DateTime, Trihedron and
                              Tangence, and the whole Poly_* family is surface
surface triangulation         geometry::triangulate(), ONE production consumer:
                              STL export
display tessellation          NONE -- src/renderer/ contains only .gitkeep
raw mesh structure            vertices duplicated per face; welds by exact
                              coordinate to watertight -- 15 assertions across 11
                              test files, not a spot check
geometry validity             Body::isValid() via BRepCheck_Analyzer;
                              TopologySummary; no shape healing anywhere
stable face reference         FaceName -- generative, resolved at use, structured
                              in failure
face NAMES on a body          already carried, and propagated through booleans by
                              kernel history: listFaces() -> FaceInfo::names
stale-geometry guard          the P15-MASS configuration-override refusal
revision machinery            Document::revision(), revisionOf(ObjectId),
                              Regenerator dirty propagation
third-party deps              Catch2, nlohmann_json, Eigen3, OpenCASCADE, Qt6 --
                              no mesher of any kind
ARCHITECTURE.md:37            already declares "simulation meshes" DERIVED, and
                              already lists Simulations [future] under Document
```

**P16's central invariant is pre-existing architecture.** This milestone details it.

## Architecture

```text
CANONICAL (Document)                    DERIVED (Mesher service)
MeshControl                             Mesh
  which body                              nodes
  element family and order                elements, one region per solid
  global sizing                           boundary facets, each attributed to a
  local sizing -> FaceName                  CAD face
  quality targets                         quality metrics
                                          build stamp
persisted, undoable, in the             never persisted, no ObjectId, not in the
dependency graph                        dependency graph
```

Meshing is layer **4**, sharing the layer with `drawing`. It uses `core` (0) and `features` (2);
`io` (5) serializes its controls; `renderer` (6) can display a mesh. Nothing is renumbered, and
`assembly` (3) stays reachable so that meshing an assembly occurrence later needs no renumbering —
which both ADR-006 and ADR-015 had to do.

Surface triangulation of a `Body` stays kernel work in `core/geometry` behind the `occt` adapter,
following the precedent the layering table states for hidden-line removal. What lives in `meshing`
is the domain: controls, the mesher service, regions, quality, validation, and the volume backend
behind an enforced boundary.

## Decisions

| ADR | Decision |
| --- | --- |
| [ADR-030](../../architecture/decisions/ADR-030-a-mesh-is-derived-state-and-a-meshing-control-is-the-intent.md) | A mesh is derived state and a meshing control is the intent |
| [ADR-031](../../architecture/decisions/ADR-031-a-mesh-node-is-a-handle-not-an-identity.md) | A mesh node is a handle, not an identity |
| [ADR-032](../../architecture/decisions/ADR-032-a-mesh-boundary-names-a-cad-face-and-a-selection-is-never-a-mesh-entity.md) | A mesh boundary names a CAD face, and a selection is never a mesh entity |
| [ADR-033](../../architecture/decisions/ADR-033-the-volume-mesher-is-a-backend-behind-an-enforced-boundary.md) | The volume mesher is a backend behind an enforced boundary |

## The backend

Full comparison, with every licence read from its primary source: [BACKEND_MATRIX.md](BACKEND_MATRIX.md).

The finding that decides it is not about meshers at all. **BetterCAD has no licence** — `LICENSE`
says so in as many words, and grants no permission to distribute. Everything it ships is weak
copyleft, dynamically linked. So a GPL or AGPL mesher would not merely add a dependency: it would
decide BetterCAD's own unchosen licence. That is the owner's decision.

```text
OCCT 8.0.1   no volume mesher at all
Netgen       LGPL-2.1        ADMISSIBLE -- same obligation class as OCCT
MMG          LGPL-3+         admissible, but its own licence says it is for mesh
                             MODIFICATION: it adapts a mesh, it does not make one
Gmsh         GPL-2+          not admissible while BetterCAD's licence is unchosen
CGAL Mesh_3  GPL             not admissible, and unsuitable anyway: it
                             re-discretises the boundary, so a facet need not lie
                             on the CAD face
TetGen       AGPL-3          not admissible
write our own                rejected as the FIRST implementation: robust boundary
                             recovery with Steiner points is research-grade work
```

**One candidate survives the rule: Netgen.** Introducing the dependency is deliberately *not*
done here — `P16-VOL-001` carries it as an entry condition. `P16-DATA-001` through `P16-SURF-001`
do not depend on it.

## The twenty questions, answered

The milestone requires each answered explicitly, with no TBD.

**Does BetterCAD already contain an engineering mesher?**
No. No tetrahedral, hexahedral or prism mesh, no node or element type, no `NodeId`, no
`ElementId`, and no third-party mesher among its dependencies.

**Is existing triangulation display-only or engineering-grade?**
Neither, and the distinction matters. `geometry::triangulate()` is a *surface* triangulator whose
only production consumer is STL export. It is not display tessellation — there is no renderer to
consume one. It is not an engineering mesh — it is surface-only, its vertices are duplicated per
face, and it carries no quality metrics and no geometry mapping. It is a candidate *input*.

**What generates a closed surface mesh?**
`triangulate()` produces a geometrically watertight, outward-oriented surface, and broadly so:
**15 watertightness assertions across 11 test files**, covering chamfers, fillets, holes, lofts,
revolves, sweeps, mirrors and both pattern kinds. But it is **not topologically conforming**: a
vertex on a shared edge appears once per face. `P16-SURF-001` welds it and validates the result. Not with OCCT's
`Poly_MergeNodesTool`, which its own header says merges "for visualization purposes … but split
the ones on sharp corners", the opposite of watertight.

**What generates a volume mesh?**
Nothing today. An external backend, behind ADR-033's interface, after the admission decision.

**Which element types are supported first?**
The 4-node linear tetrahedron, and nothing else.

**Are Tet4 elements sufficient for the P17 foundation?**
For building and qualifying the pipeline, yes. **For accurate stress in bending, no** — a linear
tet has constant strain, is excessively stiff in bending, and converges slowly at exactly the
stress concentrations that matter. Recorded now as a known limitation, not discovered later as a
validation failure, and the reason an element carries its type from day one so Tet10 is an
addition rather than a rewrite.

**Who owns generated mesh state?**
A `Mesher` service, exactly as the `Regenerator` owns derived bodies. Not the Document, not the
GUI.

**When exactly is a mesh invalidated?**
When its control's revision, its source feature's revision or the document's revision differs from
the build stamp, or when the Regenerator reports anything dirty upstream of the source. Plus the
configuration refusal. The rule is deliberately conservative: it can waste work, never mislead.

**Can a mesh survive geometry regeneration?**
No, and it must not appear to. A regeneration that touched the source body invalidates the mesh;
the next request rebuilds it. Because nothing derived is persisted, a stale mesh can only exist
inside one process lifetime.

**Are NodeId and ElementId stable across remesh?**
No. They are handles into one generation of one mesh, and are deliberately **not**
`bettercad::Id` — whose documented contract is stable, persisted identity with never-reused
values. A mesh carries a generation counter so a stale handle is refused rather than
reinterpreted.

**How are boundary facets associated with CAD geometry?**
Each facet is attributed to the CAD face it came from, and the mesh carries that face's `FaceName`
set. The relation is **many-to-many** and stored as one, because `Faces.hpp` documents that a
split face carries its name on both parts and merged faces carry all their names. A face with no
name is normal — primitives take no namer — and is not a failure.

**Can selections survive remesh?**
Yes, definitionally: a selection names CAD geometry and never a mesh entity, so a remesh cannot
touch it. There is no remapping step to get wrong.

**How are holes and internal voids represented?**
As boundary, with no special concept. A hole's wall is a face like any other. An internal void is
an inner shell whose facet normals point out of the material, which is to say into the void.

**What happens with multiple disconnected solids?**
A `Body` is "zero or more solids". A mesh has one region per solid and shares no node between
regions; an empty body is refused. Touching solids give coincident but distinct nodes: P16 does
not bond and does not detect contact, so **a multi-solid part meshed by P16 is mechanically a set
of independent solids**, and P17 must not assume otherwise.

**What happens with transformed geometry?**
Nothing special. Patterns and mirrors bake their transforms into the body during regeneration, so
a patterned instance is already in model coordinates. A mesh is in its body's frame and carries no
transform. Quality metrics are computed in that frame and are unaffected by a rigid placement —
`Placement` is rotation and translation with no scale field.

**What happens under a configuration change?**
The mesh request is **refused** with a structured diagnostic, reusing P15-MASS-001's guard
unchanged, because the carried defect means the geometry under an override is the base
configuration's. Refusing is correct while the defect stands; the guard and its tests are removed
together when it is fixed.

**How does P17 request a mesh?**
It asks the mesher for a mesh for a control and receives a `ValidatedMesh` or a structured
diagnostic. It never calls a backend, never meshes, and cannot obtain an unvalidated mesh for
computation — that is enforced by the type, not by a comment.

**What is persisted?**
Meshing controls only: which body, element family and order, sizing including local sizing bound
to `FaceName`, and quality targets.

**What is recomputed?**
Everything else: nodes, elements, regions, boundary facets, attributions and quality metrics. A
`.bcad` file contains no mesh.

**What backend-specific state is forbidden from leaking into core APIs?**
All of it. No backend type, enum or handle in any public header, control, mesh, diagnostic or
file. A backend's message may be quoted inside a structured BetterCAD diagnostic; its types may
not cross the interface. Backend code lives only in `src/meshing/<backend>/`, and rule 5 of the
architecture check now enforces that.

## Blast radius

The only production change is to the architecture gate.

```text
DIRECT       tests/architecture/CheckLayering.cmake -- rule 5 added, plus a
             layer_meshing entry
             tests/architecture/CMakeLists.txt -- one new checker self-test
             tests/architecture/fixtures/ -- one new violating fixture, and one
             valid file under the existing valid fixture

DEPENDENTS   nothing else reads CheckLayering.cmake. It is run by
             architecture.layering and by the eight architecture.checker.* tests,
             all registered in the one CMakeLists above

NOT TOUCHED  no C++ changed. No compiler flag, no target, no public header, no
             serialization, no regeneration path, no CLI, no reference model.
             The change cannot alter any compiled output
```

The regression set is therefore the architecture tests as the subject, run inside a full clean
three-preset build and full test run — the project standard for a production change, not narrowed
because the change looks safe.

## Implementation

```text
tests/architecture/CheckLayering.cmake
    + rule 5: a volume-meshing backend's headers may only be included from
      src/meshing/<backend>/. Keyed on the entry headers themselves (nglib.h,
      tetgen.h, gmsh.h, CGAL/..., mmg/..., netgen/...) because there is no
      extension to key on
    + set(layer_meshing 4), with the rationale in a comment
    + rule 5 added to the file's own rule list

tests/architecture/CMakeLists.txt
    + architecture.checker.mesh-backend-leak

tests/architecture/fixtures/mesh-backend-leak/src/features/Leak.cpp        new
tests/architecture/fixtures/valid/src/meshing/netgen/Adapter.cpp           new
```

Why a production change at all in an architecture milestone: because the audit found that P16's
invariant *"backend-specific behaviour must remain behind a meshing interface"* was
**unenforceable**. Rule 1 recognises an OCCT header by its `.hxx` extension, and every candidate
backend ships `.h` headers, so no rule would have fired on a backend include anywhere in the
tree. An invariant only a reviewer enforces is one the next milestone breaks, and `P16-VOL-001`
is where the temptation arrives. Eleven lines of comment and four of rule, added before the first
backend line is written rather than after.

## Tests

One new permanent test, and one existing fixture extended. Both prove behaviour the ad-hoc
mutations in this session's evidence proved only once.

```text
architecture.checker.mesh-backend-leak   NEW. A backend header in src/features/
                                         must be reported. Exit 1, and the message
                                         must name it
architecture.checker.valid               EXTENDED. src/meshing/netgen/Adapter.cpp
                                         includes <nglib.h> AND a features header,
                                         so it holds three things in place at once:
                                         the allow-path for a backend adapter, the
                                         layer_meshing table entry (without it the
                                         checker reports an unknown module), and
                                         the direction meshing -> features
architecture.layering                    unchanged, and must still pass on the real
                                         tree
```

**The rule was shown to fire before it was trusted.** Three mutations, each reverted and the tree
re-verified:

```text
nglib.h in src/features/            FAILED, named correctly       as required
tetgen.h in a PUBLIC header         FAILED, named correctly       as required
nglib.h in src/meshing/netgen/      PASSED                        as required
restored tree                       398 files, 0 violations       as required
```

A rule never shown to fail is not known to work. The permanent fixtures make that check
repeatable rather than a claim in a document.

## Adversarial review

Full record: [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
FINDINGS           6
RESOLVED IN ADRs   5
RESIDUAL           1 (F6, assigned to P16-DATA-001 / P16-CMD-001)
NEW PRODUCTION
DEFECTS            0
```

Five of the six were found **while the architecture was being written**, which is the value of
doing it at all:

```text
F1  the attribution rule would have refused to mesh a box: primitives take no
    namer, so their faces carry no FaceName
F2  the face-to-name relation is many-to-many; optional<FaceName> per facet is
    wrong in both directions after the first boolean
F3  the rule meant to contain a backend did not exist, and rule 1 would not have
    caught one -- fixed in this milestone and proven to fire
F4  bettercad::Id would have promised mesh nodes stability and persistence they
    cannot have, and silently dropped IdAllocator's never-reuse guarantee
F5  two paths to a solver-facing mesh, one of which skipped validation -- now
    separated by type
F6  RESIDUAL: a control whose body is deleted must become explicitly unresolved.
    No code exists to fix here; owed by P16-DATA-001 and P16-CMD-001
```

Attacks that found nothing are recorded too, with what was checked — including the one that
mattered most: **can a mesh be believed valid after an undo returns the document to an earlier
state?** No, and not by luck. Every write to `revision_` in `Document.cpp` is `++revision_`, and
undo increments it too because it applies its inverse through the same `restore*` entry points. The
revision is monotonic, so there is no ABA hazard. Checked in the source rather than inferred.

## Determinism

Nothing in this milestone computes anything, so there is no numerical determinism to measure. The
architecture check is a pure function of the source tree; it was run repeatedly under the
qualification's repeat stage.

Determinism is *required* of a backend by ADR-033's admission rule and must be **measured** at
`P16-VOL-001` — repeated runs, and across the three presets. The existing triangulator already
sets the precedent, with `parameters.InParallel = false; // deterministic`.

## Known limitations

```text
No mesher exists. This milestone produced an architecture and one enforcement
rule; it produced no meshing capability.

The backend is not chosen. One candidate meets ADR-033's admission rule on paper.
Nothing has been linked, built or measured, and admitting a dependency -- with it
a constraint on BetterCAD's unchosen licence -- is the project owner's decision.
P16-VOL-001 is gated on it. P16-DATA-001 through P16-SURF-001 are not.

Tet4 is not enough for accurate bending stress. It is enough for the pipeline.
Recorded in ADR-031, and P17's validation cases must distinguish discretisation
error from a solver defect.

A multi-solid part is not bonded. P16 meshes each solid independently and does
not detect contact.

Meshing will refuse under a configuration override, because of the carried
regeneration defect. Correct while the defect stands.

Semantic face identity across a topology change is P21. Until then a
topology-changing edit can leave a load explicitly unresolved, which is reported
and never silently rebound.

F6 is open: a control whose body is deleted must become explicitly unresolved.
Owed by P16-DATA-001 and P16-CMD-001.

The containment rule names candidate backends' headers, so it must be updated
when a backend is admitted. Stated in the rule's own comment.
```

## Regression

Harness: `qualification/qualify.cmd`, carried from P15-QUAL-001 with its logic **byte-identical**
from line 30 on — only the header comment differs, which is checkable with `diff`. Its own
regression, `qualification/verify-harness.cmd`, was run first and passed: a failed stage gives a
non-zero exit, and a zero-match repeat filter is counted as a failed stage.

```text
PRESETS       debug-ext, release-ext, debug-shared-ext -- each configured,
              fully cleaned, rebuilt with warnings as errors, proven no-op on a
              second build, then tested
REPEAT        -R architecture --repeat until-fail:5, in release-ext and debug-ext
```

| Preset | Build | Full ctest | Tests |
| --- | --- | --- | --- |
| `debug-ext` | 21m28s | 11m43s | **2815/2815** |
| `release-ext` | 21m31s | 12m10s | **2815/2815** |
| `debug-shared-ext` | 18m46s | 13m16s | **2815/2815** |
| repeat `release-ext` | — | 9 tests x5 | **9/9** |
| repeat `debug-ext` | — | 9 tests x5 | **9/9** |

```text
2815 = the 2814 P15 qualified, plus architecture.checker.mesh-backend-leak
8535 test executions total (2815 x 3, plus 9 x 5 x 2)
0 failures
```

**0 compiler warnings in all six build and rebuild logs.** Checked strictly, for `warning:`
and `[-W`, so the figure is 0 out of 0 rather than 0 tolerated. A first, looser count
reported one match per build log; it was this grep hitting the **filename** `Error.cpp`, not a
diagnostic.

**The binaries tested are the binaries built.** Each preset's second build exited 0 having
recompiled and relinked nothing: `grep -icE "Building CXX|Linking CXX"` over all three rebuild
logs returns 0. Each contains one line — the git-revision stamp, which always runs and whose
`restat` then prunes everything downstream.

**The new test ran and passed in all three presets** (`Test #2690`), and
`architecture.layering` passed in all three on the real tree (`Test #2683`).

### The first attempt failed, and is recorded rather than quietly replaced

Attempt 1 failed with **5 stages**: all three configures exited 1 because the `-ext` presets
require `BETTERCAD_BUILD_ROOT` and it was unset —

> "This preset builds outside the source tree and needs BETTERCAD_BUILD_ROOT to say where."

No source changed between the attempts, so nothing was voided; the harness simply had not been
given its environment. Two things are worth keeping from it. The **zero-match guard fired for
real** — with no build there were no tests, the repeat filter selected 0, and the harness
counted that as a failed stage rather than passing by running nothing. And the harness's exit
code was correct while **my invocation masked it**: I piped its output, so the pipeline reported
the exit status of `echo`, and the background notification said "exit code 0" over a
`QUALIFY_EXIT=5`. The re-run captures the status in a variable before anything else runs.

## Result

```text
RESULT:      PASS
HARNESS:     "Qualification passed: every stage exited 0."
ELAPSED:     1h39m37s (14:32:35 -> 16:12:12, 2026-09-30)
TESTS:       2815/2815 in each of three presets; 8535 executions; 0 failures
WARNINGS:    0 in all six build and rebuild logs
TREE:        the eight qualified tree IDs are identical before the first build
             and after the last test run
EVIDENCE:    qualification/qualification-times.txt and the per-preset logs
```

Qualified source trees, recorded before the first build and unchanged after the last test run:

```text
apps              7532b4b3748efaa1282874af618a6d41bcb87751
include           6d55546edc191bbc2ca37d1db1b17dcb68cd3fcd
src               c0c52b09a0497e075aeeb22ff87b97bb53303c3f
tests             caf9725f732ab17edeaebd896a1f4de43a5e87af
examples          9187931fa6824e98d3da6296cdc40af6ca4b8607
cmake             7ed703a3031144d26063a1ff02996e7664e29524
CMakeLists.txt    13823e788ed3975792affa9ee4354ffce9479d80
CMakePresets.json 3229f0f89b236744d7d5c082bed47c28b718288e
```

`docs/`, `TODO.md` and `ARCHITECTURE.md` are outside the fingerprint, so the documentation and
the TODO closeout that follow this run do not affect it.

## Revision

First issue, 2026-09-30.
