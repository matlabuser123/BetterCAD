# P17-BC-001 — unsupported targets

## The known limitation

```text
stable canonical FaceName available for a drilled hole's cylindrical wall:
    NO

nearest / geometric fallback:
    NO

preparation:
    explicit failure -- RestraintProblem::TargetUnresolved, ErrorCode::NotFound

PASS
```

`cutHole` names a hole's flat faces and **not** its cylindrical wall, so the
naming chain never attributed that wall and there is no canonical reference a
user could write for it. This is a P16 limitation and it is recorded as one.
P17-LOAD-001 recorded the same gap for loads; nothing about it changed, and
nothing here works around it.

## How it is tested, and why that is honest

RM-MESH-03 and RM-MESH-04 put their holes in the **profile sketch**, for
exactly this reason: their bores are swept by profile circles, so they are
named `Side` faces and are perfectly usable targets. So the unsupported case is
not a fixture that happens not to exist — it is a selector aimed at a face the
naming chain never attributed, which is what a drilled hole's wall amounts to:

```cpp
const FaceName unnamed{built->solid,
                       FaceSelector{.role = FaceRole::Side,
                                    .entity = EntityId::fromValue(99999)}};
```

**And the complement is tested in the same test case.** The *named* bore of the
same geometric kind — a cylindrical hole wall — is restrained normally, with
every one of its mapped nodes constrained. That pairing is what makes the
refusal a naming-chain gap rather than a P17 defect:

```text
tests/reference/StructuralBCReferenceTests.cpp
    StructuralBC_RefusesTheDrilledHoleWallWithNoGeometricFallback
        "a wall with no canonical name is refused, and nothing nearby is rebound"
        "the named bore of the same geometric kind is restrained normally"
```

## Nothing nearby is substituted

The refusal carries the words, and the test asserts them:

```text
restraint:9: its target face names no face of the body as it is now, so there
is nothing to constrain. Nothing nearby is rebound
```

The block fixture's version of this test is sharper still: it names a feature
that is not in the body at all, on a block with **six** nameable faces. A
nearest-face fallback would have found one of them, so the refusal is evidence
that there is no fallback and not merely that this particular lookup missed.

The implementation is searched rather than reviewed: no `nearest`, no
`distance`, no `centroid`, no `tolerance` and no normal-similarity test appears
anywhere in this milestone's code. Facet geometry is not read at all — a
restraint needs the facets' nodes, not their position.

And the mutation that uses an unresolved target anyway (disabling the
`fullyResolved()` check in the shared resolver) is **killed** by five tests
across P17-BC and P17-LOAD.

## Ambiguous targets

```text
NO SUCH STATE EXISTS
```

P16's `MappingState` has exactly two values, `Resolved` and `Unresolved`, and
its header says why:

> a `FaceName` can be ambiguous, which P12-STREF-001 documents, and that is
> exactly why this layer does not map through one

So there is no ambiguous resolution to refuse, and no "choose the first" to
avoid. `RestraintProblem` has no `Ambiguous` value; adding one would have been
a branch nothing could take, and shipping it as a placeholder is the failure
`CLAUDE.md` names. Brief section 101 is conditional — "if P16 can produce
`Ambiguous`, test it" — and the condition is false.

`Unsupported` is absent for the same kind of reason: P16's header records that
"attribution needs no surface kind at all, so a cone, a sphere, a torus and a
B-spline map exactly as a plane does". An unsupported *topology* does not exist
in the mapping layer; an unattributed *face* does, and it is `Unresolved`.

## Stale geometry, stale mesh, no mesh

All three are refused **before** this milestone is reached, by the input
boundary ADR-036 established:

```text
no mesh         requireStructuralModel refuses: "meshing control 3 has no mesh
                that describes the model: no mesh". No StructuralModel exists,
                so there is nothing to prepare against, and no zero-constraint
                set is fabricated
stale mesh      same gate -- possession of a StructuralModel is the evidence
                that the mesh describes the model
stale geometry  same gate, which reaches geometry currency through P16's one
                geometry boundary
```

`prepareStructuralRestraints` takes a `StructuralModel`, so there is nothing to
re-ask here. A third gate would be a branch nothing could take, and
`meshing::boundaryNodesOf` checks the map against the mesh once more on its own
account.

Tested by `StructuralBC_CannotBePreparedBeforeAMeshExists`, which builds a
document with a body and a control, regenerates it, and asks for the model
without meshing.

## Named boundary sets

`NamedBoundarySet` exists on the `MeshControl` and names faces by `FaceName`. A
restraint that targeted one would reach the same faces through an extra level
of indirection and would couple structural intent to **meshing** intent — a set
renamed or re-aimed for meshing reasons would silently change a restraint.

Deliberately not done. It is recorded here rather than shipped as an unused
payload, and adding it later is a new target kind on `StructuralRestraint`
rather than a change to anything this milestone decided.
