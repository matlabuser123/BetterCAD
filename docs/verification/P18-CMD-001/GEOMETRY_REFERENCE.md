# P18-CMD-001 — the GeometryReference is preserved, never resolved

```text
A thermal load or condition names a CAD face by FaceName. The command stores that FaceName as given and
restores it as stored. It never stores the facets, nodes or numbering the name resolves to on any mesh.
```

## What the canonical GeometryReference is in P18

`FaceName { ObjectId feature; FaceSelector face; }`, with `FaceSelector { role, entity, along, alongSketch, edge, copies }`. It is the **only** canonical target of a `SurfaceHeatFluxLoad` and of both boundary-condition
kinds (a volumetric source has none). The brief's second variant, a `NamedBoundarySet`, is a P16 *mesh* concept and is **not** a canonical thermal target in P18-DATA, so it is not a payload here; recorded in
[SCOPE.md](SCOPE.md).

## Every selector form is round-tripped

`everyFaceForm()` builds one `FaceName` of every form the data model's `validate(FaceSelector)` accepts, and one that names a feature that does not exist:

```text
start cap                                         role = StartCap
end cap                                           role = EndCap
side face of a profile entity                     role = Side, entity
side face swept along a path edge                 role = Side, entity, along
… in another sketch                               role = Side, entity, along, alongSketch
chamfer face                                      role = Chamfer, edge
a copied face (two copies, in order)              role = EndCap, copies = {(feature 9, 1), (feature 10, 2)}
an UNRESOLVED reference                           a feature id that no object has
```

`LoadGeometryReferencePreserved` and `BoundaryGeometryReferencePreserved` take each form, make a load and a held temperature (and a convection) of it, and require, for **every** form: the fingerprint after undo equals
the one before; the fingerprint after redo equals the one after the first execute; and the stored record equals the record given, *field for field including the copies list*. The fingerprint hashes every selector member
and the copy list in order, so a normalisation that dropped or reordered one would be caught.

| Phase | What is compared |
| --- | --- |
| before the command | the fingerprint of the empty/previous definition |
| after execute | the record in the definition equals the one given |
| after undo | the fingerprint equals "before" |
| after redo | the fingerprint equals "after execute", and the stored `FaceName` equals the one given |

## What is *not* in a payload

```text
mesh facet ids stored in a command:               NO
NodeIds stored in a command target:               NO
ThermalDofIndex / ThermalFreeEquationIndex:       NO   (not even declared where a command is declared)
a resolved face set / a MeshStamp:                NO
```

* A **remesh** leaves every payload valid: `HistorySurvivesARemesh` edits a load on mesh generation one, remeshes, then undoes and redoes; the canonical intent is unchanged by the remesh, undo and redo restore it,
  the command's two definitions are untouched, and preparation resolves the same `FaceName` against the new mesh.
* An **unresolved** reference is still valid canonical intent: `ALoadMayBeDefinedBeforeAnyMeshExists` defines a load on a face of a feature that does not exist; it is accepted, preserved exactly, and reported
  unresolved by preparation (P18-LOAD / P18-VALID), not repaired here. There is **no nearest-face fallback** and no rebinding to the current geometry anywhere in `ThermalCommands.cpp` (the gate forbids any
  mesh identity in the file).

## An observation about the data model, and the one rule the command layer adds

`validate(ThermalLoad)` and `validate(ThermalBoundaryCondition)` validate the face **selector** but accept a `FaceName` whose `feature` is the invalid default id (`FaceName{}`): a malformed *selector* (a side face with no
entity) is refused, an invalid *feature handle* is not, and would only ever be reported unresolved when the record is prepared. That is P18-DATA's qualified behaviour and it is **left untouched** (recorded as O2 in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md)).

The independent review found that a command which accepted `FaceName{}` would put a target into the history that names nothing, so the command layer adds exactly one rule of its own, at its own boundary:

```text
REFUSED   a load or condition (add, edit, and inside a create's definition) whose FaceName has the INVALID feature handle      -> InvalidArgument
ACCEPTED  a FaceName whose feature handle is VALID but names no feature that exists (now, or yet)                              -> reported unresolved by preparation
```

The line is deliberate: an invalid handle is not a representation of a canonical target, while "does this face resolve" is a question about a document and a mesh, which a load defined before any mesh exists must
be allowed to leave open (`ALoadMayBeDefinedBeforeAnyMeshExists`). `ATargetThatNamesNoFeatureIsRefused` pairs the refusal with a snapshot proving nothing moved. A volumetric source has no face and is unaffected.
