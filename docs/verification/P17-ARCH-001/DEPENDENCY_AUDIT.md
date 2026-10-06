# P17-ARCH-001 — what P17 consumes, and what it was therefore not allowed to build

```text
SUBJECT:  the public P15 and P16 interfaces a structural analysis is permitted
          to use, and the duplication that was avoided by auditing before
          writing
METHOD:   read the public headers in the committed tree; every name below was
          found in the code, not inferred from a milestone brief
```

## The headline

**The audit changed the milestone.** Three things P17 was expected to define
already exist, built by the earlier phases *for this consumer, by name*:

```text
materials::LinearElasticConstants      documented "This is the boundary a
                                       structural solver consumes (P17)"
ConsumerKind::FeaLinearStatic and      P15's completeness requirements, which
  FeaLinearStaticWithGravity           already split gravity from stiffness
meshing::describesTheModel(currency)   the exact predicate a solver entry point
                                       needs, returning true only for Current
```

So the material resolution this milestone was asked to architect is a function
call, the validation is already inside it, and the staleness predicate is
already written. What was left to build is the one thing nothing upstream could
provide: a boundary that cannot be bypassed.

## P15 — material consumption

| P17 needs | The qualified API | Layer |
| --- | --- | --- |
| The document's material | `features::requireEffectiveMaterial(document)` | `features` 20 |
| Whether it is assigned / unresolved / invalid | `features::materialAssignment(document)` | `features` 20 |
| E, nu, G, K | `features::requireLinearElasticConstants(document, id)` | `features` 20 |
| Density, when self-weight enters scope | `features::requireDensity(document, id)` | `features` 20 |
| The constants themselves | `materials::LinearElasticConstants` | `core` 0 |
| What a consumer requires | `materials::ConsumerKind` | `core` 0 |

`requireEffectiveMaterial` is documented as "What P15-MASS-001, **P17** and P18
consume". `requireLinearElasticConstants` is documented as "the boundary a
structural solver consumes (P17). It hands over a complete set or it fails --
never a partial one, and never a fabricated default. There is no `nu = 0.3`
fallback anywhere behind it."

### The validation is already there, measured

Read out of `src/features/material/Materials.cpp`:

```text
no Young's modulus                     -> refused
E not finite, or E <= 0                -> refused
no Poisson's ratio                     -> refused
nu not finite, or outside the limits   -> refused
```

and the limits, from `core/materials/MechanicalProperties.hpp`:

```cpp
inline constexpr double minPoissonRatio = -1.0;
inline constexpr double maxPoissonRatio = 0.5;
```

compared with `<=` and `>=`, so the bounds are exclusive: `-1 < nu < 0.5`. That
is exactly the range P17's brief asks for, already implemented, already
qualified, and reporting **every** gap in one diagnostic rather than failing at
the first.

So `P17-MAT-001`'s "validate E > 0" and "validate -1 < nu < 0.5" checkboxes are
satisfied by consumption. It will have work to do — a per-body material story,
and the density decision — but not this.

### The duplication that was avoided

ADR-028 already forbids it, in P15's own words:

> **Hard rule: a downstream solver must not maintain a second authoritative
> material database.** No `steel()` helper in the FEA module, no default modulus
> constant, no "typical values" fallback.

So this was never an open design question, and the architecturally significant
act was to notice that and comply:

```text
src/structural/  contains no modulus, no default, no material table
StructuralModel  holds LinearElasticConstants BY VALUE -- four numbers that are
                 a derived view of P15's canonical data, obtained through the
                 one contract, and recomputed on every prepare
```

A `struct FeaMaterial { double E; double nu; };` would have been the forbidden
thing. It does not exist.

## P16 — mesh, mapping and quality consumption

| P17 needs | The qualified API | Why not reimplemented |
| --- | --- | --- |
| Is the body analysable at all | `meshing::geometryIneligibility` / `requireMeshableGeometry` | Ten reasons in one boundary, including the configuration refusal |
| The geometry stamp | `meshing::geometryRevision` | Mixes dependency revision counters; a reimplementation would differ |
| Is the held mesh the model's | `Mesher::currency` + `meshing::describesTheModel` | Compares controls **by value**, so a rename does not invalidate |
| The mesh | `Mesher::mesh(control)` | — |
| The CAD-face mapping | `Mesher::map(control)` | Taken in the same lookup, so it cannot be mismatched |
| Quality measurements | `Mesher::quality(control)` | Measured per metric with a worst element each |
| Is the report under current policy | `Mesher::qualityDescribesCurrentPolicy` | A threshold edit reclassifies without invalidating |
| Mesh validity | `MeshQualityReport::structural`, `structurallyValid` | **Not re-derived.** P16 refuses to *hold* an invalid mesh |
| Handle ownership | `Mesh::owns(stamp)` | The documented way to check a handle belongs to a mesh |
| Named boundary intent | `meshing::NamedBoundarySet`, `BoundaryFacetSet::fullyResolved()` | For `P17-LOAD-001` and `P17-BC-001` to resolve a `FaceName` |
| The control and its body | `meshing::findMeshControl`, `MeshControlDefinition::body` | — |

### Nothing reaches a backend

```text
src/structural/ includes no nglib.h, no netgen header, no OCCT header
enforced by     rule 5 (mesh backends) and rule 1 (OCCT), both module-agnostic
proved by       architecture.checker.structural-backend-leak
```

A solver reading `nglib` directly would bypass P16's validation, its currency
and its Netgen orientation correction in one line. That correction matters: P16's
adapter undoes Netgen's inverted node order with one swap at the boundary, so a
mesh arriving at P17 already has positive signed volume. `P17-ELEM-001` must
still reject an inverted Tet on its own account rather than rely on an upstream
guarantee it does not verify — but it will never see a raw backend mesh.

## The one thing P17 had to build

```text
requireStructuralModel + StructuralModel
```

Nothing upstream could provide it, because the gap is P16's recorded limitation
and it is a limitation *about callers*:

> nothing FORCES a holder to call `isStale`

`Mesher::mesh()` returns a stale mesh deliberately — P16-VIZ-001 depends on it —
so the fix cannot be in P16 without breaking qualified behaviour. It has to be a
property of the consumer's signature, which is what ADR-036 decides.

## Layer consistency of the above

Every consumed interface is strictly below 50:

```text
core        0     Result, Error, ObjectId, MeshControlId, units,
                  materials::LinearElasticConstants, ConsumerKind
features   20     Regenerator, requireEffectiveMaterial,
                  requireLinearElasticConstants, requireDensity, Material
meshing    40     Mesher, VolumeMesh, GeometryMeshMap, MeshQualityReport,
                  GeometryPreparation, MeshIds, findMeshControl
```

and `bettercad_structural` links exactly `BetterCAD::core`,
`BetterCAD::features`, `BetterCAD::meshing`. No `io`, no `renderer`, no Qt, no
Eigen, no backend.

## Eigen: present, pinned, and deliberately not linked

```text
cmake/BetterCADDependencies.cmake:50   Eigen 5.0.1, header-only, SHA256-pinned,
                                       exposed as BetterCAD::eigen and declared
                                       "private to bettercad_sketch"
```

So `P17-SOLVE-001`'s library audit starts from something already qualified
rather than a blank page. This module does not link it, for two reasons worth
separating:

```text
1. Nothing here needs it. There is no matrix in this milestone.
2. Admitting a solver dependency is a LICENCE decision. The project has no
   licence yet, so a copyleft sparse solver would decide it by accident. Eigen
   is MPL-2.0 and passes the project's own weak-copyleft rule; several common
   sparse direct solvers do not. P17-SOLVE-001 must state the licence of
   anything it proposes and leave a GPL/AGPL choice to the owner.
```

## Searches run before qualification

```text
#include .*apps/ from include/ or src/        0 occurrences, before and after
nglib / netgen headers in src/structural/     0
OCCT (*.hxx) in src/structural/               0
Qt headers in src/structural/                 0
a second Young's modulus or Poisson store     0 -- no double field named for a
                                              material property exists in
                                              src/structural/
meshing -> structural includes                0
io / renderer -> structural includes          0 (nothing consumes it yet)
```

The last line is a limitation of this milestone rather than a result: `io`,
`renderer` and the CLI do not consume the structural module yet, so the
permitted downward directions are proved by fixtures rather than by real
callers. `P17-PERSIST-001`, `P17-VIZ-001` and `P17-CLI-001` are where those
become real, and the fixtures are what will stop them taking a shortcut.
