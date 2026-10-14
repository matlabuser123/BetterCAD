# P17-PERSIST-001 — unresolved references, and the rebind that cannot happen

```text
A well-formed GeometryReference that no current face satisfies is PRESERVED
EXACTLY. Not dropped, not emptied, and above all not rebound.
```

This is the brief's hardest gate (§15–17, §55, §88) and the one where a
plausible-looking "helpful" behaviour would be the worst possible outcome: a
loader that quietly reattached a 2.5 MPa pressure to a different face would
produce a document that solves, reports no error, and is wrong.

## What is persisted

Nothing new. `faceNameToJson` / `faceNameFromJson` already exist and are the
qualified encoding used by datums, drafts, shells, dimensions, annotations and
meshing controls. They write every field of the selector and validate it on
reading.

```json
"face": { "feature": 6, "face": { "role": "side", "entity": 5 } }
```

A second encoding here would be a second contract, which is P16's reason and
holds unchanged.

## The reference table

| Target | Reference before | Serialized | Reference after | Semantically equal? | Resolves after load? |
| --- | --- | --- | --- | --- | --- |
| load 4, pressure | `{feature, EndCap}` | `{"feature": N, "face": {"role": "end_cap"}}` | identical | **yes** | yes |
| load 2, traction | `{feature, Side, entity=lines[1]}` | `… {"role": "side", "entity": M}` | identical | **yes** | yes |
| restraint 3, fixed | `{feature, StartCap}` | `… {"role": "start_cap"}` | identical | **yes** | yes |
| restraint 1, ux | `{feature, Side, entity=lines[2]}` | `… {"role": "side", "entity": K}` | identical | **yes** | yes |
| restraint 5, uy+uz | `{feature, Side, entity=9991}` | `… {"role": "side", "entity": 9991}` | identical | **yes** | **NO — unresolved, by design** |

The two resolvable targets in the fixture are **distinct**, and the test
asserts they differ:

```cpp
CHECK(after.loads[0].target() != after.loads[1].target());
CHECK(after.restraints[0].face() != after.restraints[1].face());
```

Without that, a serializer that wrote one hardcoded face would pass.

## The unresolved case, in full

`StructuralPersistence_AnUnresolvedReferenceIsPreservedAndNotRebound`:

```text
original canonical GeometryReference   {feature, Side, entity 9991}
resolvable before the save             NO -- asserted: the validation report
                                       is Rejected before anything is saved
loaded GeometryReference               {feature, Side, entity 9991}
semantic equality                      YES
the load still exists                  YES -- two loads before, two after
the target is still populated          YES -- the selector still names 9991,
                                       rather than becoming a default face
loader rebound to another face          NO -- checked against all SIX faces
                                       the body actually has, by name
P17 validation after the load          unresolved reference, solve refused
```

**Checked against every face the body has, not against one.** Every real face
is a face the reference could have been rebound *to*, so each is excluded
individually:

```cpp
for (const FaceName& real :
     {part.startCap(), part.endCap(), part.side(0), part.side(1),
      part.side(2), part.side(3)}) {
    CHECK(after.loads[1].pressure()->face != real);
}
```

**And it still refuses the solve after the load.**
`StructuralPersistence_AnUnresolvedReferenceStillRefusesTheSolveAfterLoading`
takes the second half: load the document, regenerate, generate a mesh, and then

```text
validateStructuralAnalysisForSolve  ->  Rejected,
                                        with RestraintsUnresolvable
solveStructuralAnalysis             ->  refused
```

Persistence has done its job by preserving the intent; refusing the solve is
`P17-VALID`'s, and this is what proves the preserved reference is genuinely
still unresolved rather than quietly made to work.

## Why a rebind cannot be written here at all

The strongest part of this claim is not a test. It is the reader's signature:

```cpp
Result<std::unique_ptr<structural::StructuralAnalysis>>
structuralAnalysisFromJson(const Json& data, std::string name, std::string_view path);
```

No `Document`. No `Regenerator`. No `Mesher`. No geometry, no mesh, no
`GeometryMeshMap`. **There is nothing for a reference to be rebound TO.** A
nearest-face fallback could not be written in this file without first adding a
parameter and changing every caller.

That was measured rather than reasoned: it is why the brief's requested
"rebind unresolved reference to first nearby face" mutation (§139) could not be
written, and [MUTATION.md](MUTATION.md) records it as an unreachable probe
rather than as one that passed.

The same division holds on the resolving side. P16-MAP-001 owns which triangles
lie on which CAD face, and P17 asks that question rather than answering it
again — no distance tolerance, no nearest face, no normal-similarity threshold,
no centroid test, in the serializer or anywhere else in the structural module.

## Malformed is a different thing, and the distinction is mandatory

| | Malformed | Well formed but unresolved |
| --- | --- | --- |
| what is wrong | the *representation* cannot be decoded | the representation is fine; the geometry changed |
| examples | an unknown role, a `side` naming no entity, a non-object | a `side` naming an entity no face is generated by |
| result | **the file is refused**, naming `restraints[0].face` | **the document loads** |
| then what | the user has a corrupt file | `P17-VALID` refuses the solve until the user fixes the reference |

Three malformed cases and one well-formed-unresolved case, in one test, so the
two halves cannot drift apart.

## The drilled-hole limitation is not worked around

A drilled hole's cylindrical wall still has no stable `FaceName` — recorded in
the face-naming audit, and unchanged by this milestone. Persistence does **not**
encode a facet id, a coordinate-based cylinder query or a nearest-face search
to make that target look supported (the brief's §8). An unsupported target
remains unsupported, and the file format has no way to express one.
