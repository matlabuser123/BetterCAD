# P17-MAT-001 — what a material change invalidates

```text
SUBJECT:  the milestone's hard requirement -- material edit stales the FEA
          result and NOT the P16 mesh -- measured rather than argued
```

## The matrix

| Change | Geometry current? | Mesh current? | Mesh stamp moved? | Meshing intent moved? | Old FEA result current? |
| --- | --- | --- | --- | --- | --- |
| `E` edit, 210 → 190 GPa | **yes** | **yes** | **no** | **no** | **STALE** (`material`) |
| `nu` edit, 0.3 → 0.33 | yes | **yes** | no | no | **STALE** (`material`) |
| Material reassignment, A → B | yes | **yes** | no | no | **STALE** (`material`) |
| Same `MaterialId`, `E` 210 → 205 | yes | **yes** | no | no | **STALE** (`material`) |
| `rho` edit, no-gravity analysis | yes | yes | no | no | **STALE** (`material`) — see below |
| `rho` edit, gravity analysis | yes | yes | no | no | **STALE** (`material`) |
| Geometry edit | stale until regenerated | stale | — | no | inputs unavailable |
| Mesh size edit | yes | stale (`StaleIntent`) | no | yes | inputs unavailable |
| Quality threshold edit | yes | **yes** | no | yes (policy only) | **CURRENT** |

Every "mesh current?" cell is answered by **P16's own currentness API**,
`Mesher::currency` and `meshing::describesTheModel`, not by comparing a pointer.
Every "result current?" cell is answered by `staleReasons`, which names the
dependency.

## The hard requirement, measured

`StructuralMaterial_AYoungsModulusEditLeavesTheMeshCurrentAndStalesTheResult`:

```text
E 210 -> 190 GPa:
  Mesher::currency                  Current
  describesTheModel(currency)       true
  mesh().stamp()                    UNCHANGED
  geometryRevision(document, body)  UNCHANGED
  MeshControlDefinition             UNCHANGED (compared by value)
  staleReasons(before, after)       { Material }      <- exactly one
  source.mesh, source.geometry      UNCHANGED
  resolved E                        190 GPa, from P15
```

Four independent things are asserted not to have moved — the mesh's currency,
its stamp, the geometry revision and the whole meshing intent — and exactly one
thing is asserted to have moved. A change that coupled material to meshing
would break at least one of the four.

**Why it is right rather than merely desired:** a mesh is a function of geometry
and meshing intent. `E` is in neither. An architecture that invalidated the mesh
on a modulus change would remesh a hundred thousand elements to answer a
question about a number.

## Invalidation is dynamic, not imperative

There is no hook, and that is by design rather than by omission. Searched:

```text
invalidateMesh | markMeshStale | meshDirty      0 occurrences in src/ and
                                                include/, except ONE COMMENT in
                                                meshing/Mesher.hpp which says
                                                "UI-owned markMeshStale() is not
                                                authority"
```

So a material edit does not notify anything. It moves the material's document
revision, and the next `currentResultSource` reads a different revision and
reports a mismatch. Nothing can forget to fire, nothing can fire twice, and
there is no second stale flag to drift from the first — which is what the brief
prefers and what P16 already decided.

## `MaterialId` alone is not enough, and this is the proof

`StructuralMaterial_TheSameIdWithAChangedModulusIsADifferentSolverInput`:

```text
before:  MaterialId 1, designation "Steel", E = 210 GPa, revision 1
after:   MaterialId 1, designation "Steel", E = 205 GPa, revision 2

source.material          UNCHANGED  <- the id is the same
source.materialRevision  1 -> 2     <- which is why the id alone cannot decide
staleReasons             { Material }
StructuralMaterial ==    false      <- different solver input
```

Currentness that compared the id would call the old result current, and the user
would read a stress computed from a modulus their model no longer has. The
`StructuralResultSource` carries **both** the id and the revision for exactly
this case, and `StructuralMaterial::operator==` compares the effective inputs
and the source identity rather than the id.

## The density-only edit: decided, with the reason

The brief offers two defensible designs and says not to guess. The audit
settled it:

```text
A. any canonical material revision change stales the result
B. the source fingerprints only the properties this consumer CONSUMES, so a
   density-only edit leaves a no-gravity result current
```

**BetterCAD does A**, and not because it is easier. `StructuralResultSource`
carries `materialRevision`, which is `DocumentObject::revision()` — a
**per-material** counter that moves on any effective change to that material.
There is no per-property revision in P15 and inventing one in P17 would be a
second revision mechanism for material state, which is the duplication ADR-028
forbids in a different guise.

The cost is stated plainly: **a density edit stales a no-gravity structural
result that did not use the density.** That is one unnecessary re-solve. The
alternative — P17 deciding which properties it consumed and fingerprinting only
those — would put a second notion of "what this analysis depends on" beside
P15's revision, and the two would drift. P16 made the same trade for geometry
and recorded it in the same words: the cost of being wrong this way is one
regeneration that was not needed, and the cost of the other way is a result
presented as describing a model it does not.

Semantic precision was preferred where the infrastructure already supported it:
a **no-op** material edit does not move the revision at all, so re-setting the
same modulus stales nothing. That is the precision available without a second
mechanism.

## What does NOT stale a result

```text
camera, viewer visibility, mesh visibility, deformation scale, selection
highlight, CAD display tessellation, quality-colour choice
```

Presentation state, connected to nothing. The sharpest case is a **quality
threshold** edit, which is canonical meshing intent and still stales nothing: it
changes how an element is classified and can never change the element, so P16
keeps the mesh `Current` and the result stays current with it. Tested in
`StructuralData_AViewerOnlyChangeDoesNotStaleAResult`.

## Material edit before any result

`analysisState` reports `Ready` while inputs resolve and nothing is solved, and
`InputsUnavailable` if the edit made the material unusable. Neither invents a
"stale result" when no result exists — `resultCurrency(nullptr, …)` is
`NoResult`, and the state machine stays coherent.

## An invalid material blocks readiness

If a required property is unavailable, `currentResultSource` fails — it calls
`requireStructuralModel`, which resolves the material — so `analysisState` is
`InputsUnavailable` and never `Ready`. `P17-VALID-001` will add its own policy
on top; the input half already refuses.

## Read-only, and nothing is repaired

`StructuralMaterial_ResolutionIsDeterministicAndReadOnly`:

```text
16 successful resolutions   document revision UNCHANGED
8 failed resolutions        document revision UNCHANGED
after 8 failures            nu is STILL ABSENT -- not filled with 0.3, not
                            filled at all
                            E is still present and untouched
```

P15's rule is that missing data is reported and never filled, and a solver that
filled it would make the user's model silently different from the one they
described. Nothing here clamps a ratio, takes an absolute value or substitutes a
density, and every `Document` parameter in the module is `const`.
