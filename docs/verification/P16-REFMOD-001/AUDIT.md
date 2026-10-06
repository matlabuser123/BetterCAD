# P16-REFMOD-001 — audit of the existing reference-model infrastructure

```text
SUBJECT:  what already exists for committed reference models, so that this
          milestone extends it instead of building a fourth one
WRITTEN:  before any production code, per the brief's step 2
```

> **Finding, up front: BetterCAD already has a reference-model system, and it
> is on its fifth suite.** Nothing here needs a new test-model framework, a new
> analytical toolkit or a new runner. What P16 adds is a suite, its oracles and
> its gates.

## Where reference models live

```text
examples/reference_models/            the builders, as a STATIC LIBRARY
    ReferenceModels.hpp               P11/P12  twelve mechanical parts
    AssemblyReferenceModels.hpp       P13      eight assemblies
    DrawingReferenceModels.hpp        P14      seven drawings
    MaterialReferenceModels.hpp       P15      eight engineering-data models
    BuildSupport.hpp                  ModelBuilder, SketchBuilder
    Fingerprint.cpp                   ModelFingerprint / BodyFingerprint
    main.cpp                          the runner: builds every model, prints,
                                      and with --out writes .bcad and exports

tests/reference/                      the validation, as Catch2 cases
    Analytic.hpp                      INDEPENDENT geometry: Green's theorem,
                                      Pappus, and closed forms in SI
    ReferenceTestSupport.hpp          checkSound, checkProperties, checkBounds,
                                      checkRepeatedRegeneration, checkSaveLoad,
                                      checkStepExport, checkStlExport
    <Domain>ModelsTests.cpp           one file per suite
```

The library is built with the **public** APIs only, and
`tests/architecture/CheckLayering.cmake` checks `examples/*` "like any client
of the library". So a reference model cannot reach a private header, and
cannot be built through a back door — which is the property that makes the
suite evidence rather than a demo.

## The shape every suite has followed

P13, P14 and P15 each added the same five things, and nothing else:

```text
1. <Domain>ReferenceModels.hpp   a struct of IDs per model, a builder per
                                 model, an enum of kinds, and ONE constexpr
                                 catalog array carrying id / name / fileStem /
                                 purpose / mainParameter
2. <Domain>Models.cpp            the builders
3. <Domain>Catalog.cpp           kind -> document, one switch
4. one loop in main.cpp          behind a --<domain> flag, because the full
                                 runner is a minute of work a focused fixture
                                 has no use for
5. tests/reference/<Domain>ModelsTests.cpp
```

P16 follows it exactly: `MeshReferenceModels.hpp`, `MeshModels.cpp`,
`MeshCatalog.cpp`, a `--meshes` loop, and `MeshModelsTests.cpp`.

**The one addition to the library's own build:** `BetterCAD::meshing` becomes a
PUBLIC link of `bettercad_reference_models`. Meshing is layer 4 beside
`drawing`, which the library already links, so no layer is crossed and no
renumbering is involved.

## What the analytical toolkit already provides

`tests/reference/Analytic.hpp` opens with: *"Nothing here uses BetterCAD or the
kernel."* That is precisely §4's rule, already enforced by construction, and it
already carries closed forms this milestone's oracles need:

```text
cuboid(a, b, c, rho)            V = abc, in SI
cylinder(r, h, rho)             V = pi r^2 h
hollowCylinder(Ro, Ri, h, rho)  V = pi(Ro^2 - Ri^2) h
pi = std::numbers::pi           exactly what §24 asks for, already here
Rotation / rotated / placed     R, I' = R I R^T, and R x + t
```

So §23's four formulas are **three-quarters already written and already
independent**. What P16 adds is a small mm-based section: the plate-with-hole
volume, the inscribed-polygon bounds that make a curved model's tolerance a
derivation rather than a guess, and the areas those bounds need.

## What the meshing stack already guarantees, and what that does to the gates

This is the finding that most changes the work. Several of §6's shared
structural checks are not assertions a test has to make — they are
preconditions of the type existing at all:

```text
generateVolumeMesh REFUSES, and VolumeMesh's constructor is private with
generateVolumeMesh its only friend:

  InvalidMesh             a missing or repeated node handle, a DEGENERATE,
                          INVERTED or DUPLICATE tetrahedron, a non-finite
                          coordinate                  -- validate(mesh)
  BoundaryNotConforming   the tetrahedralisation's boundary is not exactly the
                          surface it was built from, counted BOTH ways
  VolumeNotRecovered      the tetrahedra do not fill what the boundary encloses
  MultipleSolids          more than one solid
  SizingNotResolved       a local control's face does not resolve
```

So "zero inverted Tet4" is structurally unobtainable in a published mesh. That
does **not** make the gate vacuous, and the suite still checks every one of
them independently — because the production claim under test is exactly that
refusal, and a reference suite that trusted it would be asserting the
implementation's own summary of itself. Each check is written against the
mesh's own nodes and elements, never against `VolumeMesh`'s recorded fields.

Two consequences for the brief's wording:

* §08's "NG_OK + 0 tetrahedra reported as success" is already unreachable
  through this path (`BackendFailed` covers "reported success without
  producing tetrahedra"). RM-MESH-08 therefore checks the stronger,
  observable property: **no mesh is published at all** — `heldMeshCount() == 0`,
  `mesh() == nullptr`, currency `GenerationFailed` — rather than a mesh with a
  zero count.
* §26's orientation census is computed from the node coordinates in the test,
  so the counts are measured rather than inherited.

## Quality: there is no threshold policy, deliberately

`reportOnlyThresholds()` is **empty**, and `MeshQuality.hpp` says why: quality
thresholds are solver requirements, P17 owns them, and *"inventing numbers now
would be fabricating engineering judgement and dressing it as a default."*

So under the default policy nothing can be a Warning or a Failure, and
`satisfiesPolicy()` reduces to structural validity. §30 forbids adding
reference-model thresholds, and §45 requires `invalid = 0`.

**What that means for RM-MESH-05.** "One or more shape metrics materially
worse" is a *comparison between two meshes*, not a threshold, so it needs no
invented policy: the thin model's metrics are compared against RM-MESH-01's.
And the metric chosen matters —

```text
TetAspectRatio = l_max / l_min       a property of the BOX, not the elements
TetRadiusRatio = 3r / R              a property of the element
TetMinDihedralAngle                  a property of the element
```

so the "materially worse" assertion is made on radius ratio and minimum
dihedral, with aspect ratio reported for completeness.

> **Corrected after measuring.** This section first said the aspect ratio
> "saturates near sqrt(3) on a block and CANNOT see a sliver". That is true of
> a *cube* — the longest tetrahedron edge is the body diagonal and the shortest
> is an edge, whatever the element shapes — and it is how the finding was
> recorded from P16-QUALITY-001. On the suite's models it is wrong as stated:
> measured, `TetAspectRatio` reads 4.09 on RM-MESH-01 (a 120x70x35 block) and
> 80.0 on RM-MESH-05 (a 120x80x1.5 plate), so it does discriminate. What it is
> measuring, though, is the box's proportions and not the elements': it would
> read 80 for a well-proportioned mesh of the same plate. The radius ratio is
> 1355 times worse on the thin plate against the aspect ratio's 20 times, and
> the minimum dihedral falls from 14.6 degrees to 0.72. The conclusion stands,
> for a sharper reason than the one first written down.

## Face naming decides how two models must be built

A boundary region can only be a *forward* query target if the face carries a
`FaceName`. `MappedFace::names` "MAY BE EMPTY, and that is a real state":
`cutHole` names a hole's flat faces and **not its cylindrical wall**.

RM-MESH-03 must map the **hole wall**, and RM-MESH-04 the **inner wall**. So
neither may use a hole feature:

```text
BUILD                        hole wall nameable?
HoleFeature (drilled)        NO  -- the wall carries no FaceName
extrude of a profile with
  an inner loop              YES -- FaceRole::Side, entity = the circle
```

Both are therefore extrusions of a two-loop sketch, which is also how
`VolumeMeshTests.cpp`'s own tube fixture is built. This is a construction
decision forced by the naming contract, not a preference, and it is recorded
in each builder.

## Rigid transforms: there is no transform feature

RM-MESH-06 needs a rigidly transformed body. The repository has none to use:

```text
features/            no move, no transform, no placement feature
P13-XFORM-001        COMPONENT placements, and its scope says explicitly
                     "no transformed bodies"
assembly occurrence  meshing one is a later milestone's (the note in
                     CheckLayering.cmake on why meshing shares layer 4)
```

What does exist is `Frame3D::fromAxes(origin, xAxis, yAxis, normal)`, which
stores the axes unchanged after checking orthonormality within 1e-12. A sketch
placed on such a frame maps local `(u, v)` to `origin + u X + v Y`, and the
extrude runs along the normal — so the body is exactly `R p + t` with
`R = [X Y N]` and `t = origin`, through the public API.

**The transform is therefore chosen in the test, not read from the product**,
which is what §4 requires of an oracle. The triad used is orthonormal and
right-handed by hand:

```text
X = ( 2/3,  2/3, -1/3)      |X| = |Y| = |N| = 1
Y = (-1/3,  2/3,  2/3)      X.Y = X.N = Y.N = 0
N = ( 2/3, -1/3,  2/3)      X x Y = N
t = (37, -19, 23) mm
```

a compound rotation that mixes all three axes, so an axis-permutation defect
cannot survive it.

Whether Netgen is *equivariant* under that rotation — same counts, same
topology — is a measurement, not an assumption. The brief's own wording
("where backend determinism permits") anticipates both answers. It is measured
first, and the assertion written to what was measured, with the figure
recorded.

## RM-MESH-08: which invalid input

The brief offers "open shell **or** failed/stale body". The repository's
reachable refusals:

```text
GeometryIneligibility        reachable from a committed document?
ConfigurationOverrideActive  yes, but it is a configuration case (RM-MESH-09)
NeverRegenerated             yes, but it is a state, not a model
RegenerationFailed           YES -- an extrude of an UNCLOSED profile:
                             "the sketch has no closed profile"
RegenerationBlocked          yes -- an over-constrained upstream sketch
GeometryStale                yes, but it is a state (and is §12's subject)
NoBody                       yes -- a sketch
EmptyBody                    yes -- an intersection that misses
NotASolid                    this is where an open shell lands, "on TOPOLOGY" --
                             but no feature in this repository produces a
                             non-solid body, so it is not reachable from a
                             committed model
InvalidBRep / ZeroVolume     no honest construction found
```

RM-MESH-08 is therefore **an extrude of an open profile** — three sides of a
rectangle. It is the brief's "failed body", it is deterministic, it survives
save/load (the document stores the definition, and the failure recurs on load),
and its diagnostic names the real cause rather than a downstream symptom.

**And it carries a `MeshControl`.** Without one the CLI would fail with
`no_mesh_control` — true, and not the reason. That is exactly the defect
P16-CLI-001's mutation M6 found, and a reference model that reproduced it
would be asserting the wrong failure.

## CLI: the commands already exist

```text
mesh-control-add  mesh-set-global-size  mesh-local-add  mesh-local-remove
mesh-settings  mesh-generate  mesh-info  mesh-quality  mesh-validate
mesh-boundaries                              each with --json
```

§19's minimum (generate, info, quality, validate, boundaries) is met by what is
committed. No CLI change is needed or wanted: the CLI is an adapter, and
P16-CLI-001's gate was that it creates no meshing semantics of its own.

The fixture pattern to follow is P15's `refmod.material.*`: the runner is a
ctest **FIXTURES_SETUP** (not merely a DEPENDS, which ctest drops when the run
is filtered), the models are written to the build tree, and separate
`bettercad_cli` processes read them back.

## The zero-match guard already exists

`tests/cli/ZeroMatchGuard.cmake` asserts a *discovered count* per filter and
proves its own counter with a deliberately impossible pattern. §37 is satisfied
by adding this milestone's filter to that table — not by writing a second
guard.

## Decisions this audit forces

```text
1. extend examples/reference_models, do NOT create a parallel system
2. extend tests/reference/Analytic.hpp with an mm-based P16 section; do NOT
   restate the three closed forms it already has
3. every valid model carries a MeshControl, so every model is exercisable
   through the CLI and through persistence -- and so does RM-MESH-08
4. RM-MESH-03 and RM-MESH-04 are two-loop extrusions, because a drilled
   hole's wall carries no FaceName
5. RM-MESH-06 is built on an explicitly rotated Frame3D, with R and t chosen
   in the test; it has its OWN base block, so it depends on no other model
6. curved-model tolerances are INSCRIBED-POLYGON BOUNDS derived from the
   surface deflection, not fitted percentages
7. the quality policy is reportOnlyThresholds(), unchanged; "materially
   worse" is a comparison between models, on radius ratio and min dihedral
8. the structural checks are written against the mesh's own data, never
   against VolumeMesh's recorded summary of itself
9. one Catch2 tag -- [refmod][mesh] -- and the filter added to the existing
   ZeroMatchGuard table
```
