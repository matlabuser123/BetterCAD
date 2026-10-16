# P18-MAT-001 — what a material change invalidates

```text
SUBJECT:  the milestone's hard requirement -- material edit stales the thermal
          result and NOT the P16 mesh -- measured rather than argued
POLICY:   WHOLE-MATERIAL REVISION, as P17-MAT-001 decided and for the same
          reason; stated below with its cost
```

## The matrix

| Mutation | Geometry current? | P16 mesh current? | Mesh stamp / NodeIds moved? | Meshing intent moved? | Old steady result | Old transient result |
| --- | --- | --- | --- | --- | --- | --- |
| `k` edit, 42.75 → 50 W/(m K) | **yes** | **yes** | **no** | **no** | **STALE** (`material`) | **STALE** (`material`) |
| `rho` edit, 7853.25 → 7900 kg/m³ | yes | **yes** | no | no | **STALE** (`material`) — policy* | **STALE** (`material`) |
| `cp` edit, 486.5 → 490 J/(kg K) | yes | **yes** | no | no | **STALE** (`material`) — policy* | **STALE** (`material`) |
| `E` edit (a structural property) | yes | **yes** | no | no | **STALE** (`material`) — policy* | **STALE** (`material`) |
| no-op material edit (same values) | yes | yes | no | no | **CURRENT** | **CURRENT** |
| REFUSED material edit (cp = −1, rho = NaN) | yes | yes | no | no | **CURRENT**; revisions unchanged | **CURRENT** |
| reassignment A → B | yes | **yes** | no | no | **STALE** (`material`); B resolves | **STALE** |
| reassignment to a numerically identical clone | yes | **yes** | no | no | **STALE** (`material`); identity changed | **STALE** |
| assignment removed | yes | **yes** | no | no | **InputsUnavailable**; no source to compare | same |
| material deleted | yes | **yes** | no | no | **InputsUnavailable** | same |
| remesh, no material edit | yes | new generation | yes | no | **STALE** (`mesh` only); material source unchanged | same |

Every "mesh current?" cell is answered by **P16's own currentness API**,
`Mesher::currency` and `meshing::describesTheModel`, not by comparing a
pointer; the stamp and the full `NodeId` list are compared as well. Every
"result" cell is answered by `staleReasons` and `analysisState`, which name
the dependency.

## The hard requirement, measured

`ThermalMaterial_AConductivityEditLeavesTheMeshCurrentAndStalesTheResult`:

```text
k 42.75 -> 50 W/(m K):
  Mesher::currency                  Current
  describesTheModel(currency)       true
  mesh().stamp()                    UNCHANGED
  every NodeId                      UNCHANGED
  geometryRevision(document, body)  UNCHANGED
  MeshControlDefinition             UNCHANGED, compared by value
  staleReasons(before, after)       { Material }      <- exactly one
  source.material                   UNCHANGED
  source.materialRevision           +1
  resolved k                        50 W/(m K), from P15
  analysisState(result)             SolvedCurrent -> SolvedStale
```

Six independent things asserted not to have moved, one asserted to have
moved. No hidden remesh: the stamp and the NodeIds are the same objects.

## `policy*`: whole-material revision, decided

The brief offers two defensible models and says not to guess:

```text
A. any canonical material edit → new material revision → every thermal result
   on that material is stale, steady included
B. only the properties consumed by that mode stale it: a rho-only edit leaves
   a steady result current
```

**BetterCAD does A**, and P18 inherits it rather than deciding it:
`ThermalResultSource::materialRevision` is `Material::revision()`, a
**per-material** counter, and there is no per-property revision in P15.
Inventing one in P18 would be a second revision mechanism for material state
beside P15's -- the duplication ADR-028 forbids in a different guise -- and
would make P17 and P18 treat one material edit differently. P17-MAT-001
recorded the same decision for a density edit on a no-gravity analysis.

The cost, stated plainly: **a `rho` or `cp` edit stales a steady result that
did not use either, and an `E` or `nu` edit stales a thermal result that uses
neither.** One unnecessary re-solve. The alternative puts a second notion of
"what this analysis depends on" beside P15's revision, and the two drift.

The precision available without a second mechanism is kept: a **no-op** edit
moves nothing, so re-setting the same value stales nothing.

## Shared density

`rho` has one home, `MechanicalProperties::density`, and one revision. A
density edit therefore stales every consumer that depends on the material --
P17's gravity result and P18's transient result -- and leaves the P16 mesh
current. There is no `thermalDensity`; `ThermalCapacity::density` is P15's
density handed over by `requireTransientConductionProperties`.

## Invalidation is dynamic, not imperative

There is no hook. A material edit moves the material's document revision;
the next `currentResultSource` reads a different revision and reports a
mismatch. Nothing can forget to fire, nothing can fire twice, and there is no
second stale flag to drift from the first.

## Two source axes

`ThermalMaterial_ARemeshLeavesTheMaterialSourceAloneAndStalesOnTheMeshAxis`:
a remesh with no material edit moves `source.mesh` and nothing about
`source.material` or `source.materialRevision`; the resolved
`ThermalMaterial` is equal before and after; the result is stale for reason
`mesh` and for no other. Mesh provenance and material provenance are
separate fields of the tuple, never one opaque handle.
